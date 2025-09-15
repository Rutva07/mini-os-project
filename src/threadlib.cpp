#include "threadlib.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
  #define NOMINMAX
  #include <windows.h>
#else
  #include <ucontext.h>
#endif

namespace mini_os {

using Clock = std::chrono::steady_clock;
using Ms    = std::chrono::microseconds;
static inline int64_t now_ms() {
  return std::chrono::duration_cast<Ms>(Clock::now().time_since_epoch()).count();
}

// ------------------------------ Logging -------------------------------------
struct Logger {
  std::ofstream out;
  Logger(const char* path) {
    out.open(path, std::ios::out | std::ios::trunc);
    if (out.is_open()) out << "t_us,event,tid,info\n";
  }
  void log(const char* event, int tid, const std::string& info = "") {
    if (!out.is_open()) return;
    out << now_ms() << "," << event << "," << tid << "," << info << "\n";
  }
};
static Logger g_log("schedule_log.csv");

// ------------------------------ Thread core ---------------------------------

enum class ThreadState { NEW, READY, RUNNING, BLOCKED, SLEEPING, FINISHED };

struct Context;
struct Thread;

struct WaitQueue {
  std::deque<int> q;
  void push(int tid) { q.push_back(tid); }
  bool empty() const { return q.empty(); }
  int  pop() { int t = q.front(); q.pop_front(); return t; }
};

// Named resource -> wait queue
static std::map<std::string, WaitQueue> g_resources;

// TLS per thread
static std::unordered_map<int, std::unordered_map<std::string, std::intptr_t>> g_tls;

// Cross-platform lightweight context
#if defined(_WIN32)
struct Context {
  LPVOID fiber = nullptr;
};
static LPVOID g_mainFiber = nullptr;
#else
constexpr size_t STACK_SIZE = 1 << 16; // 64 KiB
struct Context {
  ucontext_t ctx{};
  std::unique_ptr<char[]> stack;
};
static ucontext_t g_sched_ctx;
#endif

struct Thread {
  int            tid = -1;
  int            base_priority = 1;  // 1..10
  int            dyn_priority  = 1;
  ThreadState    state = ThreadState::NEW;
  std::string    name;
  std::function<void()> func;
  Context        cx;
  int64_t        wake_time_ms = 0;   // for sleeping
  int            quantum_budget = 8; // remaining work units before auto-yield
  int            mlfq_level = 0;     // 0 is highest
};

// ------------------------------ Scheduler -----------------------------------

struct Scheduler {
  SchedPolicy policy = SchedPolicy::RoundRobin;

  // Round-robin / priority queue
  std::deque<int> rrq;

  // MLFQ queues
  std::vector<std::deque<int>> mlfq;
  int levels = 3;
  std::vector<int> quantum_by_level = {8, 4, 2};
  bool enable_aging = true;
  int  aging_interval_ms = 500;
  int64_t last_age_ms = now_ms();

  void set_policy_from_env() {
    if (policy != SchedPolicy::RoundRobin && policy != SchedPolicy::Priority && policy != SchedPolicy::MLFQ) {
      policy = SchedPolicy::RoundRobin;
    }
    const char* s = std::getenv("SCHED");
    if (!s) return;
    std::string v(s);
    if (v == "prio" || v == "priority") policy = SchedPolicy::Priority;
    else if (v == "mlfq") policy = SchedPolicy::MLFQ;
    else policy = SchedPolicy::RoundRobin;
  }

  void init_mlfq_if_needed() {
    if ((int)mlfq.size() != levels) mlfq.assign(levels, {});
    if ((int)quantum_by_level.size() != levels) {
      quantum_by_level.clear();
      for (int i = 0; i < levels; ++i) quantum_by_level.push_back(std::max(1, 8 >> i));
    }
  }

  void enqueue_rr(int tid) { rrq.push_back(tid); }

  void enqueue_prio(const std::vector<Thread>& ths, int tid) {
    auto it = rrq.begin();
    for (; it != rrq.end(); ++it) {
      if (ths[tid].base_priority > ths[*it].base_priority) break;
    }
    rrq.insert(it, tid);
  }

  void enqueue_mlfq(std::vector<Thread>& ths, int tid) {
    init_mlfq_if_needed();
    auto& th = ths[tid];
    th.mlfq_level = std::clamp(th.mlfq_level, 0, levels-1);
    th.quantum_budget = quantum_by_level[th.mlfq_level];
    mlfq[th.mlfq_level].push_back(tid);
  }

