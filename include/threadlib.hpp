#ifndef THREADLIB_HPP
#define THREADLIB_HPP

#include <functional>
#include <string>
#include <cstdint>
#include <optional>

namespace mini_os {

using ThreadFunc = std::function<void()>;

// Scheduler policies
enum class SchedPolicy { RoundRobin, Priority, MLFQ };

// Create a thread with name and priority (1..10)
int  thread_create(const ThreadFunc& func, const std::string& name = "task", int priority = 1);

// Start the scheduler loop; returns when all threads finish
void thread_run();

// Cooperative yield
void thread_yield();

// Sleep for N milliseconds
void thread_sleep(int ms);

// Simple wait/signal on a named resource
void thread_wait(const std::string& resource);
void thread_signal(const std::string& resource);

// Simulate work units. If the thread exceeds its quantum budget, it auto-yields.
// Return value: remaining budget after this call.
int  thread_work(int units = 1);

// Set scheduler policy directly (overrides env var)
void set_policy(SchedPolicy p);

// Thread-local storage (simple key/value integers or pointer-sized values)
void tls_set(const std::string& key, std::intptr_t value);
std::optional<std::intptr_t> tls_get(const std::string& key);

// Configure MLFQ parameters
void mlfq_set_levels(int levels);              // number of queues (default 3)
void mlfq_set_quantum_by_level(int level, int quantum_units); // e.g., {8,4,2}
void mlfq_enable_aging(bool enable);
void mlfq_set_aging_interval_ms(int ms);

} // namespace mini_os

#endif // THREADLIB_HPP