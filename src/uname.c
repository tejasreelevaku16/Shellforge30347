#include <stdio.h>
#include <sys/utsname.h>
#include "uname.h"

void execute_uname(void)
{
    struct utsname system_info;

    if (uname(&system_info) == -1)
    {
        perror("uname");
        return;
    }

    printf("%s\n", system_info.sysname);
}
