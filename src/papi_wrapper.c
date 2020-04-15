/**
 * papi_wrapper.c
 * Copyright (c) 2019 - 2020 Marcos Horro <marcos.horro@udc.gal>
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
 *          Gabriel Rodríguez
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
#if defined(_OPENMP)
#    include <omp.h>
#    include <pthread.h>
#endif
/* Include definitions */
#include "papi_wrapper.h"

/* By default, collect PAPI counters on thread 0. */
int pw_counters_threadid = 0;
#ifndef PW_THREAD_MONITOR
#    define PW_THREAD_MONITOR 0
#else
pw_counters_threadid = PW_THREAD_MONITOR;
#endif

/* Macros defined for setting cache size */
#ifndef PW_CACHE_MB
#    define PW_CACHE_MB (1024 * 1024)
#endif

/* In bytes */
#ifndef PW_CACHE_SIZE
#    define PW_CACHE_SIZE (33 * PW_CACHE_MB)
#endif

/* Read configuration files */
char *_pw_eventlist[] = {
#include PAPI_FILE_LIST
    NULL};
#ifdef PW_SAMPLING
int overflow_enabled   = 0;
int _pw_samplinglist[] = {
#    include PAPI_FILE_SAMPLING
    -1};
#endif

/* Global variables */
int *             pw_eventlist;
int               pw_num_ctrs     = -1;
int               pw_num_hw_ctrs  = -1;
int               pw_multiplexing = 0;
int               pw_eventset;
long long         pw_values[PW_MAX_COUNTERS];
PW_thread_info_t *PW_thread;

/* Auxiliar functions */
static void
PW_error(const char *file, int line, const char *call, int retval);
#ifdef PW_DEBUG
#    include <stdarg.h>
void
pw_dprintf(int dlvl, const char *fmt, ...)
{
    if (PW_DEBUG_LVL > dlvl) return;
    va_list ap;
    va_start(ap, fmt);
    printf("[%d][DEBUG]: ", dlvl);
    vprintf(fmt, ap);
    printf(" [END_DEBUG]\n");
    va_end(ap);
}
#else
static inline void
pw_dprintf(int dlvl, const char *fmt, ...){};
#endif

/**
 * Cache line flush Intel Processors
 *
 * @param p pointer with address to flush
 * @param allocation_size the size from p to flush
 * @note this flush is broadcast and extracted
 */
void
pw_intel_clflush(volatile void *p, unsigned int allocation_size)
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
 * @brief Dumb function to count number of counters to measure
 */
void
pw_get_num_ctrs()
{
    int evid = 0;
    for (evid = 0; _pw_eventlist[evid]; ++evid)
    {
    };
    pw_num_ctrs = evid;
    if ((pw_num_hw_ctrs = PAPI_num_counters()) <= 0)
    {
        PW_error(__FILE__, __LINE__, "PAPI_num_counters", pw_num_hw_ctrs);
    }
#if PW_EXEC_MODE == PW_ALL_EXC
    pw_multiplexing = (pw_num_ctrs > pw_num_hw_ctrs);
#endif
    int max_multiplex = PAPI_get_opt(PAPI_MAX_MPX_CTRS, NULL);
    pw_dprintf(PW_D_LOW, "max_multiplex = %d", max_multiplex);
    pw_dprintf(PW_D_LOW,
               "pw_num_ctrs = %d "
               "pw_num_hw_ctrs = %d "
               "pw_multiplexing = %d ",
               pw_num_ctrs, pw_num_hw_ctrs, pw_multiplexing);
}

/**
 * @brief Flushes cache by using assembly instructions and calloc
 *
 */
