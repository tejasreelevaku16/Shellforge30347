#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/socket.h>

#include "builtin.h"
#include "jobs.h"
#include "job_control.h"

/* =========================================================
   BUILTIN: cd
   ========================================================= */

static int builtin_cd(command_t *cmd)
{
    const char *directory;

    if (cmd->argc == 1)
    {
        directory = getenv("HOME");

        if (directory == NULL)
        {
            fprintf(stderr, "cd: HOME not set\n");
            return -1;
        }
    }
    else if (cmd->argc == 2)
    {
        directory = cmd->argv[1];
    }
    else
    {
        fprintf(stderr, "cd: too many arguments\n");
        return -1;
    }

    if (chdir(directory) != 0)
    {
        perror("cd");
        return -1;
    }

    return 0;
}

/* =========================================================
   BUILTIN: pwd
   ========================================================= */

static int builtin_pwd(command_t *cmd)
{
    char current_directory[4096];

    if (cmd->argc > 1)
    {
        fprintf(stderr, "pwd: too many arguments\n");
        return -1;
    }

    if (getcwd(current_directory, sizeof(current_directory)) == NULL)
    {
        perror("pwd");
        return -1;
    }

    printf("%s\n", current_directory);

    return 0;
}

/* =========================================================
   BUILTIN: echo
   ========================================================= */

static int builtin_echo(command_t *cmd)
{
    for (int i = 1; i < cmd->argc; i++)
    {
        printf("%s", cmd->argv[i]);

        if (i < cmd->argc - 1)
        {
            printf(" ");
        }
    }

    printf("\n");

    return 0;
}

/* =========================================================
   BUILTIN: exit
   ========================================================= */

static int builtin_exit(command_t *cmd)
{
    if (cmd->argc > 1)
    {
        fprintf(stderr, "exit: too many arguments\n");
        return -1;
    }

    /* Return 1 so the main shell loop can terminate. */
    return 1;
}

/* =========================================================
   BUILTIN: jobs
   ========================================================= */

int builtin_jobs(command_t *cmd)
{
    (void)cmd;

    jobs_print();

    return 0;
}

/* =========================================================
   BUILTIN: fg
   ========================================================= */

int builtin_fg(command_t *cmd)
{
    int job_id;
    int status;
    job_t *job;
    pid_t result;

    if (cmd->argc != 2)
    {
        fprintf(stderr, "fg: usage: fg <job number>\n");
        return 1;
    }

    char *endptr;
    long parsed_id = strtol(cmd->argv[1], &endptr, 10);

    if (cmd->argv[1][0] == '\0' ||
        *endptr != '\0' ||
        parsed_id <= 0 ||
        parsed_id > 2147483647L)
    {
        fprintf(stderr, "fg: invalid job number\n");
        return 1;
    }

    job_id = (int)parsed_id;
    job = job_find(job_id);

    if (job == NULL)
    {
        fprintf(stderr, "fg: no such job: %d\n", job_id);
        return 1;
    }

    /* Give the terminal to the foreground process group. */
    give_terminal_to(job->pgid);

    if (job->state == JOB_STOPPED)
    {
        if (kill(-job->pgid, SIGCONT) == -1)
        {
            perror("fg: SIGCONT");
            take_terminal_back();
            return 1;
        }
    }

    job_continue(job->pgid);

    /* Wait for the foreground process group. */
    while (1)
    {
        result = waitpid(-job->pgid, &status, WUNTRACED);

        if (result == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }

            break;
        }

        if (WIFSTOPPED(status))
        {
            job_stop(job->pgid);
            break;
        }

        if (WIFEXITED(status) || WIFSIGNALED(status))
        {
            job_done(job->pgid);
            break;
        }
    }

    /* Return the terminal to Shellforge. */
    take_terminal_back();

    /* Remove a completed job from the job list. */
    job = job_find(job_id);

    if (job != NULL && job->state == JOB_DONE)
    {
        job_remove(job_id);
    }

    return 0;
}

/* =========================================================
   BUILTIN: bg
   ========================================================= */

