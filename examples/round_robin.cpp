#include "threadlib.hpp"
#include <iostream>

using namespace mini_os;

void taskA() {
  for (int i = 0; i < 5; ++i) {
    std::cout << "[A] iteration " << i << "\n";
    thread_work(2); // simulate CPU work; auto-yields on quantum
    thread_yield();
  }
}

void taskB() {
  for (int i = 0; i < 5; ++i) {
    std::cout << "[B] iteration " << i << "\n";
    thread_work(2);
    thread_yield();
  }
}

int main() {
  std::cout << "Example: Round Robin (set SCHED=rr|prio|mlfq)\n";
  thread_create(taskA, "A");
  thread_create(taskB, "B");
  set_policy(SchedPolicy::RoundRobin);
  thread_run();
  std::cout << "Done. Log: schedule_log.csv\n";
}