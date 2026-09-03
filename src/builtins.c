#include "shell.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <readline/history.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef int (*BuiltinHandler)(ShellState *, const Command *, bool);

typedef struct {
    const char *name;
    BuiltinHandler handler;
} BuiltinDefinition;

static int builtin_cd(ShellState *state, const Command *command,
                      bool in_child);
static int builtin_pwd(ShellState *state, const Command *command,
                       bool in_child);
static int builtin_exit(ShellState *state, const Command *command,
                        bool in_child);
static int builtin_clear(ShellState *state, const Command *command,
                         bool in_child);
static int builtin_history(ShellState *state, const Command *command,
                           bool in_child);
static int builtin_help(ShellState *state, const Command *command,
                        bool in_child);
static int builtin_jobs(ShellState *state, const Command *command,
                        bool in_child);
static int builtin_fg(ShellState *state, const Command *command,
                      bool in_child);
static int builtin_bg(ShellState *state, const Command *command,
                      bool in_child);
static int builtin_tree(ShellState *state, const Command *command,
                        bool in_child);

static const BuiltinDefinition BUILTINS[] = {
    {"cd", builtin_cd},
    {"pwd", builtin_pwd},
    {"exit", builtin_exit},
    {"clear", builtin_clear},
    {"history", builtin_history},
    {"help", builtin_help},
    {"jobs", builtin_jobs},
    {"fg", builtin_fg},
    {"bg", builtin_bg},
    {"tree", builtin_tree},
};

static const size_t BUILTIN_COUNT = sizeof(BUILTINS) / sizeof(BUILTINS[0]);

bool is_builtin(const char *name) {
    if (name == NULL) {
        return false;
    }
    for (size_t index = 0; index < BUILTIN_COUNT; index++) {
        if (strcmp(name, BUILTINS[index].name) == 0) {
            return true;
        }
    }
    return false;
}

int execute_builtin(ShellState *state, const Command *command, bool in_child) {
    for (size_t index = 0; index < BUILTIN_COUNT; index++) {
        if (strcmp(command->argv[0], BUILTINS[index].name) == 0) {
            return BUILTINS[index].handler(state, command, in_child);
        }
    }
    return 127;
}

static int builtin_cd(ShellState *state, const Command *command,
                      bool in_child) {
    (void)state;
    (void)in_child;
    if (command->argc > 2) {
        fprintf(stderr, "cd: usage: cd [directory]\n");
        return 2;
    }

    const char *target = command->argc == 1 ? getenv("HOME") :
                         command->argv[1];
    if (target == NULL) {
        fprintf(stderr, "cd: HOME is not set\n");
        return 1;
    }
    if (chdir(target) < 0) {
        fprintf(stderr, "cd: %s: %s\n", target, strerror(errno));
        return 1;
    }
    return 0;
}

static int builtin_pwd(ShellState *state, const Command *command,
                       bool in_child) {
    (void)state;
    (void)in_child;
    if (command->argc != 1) {
        fprintf(stderr, "pwd: usage: pwd\n");
        return 2;
    }

    char *directory = getcwd(NULL, 0);
    if (directory == NULL) {
        perror("pwd");
        return 1;
    }
    puts(directory);
    free(directory);
    return 0;
}

static int builtin_exit(ShellState *state, const Command *command,
                        bool in_child) {
    if (command->argc > 2) {
        fprintf(stderr, "exit: usage: exit [status]\n");
        return 2;
    }

    int status = state->exit_status;
    if (command->argc == 2) {
        char *end = NULL;
        errno = 0;
        long parsed = strtol(command->argv[1], &end, 10);
        if (errno != 0 || *command->argv[1] == '\0' || *end != '\0' ||
            parsed < 0 || parsed > 255) {
            fprintf(stderr, "exit: status must be between 0 and 255\n");
            return 2;
        }
        status = (int)parsed;
    }

    if (!in_child) {
        state->should_exit = true;
    }
    state->exit_status = status;
    return status;
}

static int builtin_clear(ShellState *state, const Command *command,
                         bool in_child) {
    (void)state;
    (void)in_child;
    if (command->argc != 1) {
        fprintf(stderr, "clear: usage: clear\n");
        return 2;
    }
    fputs("\033[2J\033[H", stdout);
    fflush(stdout);
    return 0;
}

static int builtin_history(ShellState *state, const Command *command,
                           bool in_child) {
    (void)state;
    (void)in_child;
    if (command->argc != 1) {
        fprintf(stderr, "history: usage: history\n");
        return 2;
    }

    HIST_ENTRY **entries = history_list();
    if (entries == NULL) {
        return 0;
    }
    for (int index = 0; entries[index] != NULL; index++) {
        printf("%5d  %s\n", history_base + index, entries[index]->line);
    }
    return 0;
}