int builtin_bg(command_t *cmd)
{
    int job_id;
    job_t *job;

    if (cmd->argc != 2)
    {
        fprintf(stderr, "bg: usage: bg <job number>\n");
        return 1;
    }

    char *endptr;
    long parsed_id = strtol(cmd->argv[1], &endptr, 10);

    if (cmd->argv[1][0] == '\0' ||
        *endptr != '\0' ||
        parsed_id <= 0 ||
        parsed_id > 2147483647L)
    {
        fprintf(stderr, "bg: invalid job number\n");
        return 1;
    }

    job_id = (int)parsed_id;
    job = job_find(job_id);

    if (job == NULL)
    {
        fprintf(stderr, "bg: job not found: %d\n", job_id);
        return 1;
    }

    if (kill(-job->pgid, SIGCONT) == -1)
    {
        perror("bg: kill");
        return 1;
    }

    job_continue(job->pgid);

    printf("[%d] %s &\n", job->job_id, job->command);

    return 0;
}

/* =========================================================
   CUSTOM BUILTIN: diskpeek
   ========================================================= */

static void diskpeek_scan(const char *path,
                          long *files,
                          long *directories,
                          unsigned long long *bytes)
{
    DIR *dir = opendir(path);

    if (dir == NULL)
    {
        fprintf(stderr, "diskpeek: cannot open '%s'\n", path);
        return;
    }

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL)
    {
        char full_path[4096];
        struct stat info;

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        int written = snprintf(full_path, sizeof(full_path),
                               "%s/%s", path, entry->d_name);

        if (written < 0 || (size_t)written >= sizeof(full_path))
        {
            continue;
        }

        if (lstat(full_path, &info) == -1)
        {
            continue;
        }

        if (S_ISDIR(info.st_mode))
        {
            (*directories)++;

            diskpeek_scan(full_path, files, directories, bytes);
        }
        else if (S_ISREG(info.st_mode))
        {
            (*files)++;

            if (info.st_size > 0)
            {
                *bytes += (unsigned long long)info.st_size;
            }
        }
    }

    closedir(dir);
}

static int builtin_diskpeek(command_t *cmd)
{
    const char *path = ".";
    struct stat info;

    long files = 0;
    long directories = 0;
    unsigned long long bytes = 0;

    if (cmd->argc > 2)
    {
        fprintf(stderr, "diskpeek: usage: diskpeek [directory]\n");
        return 1;
    }

    if (cmd->argc == 2)
    {
        path = cmd->argv[1];
    }

    if (stat(path, &info) == -1)
    {
        perror("diskpeek");
        return 1;
    }

    if (!S_ISDIR(info.st_mode))
    {
        fprintf(stderr, "diskpeek: '%s' is not a directory\n", path);
        return 1;
    }

    diskpeek_scan(path, &files, &directories, &bytes);

    printf("\n=====================================\n");
    printf("          DiskPeek Report\n");
    printf("=====================================\n");
    printf("Directory   : %s\n", path);
    printf("Files       : %ld\n", files);
    printf("Directories : %ld\n", directories);
    printf("Total size  : %.2f MB\n",
           (double)bytes / (1024.0 * 1024.0));
    printf("=====================================\n");

    return 0;
}


/* =========================================================
   CUSTOM BUILTIN: findx
   Usage: findx [directory] pattern
   With one argument, searches the current directory tree.
   A pattern without a dot can match a filename or extension.
   ========================================================= */

static int findx_matches(const char *name, const char *pattern)
{
    size_t name_len = strlen(name);
    size_t pattern_len = strlen(pattern);

    if (strcmp(name, pattern) == 0)
        return 1;

    if (pattern_len > 0)
    {
        const char *extension = pattern[0] == '.' ? pattern + 1 : pattern;
        size_t extension_len = strlen(extension);

        if (extension_len > 0 &&
            name_len > extension_len &&
            name[name_len - extension_len - 1] == '.' &&
            strcmp(name + name_len - extension_len, extension) == 0)
        {
            return 1;
        }
    }

    return 0;
}