void
pw_prepare_instruments()
{
#pragma omp parallel
    {
        int cache_elemns = PW_CACHE_SIZE / sizeof(double);
        /* calloc() sets memory to zero */
        double *flush = (double *)calloc(cache_elemns, sizeof(double));
#if defined(__x86_64)
        pw_intel_clflush(flush, cache_elemns * sizeof(double));
#endif
        free(flush);
#pragma omp barrier
    }
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
PW_error(const char *file, int line, const char *call, int retval)
{
    char buf[128];

    memset(buf, '\0', sizeof(buf));
    if (retval != 0)
        fprintf(stdout, "%-40s FAILED\nLine # %d\n", file, line);
    else
    {
        fprintf(stdout, "%-40s SKIPPED\n", file);
        fprintf(stdout, "Line # %d\n", line);
    }
    if (retval == PAPI_ESYS)
    {
        sprintf(buf, "System error in %s", call);
        perror(buf);
    } else if (retval > 0)
        fprintf(stdout, "Error: %s\n", call);
    else if (retval == 0)
        fprintf(stdout, "Error: %s\n", call);
    else
    {
        char errstring[PAPI_MAX_STR_LEN];
        // PAPI 5.4.3 has changed the API for PAPI_perror.
#if defined(PAPI_VERSION)                          \
    && ((PAPI_VERSION_MAJOR(PAPI_VERSION) == 5     \
         && PAPI_VERSION_MINOR(PAPI_VERSION) >= 4) \
        || PAPI_VERSION_MAJOR(PAPI_VERSION) > 5)
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

#ifdef PW_SAMPLING
/**
 * @brief Handling event overflow
 *
 * @param EventSet Event set which produces the overflow
 * @param address
 * @param overflow_vector
 * @param context
 */
void
papi_overflow_handler(int EventSet, void *address, long long overflow_vector,
                      void *context)
{
    int retval;
    int n_thread = omp_get_thread_num();
    PW_OVRFLW(n_thread, EventSet)++;
    if ((retval = PAPI_reset(EventSet)) != PAPI_OK)
    {
        PW_error(__FILE__, __LINE__, "PAPI_reset", retval);
    }
}
#endif

/**
 * @brief PAPI set options
 *
 * @param n_thread Thread number of caller
 */
void
pw_set_opts(int n_thread, int evid)
{
    int           retval;
    PAPI_option_t options;

#ifdef PAPI_MULTITHREAD
    int evtset = PW_EVTSET(n_thread, evid);
#else
    int evtset   = pw_eventset;
#endif

    /* Domain */
    memset(&options, 0x0, sizeof(options));
    options.domain.eventset = evtset;
    options.domain.domain   = PW_DOM;
    if ((retval = PAPI_set_opt(PAPI_DOMAIN, &options)) != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_set_opt", retval);

    /* Granularity */
    memset(&options, 0x0, sizeof(options));
    options.granularity.eventset    = evtset;
    options.granularity.granularity = PW_GRN;
    if ((retval = PAPI_set_opt(PAPI_GRANUL, &options)) != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_set_opt", retval);
}

/* Core functions */

/**
 * @brief PAPI initilization
 *
 * @note This function must be called
 */
void
pw_init()
{
    /* need to know how many counters will be counted in order to perform
     * multiplexing or not */
    pw_get_num_ctrs();
#ifdef _OPENMP
#    pragma omp parallel
    {
#    ifndef PAPI_MULTITHREAD
#        pragma omp master
        {
            if (omp_get_max_threads() < pw_counters_threadid)
                pw_counters_threadid = omp_get_max_threads() - 1;
        }
#        pragma omp barrier
        if (omp_get_thread_num() == pw_counters_threadid)
        {
#    endif
#endif
            int retval;
            int k;
#ifdef PAPI_MULTITHREAD
#    pragma omp master
            {
                if (PAPI_set_debug(PAPI_VERB_ESTOP) != PAPI_OK)
                    PW_error(__FILE__, __LINE__, "PAPI_set_debug", retval);
                if ((retval = PAPI_library_init(PAPI_VER_CURRENT))
                    != PAPI_VER_CURRENT)
                    PW_error(__FILE__, __LINE__, "PAPI_library_init", retval);
                if ((pw_multiplexing)
                    && (retval = PAPI_multiplex_init() != PAPI_OK))
                    PW_error(__FILE__, __LINE__, "PAPI_multiplex_init", retval);
                if ((retval = PAPI_thread_init((void *)pthread_self))
                    != PAPI_OK)
                    PW_error(__FILE__, __LINE__, "PAPI_thread_init", retval);
                if ((retval = PAPI_set_granularity(PW_GRN)) != PAPI_OK)
                    PW_error(__FILE__, __LINE__, "PAPI_set_granularity",
                             retval);
                int N_THREADS = omp_get_num_threads();
                pw_dprintf(PW_D_LOW, "pw_init(); th = %2d\tNthread = %2d\n",
                           omp_get_thread_num(), omp_get_num_threads());
                PW_thread = (PW_thread_info_t *)malloc(sizeof(PW_thread_info_t)
                                                       * N_THREADS);
                pw_eventlist = (int *)calloc(PW_MAX_COUNTERS, sizeof(int));
                int n_thread = 0;
                for (n_thread = 0; n_thread < N_THREADS; ++n_thread)
                {
                    PW_thread[n_thread].pw_values =
                        (long long *)calloc(PW_MAX_COUNTERS, sizeof(long long));
                    PW_thread[n_thread].pw_eventset =
                        (int *)calloc(PW_NUM_EVTSET, sizeof(int));
#    ifdef PW_SAMPLING
                    PW_thread[n_thread].pw_overflows =
                        (long long *)calloc(PW_MAX_COUNTERS, sizeof(long long));
#    endif
                }
                for (k = 0; _pw_eventlist[k]; ++k)
                {
                    if ((retval = PAPI_event_name_to_code(
                             (char *)_pw_eventlist[k], &(pw_eventlist[k])))
                        != PAPI_OK)
                        PW_error(__FILE__, __LINE__, "PAPI_event_name_to_code",
                                 retval);
                }
                pw_eventlist[k] = 0;
            }
#    pragma omp barrier
#    pragma omp critical
            {
                int n_thread = omp_get_thread_num();
                int evid     = 0;
#    if PW_EXEC_MODE == PW_SNG_EXC
                for (evid = 0; pw_eventlist[evid] != 0; evid++)
                {
#    endif
                    pw_dprintf(PW_D_LOW, "%2d thread; %2d evid\n", n_thread,
                               evid);
                    PW_EVTSET(n_thread, evid) = PAPI_NULL;
                    if ((retval =
                             PAPI_create_eventset(&(PW_EVTSET(n_thread, evid))))
                        != PAPI_OK)
                        PW_error(__FILE__, __LINE__, "PAPI_create_eventset",
                                 retval);
                    if (pw_multiplexing)
                    {
                        if (retval = PAPI_assign_eventset_component(
                                         (PW_EVTSET(n_thread, evid)), 0)
                                     != PAPI_OK)
                            PW_error(__FILE__, __LINE__,
                                     "PAPI_assign_eventset_component", retval);
                        retval =
                            PAPI_get_multiplex((PW_EVTSET(n_thread, evid)));
                        if (retval > 0)
                            pw_dprintf(PW_D_LOW, "This event set is ready for "
                                                 "multiplexing\n");
                        if (retval == 0)
                        {
                            pw_dprintf(PW_D_LOW,
                                       "This event set is not enabled for "
                                       "multiplexing (thread %d)",
                                       omp_get_thread_num());
                        }
                        if (retval < 0)
                            PW_error(__FILE__, __LINE__, "PAPI_set_multiplex",
                                     retval);
                        retval = PAPI_set_multiplex((PW_EVTSET(n_thread, evid))
                                                    != PAPI_OK);
                        if (((retval == PAPI_EINVAL)
                             && (PAPI_get_multiplex(PW_EVTSET(n_thread, evid)
                                                    == 1))))
                        {
                            pw_dprintf(PW_D_LOW,
                                       "PAPI_set_multiplex already enabled",
                                       retval);
                        } else if (retval != PAPI_OK)
                        {
                            PW_error(__FILE__, __LINE__, "PAPI_set_multiplex",
                                     retval);
                        }
                        if ((retval = PAPI_register_thread()) != PAPI_OK)
                        {
                            PW_error(__FILE__, __LINE__, "PAPI_register_thread",
                                     retval);
                        }
                    }
#    if PW_EXEC_MODE == PW_SNG_EXC
                }
#    endif
            }
#    pragma omp barrier
#else
    pw_eventset  = PAPI_NULL;
    pw_eventlist = (int *)malloc(sizeof(int) * PW_MAX_COUNTERS);
    if ((retval = PAPI_library_init(PAPI_VER_CURRENT)) != PAPI_VER_CURRENT)
        PW_error(__FILE__, __LINE__, "PAPI_library_init", retval);
    if ((retval = PAPI_create_eventset(&pw_eventset)) != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_create_eventset", retval);
    for (k = 0; _pw_eventlist[k]; ++k)
    {
        if ((retval = PAPI_event_name_to_code((char *)_pw_eventlist[k],
                                              &(pw_eventlist[k])))
            != PAPI_OK)
            PW_error(__FILE__, __LINE__, "PAPI_event_name_to_code", retval);
    }
    pw_eventlist[k] = 0;
#endif
#ifdef _OPENMP
#    ifndef PAPI_MULTITHREAD
        }
#    endif
    }
#    pragma omp barrier
#endif
}

/**
 * @brief PAPI close
 *
 * @note This function must be called to free components
 */
void
pw_close()
{
#ifdef _OPENMP
#    pragma omp parallel
    {
#    ifndef PAPI_MULTITHREAD
        if (omp_get_thread_num() == pw_counters_threadid)
        {
            if (PAPI_is_initialized()) PAPI_shutdown();
        }
#    else
#        pragma omp master
        {
            if (PAPI_is_initialized()) PAPI_shutdown();
        }
#    endif
#    ifdef _OPENMP
    }
#        pragma omp barrier
#    endif
#endif
}

/**
 * @brief Start all counters at the same time, called when PW_ALL_EXEC mode
 * activated.
 *
 */
int
pw_start_all_counters()
{
#ifdef _OPENMP
#    pragma omp parallel
    {
#    ifndef PAPI_MULTITHREAD
        if (omp_get_thread_num() == pw_counters_threadid)
        {
#    else
            int n_thread = omp_get_thread_num();
#    endif
#endif
            int retval = 1;
            int evid   = 0;
#ifdef PAPI_MULTITHREAD
#    pragma omp critical
            {
                for (evid = 0; pw_eventlist[evid] != 0; ++evid)
                {
                    char descr[PAPI_MAX_STR_LEN];
                    PAPI_event_code_to_name(pw_eventlist[evid], descr);
                    retval = PAPI_add_event(PW_EVTSET(n_thread, 0),
                                            pw_eventlist[evid]);
                    if ((retval != PAPI_OK) && (retval != PAPI_ECNFLCT))
                        PW_error(__FILE__, __LINE__, "PAPI_add_event", retval);
                    if (retval == PAPI_ECNFLCT) pw_dprintf(PW_D_LOW, descr);
#    ifdef PW_SAMPLING
                    if ((retval = PAPI_overflow(
                             PW_EVTSET(n_thread, evid), pw_eventlist[evid],
                             _pw_samplinglist[evid], PW_OVRFLW_TYPE,
                             papi_overflow_handler))
                        != PAPI_OK)
                        PW_error(__FILE__, __LINE__, "PAPI_overflow", retval);
#    endif
                }
                pw_set_opts(n_thread, 0);
            }
#    pragma omp barrier
            if ((retval = PAPI_start(PW_EVTSET(n_thread, 0))) != PAPI_OK)
                PW_error(__FILE__, __LINE__, "PAPI_start", retval);
#else
    PAPI_event_info_t evinfo;
    for (evid = 0; pw_eventlist[evid] != 0; ++evid)
    {
        if ((retval = PAPI_add_event(pw_eventset, pw_eventlist[evid]))
            != PAPI_OK)
            PW_error(__FILE__, __LINE__, "PAPI_add_event", retval);
        if (PAPI_get_event_info(pw_eventlist[evid], &evinfo) != PAPI_OK)
            PW_error(__FILE__, __LINE__, "PAPI_get_event_info", retval);
        pw_set_opts(0, evid);
    }
    if ((retval = PAPI_start(pw_eventset)) != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_start", retval);
#endif
#ifdef _OPENMP
#    ifndef PAPI_MULTITHREAD
        }
#    endif
    }
#    pragma omp barrier
#endif
    return 0;
}

/**
 * @brief Start each event individually, called when PW_SNG_EXEC mode
 * activated.
 *
 */
int
pw_start_counter(int evid)
{
#ifdef _OPENMP
#    pragma omp parallel
    {
#    ifndef PAPI_MULTITHREAD
        if (omp_get_thread_num() == pw_counters_threadid)
        {
#    endif
#endif
            char              descr[PAPI_MAX_STR_LEN];
            int               retval = 1;
            PAPI_event_info_t evinfo;
            PAPI_event_code_to_name(pw_eventlist[evid], descr);
#ifdef PAPI_MULTITHREAD
            int n_thread = omp_get_thread_num();
#    pragma omp critical
            {
                if (PAPI_add_event(PW_EVTSET(n_thread, evid),
                                   pw_eventlist[evid])
                    != PAPI_OK)
                    PW_error(__FILE__, __LINE__, "PAPI_add_event", 1);
                if (PAPI_get_event_info(pw_eventlist[evid], &evinfo) != PAPI_OK)
                    PW_error(__FILE__, __LINE__, "PAPI_get_event_info", retval);
                pw_set_opts(n_thread, evid);
#    ifdef PW_SAMPLING
                if ((retval = PAPI_overflow(
                         PW_EVTSET(n_thread, evid), pw_eventlist[evid],
                         _pw_samplinglist[evid], PW_OVRFLW_TYPE,
                         papi_overflow_handler))
                    != PAPI_OK)
                    PW_error(__FILE__, __LINE__, "PAPI_overflow", retval);
#    endif
            }
#    pragma omp barrier
            if ((retval = PAPI_start(PW_EVTSET(n_thread, evid))) != PAPI_OK)
                PW_error(__FILE__, __LINE__, "PAPI_start", retval);
#else
    if (PAPI_add_event(pw_eventset, pw_eventlist[evid]) != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_add_event", 1);
    if (PAPI_get_event_info(pw_eventlist[evid], &evinfo) != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_get_event_info", retval);
    pw_set_opts(0, evid);
    if ((retval = PAPI_start(pw_eventset)) != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_start", retval);
#endif
#ifdef _OPENMP
#    ifndef PAPI_MULTITHREAD
        }
#    endif
    }
#    pragma omp barrier
#endif
    return 0;
}

/**
 *
 */
int
pw_start_counter_thread(int evid, int th)
{
    char              descr[PAPI_MAX_STR_LEN];
    int               retval = 1;
    PAPI_event_info_t evinfo;
    PAPI_event_code_to_name(pw_eventlist[evid], descr);
    int n_thread = th;
#pragma omp barrier
    pw_dprintf(PW_D_LOW, "pw_start_counter_thread(); th = %2d evid = %2d\n",
               n_thread, evid);
    if (PAPI_add_event(PW_EVTSET(n_thread, evid), pw_eventlist[evid])
        != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_add_event", 1);
    if ((retval = PAPI_start(PW_EVTSET(n_thread, evid))) != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_start", retval);
}

/**
 * @brief Stop all counters for each thread
 *
 * @param evid Counter id
 */
void
pw_stop_all_counters()
{
#ifdef _OPENMP
#    pragma omp parallel
    {
#    ifndef PAPI_MULTITHREAD
        if (omp_get_thread_num() == pw_counters_threadid)
        {
#    endif
#endif
#ifdef PAPI_MULTITHREAD
            int        evid;
            int        retval;
            int        n_thread = omp_get_thread_num();
            long long *values   = NULL;
#    if PW_EXEC_MODE == PW_SNG_EXC
            for (evid = 0; pw_eventlist[evid] != 0; evid++)
            {
#    endif
#    ifdef PW_SAMPLING
                if ((retval = PAPI_accum(PW_EVTSET(n_thread, evid),
                                         &(PW_VALUES(n_thread, evid))))
                    != PAPI_OK)
                    PW_error(__FILE__, __LINE__, "PAPI_accum", retval);
                PW_VALUES(n_thread, evid) +=
                    (PW_OVRFLW(n_thread, evid) * _pw_samplinglist[evid]);
#    else
            values       = &PW_VALUES(n_thread, evid);
#    endif
                if ((retval = PAPI_stop(PW_EVTSET(n_thread, evid),
                                        (long long *)values))
                    != PAPI_OK)
                    PW_error(__FILE__, __LINE__, "PAPI_stop", retval);
                if ((retval = PAPI_cleanup_eventset(PW_EVTSET(n_thread, evid)))
                    != PAPI_OK)
                    PW_error(__FILE__, __LINE__, "PAPI_cleanup_eventset",
                             retval);
                if ((retval =
                         PAPI_destroy_eventset(&(PW_EVTSET(n_thread, evid))))
                    != PAPI_OK)
                    PW_error(__FILE__, __LINE__, "PAPI_destroy_eventset",
                             retval);
#    if PW_EXEC_MODE == PW_SNG_EXC
            }
#    endif
#else
    int       evid;
    int       retval;
    long long values[1];
    values[0] = 0;
    if ((retval = PAPI_read(pw_eventset, &values[0])) != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_read", retval);

    if ((retval = PAPI_stop(pw_eventset, NULL)) != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_stop", retval);
    for (evid = 0; pw_eventlist[evid] != 0; evid++)
    {
        pw_values[evid] = values[0];

        if ((retval = PAPI_remove_event(pw_eventset, pw_eventlist[evid]))
            != PAPI_OK)
            PW_error(__FILE__, __LINE__, "PAPI_remove_event", retval);
    }
#endif
#ifdef _OPENMP
#    ifndef PAPI_MULTITHREAD
        }
#    endif
    }
#    pragma omp barrier
#endif
}

/**
 * @brief Stop each counter individually
 *
 * @param evid Counter id
 */
void
pw_stop_counter(int evid)
{
#ifdef _OPENMP
#    pragma omp parallel
    {
#    ifndef PAPI_MULTITHREAD
        if (omp_get_thread_num() == pw_counters_threadid)
        {
#    endif
#endif
#ifdef PAPI_MULTITHREAD
            int        retval;
            int        n_thread = omp_get_thread_num();
            long long *values   = NULL;
#    ifdef PW_SAMPLING
            if ((retval = PAPI_accum(PW_EVTSET(n_thread, evid),
                                     &(PW_VALUES(n_thread, evid))))
                != PAPI_OK)
                PW_error(__FILE__, __LINE__, "PAPI_accum", retval);
            PW_VALUES(n_thread, evid) +=
                (PW_OVRFLW(n_thread, evid) * _pw_samplinglist[evid]);
#    else
            values       = &PW_VALUES(n_thread, evid);
#    endif
            if ((retval =
                     PAPI_stop(PW_EVTSET(n_thread, evid), (long long *)values))
                != PAPI_OK)
                PW_error(__FILE__, __LINE__, "PAPI_stop", retval);
            if ((retval = PAPI_cleanup_eventset(PW_EVTSET(n_thread, evid)))
                != PAPI_OK)
                PW_error(__FILE__, __LINE__, "PAPI_cleanup_eventset", retval);
            if ((retval = PAPI_destroy_eventset(&(PW_EVTSET(n_thread, evid))))
                != PAPI_OK)
                PW_error(__FILE__, __LINE__, "PAPI_destroy_eventset", retval);
#else
    int       retval;
    long long values[1];
    values[0] = 0;
    if ((retval = PAPI_read(pw_eventset, &values[0])) != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_read", retval);

    if ((retval = PAPI_stop(pw_eventset, NULL)) != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_stop", retval);

    pw_values[evid] = values[0];

    if ((retval = PAPI_remove_event(pw_eventset, pw_eventlist[evid]))
        != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_remove_event", retval);
#endif
#ifdef _OPENMP
#    ifndef PAPI_MULTITHREAD
        }
#    endif
    }
#    pragma omp barrier
#endif
}

/**
 * @brief Stop each counter individually
 *
 * @param evid Counter id
 */
void
pw_stop_counter_thread(int evid, int th)
{
    int        retval;
    int        n_thread = th;
    long long *values   = NULL;
    values              = &PW_VALUES(n_thread, evid);
    if ((retval = PAPI_stop(PW_EVTSET(n_thread, evid), (long long *)values))
        != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_stop", retval);
    if ((retval =
             PAPI_remove_event(PW_EVTSET(n_thread, evid), pw_eventlist[evid]))
        != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_cleanup_eventset", retval);
    pw_dprintf(PW_D_LOW, "pw_stop_counter_thread(); th = %2d\n", n_thread);
    if ((retval = PAPI_cleanup_eventset(PW_EVTSET(n_thread, evid))) != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_cleanup_eventset", retval);
    if ((retval = PAPI_destroy_eventset(&(PW_EVTSET(n_thread, evid))))
        != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_destroy_eventset", retval);
#pragma omp barrier
}

/**
 * @brief Printing the values of the counters
 *
 */
void
pw_print()
{
    int verbose = 0;
#ifdef _OPENMP
#    ifndef PAPI_MULTITHREAD
#        pragma omp parallel
    {
        if (omp_get_thread_num() == pw_counters_threadid)
        {
#        ifdef PW_VERBOSE
            verbose = 1;
#        endif
#    endif
#endif
#ifdef PAPI_MULTITHREAD
            int N_THREADS = 1;
#    pragma omp parallel
            {
#    pragma omp master
                {
                    N_THREADS = omp_get_num_threads();
                }
            }
            int n_thread = 0;
#    pragma omp for ordered schedule(static, 1)
            for (n_thread = 0; n_thread < N_THREADS; ++n_thread)
            {
                int evid;
                printf("PAPI thread %2d\t", n_thread);
                for (evid = 0; pw_eventlist[evid] != 0; ++evid)
                {
                    if (verbose) printf("%s=", _pw_eventlist[evid]);
                    printf("%llu ", PW_VALUES(n_thread, evid));
                    if (verbose) printf("\n");
                }
                printf("\n");
            }
#    pragma omp barrier
#    pragma omp             master
            {
                free(PW_thread);
            }
#else
    int evid;
    printf("PAPI thread %2d\t", pw_counters_threadid);
    for (evid = 0; pw_eventlist[evid] != 0; ++evid)
    {
        if (verbose) printf("%s=", _pw_eventlist[evid]);
        printf("%llu ", pw_values[evid]);
        if (verbose) printf("\n");
    }
    printf("\n");
#endif
#ifdef _OPENMP
#    ifndef PAPI_MULTITHREAD
        }
    }
#        pragma omp barrier
#    endif
#endif
}
