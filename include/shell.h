#ifndef NOVASH_SHELL_H
#define NOVASH_SHELL_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <termios.h>

#define NOVASH_MAX_JOBS 64

typedef struct {
    char **argv;
    size_t argc;
    size_t capacity;
    char *input_path;
    char *output_path;
    bool append_output;
} Command;

typedef struct {
    Command *commands;
    size_t count;
    size_t capacity;
    bool background;
    char *source;
} Pipeline;

typedef enum {
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_DONE
} JobState;

typedef struct Job {
    int id;
    pid_t pgid;
    pid_t *pids;
    bool *completed;
    bool *stopped;
    size_t process_count;
    size_t remaining;
    JobState state;
    int exit_status;
    bool has_terminal_modes;
    struct termios terminal_modes;
    char *command;
    struct Job *next;
} Job;

typedef struct {
    bool interactive;
    bool should_exit;
    int exit_status;
    int terminal_fd;
    pid_t shell_pgid;
    struct termios shell_terminal_modes;
    Job *jobs;
    int next_job_id;
} ShellState;

int parse_pipeline(const char *line, Pipeline *pipeline, char **error_message);
void free_pipeline(Pipeline *pipeline);

void initialize_shell(ShellState *state);
void shutdown_shell(ShellState *state);
int execute_pipeline(ShellState *state, const Pipeline *pipeline);

bool is_builtin(const char *name);
int execute_builtin(ShellState *state, const Command *command, bool in_child);

Job *create_job(pid_t pgid, const pid_t *pids, size_t process_count,
                const char *command);
void add_job(ShellState *state, Job *job);
void free_job(Job *job);
void reap_background_jobs(ShellState *state, bool notify);
int wait_for_foreground_job(ShellState *state, Job *job);
int put_job_in_foreground(ShellState *state, int job_id);
int continue_job_in_background(ShellState *state, int job_id);
void print_jobs(ShellState *state);
void remove_completed_jobs(ShellState *state);

void install_shell_signal_handlers(void);
void restore_child_signal_handlers(void);
bool consume_child_signal(void);

#endif