static int builtin_help(ShellState *state, const Command *command,
                        bool in_child) {
    (void)state;
    (void)in_child;
    if (command->argc != 1) {
        fprintf(stderr, "help: usage: help\n");
        return 2;
    }

    puts("NovaShell built-ins:");
    puts("  cd [dir]        Change the current directory");
    puts("  pwd             Print the current directory");
    puts("  tree [dir]      Display a directory tree");
    puts("  history         Show command history");
    puts("  jobs            List background and stopped jobs");
    puts("  fg %id          Move a job to the foreground");
    puts("  bg %id          Continue a stopped job in background");
    puts("  clear           Clear the terminal");
    puts("  exit [status]   Exit NovaShell");
    puts("");
    puts("Operators: |  <  >  >>  &");
    return 0;
}

static int parse_job_id(const Command *command, const char *name,
                        int *job_id) {
    if (command->argc != 2) {
        fprintf(stderr, "%s: usage: %s %%job\n", name, name);
        return -1;
    }

    const char *text = command->argv[1];
    if (*text == '%') {
        text++;
    }
    char *end = NULL;
    long parsed = strtol(text, &end, 10);
    if (*text == '\0' || *end != '\0' || parsed <= 0 ||
        parsed > INT_MAX) {
        fprintf(stderr, "%s: invalid job id: %s\n", name,
                command->argv[1]);
        return -1;
    }
    *job_id = (int)parsed;
    return 0;
}

static int reject_child_job_control(const char *name, bool in_child) {
    if (!in_child) {
        return 0;
    }
    fprintf(stderr, "%s: unavailable in a pipeline or background command\n",
            name);
    return 1;
}

static int builtin_jobs(ShellState *state, const Command *command,
                        bool in_child) {
    if (reject_child_job_control("jobs", in_child) != 0) {
        return 1;
    }
    if (command->argc != 1) {
        fprintf(stderr, "jobs: usage: jobs\n");
        return 2;
    }
    print_jobs(state);
    return 0;
}

static int builtin_fg(ShellState *state, const Command *command,
                      bool in_child) {
    if (reject_child_job_control("fg", in_child) != 0) {
        return 1;
    }
    int job_id = 0;
    if (parse_job_id(command, "fg", &job_id) != 0) {
        return 2;
    }
    return put_job_in_foreground(state, job_id);
}

static int builtin_bg(ShellState *state, const Command *command,
                      bool in_child) {
    if (reject_child_job_control("bg", in_child) != 0) {
        return 1;
    }
    int job_id = 0;
    if (parse_job_id(command, "bg", &job_id) != 0) {
        return 2;
    }
    return continue_job_in_background(state, job_id);
}

static int compare_names(const void *left, const void *right) {
    const char *const *left_name = left;
    const char *const *right_name = right;
    return strcmp(*left_name, *right_name);
}

static bool path_is_directory(const char *path) {
    struct stat metadata;
    return lstat(path, &metadata) == 0 && S_ISDIR(metadata.st_mode);
}

static int print_tree(const char *path, size_t depth) {
    DIR *directory = opendir(path);
    if (directory == NULL) {
        fprintf(stderr, "tree: %s: %s\n", path, strerror(errno));
        return 1;
    }

    char **names = NULL;
    size_t count = 0;
    size_t capacity = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        if (count == capacity) {
            size_t new_capacity = capacity == 0 ? 16 : capacity * 2;
            char **resized = realloc(names,
                                     new_capacity * sizeof(*resized));
            if (resized == NULL) {
                perror("tree");
                closedir(directory);
                for (size_t index = 0; index < count; index++) {
                    free(names[index]);
                }
                free(names);
                return 1;
            }
            names = resized;
            capacity = new_capacity;
        }
        names[count] = strdup(entry->d_name);
        if (names[count] == NULL) {
            perror("tree");
            closedir(directory);
            for (size_t index = 0; index < count; index++) {
                free(names[index]);
            }
            free(names);
            return 1;
        }
        count++;
    }
    closedir(directory);
    qsort(names, count, sizeof(*names), compare_names);

    int result = 0;
    for (size_t index = 0; index < count; index++) {
        size_t path_length = strlen(path) + strlen(names[index]) + 2;
        char *child_path = malloc(path_length);
        if (child_path == NULL) {
            perror("tree");
            result = 1;
            free(names[index]);
            continue;
        }
        snprintf(child_path, path_length, "%s/%s", path, names[index]);
        bool is_directory = path_is_directory(child_path);
        printf("%*s%s%s\n", (int)(depth * 2), "", names[index],
               is_directory ? "/" : "");
        if (is_directory && print_tree(child_path, depth + 1) != 0) {
            result = 1;
        }

        free(child_path);
        free(names[index]);
    }
    free(names);
    return result;
}

static int builtin_tree(ShellState *state, const Command *command,
                        bool in_child) {
    (void)state;
    (void)in_child;
    if (command->argc > 2) {
        fprintf(stderr, "tree: usage: tree [directory]\n");
        return 2;
    }
    const char *path = command->argc == 2 ? command->argv[1] : ".";
    puts(path);
    return print_tree(path, 1);
}
