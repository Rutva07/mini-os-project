#include "threadlib.hpp"
#include <iostream>

using namespace mini_os;

int main() {
  std::cout << "Example: MLFQ\n";

  // Configure MLFQ
  set_policy(SchedPolicy::MLFQ);
  mlfq_set_levels(3);
  mlfq_set_quantum_by_level(0, 8);
  mlfq_set_quantum_by_level(1, 4);
  mlfq_set_quantum_by_level(2, 2);
  mlfq_enable_aging(true);
  mlfq_set_aging_interval_ms(800);

  // CPU hog
  thread_create([]{
    for (int i=0;i<12;++i){
      std::cout << "[HOG] unit " << i << "\n";
      for (volatile int k=0;k<800000;k++);
      thread_work(2); // will keep expiring and get demoted
      // deliberately skip thread_yield sometimes
      if (i % 2 == 0) thread_yield();
    }
  }, "hog", 3);

  // Interactive (I/O)
  thread_create([]{
    for (int i=0;i<10;++i){
      std::cout << "[UI] step " << i << " (sleep 150ms)\n";
      thread_sleep(150); // promotes in MLFQ
      thread_work(1);
      thread_yield();
    }
  }, "ui", 5);

  // Medium
  thread_create([]{
    for (int i=0;i<8;++i){
      std::cout << "[MID] work " << i << "\n";
      for (volatile int k=0;k<400000;k++);
      thread_work(2);
      thread_yield();
    }
  }, "mid", 5);

  thread_run();
  std::cout << "Done. Log: schedule_log.csv\n";
}