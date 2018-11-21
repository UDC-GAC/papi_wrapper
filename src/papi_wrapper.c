/**
 * papi_wrapper.c
 * Copyright (c) 2018 Marcos Horro <marcos.horro@udc.gal>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors: Marcos Horro
 */

#define _GNU_SOURCE
#include <assert.h>
#include <math.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#if defined(PAPIWRAP)
#undef PAPIWRAP
#include "papi_wrapper.h"
#define PAPIWRAP
#else
#include "papi_wrapper.h"
#endif

#if defined(_OPENMP)
# define PAPI_MULTITHREAD
#endif

/* By default, collect PAPI counters on thread 0. */
#ifndef PAPIWRAP_THREAD_MONITOR
#define PAPIWRAP_THREAD_MONITOR 0
#endif

/* Total LLC cache size. By default 32+MB.. */
#ifndef PAPIWRAP_CACHE_SIZE_KB
#define PAPIWRAP_CACHE_SIZE_KB 32770
#endif

int papiwrap_counters_threadid = PAPIWRAP_THREAD_MONITOR;
double papiwrap_program_total_flops = 0;

#ifdef PAPIWRAP
#include <papi.h>
#define PAPIWRAP_MAX_NB_PAPI_COUNTERS 96
char* _papiwrap_eventlist[] = {
#include "papi_counters.list"
    NULL};
int papiwrap_eventset;
int papiwrap_eventlist[PAPIWRAP_MAX_NB_PAPI_COUNTERS];
long_long papiwrap_values[PAPIWRAP_MAX_NB_PAPI_COUNTERS];

#endif

/**
 * Cache line flush Intel Processors
 *
 * @param p pointer with address to flush
 * @param allocation_size the size from p to flush
 * @note this flush is broadcast and extracted
 */
void
intel_clflush(volatile void *p, unsigned int allocation_size)
{
    const size_t cache_line = 64;
    const char * cp         = (const char *)p;
    size_t       i          = 0;

    if (p == NULL || allocation_size <= 0) return;

    for (i = 0; i < allocation_size; i += cache_line)
    {
        asm volatile("clflush (%0)\n\t" : : "r"(&cp[i]) : "memory");
    }

    /* according to Intel 64 and IA-32 Architectures optimization reference
     * manual 7.4.9, this instruction is no longer required; but just in case...
     */
    asm volatile("sfence\n\t" : : : "memory");
}


/**
 * @brief Display PAPI errors 
 *
 * @param file File name
 * @param line Line where the error occurred
 * @param call Function called
 * @param retval Value returned
 * @note based on test_fail in papi_utils.c
 */
static void
PAPI_WRAP_error(const char* file, int line, const char* call,
                      int retval) 
{
  char buf[128];

  memset(buf, '\0', sizeof(buf));
  if (retval != 0)
    fprintf(stdout, "%-40s FAILED\nLine # %d\n", file, line);
  else {
    fprintf(stdout, "%-40s SKIPPED\n", file);
    fprintf(stdout, "Line # %d\n", line);
  }
  if (retval == PAPI_ESYS) {
    sprintf(buf, "System error in %s", call);
    perror(buf);
  } else if (retval > 0)
    fprintf(stdout, "Error: %s\n", call);
  else if (retval == 0)
    fprintf(stdout, "Error: %s\n", call);
  else {
    char errstring[PAPI_MAX_STR_LEN];
// PAPI 5.4.3 has changed the API for PAPI_perror.
#if defined(PAPI_VERSION) && ((PAPI_VERSION_MAJOR(PAPI_VERSION) == 5 &&  \
                               PAPI_VERSION_MINOR(PAPI_VERSION) >= 4) || \
                              PAPI_VERSION_MAJOR(PAPI_VERSION) > 5)
    fprintf(stdout, "Error in %s: %s\n", call, PAPI_strerror(retval));
#else
    PAPI_perror(retval, errstring, PAPI_MAX_STR_LEN);
    fprintf(stdout, "Error in %s: %s\n", call, errstring);
#endif
  }
  fprintf(stdout, "\n");
  if (PAPI_is_initialized()) PAPI_shutdown();
  exit(1);
}


/**
 * @brief Handling event overflow
 *
 * @param EventSet Event set which produces the overflow
 * @param address 
 * @param overflow_vector
 * @param context 
 */
void
papi_overflow_handler(int EventSet, void* address,
                           long_long overflow_vector, void* context) 
{
}


/**
 * @brief PAPI initilization
 *
 * @note This function must be called
 */
