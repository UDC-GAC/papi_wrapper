/**
 * papi_wrapper.h
 * Copyright (c) 2019 Marcos Horro <marcos.horro@udc.gal>
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
#ifndef PAPI_WRAPPER_H
#define PAPI_WRAPPER_H

/* Need to compile with -lpapi flag */
#include <papi.h>

/* Defined macros */
#define PW_D_LOW 0x01
#define PW_D_MED 0x02
#define PW_D_HIGH 0x03

#define PW_SNG_EXC 0x10
#define PW_ALL_EXC 0x11

#define PW_NUM_EVTSET 30
#define PW_MAX_COUNTERS 96

/* Debug mode */
#ifdef DEBUG
#define PW_DEBUG
#endif

#ifdef PW_DEBUG
#ifndef PW_DEBUG_LVL
#define PW_DEBUG_LVL PW_D_LOW
#endif
#endif

/* Default execution mode (-DPW_EXEC_MODE): single event (slow mode) */
#ifndef PW_EXEC_MODE
#define PW_EXEC_MODE PW_SNG_EXC
#endif

/* Default granularity (-DPW_GRN) for all EventSets*/
#ifndef PW_GRN
#define PW_GRN PAPI_GRN_THR
#endif

/* Default domain (-DPW_DOM) for each EventSet */
#ifndef PW_DOM
#define PW_DOM PAPI_DOM_KERNEL
#endif

/**
 * @brief Struct to handle each PAPI thread info
 *
 * We have to store the EventSet, also the values obtained by all the different
 * counters. The same way, we also enable some attributes when sampling is
 * enabled.
 */
typedef struct PW_thread_info {
  int *pw_eventset;
  int pw_domain;
  long long *pw_values;
#ifdef PW_SAMPLING
  int pw_overflow_enabled;
  long long *pw_overflows;
#endif
} PW_thread_info_t;

/* Useful macros */
#define PW_VALUES(n_thread, evid) (PW_thread[n_thread].pw_values[evid])
#define PW_EVTSET(n_thread, evid) (PW_thread[n_thread].pw_eventset[evid])
#ifdef PW_SAMPLING
#define PW_OVRFLW_ON(n_thread) (PW_thread[n_thread].pw_overflow_enabled = 1)
#define PW_OVRFLW_OFF(n_thread) (PW_thread[n_thread].pw_overflow_enabled = 0)
#define PW_OVRFLW(n_thread, evid) (PW_thread[n_thread].pw_overflows[evid])
#define PW_OVRFLW_RST(n_thread, evid) \
  (PW_thread[n_thread].pw_overflows[evid] = 0)
/* This is the recommended type of overflowing with PAPI. See PAPI_overflow
 * manual for more details */
#define PW_OVRFLW_TYPE PAPI_OVERFLOW_FORCE_SW
#endif

/* File configurations */
#ifndef PAPI_FILE_LIST
#define PAPI_FILE_LIST "papi_counters.list"
#endif

#ifdef PW_SAMPLING
#ifndef PAPI_FILE_SAMPLING
#define PAPI_FILE_SAMPLING "papi_sampling.list"
#endif
#endif

/* some other declarations */
extern PW_thread_info_t *PW_thread;
extern int *pw_eventlist;
#define pw_set_thread_report(x) pw_counters_threadid = x;

#define pw_start_instruments_loop(th)               \
  int evid;                                         \
  for (evid = 0; pw_eventlist[evid] != 0; evid++) { \
    pw_prepare_instruments();                       \
    if (pw_start_counter_thread(evid, th)) continue;

#define pw_stop_instruments_loop(th) \
  pw_stop_counter_thread(evid, th);  \
  }

#define pw_init_instruments \
  pw_init();                \
  pw_prepare_instruments();

#define pw_start_instruments                        \
  int evid;                                         \
  for (evid = 0; pw_eventlist[evid] != 0; evid++) { \
    pw_prepare_instruments();                       \
    if (pw_start_counter(evid)) continue;

#define pw_init_start_instruments \
  pw_init_instruments;            \
  pw_start_instruments;

#define pw_stop_instruments \
  pw_stop_counter(evid);    \
  }

#define pw_print_instruments \
  pw_print();                \
  pw_close();

/* Function declarations */
extern void pw_prepare_instruments();
extern int pw_start_counter(int evid);
extern int pw_start_counter_thread(int evid, int th);
extern int pw_start_all_counters();
extern void pw_stop_counter(int evid);
extern void pw_stop_counter_thread(int evid, int th);
extern void pw_stop_all_counters();
extern void pw_init();
extern void pw_close();
extern void pw_print();

#endif /* !PAPI_WRAPPER_H */