  void enqueue(std::vector<Thread>& ths, int tid) {
    switch (policy) {
      case SchedPolicy::RoundRobin: enqueue_rr(tid); break;
      case SchedPolicy::Priority:   enqueue_prio(ths, tid); break;
      case SchedPolicy::MLFQ:       enqueue_mlfq(ths, tid); break;
    }
  }

  bool empty() const {
    if (policy == SchedPolicy::MLFQ) {
      for (auto& q : mlfq) if (!q.empty()) return false;
      return true;
    }
    return rrq.empty();
  }

  int pop(std::vector<Thread>& ths) {
    if (policy == SchedPolicy::MLFQ) {
      init_mlfq_if_needed();
      for (int lvl = 0; lvl < levels; ++lvl) {
        if (!mlfq[lvl].empty()) { int t = mlfq[lvl].front(); mlfq[lvl].pop_front(); return t; }
      }
      return -1;
    } else {
      int t = rrq.front(); rrq.pop_front(); return t;
    }
  }

  void demote_mlfq(std::vector<Thread>& ths, int tid) {
    if (policy != SchedPolicy::MLFQ) return;
    auto& th = ths[tid];
    th.mlfq_level = std::min(th.mlfq_level + 1, levels - 1);
    th.quantum_budget = quantum_by_level[th.mlfq_level];
  }

  void promote_mlfq(std::vector<Thread>& ths, int tid) {
    if (policy != SchedPolicy::MLFQ) return;
    auto& th = ths[tid];
    th.mlfq_level = std::max(th.mlfq_level - 1, 0);
    th.quantum_budget = quantum_by_level[th.mlfq_level];
  }

