/**
 * papi_wrapper.h
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
#ifndef PAPI_WRAPPER_H
#define PAPI_WRAPPER_H

#ifndef PAPI_FILE_LIST
#    define PAPI_FILE_LIST "papi_counters.list"
#endif

#ifdef PW_SAMPLING
#    ifndef PAPI_FILE_SAMPLING
#        define PAPI_FILE_SAMPLING "papi_sampling.list"
#    endif
#endif

#define PW_MAX_COUNTERS 96

typedef struct PW_thread_info
{
    int        pw_eventset;
    long long *pw_values;
#ifdef PW_SAMPLING
    long long *pw_overflows;
#endif
} PW_thread_info_t;

/* useful macros */
#define PW_VALUES(n_thread, evid) (PW_thread[n_thread].pw_values[evid])
#define PW_OVRFLW(n_threadi, evid) (PW_thread[n_thread].pw_overflows[evid])
#define PW_EVTSET(n_thread) (PW_thread[n_thread].pw_eventset)

extern PW_thread_info_t *PW_thread;
extern int *             pw_eventlist;
#define pw_set_thread_report(x) pw_counters_threadid = x;
#define pw_start_instruments                        \
    pw_prepare_instruments();                       \
    pw_init();                                      \
    int evid;                                       \
    for (evid = 0; pw_eventlist[evid] != 0; evid++) \
    {                                               \
        if (pw_start_counter(evid)) continue;

#define pw_stop_instruments \
    pw_stop_counter(evid);  \
    }                       \
    pw_close();

#define pw_print_instruments pw_print();

/* function declarations */
extern int
pw_start_counter(int evid);
extern void
pw_stop_counter(int evid);
extern void
pw_init();
extern void
pw_close();
extern void
pw_print();

#endif /* !PAPI_WRAPPER_H */
