# papi_wrapper
Simple wrapper for PAPI most common used functions

PAPI's low-level API allows programmers to program different hardware counters while executing a program. However, programming those counters may introduce a lot of complex code onto the original program. Besides, configuring properly PAPI may be tedious. For these reasons we have created a set of macros based on [PolyBench](https://sourceforge.net/projects/polybench/)'s in order to simplify the problem.

This interface also works for multithreaded program (OpenMP). See [Usage](#usage) and [Options](#options) sections for further details.

## Usage

It is only needed to rewrite our code as:

```
papi_start /* starts automatically PAPI counters */

/* region of interest (ROI) to measure */

papi_stop /* stop counting */
papi_print /* print results */
```

This way, it is only needed to add the header `#include "papi_wrapper.h"` to the source code and compile with `-I/source/to/papi-wrapper /source/to/papi-wrapper/papi_wrapper.c`.

## Options

Most remarkable options are:

 * `-DPAPI_MULTITHREAD` -- enabled by default when compiling with `-fopenmp`
 * `-DSAMPLING_RATE=<N>` *experimental* -- disabled by default. Allows to modify the sampling rate by triggering an overflow. *N* stands for number of events occurred.
