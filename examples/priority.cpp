#include "threadlib.hpp"
#include <iostream>

using namespace mini_os;

void busy(const char* tag) {
  for (int i=0;i<6;++i) {
    for (volatile int k=0;k<300000;k++); // CPU spin
    std::cout << "[" << tag << "] step " << i << "\n";
    thread_work(3);
    thread_yield();
  }
}

int main() {
  std::cout << "Example: Priority (set SCHED=prio)\n";
  thread_create([]{ busy("low"); },  "low", 1);
  thread_create([]{ busy("mid"); },  "mid", 5);
  thread_create([]{ busy("high"); }, "high", 9);
  set_policy(SchedPolicy::Priority);
  thread_run();
  std::cout << "Done. Log: schedule_log.csv\n";
}