static void findx_scan(const char *directory, const char *pattern, int *matches)
{
    DIR *dir = opendir(directory);
    if (dir == NULL)
    {
        fprintf(stderr, "findx: cannot open '%s': %s\n", directory, strerror(errno));
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        char path[4096];
        int written = snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(path))
            continue;

        struct stat info;
        if (lstat(path, &info) == -1)
            continue;

        if (findx_matches(entry->d_name, pattern))
        {
            printf("%s\n", path);
            (*matches)++;
        }

        if (S_ISDIR(info.st_mode))
            findx_scan(path, pattern, matches);
    }

    closedir(dir);
}

static int builtin_findx(command_t *cmd)
{
    const char *directory = ".";
    const char *pattern;
    int matches = 0;
    struct stat info;

    if (cmd->argc == 2)
        pattern = cmd->argv[1];
    else if (cmd->argc == 3)
    {
        directory = cmd->argv[1];
        pattern = cmd->argv[2];
    }
    else
    {
        fprintf(stderr, "findx: usage: findx [directory] filename_or_extension\n");
        return 1;
    }

    if (pattern[0] == '\0')
    {
        fprintf(stderr, "findx: search pattern cannot be empty\n");
        return 1;
    }

    if (stat(directory, &info) == -1)
    {
        perror("findx");
        return 1;
    }

    if (!S_ISDIR(info.st_mode))
    {
        fprintf(stderr, "findx: '%s' is not a directory\n", directory);
        return 1;
    }

    findx_scan(directory, pattern, &matches);
    if (matches == 0)
        printf("findx: no matches found\n");
    else
        printf("findx: %d match(es) found\n", matches);

    return 0;
}

/* =========================================================
   CUSTOM BUILTIN: netpeek
   Shows local IPv4/IPv6 addresses and interface state.
   ========================================================= */

static int builtin_netpeek(command_t *cmd)
{
    (void)cmd;
    struct ifaddrs *interfaces = NULL;

    if (getifaddrs(&interfaces) == -1)
    {
        perror("netpeek");
        return 1;
    }

    printf("\n========================================\n");
    printf("             NetPeek Report\n");
    printf("========================================\n");

    int found = 0;
    for (struct ifaddrs *current = interfaces;
         current != NULL; current = current->ifa_next)
    {
        if (current->ifa_addr == NULL)
            continue;

        int family = current->ifa_addr->sa_family;
        if (family != AF_INET && family != AF_INET6)
            continue;

        char address[INET6_ADDRSTRLEN];
        const void *address_data;

        if (family == AF_INET)
            address_data = &((struct sockaddr_in *)current->ifa_addr)->sin_addr;
        else
            address_data = &((struct sockaddr_in6 *)current->ifa_addr)->sin6_addr;

        if (inet_ntop(family, address_data, address, sizeof(address)) == NULL)
            continue;

        printf("Interface : %s\n", current->ifa_name);
        printf("IP version: IPv%d\n", family == AF_INET ? 4 : 6);
        printf("Address   : %s\n", address);
        printf("Status    : %s\n",
               (current->ifa_flags & IFF_UP) ? "UP" : "DOWN");
        printf("----------------------------------------\n");
        found = 1;
    }

    if (!found)
        printf("No IPv4 or IPv6 addresses found.\n");

    freeifaddrs(interfaces);
    return 0;
}

/* =========================================================
   CUSTOM BUILTIN: filepeek
   Shows metadata for a file or directory.
   ========================================================= */

