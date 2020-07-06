/**
 * papi_wrapper.c
 * Copyright (c) 2018 - 2020 Marcos Horro <marcos.horro@udc.gal>
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
#if !defined(PW_THREAD_MONITOR)
#    define PW_THREAD_MONITOR 0
#endif
int pw_counters_threadid = PW_THREAD_MONITOR;

/* Macros defined for setting cache size */
#if !defined(PW_CACHE_MB)
#    define PW_CACHE_MB (1024 * 1024)
#endif

/* In bytes */
#if !defined(PW_CACHE_SIZE)
#    define PW_CACHE_SIZE (33 * PW_CACHE_MB)
#endif

/* Read configuration files */
char *_pw_eventlist[] = {
#include PAPI_FILE_LIST
    NULL};
#if defined(PW_SAMPLING)
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
int               __PW_NSUBREGIONS = -1;

/* Auxiliary functions */
static void
PW_error(const char *file, int line, const char *call, int __pw_retval);
#if defined(PW_DEBUG)
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
    int __pw_evid = 0;
    for (__pw_evid = 0; _pw_eventlist[__pw_evid]; ++__pw_evid)
    {
    };
    pw_num_ctrs = __pw_evid;
#if defined(PAPI_VERSION) && (PAPI_VERSION_MAJOR(PAPI_VERSION) < 6)
    pw_num_hw_ctrs = PAPI_num_counters();
#else
    pw_num_hw_ctrs = PAPI_get_cmp_opt(PAPI_MAX_HWCTRS, NULL, 0);
#endif
    if (pw_num_hw_ctrs < 0)
    {
        PW_error(__FILE__, __LINE__, "PAPI_num_counters", pw_num_hw_ctrs);
    }
#if (PW_EXEC_MODE == PW_ALL_EXC)
    pw_multiplexing = (pw_num_ctrs > pw_num_hw_ctrs);
#endif
    int max_multiplex = PAPI_get_opt(PAPI_MAX_MPX_CTRS, NULL);
    pw_dprintf(PW_D_LOW, "max_multiplex = %d", max_multiplex);
    pw_dprintf(PW_D_LOW,
               "pw_num_ctrs = %d "
               "pw_num_hw_ctrs = %d "
               "pw_multiplexing = %d ",
               pw_num_ctrs,
               pw_num_hw_ctrs,
               pw_multiplexing);
}

/**
 * @brief Flushes cache by using assembly instructions and calloc
 *
 */
void
pw_prepare_instruments()
{
#if defined(_OPENMP)
#    pragma omp parallel
    {
#endif
        int cache_elemns = PW_CACHE_SIZE / sizeof(double);
        /* calloc() sets memory to zero */
        double *flush = (double *)calloc(cache_elemns, sizeof(double));
#if defined(__x86_64)
        pw_intel_clflush(flush, cache_elemns * sizeof(double));
#endif
        free(flush);
#if defined(_OPENMP)
#    pragma omp barrier
    }
#endif
}

/**
 * @brief Display PAPI errors
 *
 * @param file File name
 * @param line Line where the error occurred
 * @param call Function called
 * @param __pw_retval Value returned
 * @note based on test_fail in papi_utils.c
 */
static void
PW_error(const char *file, int line, const char *call, int __pw_retval)
{
    char buf[128];

    memset(buf, '\0', sizeof(buf));
    if (__pw_retval != 0)
        fprintf(stdout, "%-40s FAILED\nLine # %d\n", file, line);
    else
    {
        fprintf(stdout, "%-40s SKIPPED\n", file);
        fprintf(stdout, "Line # %d\n", line);
    }

    switch (__pw_retval)
    {
        case PAPI_ESYS:
            sprintf(buf, "System error in %s", call);
            perror(buf);
            break;
        default:
            // PAPI 5.4.3 has changed the API for PAPI_perror.
#if defined(PAPI_VERSION)                          \
    && ((PAPI_VERSION_MAJOR(PAPI_VERSION) == 5     \
         && PAPI_VERSION_MINOR(PAPI_VERSION) >= 4) \
        || PAPI_VERSION_MAJOR(PAPI_VERSION) > 5)
            fprintf(
                stdout, "Error in %s: %s\n", call, PAPI_strerror(__pw_retval));
#else
            PAPI_perror(__pw_retval, errstring, PAPI_MAX_STR_LEN);
            fprintf(stdout, "Error in %s: %s\n", call, errstring);
#endif
    }
    fprintf(stdout, "\n");
    if (PAPI_is_initialized()) PAPI_shutdown();
    exit(PW_ERR);
}

