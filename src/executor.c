#include "shell.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static int open_input(const char *path) {
    int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) {
        fprintf(stderr, "novash: %s: %s\n", path, strerror(errno));
    }
    return descriptor;
}

static int open_output(const char *path, bool append) {
    int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
    int descriptor = open(path, flags, S_IRUSR | S_IWUSR | S_IRGRP |
                                       S_IROTH);
    if (descriptor < 0) {
        fprintf(stderr, "novash: %s: %s\n", path, strerror(errno));
    }
    return descriptor;
}

static int redirect_descriptor(int source, int destination) {
    if (dup2(source, destination) < 0) {
        perror("novash: dup2");
        return -1;
    }
    return 0;
}

static int apply_command_redirections(const Command *command) {
    if (command->input_path != NULL) {
        int input = open_input(command->input_path);
        if (input < 0) {
            return -1;
        }
        int result = redirect_descriptor(input, STDIN_FILENO);
        close(input);
        if (result != 0) {
            return -1;
        }
    }

    if (command->output_path != NULL) {
        int output = open_output(command->output_path,
                                 command->append_output);
        if (output < 0) {
            return -1;
        }
        int result = redirect_descriptor(output, STDOUT_FILENO);
        close(output);
        if (result != 0) {
            return -1;
        }
    }
    return 0;
}

static int execute_parent_builtin(ShellState *state,
                                  const Command *command) {
    int saved_input = -1;
    int saved_output = -1;
    int result = 1;

    if (command->input_path != NULL) {
        saved_input = dup(STDIN_FILENO);
        if (saved_input < 0) {
            perror("novash: dup");
            return 1;
        }
    }
    if (command->output_path != NULL) {
        saved_output = dup(STDOUT_FILENO);
        if (saved_output < 0) {
            perror("novash: dup");
            close(saved_input);
            return 1;
        }
    }

    if (apply_command_redirections(command) == 0) {
        result = execute_builtin(state, command, false);
        fflush(stdout);
    }

    if (saved_input >= 0) {
        if (dup2(saved_input, STDIN_FILENO) < 0) {
            perror("novash: restore stdin");
        }
        close(saved_input);
    }
    if (saved_output >= 0) {
        if (dup2(saved_output, STDOUT_FILENO) < 0) {
            perror("novash: restore stdout");
        }
        close(saved_output);
    }
    return result;
}

static void close_pipes(int (*pipes)[2], size_t pipe_count) {
    for (size_t index = 0; index < pipe_count; index++) {
        close(pipes[index][0]);
        close(pipes[index][1]);
    }
}

static void execute_child(ShellState *state, const Pipeline *pipeline,
                          size_t command_index, int (*pipes)[2],
                          size_t pipe_count, pid_t process_group,
                          int start_gate[2]) {
    const Command *command = &pipeline->commands[command_index];
    pid_t own_pid = getpid();
    pid_t target_group = process_group == 0 ? own_pid : process_group;
    if (setpgid(0, target_group) < 0 && errno != EACCES) {
        perror("novash: setpgid");
        _exit(1);
    }

    close(start_gate[1]);
    char release_byte;
    while (read(start_gate[0], &release_byte, 1) < 0 && errno == EINTR) {
    }
    close(start_gate[0]);
    restore_child_signal_handlers();

    if (command_index > 0 &&
        redirect_descriptor(pipes[command_index - 1][0],
                            STDIN_FILENO) != 0) {
        _exit(1);
    }
    if (command_index + 1 < pipeline->count &&
        redirect_descriptor(pipes[command_index][1],
                            STDOUT_FILENO) != 0) {
        _exit(1);
    }
    close_pipes(pipes, pipe_count);

    if (apply_command_redirections(command) != 0) {
        _exit(1);
    }
    if (is_builtin(command->argv[0])) {
        int result = execute_builtin(state, command, true);
        fflush(NULL);
        _exit(result);
    }

    execvp(command->argv[0], command->argv);
    fprintf(stderr, "novash: %s: %s\n", command->argv[0],
            errno == ENOENT ? "command not found" : strerror(errno));
    _exit(errno == ENOENT ? 127 : 126);
}