void 
papiwrap_init()
{
#ifdef _OPENMP
#pragma omp parallel
  {
#ifndef PAPI_MULTITHREAD
#pragma omp master
    {
      if (omp_get_max_threads() < papiwrap_counters_threadid)
        papiwrap_counters_threadid = omp_get_max_threads() - 1;
    }
#pragma omp barrier
    if (omp_get_thread_num() == papiwrap_counters_threadid) {
#endif
#endif

#ifdef PAPI_MULTITHREAD
#pragma omp master
      {
        int retval;
        if ((retval = PAPI_library_init(PAPI_VER_CURRENT)) != PAPI_VER_CURRENT)
          PAPI_WRAP_error(__FILE__, __LINE__, "PAPI_library_init", retval);
        if ((retval = PAPI_thread_init(pthread_self)) != PAPI_OK)
          PAPI_WRAP_error(__FILE__, __LINE__, "PAPI_library_init", retval);
      }
#pragma omp barrier
#pragma omp critical
      {
        int n_thread = omp_get_thread_num();
        int retval;
        papiwrap_eventset[n_thread] = PAPI_NULL;
        if ((retval = PAPI_create_eventset(
                 &papiwrap_eventset[n_thread])) != PAPI_OK)
          PAPI_WRAP_error(__FILE__, __LINE__, "PAPI_create_eventset", retval);
        int k;
        for (k = 0; _papiwrap_eventlist[k]; ++k) {
          if ((retval = PAPI_event_name_to_code(
                   (char*)_papiwrap_eventlist[k],
                   &(papiwrap_eventlist[n_thread][k]))) != PAPI_OK)
            PAPI_WRAP_error(__FILE__, __LINE__, "PAPI_event_name_to_code", retval);
        }
        papiwrap_eventlist[n_thread][k] = 0;
      }
#else
  int retval;
  papiwrap_eventset = PAPI_NULL;
  if ((retval = PAPI_library_init(PAPI_VER_CURRENT)) != PAPI_VER_CURRENT)
    PAPI_WRAP_error(__FILE__, __LINE__, "PAPI_library_init", retval);
  if ((retval = PAPI_thread_init(pthread_self)) != PAPI_OK)
    PAPI_WRAP_error(__FILE__, __LINE__, "PAPI_library_init", retval);
  if ((retval = PAPI_create_eventset(&papiwrap_eventset)) != PAPI_OK)
    PAPI_WRAP_error(__FILE__, __LINE__, "PAPI_create_eventset", retval);
  int k;
  for (k = 0; _papiwrap_eventlist[k]; ++k) {
    if ((retval = PAPI_event_name_to_code(_papiwrap_eventlist[k],
                                          &(papiwrap_eventlist[k]))) !=
        PAPI_OK)
      PAPI_WRAP_error(__FILE__, __LINE__, "PAPI_event_name_to_code", retval);
  }
  papiwrap_eventlist[k] = 0;
#endif
#ifdef _OPENMP
#ifndef PAPI_MULTITHREAD
    }
#endif
  }
#pragma omp barrier
#endif
}

/**
 * @brief PAPI close
 *
 * @note This function must be called to free components
 */
void
papiwrap_close()
{
#ifdef _OPENMP
#pragma omp parallel
  {
#ifndef PAPI_MULTITHREAD
    if (omp_get_thread_num() == papiwrap_counters_threadid) {
#endif
#endif
#ifdef PAPI_MULTITHREAD
#pragma omp master
      {
        int retval;
        int n_thread = omp_get_thread_num();
        //        if ((retval = PAPI_destroy_eventset(
        //                 &papiwrap_eventset[n_thread])) != PAPI_OK)
        //          PAPI_WRAP_error(__FILE__, __LINE__, "PAPI_destroy_eventset",
        //          retval);
      }
#else
  int retval;
  if ((retval = PAPI_destroy_eventset(&papiwrap_eventset)) != PAPI_OK)
    PAPI_WRAP_error(__FILE__, __LINE__, "PAPI_destroy_eventset", retval);
#endif
#pragma omp master
      {
        if (PAPI_is_initialized()) PAPI_shutdown();
      }

#ifdef _OPENMP
#ifndef PAPI_MULTITHREAD
    }
#endif
  }
#pragma omp barrier
#endif
}

/**
 * @brief Init all counters
 *
 */
int
papiwrap_start_counter(int evid)
{
#ifndef POLYBENCH_NO_FLUSH_CACHE
  polybench_flush_cache();
#endif

#ifdef _OPENMP
#pragma omp parallel
  {
#ifndef PAPI_MULTITHREAD
    if (omp_get_thread_num() == papiwrap_counters_threadid) {
#endif
#endif
#ifdef PAPI_MULTITHREAD
      int n_thread = omp_get_thread_num();
      int retval = 1;
      char descr[PAPI_MAX_STR_LEN];
      PAPI_event_info_t evinfo;
      PAPI_event_code_to_name(papiwrap_eventlist[n_thread][evid], descr);
      if (PAPI_add_event(papiwrap_eventset[n_thread],
                         papiwrap_eventlist[n_thread][evid]) != PAPI_OK)
        PAPI_WRAP_error(__FILE__, __LINE__, "PAPI_add_event", 1);
      if (PAPI_get_event_info(papiwrap_eventlist[n_thread][evid],
                              &evinfo) != PAPI_OK)
        PAPI_WRAP_error(__FILE__, __LINE__, "PAPI_get_event_info", retval);
      if (!(overflow_enabled)) {
        if ((retval = PAPI_overflow(papiwrap_eventset[n_thread],
                                    PAPI_TOT_CYC, 10000000, 0,
                                    papi_overflow_handler)) != PAPI_OK) {
          PAPI_WRAP_error(__FILE__, __LINE__, "PAPI_overflow", retval);
        }
        overflow_enabled = 1;
      }
      if ((retval = PAPI_start(papiwrap_eventset[n_thread])) != PAPI_OK)
        PAPI_WRAP_error(__FILE__, __LINE__, "PAPI_start", retval);
#else
  int retval = 1;
  char descr[PAPI_MAX_STR_LEN];
  PAPI_event_info_t evinfo;
  PAPI_event_code_to_name(papiwrap_eventlist[evid], descr);
  if (PAPI_add_event(papiwrap_eventset, papiwrap_eventlist[evid]) !=
      PAPI_OK)
    PAPI_WRAP_error(__FILE__, __LINE__, "PAPI_add_event", 1);
  if (PAPI_get_event_info(papiwrap_eventlist[evid], &evinfo) != PAPI_OK)
    PAPI_WRAP_error(__FILE__, __LINE__, "PAPI_get_event_info", retval);
  if ((retval = PAPI_start(papiwrap_eventset)) != PAPI_OK)
    PAPI_WRAP_error(__FILE__, __LINE__, "PAPI_start", retval);
#endif
#ifdef _OPENMP
#ifndef PAPI_MULTITHREAD
    }
#endif
  }
#pragma omp barrier
#endif
  return 0;
}

