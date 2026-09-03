#include "shell.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static const char *job_state_name(JobState state) {
    switch (state) {
        case JOB_RUNNING:
            return "Running";
        case JOB_STOPPED:
            return "Stopped";
        case JOB_DONE:
            return "Done";
    }
    return "Unknown";
}

Job *create_job(pid_t pgid, const pid_t *pids, size_t process_count,
                const char *command) {
    Job *job = calloc(1, sizeof(*job));
    if (job == NULL) {
        return NULL;
    }

    job->pids = malloc(process_count * sizeof(*job->pids));
    job->completed = calloc(process_count, sizeof(*job->completed));
    job->stopped = calloc(process_count, sizeof(*job->stopped));
    job->command = strdup(command);
    if (job->pids == NULL || job->completed == NULL ||
        job->stopped == NULL || job->command == NULL) {
        free_job(job);
        return NULL;
    }

    memcpy(job->pids, pids, process_count * sizeof(*pids));
    job->pgid = pgid;
    job->process_count = process_count;
    job->remaining = process_count;
    job->state = JOB_RUNNING;
    return job;
}

void free_job(Job *job) {
    if (job == NULL) {
        return;
    }
    free(job->pids);
    free(job->completed);
    free(job->stopped);
    free(job->command);
    free(job);
}

void add_job(ShellState *state, Job *job) {
    if (job->id == 0) {
        job->id = state->next_job_id++;
    }
    job->next = state->jobs;
    state->jobs = job;
}

static Job *find_job_by_id(ShellState *state, int id) {
    for (Job *job = state->jobs; job != NULL; job = job->next) {
        if (job->id == id) {
            return job;
        }
    }
    return NULL;
}

static Job *find_job_by_pid(ShellState *state, pid_t pid,
                            size_t *process_index) {
    for (Job *job = state->jobs; job != NULL; job = job->next) {
        for (size_t index = 0; index < job->process_count; index++) {
            if (job->pids[index] == pid) {
                if (process_index != NULL) {
                    *process_index = index;
                }
                return job;
            }
        }
    }
    return NULL;
}

static int status_to_exit_code(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 0;
}

static void refresh_job_state(Job *job) {
    if (job->remaining == 0) {
        job->state = JOB_DONE;
        return;
    }
    for (size_t index = 0; index < job->process_count; index++) {
        if (!job->completed[index] && !job->stopped[index]) {
            job->state = JOB_RUNNING;
            return;
        }
    }
    job->state = JOB_STOPPED;
}

static void update_job_status(Job *job, size_t process_index, int status) {
    if (WIFSTOPPED(status)) {
        job->stopped[process_index] = true;
        refresh_job_state(job);
        return;
    }
    if (WIFCONTINUED(status)) {
        job->stopped[process_index] = false;
        job->state = JOB_RUNNING;
        return;
    }
    if ((WIFEXITED(status) || WIFSIGNALED(status)) &&
        !job->completed[process_index]) {
        job->completed[process_index] = true;
        job->stopped[process_index] = false;
        if (job->remaining > 0) {
            job->remaining--;
        }
        if (process_index + 1 == job->process_count) {
            job->exit_status = status_to_exit_code(status);
        }
        refresh_job_state(job);
    }
}

void reap_background_jobs(ShellState *state, bool notify) {
    int status = 0;
    pid_t pid;
    while ((pid = waitpid(-1, &status,
                          WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        size_t process_index = 0;
        Job *job = find_job_by_pid(state, pid, &process_index);
        if (job == NULL) {
            continue;
        }
        JobState previous_state = job->state;
        update_job_status(job, process_index, status);
        if (notify && job->state != previous_state &&
            (job->state == JOB_DONE || job->state == JOB_STOPPED)) {
            printf("\n[%d] %-8s %s\n", job->id,
                   job_state_name(job->state), job->command);
        }
    }
}

int wait_for_foreground_job(ShellState *state, Job *job) {
    while (job->remaining > 0 && job->state != JOB_STOPPED) {
        int status = 0;
        pid_t pid = waitpid(-job->pgid, &status, WUNTRACED);
        if (pid < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == ECHILD) {
                job->remaining = 0;
                job->state = JOB_DONE;
                break;
            }
            perror("waitpid");
            break;
        }

        for (size_t index = 0; index < job->process_count; index++) {
            if (job->pids[index] == pid) {
                update_job_status(job, index, status);
                break;
            }
        }
    }

    if (state->interactive) {
        if (job->state == JOB_STOPPED &&
            tcgetattr(state->terminal_fd, &job->terminal_modes) == 0) {
            job->has_terminal_modes = true;
        }
        tcsetpgrp(state->terminal_fd, state->shell_pgid);
        tcsetattr(state->terminal_fd, TCSADRAIN,
                  &state->shell_terminal_modes);
    }
    return job->exit_status;
}

int put_job_in_foreground(ShellState *state, int job_id) {
    Job *job = find_job_by_id(state, job_id);
    if (job == NULL || job->state == JOB_DONE) {
        fprintf(stderr, "fg: no such active job: %d\n", job_id);
        return 1;
    }

    if (state->interactive) {
        tcsetpgrp(state->terminal_fd, job->pgid);
        if (job->has_terminal_modes) {
            tcsetattr(state->terminal_fd, TCSADRAIN, &job->terminal_modes);
        }
    }
    if (job->state == JOB_STOPPED) {
        if (kill(-job->pgid, SIGCONT) < 0) {
            perror("fg");
            if (state->interactive) {
                tcsetpgrp(state->terminal_fd, state->shell_pgid);
            }
            return 1;
        }
        job->state = JOB_RUNNING;
        for (size_t index = 0; index < job->process_count; index++) {
            job->stopped[index] = false;
        }
    }

    int result = wait_for_foreground_job(state, job);
    if (job->state == JOB_STOPPED) {
        printf("[%d] Stopped  %s\n", job->id, job->command);
    }
    remove_completed_jobs(state);
    return result;
}

int continue_job_in_background(ShellState *state, int job_id) {
    Job *job = find_job_by_id(state, job_id);
    if (job == NULL || job->state == JOB_DONE) {
        fprintf(stderr, "bg: no such active job: %d\n", job_id);
        return 1;
    }

    if (kill(-job->pgid, SIGCONT) < 0) {
        perror("bg");
        return 1;
    }
    job->state = JOB_RUNNING;
    for (size_t index = 0; index < job->process_count; index++) {
        job->stopped[index] = false;
    }
    printf("[%d] %d %s\n", job->id, job->pgid, job->command);
    return 0;
}

void print_jobs(ShellState *state) {
    reap_background_jobs(state, false);
    for (Job *job = state->jobs; job != NULL; job = job->next) {
        printf("[%d] %-8s %s\n", job->id, job_state_name(job->state),
               job->command);
    }
    remove_completed_jobs(state);
}

void remove_completed_jobs(ShellState *state) {
    Job **cursor = &state->jobs;
    while (*cursor != NULL) {
        if ((*cursor)->state == JOB_DONE) {
            Job *completed = *cursor;
            *cursor = completed->next;
            free_job(completed);
        } else {
            cursor = &(*cursor)->next;
        }
    }
}