#if defined(PW_SAMPLING)
/**
 * @brief Handling event overflow
 *
 * @param EventSet Event set which produces the overflow
 * @param address
 * @param overflow_vector
 * @param context
 */
void
pw_overflow_handler(int       event_set,
                    void *    address,
                    long long overflow_vector,
                    void *    context)
{
    int __pw_retval;
    int __pw_nthread = omp_get_thread_num();
    PW_OVRFLW(__pw_nthread, event_set)++;
    if ((__pw_retval = PAPI_reset(event_set)) != PAPI_OK)
    {
        PW_error(__FILE__, __LINE__, "PAPI_reset", __pw_retval);
    }
}
#endif

/**
 * @brief PAPI set options
 *
 * @param __pw_nthread Thread number of caller
 */
void
pw_set_opts(int __pw_nthread, int __pw_evid)
{
    int           __pw_retval;
    PAPI_option_t options;

#if defined(PW_MULTITHREAD)
    int evtset = PW_EVTSET(__pw_nthread, __pw_evid);
#else
    int evtset = pw_eventset;
#endif

    /* Domain */
    memset(&options, 0x0, sizeof(options));
    options.domain.eventset = evtset;
    options.domain.domain   = PW_DOM;
    if ((__pw_retval = PAPI_set_opt(PAPI_DOMAIN, &options)) != PAPI_OK)
    {
        pw_dprintf(PW_D_WARNING, "[WARNING] Domain could not be set!");
    }

    /* Granularity */
    memset(&options, 0x0, sizeof(options));
    options.granularity.eventset    = evtset;
    options.granularity.granularity = PW_GRN;
    if ((__pw_retval = PAPI_set_opt(PAPI_GRANUL, &options)) != PAPI_OK)
    {
        pw_dprintf(PW_D_WARNING, "[WARNING] Granularity could not be set!");
    }
}

/**
 * @brief Auxiliary function to concat strings
 */
char *
concat(const char *s1, const char *s2)
{
    char *result =
        malloc(strlen(s1) + strlen(s2) + 1); // +1 for the null-terminator
    // in real code you would check for errors in malloc here
    strcpy(result, s1);
    strcat(result, s2);
    return result;
}

/* Core functions */

/**
 * @brief PAPI initialization
 *
 * @note This function must be called
 */
