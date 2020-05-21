# Minimum version
cmake_minimum_required(VERSION 3.10)

# Project name
project(papi_wrapper)

include_directories(../lib)
set(CMAKE_C_FLAGS " -fopenmp ")

add_executable(test_pw_multithread ../lib/papi_wrapper.c pw_multithread.c)
target_compile_definitions(test_pw_multithread PRIVATE PW_MULTITHREAD)
target_link_libraries(test_pw_multithread PUBLIC papi)