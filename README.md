# papi_wrapper
Simple wrapper for PAPI most common used functions: set up events, start counters, stop counters. Also simplifying setting up some options such as domain, granularity or overflow.

PAPI's low-level API allows programmers to program different hardware counters while executing a program. However, programming those counters may introduce a lot of complex code onto the original program. Besides, configuring properly PAPI may be tedious. For these reasons we have created a set of macros in order to simplify the problem, while configuring PAPI at compilation time: either using flags or files.

This interface works for multithreaded programs using OpenMP; actually this was the main reason for developing this interface. See [Usage](#usage) and [Options](#options) sections for further details.

## Usage

It is only needed to rewrite our code as:

```
#include <papi_wrapper.h>
...
pw_start_instruments /* starts automatically PAPI counters */

/* region of interest (ROI) to measure */

pw_stop_instruments /* stop counting */
pw_print_instruments /* print results */
```

This way, it is only needed to add the header `#include <papi_wrapper.h>` to the source code and compile with `-I/source/to/papi-wrapper /source/to/papi-wrapper/papi_wrapper.c`.

## Options

Configuration files (see their format in the repo):
 * `-DPAPI_FILE_LIST=<file>`. Default: `papi_counters.list`. Contains the name of PAPI events to count.
 * `-DPAPI_FILE_SAMPLING=<file>`. Default: `papi_sampling.list`. Contains the threshold of counters specified in `PAPI_FILE_LIST`. Useless if `PW_SAMPLING` not specified.

Configuration parameters:
 * `-DPAPI_MULTITHREAD` --- disabled by default. If not defined, only `PAPIWRAP_THREAD_MONITOR` will count events.
 * `-DPAPI_VERBOSE` --- disabled by default. More text in the output and errors.
 * `-DPW_GRN=<granularity>` --- by default: `PAPI_GRN_MIN`.
 * `-DPW_DOM=<domain>` --- by default: `PAPI_DOM_ALL`.
 * `-DPW_SAMPLING` --- enables sampling for all the events specified in `PAPI_FILE_LIST` with thresholds specified in `PAPI_FILE_SAMPLING`.

## Roadmap
Refer to project `roadmap` in projects.

## Credits
This version is based on [PolyBench](https://sourceforge.net/projects/polybench/), under GPLv2 license.
