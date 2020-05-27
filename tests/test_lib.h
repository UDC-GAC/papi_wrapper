#include "papi_wrapper.h"
#include <stdio.h>
#include <stdlib.h>

int
pw_test_pass(const char *FILE)
{
    printf("passed test:\t%s\n", FILE);
    return PW_SUCCESS;
}

int
pw_test_fail(const char *FILE)
{
    printf("failed test:\t%s\n", FILE);
    return PW_ERR;
}