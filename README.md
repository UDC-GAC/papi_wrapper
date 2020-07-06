# PW - PAPI wrapper 1.0.0

[![Build Status][travis-badge]][travis-link]
[![codecov][codecov-badge]][codecov-link]
[![MIT License][license-badge]](LICENSE.md)

Simple wrapper for PAPI most common used functions: set up events, start, stop
and print counters within a region of interest. Besides, within that region can
define many subregions in order to count them separately. Thus, this library
simplifies setting up low-level features of PAPI such as domain, granularity or
overflow.

PAPI's low-level API allows programmers to program different hardware counters
while executing a program. However, programming those counters may introduce a
lot of complex code onto the original program. Besides, configuring properly
PAPI may be tedious. For these reasons we have created a set of macros in order
to simplify the problem, while configuring PAPI at compilation time: either
using flags or files.

This interface works for multithreaded programs using OpenMP; actually this was
the main reason for developing this interface. See [Usage](#usage) and
[Options](#options) sections for further details.

## Usage

It is only needed to rewrite our code as ( `<papi.h>` header files are already
included in `papi_wrapper` ):

```
#include <papi_wrapper.h>
...
pw_init_instruments; /* initialize counters */
pw_start_instruments; /* starts automatically PAPI counters */

/* region of interest (ROI) to measure */

pw_stop_instruments; /* stop counting */
pw_print_instruments; /* print results */
```

This way, it is only needed to add the header `#include <papi_wrapper.h>` to
the source code and compile with `-I/source/to/papi-wrapper papi_wrapper.c
-lpapi`. Another way to use this wrapper library, using subregions, could be:

```
#include <papi_wrapper.h>
...
pw_init_start_instruments_sub(2); /* initialize counters */

/* region of interest (ROI) to measure */

pw_begin_subregion(0);
/* subregion 0 */
pw_end_subregion(0);

pw_begin_subregion(1);
/* subregion 1 */
pw_end_subregion(1);

pw_stop_instruments; /* stop counting */
pw_print_subregions; /* print results */
```

TL; DR: macros available in `papi_wrapper` :

 * `pw_set_thread_report` : set thread to measure when single thread.
 * `pw_init_instruments` : init PAPI and flush caches.
 * `pw_init_start_instruments` : wrapper for `pw_init_instruments` and
`pw_start_instruments`
 * `pw_init_start_instruments_sub(n)` : init library and set number of regions
   to measure
 * `pw_start_instruments` : start counting.
 * `pw_stop_instruments` : stop counting.
 * `pw_start_instruments_loop(n)` : to use within a parallel loop, e.g.
`#pragma omp parallel for` .
 * `pw_stop_instruments_loop(n)` : to use within a parallel loop, e.g.
`#pragma omp parallel for` .
 * `pw_begin_subregion(n)` : start measuring `n` region.
 * `pw_end_subregion(n)` : stop measuring `n` region.
 * `pw_print_instruments` : print counters.
 * `pw_print_subregions` : print counters by subregion measured.

For more examples, refer to `tests` subdirectory. They can be executed with
`CTest` .

## Dependencies

 * PAPI >=5.x
 * GCC C compiler >=8.x
 * [PAPI library](https://icl.utk.edu/papi/news/news.html?id=382)
 * Doxygen >=1.8

## Options

In order to differenciate PAPI macros and PAPI\_wrapper macros, as mnemonics
all PAPI\_wrapper options begin with the prefix `PW_` .

High-level configuration parameters:
 * `-DPW_THREAD_MONITOR` - default value `0` . Indicates the master thread if
`PW_MULTITHREAD` also enabled.
 * `-DPW_MULTITHREAD` - disabled by default. If not defined, only
`PW_THREAD_MONITOR` will count events (only one thread). This option is not
   compatible when using uncore events, it basically makes the PAPI library
   crash. Need to be compiled with `-fopenmp` .
 * `-DPW_VERBOSE` - disabled by default. More text in the output and errors.
 * `-DPW_CSV` - disabled by default. Print in CSV format using comma
( `-DPW_CSV_SEPARATOR=","` ) as divider where first row contains the thread number
   and the names of the hardware counters used, containing the following rows
   each thread and its counter values.

Low-level configuration parameters (refer to [PAPI](https://icl.utk.edu/papi/)
for further information):
 * `-DPW_GRN=<granularity>` - default value `PAPI_GRN_MIN` .
 * `-DPW_DOM=<domain>` - default value `PAPI_DOM_ALL` .
 * `-DPW_SAMPLING` - disabled by default. Enables sampling for all the events
   specified in `PW_FLIST` with thresholds specified in `PW_FSAMPLE` .

Configuration files (see their format incircleci/circleci-docs/tree/teesloane-patch-5

## Implementation details

PAPI wrapper may be precompiled and linked to your executable or compiled
directly with your sources. PAPI wrapper basically initializes a PAPI event set
for each counter specified in the `PW_FLIST` . If multithread is enabled, then
all threads will count events individually and simultaneously, but one counter
at a time. Multiplexing is a experimental feature that should be avoided in
PAPI, since in [PAPI's discussions there is some skepticism regarding its
reliability](https://groups.google.com/a/icl.utk.edu/forum/#!searchin/ptools-perfapi/multiplexing%7Csort:date/ptools-perfapi/gi3e0EBVRGo/2x5kB3dEDwAJ).

## Overhead

When talking about the overhead of a library, we can think about the overhead
of using it regarding execution time or memory. In any case, overheads:

 * Costs of the PAPI library: initializing the library, starting counters,
   stoping them. For further details refer to `papi_cost` utility.
 * Costs of PAPI wrapper: iterating over the list of events to measure. Each
   counter is measured individually, so there is no concurrency at all when it
   comes to measure different events. There is only concurrency when measuring
   different thread: they are all measured at the same time

## Known issues

List of known issues when testing:
 * Do not mix uncore and not uncore events in the list: undefined behavior.
 * Uncore events must explicitly have the `:cpu=X` flag on them.
 * With the major version 1.0.0, PAPI wrapper introduces subregions, which
basically permits measuring different regions of code simoultaneously and
individually. Nonetheless, if the region or subregion measured has an order of
magnitude lower than the proper PAPI library cost (refer to
[Overhead](#overhead)), the result will have too much noise.

## Versions and changelog

Refer to [Releases](https://github.com/markoshorro/papi_wrapper/releases)
webpage. Versioning follows [Semantic Versioning
2.0.0](https://semver.org/spec/v2.0.0.html).

## Contact

Maintainer:

  + Marcos Horro (marcos.horro (at) udc.gal)

Authors:

  + Marcos Horro
  + Dr. Gabriel Rodríguez

## Credits

This version is based on
[PolyBench](https://sourceforge.net/projects/polybench/), under GPLv2 license.

## License

MIT License.

[travis-badge]:    https://travis-ci.org/markoshorro/papi_wrapper.svg?branch=master
[travis-link]:     https://travis-ci.org/markoshorro/papi_wrapper
[license-badge]:   https://img.shields.io/badge/license-MIT-007EC7.svg
[codecov-badge]:   https://codecov.io/gh/markoshorro/papi_wrapper/branch/master/graph/badge.svg
[codecov-link]:    https://codecov.io/gh/markoshorro/papi_wrapper
