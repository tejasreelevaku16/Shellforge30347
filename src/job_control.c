#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include "job_control.h"

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

static pid_t shell_pgid;
static int shell_terminal;

void job_control_init(void)
{
    shell_terminal = STDIN_FILENO;
    shell_pgid = getpid();

    if (!isatty(shell_terminal)) {
        return;
    }

    while (tcgetpgrp(shell_terminal) !=
           (shell_pgid = getpgrp())) {
        kill(-shell_pgid, SIGTTIN);
    }

    if (setpgid(shell_pgid, shell_pgid) < 0 &&
        errno != EACCES && errno != EPERM) {
        perror("shellforge: setpgid");
    }

    shell_pgid = getpgrp();

    if (tcsetpgrp(shell_terminal, shell_pgid) < 0) {
        perror("shellforge: tcsetpgrp");
    }

    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
}

void give_terminal_to(pid_t pgid)
{
    if (isatty(shell_terminal) && pgid > 0) {
        if (tcsetpgrp(shell_terminal, pgid) < 0) {
            perror("shellforge: tcsetpgrp");
        }
    }
}

void take_terminal_back(void)
{
    if (isatty(shell_terminal)) {
        if (tcsetpgrp(shell_terminal, shell_pgid) < 0) {
            perror("shellforge: tcsetpgrp");
        }
    }
}

int job_control_wait_foreground(job_t *job)
{
    if (job == NULL || job->pgid <= 0) {
        return -1;
    }

    int status;
    pid_t result;

    give_terminal_to(job->pgid);

    do {
        result = waitpid(-job->pgid, &status, WUNTRACED);
    } while (result < 0 && errno == EINTR);

    take_terminal_back();

    if (result < 0) {
        if (errno != ECHILD) {
            perror("shellforge: waitpid");
        }
        return -1;
    }

    if (WIFSTOPPED(status)) {
        job->state = JOB_STOPPED;
        printf("\n[%d] Stopped  %s\n",
               job->job_id, job->command);
    } else if (WIFEXITED(status) || WIFSIGNALED(status)) {
        job->state = JOB_DONE;
    }

    return 0;
}

int job_control_foreground(int job_id)
{
    job_t *job = job_find(job_id);

    if (job == NULL) {
        fprintf(stderr, "shellforge: no such job: %d\n", job_id);
        return -1;
    }

    if (job->state == JOB_STOPPED) {
        if (kill(-job->pgid, SIGCONT) < 0) {
            perror("shellforge: fg");
            return -1;
        }
        job->state = JOB_RUNNING;
    }

    return job_control_wait_foreground(job);
}

int job_control_background(int job_id)
{
    job_t *job = job_find(job_id);

    if (job == NULL) {
        fprintf(stderr, "shellforge: no such job: %d\n", job_id);
        return -1;
    }

    if (kill(-job->pgid, SIGCONT) < 0) {
        perror("shellforge: bg");
        return -1;
    }

    job->state = JOB_RUNNING;

    printf("[%d] %s &\n", job->job_id, job->command);
    return 0;
}