static int create_pipes(int (*pipes)[2], size_t pipe_count) {
    for (size_t index = 0; index < pipe_count; index++) {
        if (pipe(pipes[index]) < 0) {
            perror("novash: pipe");
            for (size_t previous = 0; previous < index; previous++) {
                close(pipes[previous][0]);
                close(pipes[previous][1]);
            }
            return -1;
        }
    }
    return 0;
}

static void terminate_started_pipeline(pid_t process_group,
                                       const pid_t *pids,
                                       size_t started_count) {
    if (process_group > 0) {
        kill(-process_group, SIGTERM);
    }
    for (size_t index = 0; index < started_count; index++) {
        while (waitpid(pids[index], NULL, 0) < 0 && errno == EINTR) {
        }
    }
}

int execute_pipeline(ShellState *state, const Pipeline *pipeline) {
    if (pipeline->count == 1 && !pipeline->background &&
        is_builtin(pipeline->commands[0].argv[0])) {
        state->exit_status =
            execute_parent_builtin(state, &pipeline->commands[0]);
        return state->exit_status;
    }

    size_t pipe_count = pipeline->count - 1;
    int (*pipes)[2] = pipe_count == 0 ? NULL :
                      calloc(pipe_count, sizeof(*pipes));
    pid_t *pids = calloc(pipeline->count, sizeof(*pids));
    if ((pipe_count > 0 && pipes == NULL) || pids == NULL) {
        perror("novash");
        free(pipes);
        free(pids);
        state->exit_status = 1;
        return 1;
    }
    int start_gate[2];
    if (pipe(start_gate) < 0) {
        perror("novash: pipe");
        free(pipes);
        free(pids);
        state->exit_status = 1;
        return 1;
    }
    if (create_pipes(pipes, pipe_count) != 0) {
        close(start_gate[0]);
        close(start_gate[1]);
        free(pipes);
        free(pids);
        state->exit_status = 1;
        return 1;
    }

    pid_t process_group = 0;
    size_t started_count = 0;
    fflush(NULL);
    for (size_t index = 0; index < pipeline->count; index++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("novash: fork");
            close_pipes(pipes, pipe_count);
            close(start_gate[0]);
            close(start_gate[1]);
            terminate_started_pipeline(process_group, pids, started_count);
            free(pipes);
            free(pids);
            state->exit_status = 1;
            return 1;
        }
        if (pid == 0) {
            execute_child(state, pipeline, index, pipes, pipe_count,
                          process_group, start_gate);
        }

        if (process_group == 0) {
            process_group = pid;
        }
        if (setpgid(pid, process_group) < 0 && errno != EACCES &&
            errno != ESRCH) {
            perror("novash: setpgid");
        }
        pids[index] = pid;
        started_count++;
    }
    close_pipes(pipes, pipe_count);
    free(pipes);

    close(start_gate[0]);
    if (state->interactive && !pipeline->background &&
        tcsetpgrp(state->terminal_fd, process_group) < 0) {
        perror("novash: tcsetpgrp");
    }
    close(start_gate[1]);

    Job *job = create_job(process_group, pids, pipeline->count,
                          pipeline->source);
    free(pids);
    if (job == NULL) {
        perror("novash");
        terminate_started_pipeline(process_group, NULL, 0);
        while (waitpid(-process_group, NULL, 0) > 0) {
        }
        if (state->interactive && !pipeline->background) {
            tcsetpgrp(state->terminal_fd, state->shell_pgid);
            tcsetattr(state->terminal_fd, TCSADRAIN,
                      &state->shell_terminal_modes);
        }
        state->exit_status = 1;
        return 1;
    }

    if (pipeline->background) {
        add_job(state, job);
        printf("[%d] %d\n", job->id, job->pgid);
        fflush(stdout);
        state->exit_status = 0;
        return 0;
    }

    int result = wait_for_foreground_job(state, job);
    if (job->state == JOB_STOPPED) {
        add_job(state, job);
        printf("[%d] Stopped  %s\n", job->id, job->command);
    } else {
        free_job(job);
    }
    state->exit_status = result;
    return result;
}
