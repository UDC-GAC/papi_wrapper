# Minimum version
cmake_minimum_required(VERSION 3.10)

# Project name
project(tests_papi_wrapper)

# Use C++11
set(CMAKE_CXX_STANDARD 11)
# Require (at least) it
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Don't use e.g. GNU extension (like -std=gnu++11) for portability
set(CMAKE_CXX_EXTENSIONS OFF)

# Requirements
find_package(OpenMP REQUIRED)

# General values
include_directories(../lib)
set(CMAKE_C_FLAGS " -Wall -fopenmp ")

# Test multithread
add_executable(test_pw_multithread.o ../lib/papi_wrapper.c pw_multithread.c)
target_compile_definitions(test_pw_multithread.o PRIVATE PW_MULTITHREAD)
target_link_libraries(test_pw_multithread.o PUBLIC papi)
target_link_libraries(test_pw_multithread.o PRIVATE ${OpenMP_C_FLAGS})

# Test multithread subregions
add_executable(test_pw_multithread_subregions.o ../lib/papi_wrapper.c pw_multithread_subregions.c)
target_compile_definitions(test_pw_multithread_subregions.o PRIVATE PW_MULTITHREAD)
target_link_libraries(test_pw_multithread_subregions.o PUBLIC papi)
target_link_libraries(test_pw_multithread_subregions.o PRIVATE ${OpenMP_C_FLAGS})

# Test singlethread
#add_executable(test_pw_singlethread.o ../lib/papi_wrapper.c pw_singlethread.c)
#target_link_libraries(test_pw_singlethread.o PUBLIC papi)