static int builtin_filepeek(command_t *cmd)
{
    struct stat info;

    if (cmd->argc != 2)
    {
        fprintf(stderr, "filepeek: usage: filepeek path\n");
        return 1;
    }

    if (lstat(cmd->argv[1], &info) == -1)
    {
        perror("filepeek");
        return 1;
    }

    const char *type = S_ISREG(info.st_mode) ? "Regular file" :
                       S_ISDIR(info.st_mode) ? "Directory" :
                       S_ISLNK(info.st_mode) ? "Symbolic link" :
                       S_ISCHR(info.st_mode) ? "Character device" :
                       S_ISBLK(info.st_mode) ? "Block device" :
                       S_ISFIFO(info.st_mode) ? "FIFO/pipe" :
                       S_ISSOCK(info.st_mode) ? "Socket" : "Unknown";

    char time_buffer[64] = "unknown";
    struct tm time_info;
    if (localtime_r(&info.st_mtime, &time_info) != NULL)
        strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &time_info);

    printf("\nFilePeek Report\n");
    printf("----------------------------------------\n");
    printf("Path          : %s\n", cmd->argv[1]);
    printf("Type          : %s\n", type);
    printf("Size          : %lld bytes\n", (long long)info.st_size);
    printf("Permissions   : %04o\n", (unsigned)(info.st_mode & 07777));
    printf("Owner UID     : %ld\n", (long)info.st_uid);
    printf("Group GID     : %ld\n", (long)info.st_gid);
    printf("Hard links    : %ld\n", (long)info.st_nlink);
    printf("Last modified : %s\n", time_buffer);

    return 0;
}

/* =========================================================
   CUSTOM BUILTIN: permcheck
   Explains Unix permission bits for a path.
   ========================================================= */

static int builtin_permcheck(command_t *cmd)
{
    struct stat info;

    if (cmd->argc != 2)
    {
        fprintf(stderr, "permcheck: usage: permcheck path\n");
        return 1;
    }

    if (stat(cmd->argv[1], &info) == -1)
    {
        perror("permcheck");
        return 1;
    }

    printf("Permission report for: %s\n", cmd->argv[1]);
    printf("Mode: %04o\n", (unsigned)(info.st_mode & 0777));
    printf("Owner : %c %c %c\n",
           (info.st_mode & S_IRUSR) ? 'r' : '-',
           (info.st_mode & S_IWUSR) ? 'w' : '-',
           (info.st_mode & S_IXUSR) ? 'x' : '-');
    printf("Group : %c %c %c\n",
           (info.st_mode & S_IRGRP) ? 'r' : '-',
           (info.st_mode & S_IWGRP) ? 'w' : '-',
           (info.st_mode & S_IXGRP) ? 'x' : '-');
    printf("Other : %c %c %c\n",
           (info.st_mode & S_IROTH) ? 'r' : '-',
           (info.st_mode & S_IWOTH) ? 'w' : '-',
           (info.st_mode & S_IXOTH) ? 'x' : '-');

    return 0;
}

/* =========================================================
   CUSTOM BUILTIN: countlines
   Counts lines in a text file.
   ========================================================= */

static int builtin_countlines(command_t *cmd)
{
    FILE *file;
    int ch;
    unsigned long long lines = 0;
    int has_data = 0;
    int last_char = '\n';

    if (cmd->argc != 2)
    {
        fprintf(stderr, "countlines: usage: countlines file\n");
        return 1;
    }

    file = fopen(cmd->argv[1], "r");
    if (file == NULL)
    {
        perror("countlines");
        return 1;
    }

    while ((ch = fgetc(file)) != EOF)
    {
        has_data = 1;
        last_char = ch;
        if (ch == '\n')
            lines++;
    }

    if (ferror(file))
    {
        perror("countlines: read");
        fclose(file);
        return 1;
    }

    if (has_data && last_char != '\n')
        lines++;

    fclose(file);
    printf("%llu %s\n", lines, cmd->argv[1]);
    return 0;
}

/* =========================================================
   CUSTOM BUILTIN: headx
   Prints the first N lines; default is 10.
   ========================================================= */

static int builtin_headx(command_t *cmd)
{
    if (cmd->argc < 2 || cmd->argc > 3)
    {
        fprintf(stderr, "headx: usage: headx file [number_of_lines]\n");
        return 1;
    }

    char *endptr = NULL;
    long limit = 10;
    if (cmd->argc == 3)
    {
        errno = 0;
        limit = strtol(cmd->argv[2], &endptr, 10);
        if (errno != 0 || endptr == cmd->argv[2] || *endptr != '\0' || limit < 0)
        {
            fprintf(stderr, "headx: invalid line count\n");
            return 1;
        }
    }

    FILE *file = fopen(cmd->argv[1], "r");
    if (file == NULL)
    {
        perror("headx");
        return 1;
    }

    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    long printed = 0;

    while (printed < limit && (length = getline(&line, &capacity, file)) != -1)
    {
        if (fwrite(line, 1, (size_t)length, stdout) != (size_t)length)
        {
            perror("headx: output");
            free(line);
            fclose(file);
            return 1;
        }
        printed++;
    }

    if (ferror(file))
    {
        perror("headx: read");
        free(line);
        fclose(file);
        return 1;
    }

    free(line);
    fclose(file);
    return 0;
}

