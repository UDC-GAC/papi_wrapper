#include <omp.h>
#include <papi_wrapper.h>
#include <stdio.h>
#include <stdlib.h>

int
main()
{
    int N = 1000;
    int x[N];
    pw_init_start_instruments;
#pragma omp parallel for
    for (int i = 0; i < N; ++i)
    {
        x[i] = i * 42.3;
    }
    pw_stop_instruments;
    pw_print_instruments;

    /* avoid code elimination */
    for (int i = 0; i < N; ++i)
    {
        if (i % 100 == 0)
        {
            printf("x[%d]\t%d\n", x[i]);
        }
    }
}