  void maybe_age(std::vector<Thread>& ths) {
    if (policy != SchedPolicy::MLFQ || !enable_aging) return;
    int64_t t = now_ms();
    if (t - last_age_ms < aging_interval_ms) return;
    last_age_ms = t;
    // simple aging: move one thread from lowest non-empty queue up one level
    for (int lvl = levels - 1; lvl > 0; --lvl) {
      if (!mlfq[lvl].empty()) {
        int tid = mlfq[lvl].front(); mlfq[lvl].pop_front();
        ths[tid].mlfq_level = lvl - 1;
        ths[tid].quantum_budget = quantum_by_level[ths[tid].mlfq_level];
        mlfq[lvl - 1].push_back(tid);
        g_log.log("age", tid, "promote");
        break;
      }
    }
  }
};

static Scheduler g_sched;

// ------------------------------ Runtime -------------------------------------

static std::vector<Thread> g_threads;
static std::atomic<int>    g_current{-1};
static std::atomic<bool>   g_stop{false};
static int                 g_next_tid = 0;

// Forward decls
static void schedule();
static void platform_yield_to_scheduler();
static void switch_to_thread(int next_tid);

// API ------------------------------------------------------------------------

int  thread_create(const ThreadFunc& func, const std::string& name, int priority) {
  int tid = g_next_tid++;
  Thread t;
  t.tid = tid;
  t.func = func;
  t.name = name;
  t.base_priority = std::clamp(priority, 1, 10);
  t.dyn_priority = t.base_priority;
  t.state = ThreadState::NEW;
#if !defined(_WIN32)
  t.cx.stack = std::make_unique<char[]>(STACK_SIZE);
#endif
  g_threads.push_back(std::move(t));
  return tid;
}

void set_policy(SchedPolicy p) { g_sched.policy = p; }

void mlfq_set_levels(int levels) {
  g_sched.levels = std::clamp(levels, 1, 8);
}
void mlfq_set_quantum_by_level(int level, int quantum_units) {
  if (level < 0) return;
  if ((int)g_sched.quantum_by_level.size() <= level)
    g_sched.quantum_by_level.resize(level+1, 2);
  g_sched.quantum_by_level[level] = std::max(1, quantum_units);
}
void mlfq_enable_aging(bool enable) { g_sched.enable_aging = enable; }
void mlfq_set_aging_interval_ms(int ms) { g_sched.aging_interval_ms = std::max(1, ms); }

void tls_set(const std::string& key, std::intptr_t value) {
  int tid = g_current.load();
  g_tls[tid][key] = value;
}
std::optional<std::intptr_t> tls_get(const std::string& key) {
  int tid = g_current.load();
  auto itT = g_tls.find(tid);
  if (itT == g_tls.end()) return std::nullopt;
  auto it = itT->second.find(key);
  if (it == itT->second.end()) return std::nullopt;
  return it->second;
}

void thread_sleep(int ms) {
  int tid = g_current.load();
  auto& th = g_threads[tid];
  th.wake_time_ms = now_ms() + ms;
  th.state = ThreadState::SLEEPING;
  g_log.log("sleep", tid, std::to_string(ms));
  if (g_sched.policy == SchedPolicy::MLFQ) {
    // I/O or sleep considered interactive -> promote a level
    g_sched.promote_mlfq(g_threads, tid);
  }
  platform_yield_to_scheduler();
}

void thread_wait(const std::string& resource) {
  int tid = g_current.load();
  auto& th = g_threads[tid];
  th.state = ThreadState::BLOCKED;
  g_resources[resource].push(tid);
  g_log.log("wait", tid, resource);
  if (g_sched.policy == SchedPolicy::MLFQ) {
    g_sched.promote_mlfq(g_threads, tid);
  }
  platform_yield_to_scheduler();
}

void thread_signal(const std::string& resource) {
  auto it = g_resources.find(resource);
  if (it == g_resources.end() || it->second.empty()) return;
  int tid = it->second.pop();
  auto& th = g_threads[tid];
  if (th.state == ThreadState::BLOCKED) {
    th.state = ThreadState::READY;
    g_sched.enqueue(g_threads, tid);
    g_log.log("signal", tid, resource);
  }
}

// Work units: decrement quantum; if <=0, auto-yield (and demote for MLFQ)
int thread_work(int units) {
  int tid = g_current.load();
  auto& th = g_threads[tid];
  th.quantum_budget -= std::max(1, units);
  if (th.quantum_budget <= 0) {
    g_log.log("qexpire", tid, "auto-yield");
    // Demote in MLFQ if CPU-bound
    if (g_sched.policy == SchedPolicy::MLFQ) {
      g_sched.demote_mlfq(g_threads, tid);
    }
    // requeue and yield
    if (th.state == ThreadState::RUNNING) {
      th.state = ThreadState::READY;
      g_sched.enqueue(g_threads, tid);
    }
    platform_yield_to_scheduler();
  }
  return th.quantum_budget;
}

// -------------------------- Platform-specific glue --------------------------

#if defined(_WIN32)

static void ensure_main_fiber() {
  if (!g_mainFiber) {
    g_mainFiber = ConvertThreadToFiber(nullptr);
    if (!g_mainFiber) {
      std::fprintf(stderr, "ConvertThreadToFiber failed (%lu)\n", GetLastError());
      std::exit(1);
    }
  }
}

static VOID __stdcall fiber_trampoline(void* param) {
  int tid = (int)(intptr_t)param;
  g_current.store(tid);
  auto& th = g_threads[tid];
  th.state = ThreadState::RUNNING;
  g_log.log("start", tid, th.name);
  th.quantum_budget = (g_sched.policy == SchedPolicy::MLFQ)
                        ? g_sched.quantum_by_level[th.mlfq_level] : std::max(1, th.quantum_budget);

  th.func();

  th.state = ThreadState::FINISHED;
  g_log.log("finish", tid);
  platform_yield_to_scheduler();
}

static void switch_to_thread(int next_tid) {
  ensure_main_fiber();
  auto& th = g_threads[next_tid];
  if (!th.cx.fiber) {
    th.cx.fiber = CreateFiber(0, fiber_trampoline, (void*)(intptr_t)next_tid);
    if (!th.cx.fiber) {
      std::fprintf(stderr, "CreateFiber failed (%lu)\n", GetLastError());
      std::exit(1);
    }
  }
  g_current.store(next_tid);
  th.state = ThreadState::RUNNING;
  if (g_sched.policy == SchedPolicy::MLFQ) th.quantum_budget = g_sched.quantum_by_level[th.mlfq_level];
  g_log.log("run", next_tid, th.name);
  SwitchToFiber(th.cx.fiber);
}

static void platform_yield_to_scheduler() {
  ensure_main_fiber();
  SwitchToFiber(g_mainFiber);
}

#else

static void context_trampoline(int tid_int) {
  int tid = tid_int;
  g_current.store(tid);
  auto& th = g_threads[tid];
  th.state = ThreadState::RUNNING;
  g_log.log("start", tid, th.name);
  th.quantum_budget = (g_sched.policy == SchedPolicy::MLFQ)
                        ? g_sched.quantum_by_level[th.mlfq_level] : std::max(1, th.quantum_budget);

  th.func();

  th.state = ThreadState::FINISHED;
  g_log.log("finish", tid);
  platform_yield_to_scheduler();
}

static void ensure_context(int tid) {
  auto& th = g_threads[tid];
  if (th.state == ThreadState::NEW) {
    getcontext(&th.cx.ctx);
    th.cx.ctx.uc_stack.ss_sp   = th.cx.stack.get();
    th.cx.ctx.uc_stack.ss_size = STACK_SIZE;
    th.cx.ctx.uc_link          = &g_sched_ctx;
    makecontext(&th.cx.ctx, (void (*)())context_trampoline, 1, tid);
  }
}

static void switch_to_thread(int next_tid) {
  ensure_context(next_tid);
  g_current.store(next_tid);
  auto& th = g_threads[next_tid];
  th.state = ThreadState::RUNNING;
  if (g_sched.policy == SchedPolicy::MLFQ) th.quantum_budget = g_sched.quantum_by_level[th.mlfq_level];
  g_log.log("run", next_tid, th.name);
  swapcontext(&g_sched_ctx, &th.cx.ctx);
}

static void platform_yield_to_scheduler() {
  swapcontext(&g_threads[g_current.load()].cx.ctx, &g_sched_ctx);
}

#endif

// ------------------------------ Scheduling loop -----------------------------

static bool all_done() {
  for (auto& th : g_threads) if (th.state != ThreadState::FINISHED) return false;
  return true;
}

static void wake_sleepers() {
  int64_t t = now_ms();
  for (auto& th : g_threads) {
    if (th.state == ThreadState::SLEEPING && th.wake_time_ms <= t) {
      th.state = ThreadState::READY;
      g_sched.enqueue(g_threads, th.tid);
      g_log.log("wakeup", th.tid);
    }
  }
}

static void schedule_once() {
  // Move NEW to READY
  for (auto& th : g_threads) {
    if (th.state == ThreadState::NEW) {
      th.state = ThreadState::READY;
      g_sched.enqueue(g_threads, th.tid);
      g_log.log("ready", th.tid);
    }
  }

  wake_sleepers();
  g_sched.maybe_age(g_threads);

  if (g_sched.empty()) return;

  int next = g_sched.pop(g_threads);
  if (next >= 0) {
    switch_to_thread(next);
  }
}

void thread_yield() {
  int tid = g_current.load();
  if (tid >= 0) {
    auto& th = g_threads[tid];
    if (th.state == ThreadState::RUNNING) {
      th.state = ThreadState::READY;
      g_sched.enqueue(g_threads, tid);
      g_log.log("yield", tid);
    }
  }
  platform_yield_to_scheduler();
}

void thread_run() {
  g_sched.set_policy_from_env();
#if defined(_WIN32)
  if (!g_mainFiber) {
    g_mainFiber = ConvertThreadToFiber(nullptr);
    if (!g_mainFiber) {
      std::fprintf(stderr, "ConvertThreadToFiber failed (%lu)\n", GetLastError());
      std::exit(1);
    }
  }
#else
  // Prepare scheduler context
  getcontext(&g_sched_ctx);
  static char sched_stack[STACK_SIZE];
  g_sched_ctx.uc_stack.ss_sp   = sched_stack;
  g_sched_ctx.uc_stack.ss_size = sizeof(sched_stack);
  g_sched_ctx.uc_link          = nullptr;
#endif

  g_log.log("boot", -1, (g_sched.policy==SchedPolicy::RoundRobin?"rr":(g_sched.policy==SchedPolicy::Priority?"prio":"mlfq")));

  while (!all_done()) {
    schedule_once();
    if (g_sched.empty()) {
      // idle
      std::this_thread::sleep_for(Ms(1));
    }
  }

  g_log.log("halt", -1);

#if defined(_WIN32)
  ConvertFiberToThread();
  g_mainFiber = nullptr;
#endif
}

} // namespace mini_os