#include <omp.h>
#include <papi_wrapper.h>
#include <stdio.h>
#include <stdlib.h>

#include "test_lib.h"

int
main()
{
    long long N = 10;
    double    x[N];

    pw_init_instruments;
#pragma omp parallel
    {
        pw_start_instruments_loop(omp_get_thread_num());
        for (int i = 0; i < N; ++i)
        {

            x[i] = i * 42.3;
            x[i] = i / 29.8;
        }
        pw_stop_instruments_loop(omp_get_thread_num());
    }
    pw_print_instruments;

    /* avoid code elimination */
    for (int i = 0; i < N; ++i)
    {
        if (i % 1000000 == 0)
        {
            printf("x[%d]\t%f\n", i, x[i]);
        }
    }
    return pw_test_pass(__FILE__);
}
