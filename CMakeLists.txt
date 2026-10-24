cmake_minimum_required(VERSION 3.20)
project(
  rvsim
  VERSION 1.0.0
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RVSIM_SANITIZERS "Enable address/undefined sanitizers on GCC/Clang" OFF)

add_library(rvcore src/memory.cpp src/decode.cpp src/execute.cpp src/loader.cpp src/machine.cpp
                   src/trace.cpp)
target_include_directories(rvcore PUBLIC include)

if(MSVC)
  target_compile_options(rvcore PUBLIC /W4 /WX /permissive-)
else()
  target_compile_options(rvcore PUBLIC -Wall -Wextra -Wpedantic -Werror)
  if(MINGW)
    # Keep Windows executables usable without copying compiler runtime DLLs.
    target_link_options(rvcore PUBLIC -static)
    if(RVSIM_SANITIZERS)
      message(FATAL_ERROR "Use Linux GCC/Clang for address/undefined sanitizers")
    endif()
  endif()
  if(RVSIM_SANITIZERS)
    target_compile_options(rvcore PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(rvcore PUBLIC -fsanitize=address,undefined)
  endif()
endif()

add_executable(rvsim src/main.cpp)
target_link_libraries(rvsim PRIVATE rvcore)

enable_testing()
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE rvcore)
add_test(NAME core COMMAND core_tests)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME demos COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/run_demos.py --sim
                            $<TARGET_FILE:rvsim>)
add_test(NAME cli COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/cli_tests.py --sim
                          $<TARGET_FILE:rvsim>)
add_test(NAME provenance COMMAND ${Python3_EXECUTABLE}
                                 ${CMAKE_SOURCE_DIR}/scripts/verify_binaries.py)
