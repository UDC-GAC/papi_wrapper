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
#ifndef PAPIWRAP_H
#define PAPIWRAP_H

#ifndef PAPI_FILE_LIST
# define "papi_counters.list"
#endif

extern const unsigned int papiwrap_eventlist[];
#undef papiwrap_start
#undef papiwrap_stop
#undef papiwrap_print
#define papiwrap_set_thread_report(x)                     \
  papiwrap_counters_threadid = x;                         \
#define papiwrap_start                                    \
  papiwrap_prepare();                                     \
  papiwrap_init();                                        \
  int evid;                                               \
  for (evid = 0; papiwrap_eventlist[evid] != 0; evid++) { \
    if (papiwrap_start_counter(evid)) continue;

#define papiwrap_stop_instruments   \
  papiwrap_stop_counter(evid); \
  }                                  \
  papiwrap_close();

#define papiwrap_print_instruments papiwrap_rint();

extern int papiwrap_start_counter(int evid);
extern void papiwrap_stop_counter(int evid);
extern void papiwrap_init();
extern void papiwrap_close();
extern void papiwrap_print();

#endif /* !PAPIWRAP_H */
