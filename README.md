# papi_wrapper
Simple wrapper for PAPI most common used functions

PAPI's low-level API allows programmers to program different hardware counters while executing a program. However, programming those counters may introduce a lot of complex code onto the original program. Besides, configuring properly PAPI may be tedious. For these reasons we have created a set of macros based on [PolyBench](https://sourceforge.net/projects/polybench/)'s in order to simplify the problem.

This interface also works for multithreaded programs using OpenMP. See [Usage](#usage) and [Options](#options) sections for further details.

## Usage

It is only needed to rewrite our code as:

```
#include <papi_wrapper.h>
...
papiwrap_start_instruments /* starts automatically PAPI counters */

/* region of interest (ROI) to measure */

papiwrap_stop_instruments /* stop counting */
papiwrap_print_instruments /* print results */
```

This way, it is only needed to add the header `#include <papi_wrapper.h>` to the source code and compile with `-DPAPIWRAP -I/source/to/papi-wrapper /source/to/papi-wrapper/papi_wrapper.c`.

It is also to specify a list of the program counters (`-DPAPI_FILE_LIST=<file>`); by default: `papi_counters.list` (see its format inside the file).

## Options

Most remarkable options are:

 * `-DPAPI_MULTITHREAD` -- disabled by default. If not defined, only `PAPIWRAP_THREAD_MONITOR` will count events.
 * `-DPAPI_VERBOSE` -- disabled by default. Displays each counter in one line with its name.
 * `-DSAMPLING_RATE=<N>` *future release* -- disabled by default. Allows to modify the sampling rate by triggering an overflow. *N* stands for number of events occurred.

## Credits

This version is based on [PolyBench](https://sourceforge.net/projects/polybench/), under GPLv2 license. 
