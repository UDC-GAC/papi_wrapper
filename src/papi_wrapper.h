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
#ifndef papi_wrapper_H
#define papi_wrapper_H

#ifndef PAPI_FILE_LIST
#    define PAPI_FILE_LIST "papi_counters.list"
#endif

// extern int **papi_wrapper_eventlist;
extern int *papi_wrapper_eventlist;
#define papi_wrapper_set_thread_report(x) papi_wrapper_counters_threadid = x;
#define papi_wrapper_start_instruments                        \
    papi_wrapper_prepare_instruments();                       \
    papi_wrapper_init();                                      \
    int evid;                                                 \
    for (evid = 0; papi_wrapper_eventlist[evid] != 0; evid++) \
    {                                                         \
        if (papi_wrapper_start_counter(evid)) continue;

#define papi_wrapper_stop_instruments \
    papi_wrapper_stop_counter(evid);  \
    }                                 \
    papi_wrapper_close();

#define papi_wrapper_print_instruments papi_wrapper_print();

extern int
papi_wrapper_start_counter(int evid);
extern void
papi_wrapper_stop_counter(int evid);
extern void
papi_wrapper_init();
extern void
papi_wrapper_close();
extern void
papi_wrapper_print();

#endif /* !papi_wrapper_H */
