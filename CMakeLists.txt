cmake_minimum_required(VERSION 3.16)
project(MiniOSThreads LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Let users choose scheduler policy at runtime via env var SCHED
# Build a static library
add_library(threadlib STATIC
    src/threadlib.cpp
)
target_include_directories(threadlib PUBLIC include)

if (WIN32)
    target_compile_definitions(threadlib PRIVATE -DWIN32_LEAN_AND_MEAN)
endif()

# Examples
add_executable(round_robin examples/round_robin.cpp)
target_link_libraries(round_robin PRIVATE threadlib)

add_executable(priority examples/priority.cpp)
target_link_libraries(priority PRIVATE threadlib)

add_executable(sleep_io examples/sleep_io.cpp)
target_link_libraries(sleep_io PRIVATE threadlib)

add_executable(mlfq_demo examples/mlfq_demo.cpp)
target_link_libraries(mlfq_demo PRIVATE threadlib)