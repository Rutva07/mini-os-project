# Mini OS-Style Scheduler & Green Threads (C++)

This project implements **user-space green threads** with multiple schedulers and OS-like behaviors, then demonstrates them via examples.

**Features**
- Cross-platform fibers/contexts:
  - Windows: Win32 Fibers
  - Linux/macOS: POSIX `ucontext` (note: `ucontext` is deprecated on macOS; still works on many setups)
- Schedulers: `rr` (round-robin), `prio` (priority), `mlfq` (multi-level feedback queue)
- Blocking: `thread_sleep(ms)`, `thread_wait(resource)`, `thread_signal(resource)`
- Time quanta: simulate preemption by auto-yield on *work units*
- MLFQ: demote on quantum expiration, promote on I/O wakeup, optional aging
- Thread-local storage (simple key/value map per thread)
- CSV logging of scheduler events: `schedule_log.csv`

## Build

### Using CMake
```bash
# Windows (from "x64 Native Tools" or VSCode terminal with MinGW/WinLibs)
cmake -S . -B build
cmake --build build --config Release

# Linux/macOS
cmake -S . -B build
cmake --build build -j
```

### Using g++ directly (no CMake)
```bash
g++ -std=c++20 -O2 -Iinclude src/threadlib.cpp examples/round_robin.cpp -o round_robin
```

## Run

Choose a scheduler at runtime:

- `SCHED=rr` (default)
- `SCHED=prio`
- `SCHED=mlfq`

### Windows (PowerShell)
```powershell
$env:SCHED="rr"; .\build\round_robin.exe
$env:SCHED="prio"; .\build\priority.exe
$env:SCHED="mlfq"; .\build\mlfq_demo.exe
```

### Linux/macOS
```bash
SCHED=rr ./build/round_robin
SCHED=prio ./build/priority
SCHED=mlfq ./build/mlfq_demo
```

The examples write a CSV log: `schedule_log.csv`

## Examples

- `round_robin.cpp` — two tasks interleaving cooperatively
- `priority.cpp` — tasks with different base priorities, compare `rr` vs `prio`
- `sleep_io.cpp` — sleeping task, I/O wait/signaling, and CPU-bound worker
- `mlfq_demo.cpp` — CPU-hog vs interactive task under MLFQ

## Notes

- **Windows:** the runtime uses **Fibers**; the main thread is converted to a fiber automatically.
- **POSIX:** uses `ucontext`. If unavailable, you can switch to `boost::context` with minor changes.
- This is a **cooperative** threading system. Preemption is *simulated* by quantum-based auto-yield inside `thread_work()`; you still call that in your code to grant the scheduler a chance to run.