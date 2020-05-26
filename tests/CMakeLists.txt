set(CMAKE_C_FLAGS " -Wall ")
set(PW_LIB "../lib/papi_wrapper.c")

# General values
include_directories(../lib)

link_libraries(papi)

# Test singlethread
add_executable(test_pw_singlethread.o ${PW_LIB} pw_simple.c)

# No PW_MULTITHREAD but multithread
add_executable(test_pw_openmp_singlethread.o ${PW_LIB} pw_simple.c)
target_link_libraries(test_pw_openmp_singlethread.o PRIVATE OpenMP::OpenMP_CXX)
target_compile_options(test_pw_openmp_singlethread.o PRIVATE "-fopenmp")

# Test multithread subregions
add_executable(test_pw_singlethread_subregions.o ${PW_LIB} pw_subregions.c)

# Test single thread subregions
add_executable(test_pw_openmp_singlethread_subregions.o ${PW_LIB} pw_subregions.c)
target_link_libraries(test_pw_openmp_singlethread_subregions.o PRIVATE OpenMP::OpenMP_CXX)
target_compile_options(test_pw_openmp_singlethread_subregions.o PRIVATE "-fopenmp")

# Test multithread
add_executable(test_pw_multithread.o ${PW_LIB} pw_simple.c)
target_compile_definitions(test_pw_multithread.o PRIVATE PW_MULTITHREAD)
target_link_libraries(test_pw_multithread.o PRIVATE OpenMP::OpenMP_CXX)
target_compile_options(test_pw_multithread.o PRIVATE "-fopenmp")

# Test multithread subregions
add_executable(test_pw_multithread_subregions.o ${PW_LIB} pw_subregions.c)
target_compile_definitions(test_pw_multithread_subregions.o PRIVATE PW_MULTITHREAD)
target_link_libraries(test_pw_multithread_subregions.o PRIVATE OpenMP::OpenMP_CXX)
target_compile_options(test_pw_multithread_subregions.o PRIVATE "-fopenmp")

# Tests
add_test(NAME single COMMAND test_pw_singlethread.o)
add_test(NAME single_openmp COMMAND test_pw_openmp_singlethread.o)
add_test(NAME single_subregion COMMAND test_pw_singlethread_subregions.o)
add_test(NAME single_openmp_subregion COMMAND test_pw_openmp_singlethread_subregions.o)
add_test(NAME multi COMMAND test_pw_multithread.o)
add_test(NAME multi_subregion COMMAND test_pw_multithread_subregions.o)
