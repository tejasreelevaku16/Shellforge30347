#include <stdio.h>
#include <time.h>
#include "date.h"

void execute_date(void)
{
    time_t current_time;

    time(&current_time);

    printf("%s", ctime(&current_time));
}
