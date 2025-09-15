#include "threadlib.hpp"
#include <iostream>

using namespace mini_os;

int main() {
  std::cout << "Example: Sleep + I/O wait\n";
  // interactive task that waits for I/O
  thread_create([]{
    std::cout << "[IO] waiting for 'go'...\n";
    thread_wait("go");
    std::cout << "[IO] got 'go', working...\n";
    for (int i=0;i<3;++i){
      std::cout << "[IO] unit " << i << "\n";
      thread_work(2);
      thread_yield();
    }
  }, "io_waiter", 5);

  // sleeper that signals later
  thread_create([]{
    for (int i=0;i<3;++i){
      std::cout << "[SLEEP] tick " << i << " (sleep 200ms)\n";
      thread_sleep(200);
    }
    std::cout << "[SLEEP] signaling 'go'\n";
    thread_signal("go");
  }, "sleeper", 7);

  // cpu hog
  thread_create([]{
    for (int i=0;i<6;++i){
      std::cout << "[CPU] spin " << i << "\n";
      for (volatile int k=0;k<600000;k++);
      thread_work(4);
      thread_yield();
    }
  }, "cpu", 3);

  set_policy(SchedPolicy::RoundRobin); // try also Priority/MLFQ via set_policy or env var
  thread_run();
  std::cout << "Done. Log: schedule_log.csv\n";
}