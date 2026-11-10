
#ifndef BUILTIN_H
#define BUILTIN_H

#include "parser.h"

int is_builtin(const command_t *cmd);
int execute_builtin(command_t *cmd);

int builtin_jobs(command_t *cmd);
int builtin_fg(command_t *cmd);
int builtin_bg(command_t *cmd);

#endif

