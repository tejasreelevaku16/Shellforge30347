#ifndef JOB_CONTROL_H
#define JOB_CONTROL_H

#include <sys/types.h>
#include "jobs.h"
void job_control_init(void);

void give_terminal_to(pid_t pgid);

void take_terminal_back(void);

pid_t get_shell_pgid(void);
int job_control_foreground(int job_id);
int job_control_background(int job_id);
int job_control_wait_foreground(job_t *job);
#endif