/* =========================================================
   CUSTOM BUILTIN: tailx
   Prints the last N lines; default is 10.
   ========================================================= */

static int builtin_tailx(command_t *cmd)
{
    if (cmd->argc < 2 || cmd->argc > 3)
    {
        fprintf(stderr, "tailx: usage: tailx file [number_of_lines]\n");
        return 1;
    }

    char *endptr = NULL;
    long limit = 10;
    if (cmd->argc == 3)
    {
        errno = 0;
        limit = strtol(cmd->argv[2], &endptr, 10);
        if (errno != 0 || endptr == cmd->argv[2] || *endptr != '\0' || limit < 0)
        {
            fprintf(stderr, "tailx: invalid line count\n");
            return 1;
        }
    }

    FILE *file = fopen(cmd->argv[1], "r");
    if (file == NULL)
    {
        perror("tailx");
        return 1;
    }

    char **lines = NULL;
    size_t *lengths = NULL;
    size_t capacity = 0;
    size_t count = 0;
    char *line = NULL;
    size_t line_capacity = 0;
    ssize_t length;

    while ((length = getline(&line, &line_capacity, file)) != -1)
    {
        if (limit == 0)
            continue;

        if (capacity < (size_t)limit)
        {
            size_t new_capacity = capacity == 0 ? 16 : capacity * 2;
            if (new_capacity > (size_t)limit)
                new_capacity = (size_t)limit;

            char **new_lines = realloc(lines, new_capacity * sizeof(*lines));
            if (new_lines == NULL)
            {
                perror("tailx: memory");
                free(line);
                fclose(file);
                for (size_t i = 0; i < count; i++) free(lines[i]);
                free(lines);
                free(lengths);
                return 1;
            }
            lines = new_lines;

            size_t *new_lengths = realloc(lengths, new_capacity * sizeof(*lengths));
            if (new_lengths == NULL)
            {
                perror("tailx: memory");
                free(line);
                fclose(file);
                for (size_t i = 0; i < count; i++) free(lines[i]);
                free(lines);
                free(lengths);
                return 1;
            }
            lengths = new_lengths;
            capacity = new_capacity;
        }

        char *copy = malloc((size_t)length + 1);
        if (copy == NULL)
        {
            perror("tailx: memory");
            free(line);
            fclose(file);
            for (size_t i = 0; i < count; i++) free(lines[i]);
            free(lines);
            free(lengths);
            return 1;
        }
        memcpy(copy, line, (size_t)length);
        copy[length] = '\0';

        if (count == (size_t)limit)
        {
            free(lines[0]);
            memmove(lines, lines + 1, (count - 1) * sizeof(*lines));
            memmove(lengths, lengths + 1, (count - 1) * sizeof(*lengths));
            count--;
        }

        lines[count] = copy;
        lengths[count] = (size_t)length;
        count++;
    }

    if (ferror(file))
    {
        perror("tailx: read");
        free(line);
        fclose(file);
        for (size_t i = 0; i < count; i++) free(lines[i]);
        free(lines);
        free(lengths);
        return 1;
    }

    for (size_t i = 0; i < count; i++)
    {
        fwrite(lines[i], 1, lengths[i], stdout);
        free(lines[i]);
    }

    free(line);
    free(lines);
    free(lengths);
    fclose(file);
    return 0;
}

/* =========================================================
   CUSTOM BUILTIN: envpeek
   Lists environment variables or displays one variable.
   ========================================================= */

extern char **environ;