/**
 * @brief Stop all the counters
 *
 * @param evid Counter id
 */
void
papiwrap_stop_counter(int evid)
{
#ifdef _OPENMP
#pragma omp parallel
  {
#ifndef PAPI_MULTITHREAD
    if (omp_get_thread_num() == papiwrap_counters_threadid) {
#endif
#endif
#ifdef PAPI_MULTITHREAD
      int retval;
      int n_thread = omp_get_thread_num();
      long long values[1][10];
      values[0][0] = 0;
      //      if ((retval = PAPI_read(papiwrap_eventset, &values[0])) !=
      //      PAPI_OK)
      //        PAPI_WRAP_error(__FILE__, __LINE__, "PAPI_read", retval);

      if ((retval = PAPI_stop(papiwrap_eventset[n_thread], values[0])) !=
          PAPI_OK)
        PAPI_WRAP_error(__FILE__, __LINE__, "PAPI_stop", retval);
      // printf("%2d:value[%2d] \t%llu\n", n_thread, 0, values[0][0]);
      papiwrap_values[n_thread][evid] = values[0][0];

      if ((retval = PAPI_remove_event(
               papiwrap_eventset[n_thread],
               papiwrap_eventlist[n_thread][evid])) != PAPI_OK)
        PAPI_WRAP_error(__FILE__, __LINE__, "PAPI_remove_event", retval);
#else
  int retval;
  long_long values[1];
  values[0] = 0;
  if ((retval = PAPI_read(papiwrap_eventset, &values[0])) != PAPI_OK)
    PAPI_WRAP_error(__FILE__, __LINE__, "PAPI_read", retval);

  if ((retval = PAPI_stop(papiwrap_eventset, NULL)) != PAPI_OK)
    PAPI_WRAP_error(__FILE__, __LINE__, "PAPI_stop", retval);

  papiwrap_values[evid] = values[0];

  if ((retval = PAPI_remove_event(papiwrap_eventset,
                                  papiwrap_eventlist[evid])) != PAPI_OK)
    PAPI_WRAP_error(__FILE__, __LINE__, "PAPI_remove_event", retval);
#endif
#ifdef _OPENMP
#ifndef PAPI_MULTITHREAD
    }
#endif
  }
#pragma omp barrier
#endif
}

/**
 * @brief Printing the values of the counters
 *
 */
void
papiwrap_print()
{
  int verbose = 0;
#ifdef _OPENMP
#ifndef PAPI_MULTITHREAD
#pragma omp parallel
  {
    if (omp_get_thread_num() == papiwrap_counters_threadid) {
#endif
#ifdef papiwrap_VERBOSE
      verbose = 1;
#endif
      if (verbose) printf("On thread %d:\n", papiwrap_counters_threadid);
#endif
#ifdef PAPI_MULTITHREAD
#pragma omp for ordered schedule(static, 1)
      for (int n_thread = 0; n_thread < N_CORES; ++n_thread) {
        int evid;
        printf("PAPI thread %2d\t", n_thread);
        for (evid = 0; papiwrap_eventlist[n_thread][evid] != 0; ++evid) {
          if (verbose) printf("%s=", _papiwrap_eventlist[evid]);
          printf("%llu ", papiwrap_values[n_thread][evid]);
          if (verbose) printf("\n");
        }
        printf("\n");
      }
#else
  int evid;
  printf("PAPI thread %d => ", papiwrap_counters_threadid);
  for (evid = 0; papiwrap_eventlist[evid] != 0; ++evid) {
    if (verbose) printf("%s=", _papiwrap_eventlist[evid]);
    printf("%llu ", papiwrap_values[evid]);
    if (verbose) printf("\n");
  }
  printf("\n");
#endif
#ifdef _OPENMP
#ifndef PAPI_MULTITHREAD
    }
  }
#pragma omp barrier
#endif
#endif
}


