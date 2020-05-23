#include "test_lib.h"

int
pw_test_pass(const char *FILE)
{
    printf("passed test:\t%s", FILE);
    return 0;
}

int
pw_test_fail(const char *FILE)
{
    printf("failed test:\t%s", FILE);
    return 1;
}