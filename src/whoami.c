#include <stdio.h>
#include <stdlib.h>
#include "whoami.h"

void execute_whoami(void)
{
    char *username = getenv("USER");

    if (username != NULL)
        printf("%s\n", username);
    else
        printf("Unknown user\n");
}