static int builtin_envpeek(command_t *cmd)
{
    if (cmd->argc > 2)
    {
        fprintf(stderr, "envpeek: usage: envpeek [VARIABLE]\n");
        return 1;
    }

    if (cmd->argc == 2)
    {
        const char *value = getenv(cmd->argv[1]);
        if (value == NULL)
        {
            fprintf(stderr, "envpeek: '%s' is not set\n", cmd->argv[1]);
            return 1;
        }

        printf("%s=%s\n", cmd->argv[1], value);
        return 0;
    }

    for (char **entry = environ; *entry != NULL; entry++)
        printf("%s\n", *entry);

    return 0;
}

/* =========================================================
   CHECK WHETHER COMMAND IS A BUILTIN
   ========================================================= */

int is_builtin(const command_t *cmd)
{
    if (cmd == NULL || cmd->argc == 0 || cmd->argv[0] == NULL)
    {
        return 0;
    }

    if (strcmp(cmd->argv[0], "cd") == 0)
        return 1;

    if (strcmp(cmd->argv[0], "pwd") == 0)
        return 1;

    if (strcmp(cmd->argv[0], "echo") == 0)
        return 1;

    if (strcmp(cmd->argv[0], "exit") == 0)
        return 1;

    if (strcmp(cmd->argv[0], "jobs") == 0)
        return 1;

    if (strcmp(cmd->argv[0], "fg") == 0)
        return 1;

    if (strcmp(cmd->argv[0], "bg") == 0)
        return 1;

    if (strcmp(cmd->argv[0], "diskpeek") == 0)
        return 1;

    if (strcmp(cmd->argv[0], "findx") == 0)
        return 1;

    if (strcmp(cmd->argv[0], "netpeek") == 0)
        return 1;

    if (strcmp(cmd->argv[0], "filepeek") == 0)
        return 1;

    if (strcmp(cmd->argv[0], "permcheck") == 0)
        return 1;

    if (strcmp(cmd->argv[0], "countlines") == 0)
        return 1;

    if (strcmp(cmd->argv[0], "headx") == 0)
        return 1;

    if (strcmp(cmd->argv[0], "tailx") == 0)
        return 1;

    if (strcmp(cmd->argv[0], "envpeek") == 0)
        return 1;


    return 0;
}

/* =========================================================
   EXECUTE BUILTIN
   ========================================================= */

int execute_builtin(command_t *cmd)
{
    if (cmd == NULL || cmd->argc == 0 || cmd->argv[0] == NULL)
    {
        return -1;
    }

    if (strcmp(cmd->argv[0], "cd") == 0)
        return builtin_cd(cmd);

    if (strcmp(cmd->argv[0], "pwd") == 0)
        return builtin_pwd(cmd);

    if (strcmp(cmd->argv[0], "echo") == 0)
        return builtin_echo(cmd);

    if (strcmp(cmd->argv[0], "exit") == 0)
        return builtin_exit(cmd);

    if (strcmp(cmd->argv[0], "jobs") == 0)
        return builtin_jobs(cmd);

    if (strcmp(cmd->argv[0], "fg") == 0)
        return builtin_fg(cmd);

    if (strcmp(cmd->argv[0], "bg") == 0)
        return builtin_bg(cmd);

    if (strcmp(cmd->argv[0], "diskpeek") == 0)
        return builtin_diskpeek(cmd);

    if (strcmp(cmd->argv[0], "findx") == 0)
        return builtin_findx(cmd);

    if (strcmp(cmd->argv[0], "netpeek") == 0)
        return builtin_netpeek(cmd);

    if (strcmp(cmd->argv[0], "filepeek") == 0)
        return builtin_filepeek(cmd);

    if (strcmp(cmd->argv[0], "permcheck") == 0)
        return builtin_permcheck(cmd);

    if (strcmp(cmd->argv[0], "countlines") == 0)
        return builtin_countlines(cmd);

    if (strcmp(cmd->argv[0], "headx") == 0)
        return builtin_headx(cmd);

    if (strcmp(cmd->argv[0], "tailx") == 0)
        return builtin_tailx(cmd);

    if (strcmp(cmd->argv[0], "envpeek") == 0)
        return builtin_envpeek(cmd);


    return -1;
}