void
pw_init()
{
#if defined(PW_MULTITHREAD) && !defined(_OPENMP)
    PW_error(__FILE__,
             __LINE__,
             "pw_init(): -DPW_MULTITHREAD missing -fopenmp compilation flag",
             PAPI_EINVAL);
#endif
    int __pw_retval;
    int k;
#if defined(_OPENMP)
#    pragma omp parallel
    {
        int __pw_nthread  = omp_get_thread_num();
        int __pw_nthreads = omp_get_num_threads();
#    if !defined(PW_MULTITHREAD)
#        pragma omp master
        {
            if (__pw_nthreads < pw_counters_threadid)
                pw_counters_threadid = __pw_nthreads - 1;
        }
#        pragma omp barrier
        if (__pw_nthread == pw_counters_threadid)
        {
#    endif
#endif
#if defined(PW_MULTITHREAD)
#    pragma omp master
            {
                if ((__pw_retval = PAPI_set_debug(PAPI_VERB_ESTOP)) != PAPI_OK)
                    PW_error(__FILE__, __LINE__, "PAPI_set_debug", __pw_retval);
                if ((__pw_retval = PAPI_library_init(PAPI_VER_CURRENT))
                    != PAPI_VER_CURRENT)
                    if ((__pw_retval = PAPI_thread_init((void *)pthread_self))
                        != PAPI_OK)
                        PW_error(__FILE__,
                                 __LINE__,
                                 "PAPI_thread_init",
                                 __pw_retval);
#    if defined(PAPI_VERSION) && (PAPI_VERSION_MAJOR(PAPI_VERSION) < 6)
                pw_get_num_ctrs();
#    endif
                if ((__pw_retval = PAPI_set_granularity(PW_GRN)) != PAPI_OK)
                    PW_error(__FILE__,
                             __LINE__,
                             "PAPI_set_granularity",
                             __pw_retval);
                pw_dprintf(PW_D_LOW,
                           "pw_init(); __pw_th = %2d\tNthreads = %2d",
                           omp_get_thread_num(),
                           __pw_nthreads);
                PW_thread = (PW_thread_info_t *)malloc(sizeof(PW_thread_info_t)
                                                       * __pw_nthreads);
                int th    = 0;
                for (th = 0; th < __pw_nthreads; ++th)
                {

                    if (__PW_NSUBREGIONS != -1)
                    {
                        PW_thread[th].pw_subregions =
                            (PW_thread_subregion_t *)malloc(
                                sizeof(PW_thread_subregion_t)
                                * __PW_NSUBREGIONS);
                        for (int subreg = 0; subreg < __PW_NSUBREGIONS;
                             ++subreg)
                        {
                            PW_thread[th].pw_subregions[subreg].pw_values =
                                (long long *)calloc(PW_MAX_COUNTERS,
                                                    sizeof(long long));
                        }
                    }
                    PW_thread[th].pw_values =
                        (long long *)calloc(PW_MAX_COUNTERS, sizeof(long long));
                    PW_thread[th].pw_eventset =
                        (int *)calloc(PW_NUM_EVTSET, sizeof(int));
                    PW_thread[th].pw_eventlist =
                        (int *)calloc(PW_MAX_COUNTERS, sizeof(int));
#    if defined(PW_SAMPLING)
                    PW_thread[th].pw_overflows =
                        (long long *)calloc(PW_MAX_COUNTERS, sizeof(long long));
#    endif
                }
                pw_eventlist = (int *)malloc(sizeof(int) * PW_MAX_COUNTERS);
                for (k = 0; _pw_eventlist[k] != NULL; ++k)
                {
                    pw_eventlist[k] = PAPI_NULL;
                    if ((__pw_retval = PAPI_event_name_to_code(
                             _pw_eventlist[k], &(pw_eventlist[k])))
                        != PAPI_OK)
                        PW_error(__FILE__,
                                 __LINE__,
                                 "PAPI_event_name_to_code",
                                 __pw_retval);
                }
                pw_eventlist[k] = 0;
            }
#    pragma omp barrier
#    pragma omp critical
            {
                __pw_nthread  = omp_get_thread_num();
                int __pw_evid = 0;
//                for (k = 0; _pw_eventlist[k] != NULL; ++k)
//                {
//                    pw_dprintf(PW_D_LOW,
//                               "%d: adding event %s ",
//                               __pw_nthread,
//                               _pw_eventlist[k]);
//
//                    if ((__pw_retval = PAPI_event_name_to_code(
//                             _pw_eventlist[k], &(PW_EVTLST(__pw_nthread, k))))
//                        != PAPI_OK)
//                        PW_error(__FILE__,
//                                 __LINE__,
//                                 "PAPI_event_name_to_code",
//                                 __pw_retval);
//                    pw_dprintf(PW_D_LOW,
//                               "%d: added %s as %d",
//                               __pw_nthread,
//                               _pw_eventlist[k],
//                               PW_EVTLST(__pw_nthread, k));
//                }
//                PW_EVTLST(__pw_nthread, k) = 0;
#    if PW_EXEC_MODE == PW_SNG_EXC
                for (__pw_evid = 0; PW_EVTLST(__pw_nthread, __pw_evid) != 0;
                     __pw_evid++)
                {
#    endif
                    pw_dprintf(PW_D_LOW,
                               "%2d thread; %2d __pw_evid",
                               __pw_nthread,
                               __pw_evid);
                    PW_EVTSET(__pw_nthread, __pw_evid) = PAPI_NULL;
                    if ((__pw_retval = PAPI_create_eventset(
                             &(PW_EVTSET(__pw_nthread, __pw_evid))))
                        != PAPI_OK)
                        PW_error(__FILE__,
                                 __LINE__,
                                 "PAPI_create_eventset",
                                 __pw_retval);
                    if (pw_multiplexing)
                    {
                        if ((__pw_retval =
                                 PAPI_assign_eventset_component(
                                     (PW_EVTSET(__pw_nthread, __pw_evid)), 0)
                                 != PAPI_OK))
                            PW_error(__FILE__,
                                     __LINE__,
                                     "PAPI_assign_eventset_component",
                                     __pw_retval);
                        __pw_retval = PAPI_get_multiplex(
                            (PW_EVTSET(__pw_nthread, __pw_evid)));
                        if (__pw_retval > 0)
                            pw_dprintf(PW_D_LOW,
                                       "This event set is ready for "
                                       "multiplexing\n");
                        if (__pw_retval == 0)
                        {
                            pw_dprintf(PW_D_LOW,
                                       "This event set is not enabled for "
                                       "multiplexing (thread %d)",
                                       omp_get_thread_num());
                        }
                        if (__pw_retval < 0)
                            PW_error(__FILE__,
                                     __LINE__,
                                     "PAPI_set_multiplex",
                                     __pw_retval);
                        __pw_retval = PAPI_set_multiplex(
                            (PW_EVTSET(__pw_nthread, __pw_evid)) != PAPI_OK);
                        if (((__pw_retval == PAPI_EINVAL)
                             && (PAPI_get_multiplex(
                                 PW_EVTSET(__pw_nthread, __pw_evid) == 1))))
                        {
                            pw_dprintf(PW_D_LOW,
                                       "PAPI_set_multiplex already enabled",
                                       __pw_retval);
                        } else if (__pw_retval != PAPI_OK)
                        {
                            PW_error(__FILE__,
                                     __LINE__,
                                     "PAPI_set_multiplex",
                                     __pw_retval);
                        }
                        if ((__pw_retval = PAPI_register_thread()) != PAPI_OK)
                        {
                            PW_error(__FILE__,
                                     __LINE__,
                                     "PAPI_register_thread",
                                     __pw_retval);
                        }
                    }
#    if (PW_EXEC_MODE == PW_SNG_EXC)
                }
#    endif
            }
#    pragma omp barrier
#else
    PW_thread  = (PW_thread_info_t *)malloc(sizeof(PW_thread_info_t));
    if (__PW_NSUBREGIONS != -1)
    {
        PW_thread[0].pw_subregions = (PW_thread_subregion_t *)malloc(
            sizeof(PW_thread_subregion_t) * __PW_NSUBREGIONS);
        for (int subreg = 0; subreg < __PW_NSUBREGIONS; ++subreg)
        {
            PW_thread[0].pw_subregions[subreg].pw_values =
                (long long *)calloc(PW_MAX_COUNTERS, sizeof(long long));
        }
    }
    pw_eventset = PAPI_NULL;
    if ((__pw_retval = PAPI_library_init(PAPI_VER_CURRENT)) != PAPI_VER_CURRENT)
        PW_error(__FILE__, __LINE__, "PAPI_library_init", __pw_retval);
    if ((__pw_retval = PAPI_create_eventset(&pw_eventset)) != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_create_eventset", __pw_retval);
    pw_eventlist = (int *)calloc(PW_NUM_EVTSET, sizeof(int));
    for (k = 0; _pw_eventlist[k] != NULL; ++k)
    {
        pw_eventlist[k] = PAPI_NULL;
        if ((__pw_retval =
                 PAPI_event_name_to_code(_pw_eventlist[k], &(pw_eventlist[k])))
            != PAPI_OK)
            PW_error(
                __FILE__, __LINE__, "PAPI_event_name_to_code", __pw_retval);
    }
    pw_eventlist[k] = 0;
#endif
#if defined(_OPENMP)
#    if !defined(PW_MULTITHREAD)
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
#if defined(_OPENMP)
#    pragma omp parallel
    {
#    if defined(PW_MULTITHREAD)
#        pragma omp master
#    else
        if (omp_get_thread_num() == pw_counters_threadid)
#    endif
        {
            if (PAPI_is_initialized()) PAPI_shutdown();
        }
    }
#else
    if (PAPI_is_initialized()) PAPI_shutdown();
#endif
}

/**
 * @brief Start each event individually, called when PW_SNG_EXEC mode
 * activated.
 *
 */
int
pw_start_counter(int __pw_evid)
{
#if defined(_OPENMP)
#    pragma omp parallel
    {
        int __pw_nthread = omp_get_thread_num();
#    pragma omp barrier
#    if !defined(PW_MULTITHREAD)
        if (__pw_nthread == pw_counters_threadid)
        {
#    endif
#endif
            int               __pw_retval;
            PAPI_event_info_t evinfo;
#if defined(PW_MULTITHREAD)
#    pragma omp critical
            {
                __pw_nthread = omp_get_thread_num();
                if ((__pw_retval =
                         PAPI_add_event(PW_EVTSET(__pw_nthread, __pw_evid),
                                        PW_EVTLST(__pw_nthread, __pw_evid)))
                    != PAPI_OK)
                    PW_error(__FILE__, __LINE__, "PAPI_add_event", __pw_retval);
                if ((__pw_retval = PAPI_get_event_info(
                         PW_EVTLST(__pw_nthread, __pw_evid), &evinfo))
                    != PAPI_OK)
                    PW_error(
                        __FILE__, __LINE__, "PAPI_get_event_info", __pw_retval);
                pw_set_opts(__pw_nthread, __pw_evid);
#    if defined(PW_SAMPLING)
                if ((__pw_retval =
                         PAPI_overflow(PW_EVTSET(__pw_nthread, __pw_evid),
                                       PW_EVTLST(__pw_nthread, __pw_evid),
                                       _pw_samplinglist[__pw_evid],
                                       PW_OVRFLW_TYPE,
                                       pw_overflow_handler))
                    != PAPI_OK)
                    PW_error(__FILE__, __LINE__, "PAPI_overflow", __pw_retval);
#    endif
            }
#    pragma omp barrier
            if ((__pw_retval = PAPI_start(PW_EVTSET(__pw_nthread, __pw_evid)))
                != PAPI_OK)
                PW_error(__FILE__, __LINE__, "PAPI_start", __pw_retval);
#else
    if ((__pw_retval = PAPI_add_event(pw_eventset, pw_eventlist[__pw_evid]))
        != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_add_event", __pw_retval);
    if ((__pw_retval = PAPI_get_event_info(pw_eventlist[__pw_evid], &evinfo))
        != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_get_event_info", __pw_retval);
    if ((__pw_retval = PAPI_start(pw_eventset)) != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_start", __pw_retval);
#endif
#if defined(_OPENMP)
#    if !defined(PW_MULTITHREAD)
        }
#    endif
    }
#    pragma omp barrier
#endif
    return PW_SUCCESS;
}

/**
 * @brief Stop each counter individually
 *
 * @param __pw_evid Counter id
 */
void
pw_stop_counter(int __pw_evid)
{
#if defined(_OPENMP)
#    pragma omp parallel
    {
#    if !defined(PW_MULTITHREAD)
        if (omp_get_thread_num() == pw_counters_threadid)
        {
#    endif
#endif
            int __pw_retval;
#if defined(PW_MULTITHREAD)
            long long *values = NULL;

            int __pw_nthread = omp_get_thread_num();
#    if defined(PW_SAMPLING)
            if ((__pw_retval =
                     PAPI_accum(PW_EVTSET(__pw_nthread, __pw_evid),
                                &(PW_VALUES(__pw_nthread, __pw_evid))))
                != PAPI_OK)
                PW_error(__FILE__, __LINE__, "PAPI_accum", __pw_retval);
            PW_VALUES(__pw_nthread, __pw_evid) +=
                (PW_OVRFLW(__pw_nthread, __pw_evid)
                 * _pw_samplinglist[__pw_evid]);
#    else
        values = &PW_VALUES(__pw_nthread, __pw_evid);
#    endif
            if ((__pw_retval =
                     PAPI_stop(PW_EVTSET(__pw_nthread, __pw_evid), &values[0]))
                != PAPI_OK)
                PW_error(__FILE__, __LINE__, "PAPI_stop", __pw_retval);
            if ((__pw_retval =
                     PAPI_cleanup_eventset(PW_EVTSET(__pw_nthread, __pw_evid)))
                != PAPI_OK)
                PW_error(
                    __FILE__, __LINE__, "PAPI_cleanup_eventset", __pw_retval);
            if ((__pw_retval = PAPI_destroy_eventset(
                     &(PW_EVTSET(__pw_nthread, __pw_evid))))
                != PAPI_OK)
                PW_error(
                    __FILE__, __LINE__, "PAPI_destroy_eventset", __pw_retval);
#else
    long long values[0];
    if ((__pw_retval = PAPI_read(pw_eventset, &values[0])) != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_read", __pw_retval);
    if ((__pw_retval = PAPI_stop(pw_eventset, NULL)) != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_stop", __pw_retval);
    pw_values[__pw_evid] = values[0];
    if ((__pw_retval = PAPI_remove_event(pw_eventset, pw_eventlist[__pw_evid]))
        != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_remove_event", __pw_retval);
#endif
#if defined(_OPENMP)
#    if !defined(PW_MULTITHREAD)
        }
#    endif
    }
#    pragma omp barrier
#endif
}

/**
 * @brief Starts all counters for a thread
 *
 * @param __pw_evid Counter ID
 * @param __pw_th Thread ID
 */
int
pw_start_counter_thread(int __pw_evid, int __pw_th)
{
#if !defined(_OPENMP) || !defined(PW_MULTITHREAD)
    PW_error(__FILE__,
             __LINE__,
             "pw_start_counter_thread: need -fopenmp at least",
             PAPI_EINVAL);
#endif
    int __pw_retval;

#if defined(PW_MULTITHREAD)
#    pragma omp barrier
#    pragma omp critical
    {
        int __pw_nthread = __pw_th;

        pw_dprintf(PW_D_LOW,
                   "pw_start_counter_thread(); __pw_th = %2d __pw_evid = %2d\n",
                   __pw_nthread,
                   __pw_evid);
        if ((__pw_retval = PAPI_add_event(PW_EVTSET(__pw_nthread, __pw_evid),
                                          PW_EVTLST(__pw_nthread, __pw_evid)))
            != PAPI_OK)
            PW_error(__FILE__, __LINE__, "PAPI_add_event", __pw_retval);
        if ((__pw_retval = PAPI_start(PW_EVTSET(__pw_nthread, __pw_evid)))
            != PAPI_OK)
            PW_error(__FILE__, __LINE__, "PAPI_start", __pw_retval);
    }
#else
#    if defined(_OPENMP)
    if (omp_get_thread_num() == pw_counters_threadid)
    {
#    endif
        if ((__pw_retval = PAPI_add_event(pw_eventset, pw_eventlist[__pw_evid]))
            != PAPI_OK)
            PW_error(__FILE__, __LINE__, "PAPI_add_event", __pw_retval);
        if ((__pw_retval = PAPI_start(pw_eventset)) != PAPI_OK)
            PW_error(__FILE__, __LINE__, "PAPI_start", __pw_retval);
#    if defined(_OPENMP)
    }
#        pragma omp barrier
#    endif
#endif

    return PW_SUCCESS;
}

/**
 * @brief Stop each counter individually
 *
 * @param __pw_evid Counter id
 */
void
pw_stop_counter_thread(int __pw_evid, int __pw_th)
{
#if !defined(_OPENMP) || !defined(PW_MULTITHREAD)
    PW_error(__FILE__,
             __LINE__,
             "pw_start_counter_thread: need -fopenmp at least",
             PAPI_EINVAL);
#endif
    int __pw_retval;
#if defined(PW_MULTITHREAD)
    long long *values       = NULL;
    int        __pw_nthread = __pw_th;
    values                  = &PW_VALUES(__pw_nthread, __pw_evid);
    if ((__pw_retval =
             PAPI_stop(PW_EVTSET(__pw_nthread, __pw_evid), &values[0]))
        != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_stop", __pw_retval);
    if ((__pw_retval = PAPI_remove_event(PW_EVTSET(__pw_nthread, __pw_evid),
                                         PW_EVTLST(__pw_nthread, __pw_evid)))
        != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_cleanup_eventset", __pw_retval);
    pw_dprintf(
        PW_D_LOW, "pw_stop_counter_thread(); __pw_th = %2d\n", __pw_nthread);
    if ((__pw_retval =
             PAPI_cleanup_eventset(PW_EVTSET(__pw_nthread, __pw_evid)))
        != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_cleanup_eventset", __pw_retval);
    if ((__pw_retval =
             PAPI_destroy_eventset(&(PW_EVTSET(__pw_nthread, __pw_evid))))
        != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_destroy_eventset", __pw_retval);
#    pragma omp barrier

#else
#    if defined(_OPENMP)
    if (omp_get_thread_num() == pw_counters_threadid)
    {
#    endif
        long long values[0];
        if ((__pw_retval = PAPI_read(pw_eventset, &values[0])) != PAPI_OK)
            PW_error(__FILE__, __LINE__, "PAPI_read", __pw_retval);
        if ((__pw_retval = PAPI_stop(pw_eventset, NULL)) != PAPI_OK)
            PW_error(__FILE__, __LINE__, "PAPI_stop", __pw_retval);
        pw_values[__pw_evid] = values[0];
        if ((__pw_retval =
                 PAPI_remove_event(pw_eventset, pw_eventlist[__pw_evid]))
            != PAPI_OK)
            PW_error(__FILE__, __LINE__, "PAPI_remove_event", __pw_retval);
#    if defined(_OPENMP)
    }
#        pragma omp barrier
#    endif
#endif
}

/**
 * @brief Begin measuring subregion
 */
void
pw_begin_counter_subregion(int __pw_evid, int __pw_subreg_n)
{
    if (__PW_NSUBREGIONS == -1)
    {
        PW_error(__FILE__,
                 __LINE__,
                 "pw_begin_counter_subregion: -1 subregions, must set",
                 PAPI_EINVAL);
    } else if (__PW_NSUBREGIONS <= __pw_subreg_n)
    {
        PW_error(__FILE__,
                 __LINE__,
                 "pw_begin_counter_subregion: subregion number above "
                 "the specified",
                 PAPI_EINVAL);
    }
#if defined(_OPENMP)
    int __pw_nthread = omp_get_thread_num();
#    if !defined(PW_MULTITHREAD)
    if (__pw_nthread == pw_counters_threadid)
    {
#    endif
#endif
        int __pw_retval;
#if defined(PW_MULTITHREAD)
        if ((__pw_retval =
                 PAPI_read(
                     PW_EVTSET(__pw_nthread, __pw_evid),
                     &PW_SUBREG_DELTA(__pw_nthread, __pw_evid, __pw_subreg_n))
                 != PAPI_OK))
            PW_error(__FILE__, __LINE__, "PAPI_read", __pw_retval);

#else
    pw_dprintf(PW_D_LOW,
               "pw_begin_subregion(); __pw_th = %2d __pw_evid = %2d",
               0,
               __pw_evid);
    if ((__pw_retval = PAPI_read(pw_eventset,
                                 &PW_SUBREG_DELTA(0, __pw_evid, __pw_subreg_n))
                       != PAPI_OK))
        PW_error(__FILE__, __LINE__, "PAPI_read", __pw_retval);
#endif
#if defined(_OPENMP)
#    if !defined(PW_MULTITHREAD)
    }
#        pragma omp barrier
#    endif
#endif
}

/**
 * @brief Read counter in a concrete subregion
 *
 */
void
pw_end_counter_subregion(int __pw_evid, int __pw_subreg_n)
{
    if (__PW_NSUBREGIONS == -1)
    {
        PW_error(__FILE__,
                 __LINE__,
                 "pw_begin_counter_subregion: -1 subregions, must set",
                 PAPI_EINVAL);
    } else if (__PW_NSUBREGIONS <= __pw_subreg_n)
    {
        PW_error(__FILE__,
                 __LINE__,
                 "pw_begin_counter_subregion: subregion number above the "
                 "specified",
                 PAPI_EINVAL);
    }
    int       __pw_retval;
    long long values[1] = {0};
#if defined(_OPENMP)
    int __pw_nthread = omp_get_thread_num();
#    if !defined(PW_MULTITHREAD)
    if (__pw_nthread == pw_counters_threadid)
    {
#    endif
#endif
#if defined(PW_MULTITHREAD)
        if ((__pw_retval =
                 PAPI_read(PW_EVTSET(__pw_nthread, __pw_evid), &values[0]))
            != PAPI_OK)
            PW_error(__FILE__, __LINE__, "PAPI_read", __pw_retval);
        PW_SUBREG_VAL(__pw_nthread, __pw_evid, __pw_subreg_n) +=
            (values[0]
             - PW_SUBREG_DELTA(__pw_nthread, __pw_evid, __pw_subreg_n));
#else
    if ((__pw_retval = PAPI_read(pw_eventset, &values[0])) != PAPI_OK)
        PW_error(__FILE__, __LINE__, "PAPI_read", __pw_retval);
    PW_SUBREG_VAL(0, __pw_evid, __pw_subreg_n) +=
        (values[0] - PW_SUBREG_DELTA(0, __pw_evid, __pw_subreg_n));
#endif
#if defined(_OPENMP)
#    if !defined(PW_MULTITHREAD)
    }
#        pragma omp barrier
#    endif
#endif
}

/**
 * @brief Print values of each thread with format
 */
void
pw_print_values_format()
{
}

/**
 * @brief Printing the values of the counters
 *
 */
void
pw_print()
{
    int verbose = 0;
#if defined(PW_VERBOSE) && !defined(PW_CSV)
    verbose = 1;
#endif
#if defined(_OPENMP)
#    if !defined(PW_MULTITHREAD)
#        pragma omp parallel
    {
        if (omp_get_thread_num() == pw_counters_threadid)
        {
#    endif
#endif
            int __pw_evid;

#if defined(PW_CSV)
            printf("PAPI_thread");
            for (__pw_evid = 0; _pw_eventlist[__pw_evid] != NULL; ++__pw_evid)
            {
                printf("%s%s", PW_CSV_SEPARATOR, _pw_eventlist[__pw_evid]);
            }
            printf("\n");
#endif
#if defined(PW_MULTITHREAD)
            int __pw_nthreads = 1;
#    pragma omp parallel
            {
#    pragma omp master
                {
                    __pw_nthreads = omp_get_num_threads();
                }
            }
            int __pw_nthread = 0;
#    pragma omp for ordered schedule(static, 1)
            for (__pw_nthread = 0; __pw_nthread < __pw_nthreads; ++__pw_nthread)
            {
                int __pw_evid;
#    if defined(PW_CSV)
                printf("%d", __pw_nthread);
#    else
        printf("PAPI thread %2d\t", __pw_nthread);
#    endif
                for (__pw_evid = 0; PW_EVTLST(__pw_nthread, __pw_evid) != 0;
                     ++__pw_evid)
                {
                    if (verbose) printf("%s=", _pw_eventlist[__pw_evid]);
                    printf("%s%llu",
                           PW_CSV_SEPARATOR,
                           PW_VALUES(__pw_nthread, __pw_evid));
                    if (verbose) printf("\n");
                }
                printf("\n");
            }
#    pragma omp barrier
#    pragma omp master
            {
                free(PW_thread);
            }
#else
#    if defined(PW_CSV)
    printf("%d", pw_counters_threadid);
#    else
    printf("PAPI thread %2d\t", pw_counters_threadid);
#    endif
    for (__pw_evid = 0; pw_eventlist[__pw_evid] != 0; ++__pw_evid)
    {
        if (verbose) printf("%s=", _pw_eventlist[__pw_evid]);
        printf("%s%llu", PW_CSV_SEPARATOR, pw_values[__pw_evid]);
        if (verbose) printf("\n");
    }
    printf("\n");
#endif
#if defined(_OPENMP)
#    if !defined(PW_MULTITHREAD)
        }
    }
#        pragma omp barrier
#    endif
#endif
}

/**
 * @brief Printing the values of the counters and its subregions
 *
 */
void
pw_print_sub()
{
    if (__PW_NSUBREGIONS == -1)
    {
        PW_error(__FILE__,
                 __LINE__,
                 "pw_print_sub: no subregions to print",
                 PAPI_EINVAL);
    }
    int verbose = 0;
#if defined(PW_VERBOSE) && !defined(PW_CSV)
    verbose = 1;
#endif
#if defined(_OPENMP)
#    if !defined(PW_MULTITHREAD)
#        pragma omp parallel
    {
        if (omp_get_thread_num() == pw_counters_threadid)
        {
#    endif
#endif
            int __pw_evid;

#if defined(PW_CSV)
            printf("PAPI_thread");
            for (__pw_evid = 0; _pw_eventlist[__pw_evid] != NULL; ++__pw_evid)
            {
                printf("%s%s", PW_CSV_SEPARATOR, _pw_eventlist[__pw_evid]);
            }
            printf("\n");
#endif
#if defined(PW_MULTITHREAD)
            int __pw_nthreads = 1;
#    pragma omp parallel
            {
#    pragma omp master
                {
                    __pw_nthreads = omp_get_num_threads();
                }
            }
            int __pw_nthread = 0;
#    pragma omp for ordered schedule(static, 1)
            for (int __pw_subreg = 0; __pw_subreg < __PW_NSUBREGIONS;
                 ++__pw_subreg)
            {
                printf("== BEGIN SUBREGION %d ==\n", __pw_subreg);
                for (__pw_nthread = 0; __pw_nthread < __pw_nthreads;
                     ++__pw_nthread)
                {
#    if defined(PW_CSV)
                    printf("%d", __pw_nthread);
#    else
            printf("PAPI thread %2d\t", __pw_nthread);
#    endif
                    for (__pw_evid = 0; PW_EVTLST(__pw_nthread, __pw_evid) != 0;
                         ++__pw_evid)
                    {
                        if (verbose) printf("%s=", _pw_eventlist[__pw_evid]);
                        printf("%s%llu",
                               PW_CSV_SEPARATOR,
                               PW_SUBREG_VAL(
                                   __pw_nthread, __pw_evid, __pw_subreg));
                        if (verbose) printf("\n");
                    }
                    printf("\n");
                }
                printf("== END SUBREGION %d ==\n", __pw_subreg);
            }
#    pragma omp barrier
#    pragma omp master
            {
                free(PW_thread);
            }
#else
#    if defined(PW_CSV)
    printf("%d", pw_counters_threadid);
#    else
    printf("PAPI thread %2d\t", pw_counters_threadid);
#    endif
    for (__pw_evid = 0; pw_eventlist[__pw_evid] != 0; ++__pw_evid)
    {
        if (verbose) printf("%s=", _pw_eventlist[__pw_evid]);
        printf("%s%llu", PW_CSV_SEPARATOR, pw_values[__pw_evid]);
        if (verbose) printf("\n");
    }
    printf("\n");
#endif
#if defined(_OPENMP)
#    if !defined(PW_MULTITHREAD)
        }
    }
#        pragma omp barrier
#    endif
#endif
}
