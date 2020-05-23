#include <omp.h>
#include <papi_wrapper.h>
#include <stdio.h>
#include <stdlib.h>

#include "test_lib.h"

int
main()
{
    int N = 64;
    int x[N];

    pw_init_start_instruments_sub(2);
#pragma omp parallel for
    for (int i = 0; i < N; ++i)
    {
        // First subregion
        pw_begin_subregion(0);
        x[i] = i * 42.3;
        sleep(1);
        pw_end_subregion(0);

        // Second subregion
        pw_begin_subregion(1);
        x[i] = i * 42.3;
        sleep(1);
        pw_end_subregion(1);
    }
    pw_stop_instruments;
    pw_print_subregions;

    /* avoid code elimination */
    for (int i = 0; i < N; ++i)
    {
        if (i % 100 == 0)
        {
            printf("x[%d]\t%d\n", i, x[i]);
        }
    }
    return pw_test_pass(__FILE__);
}
