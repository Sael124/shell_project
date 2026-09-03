#include "shell.h"

#include <errno.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static ShellState *interactive_state = NULL;

static int handle_readline_events(void) {
    if (interactive_state != NULL && consume_child_signal()) {
        reap_background_jobs(interactive_state, true);
        rl_on_new_line();
        rl_redisplay();
    }
    return 0;
}

void initialize_shell(ShellState *state) {
    memset(state, 0, sizeof(*state));
    state->terminal_fd = STDIN_FILENO;
    state->interactive = isatty(state->terminal_fd);
    state->next_job_id = 1;
    using_history();
    rl_catch_signals = 0;

    if (state->interactive) {
        pid_t current_group;
        while (tcgetpgrp(state->terminal_fd) !=
               (current_group = getpgrp())) {
            kill(-current_group, SIGTTIN);
        }

        install_shell_signal_handlers();
        state->shell_pgid = getpid();
        if (setpgid(state->shell_pgid, state->shell_pgid) < 0 &&
            errno != EACCES && errno != EPERM) {
            perror("novash: setpgid");
            exit(EXIT_FAILURE);
        }
        if (tcsetpgrp(state->terminal_fd, state->shell_pgid) < 0) {
            perror("novash: tcsetpgrp");
            exit(EXIT_FAILURE);
        }
        if (tcgetattr(state->terminal_fd, &state->shell_terminal_modes) < 0) {
            perror("novash: tcgetattr");
            exit(EXIT_FAILURE);
        }
    } else {
        state->shell_pgid = getpgrp();
        install_shell_signal_handlers();
    }
}

void shutdown_shell(ShellState *state) {
    for (Job *job = state->jobs; job != NULL; job = job->next) {
        if (job->state != JOB_DONE) {
            kill(-job->pgid, SIGHUP);
            kill(-job->pgid, SIGCONT);
        }
    }
    while (state->jobs != NULL) {
        Job *next = state->jobs->next;
        free_job(state->jobs);
        state->jobs = next;
    }
    clear_history();
}

static char *build_prompt(void) {
    char *directory = getcwd(NULL, 0);
    const char *shown_directory = directory == NULL ? "?" : directory;
    size_t required = strlen(shown_directory) + 16;
    char *prompt = malloc(required);
    if (prompt != NULL) {
        snprintf(prompt, required, "\001\033[1;36m\002%s\001\033[0m\002\n"
                 "novash$ ", shown_directory);
    }
    free(directory);
    return prompt;
}

static int process_line(ShellState *state, const char *line,
                        bool record_history) {
    Pipeline pipeline;
    char *error_message = NULL;
    int parse_result = parse_pipeline(line, &pipeline, &error_message);
    if (parse_result < 0) {
        fprintf(stderr, "novash: syntax error: %s\n",
                error_message == NULL ? "out of memory" : error_message);
        free(error_message);
        state->exit_status = 2;
        return 2;
    }
    if (parse_result > 0) {
        return state->exit_status;
    }

    if (record_history) {
        add_history(line);
    }
    int result = execute_pipeline(state, &pipeline);
    free_pipeline(&pipeline);
    return result;
}

static int run_interactive(ShellState *state) {
    puts("NovaShell - a small POSIX shell. Type 'help' to get started.");
    interactive_state = state;
    rl_event_hook = handle_readline_events;
    while (!state->should_exit) {
        if (consume_child_signal()) {
            reap_background_jobs(state, true);
        }

        char *prompt = build_prompt();
        if (prompt == NULL) {
            perror("novash");
            return 1;
        }
        char *line = readline(prompt);
        free(prompt);
        if (line == NULL) {
            putchar('\n');
            break;
        }
        if (*line != '\0') {
            process_line(state, line, true);
        }
        free(line);
    }
    rl_event_hook = NULL;
    interactive_state = NULL;
    return state->exit_status;
}

static int run_stream(ShellState *state) {
    char *line = NULL;
    size_t capacity = 0;
    while (!state->should_exit) {
        errno = 0;
        ssize_t length = getline(&line, &capacity, stdin);
        if (length < 0) {
            if (errno == EINTR) {
                if (consume_child_signal()) {
                    reap_background_jobs(state, false);
                }
                continue;
            }
            break;
        }
        if (length > 0 && line[length - 1] == '\n') {
            line[length - 1] = '\0';
        }
        process_line(state, line, false);
        if (consume_child_signal()) {
            reap_background_jobs(state, false);
        }
    }
    free(line);
    return state->exit_status;
}

static void print_usage(FILE *stream, const char *program_name) {
    fprintf(stream, "Usage: %s [-c command]\n", program_name);
}

int main(int argc, char **argv) {
    if (argc != 1 && !(argc == 3 && strcmp(argv[1], "-c") == 0)) {
        print_usage(stderr, argv[0]);
        return 2;
    }

    ShellState state;
    initialize_shell(&state);
    int result;
    if (argc == 3) {
        result = process_line(&state, argv[2], false);
    } else if (state.interactive) {
        result = run_interactive(&state);
    } else {
        result = run_stream(&state);
    }
    shutdown_shell(&state);
    return result;
}
