#include "../headers/globals.h"

char g_shellExecDir[MAX_PATH] = {0};

static int isSpecialChar(char c) {
    return (c == '|' || c == '<' || c == '>');
}

static char *nextToken(const char **cursor) {
    const char *p = *cursor;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) {
        *cursor = p;
        return NULL;
    }

    if (isSpecialChar(*p)) {
        char *token = malloc(2);
        if (!token) { perror("malloc"); exit(1); }
        token[0] = *p;
        token[1] = '\0';
        *cursor = p + 1;
        return token;
    }

    char quote = '\0';
    if (*p == '\'' || *p == '"') {
        quote = *p;
        p++;
    }

    char buffer[MAX_LINE];
    size_t idx = 0;
    while (*p) {
        if (quote) {
            if (*p == quote) {
                p++;
                break;
            }
        } else if (isspace((unsigned char)*p) || isSpecialChar(*p)) {
            break;
        }

        if (idx < sizeof(buffer) - 1) {
            buffer[idx++] = *p;
        }
        p++;
    }
    buffer[idx] = '\0';

    char *token = strdup(buffer);
    if (!token) { perror("strdup"); exit(1); }
    *cursor = p;
    return token;
}

void initShellExecDir(char *argv0) {
    if (!argv0) return;

    char resolved[PATH_MAX];
    if (!realpath(argv0, resolved)) {
        perror("realpath");
        return;
    }

    char *lastSlash = strrchr(resolved, '/');
    if (!lastSlash) return;

    *lastSlash = '\0';
    strncpy(g_shellExecDir, resolved, sizeof(g_shellExecDir) - 1);
    g_shellExecDir[sizeof(g_shellExecDir) - 1] = '\0';
}

/* -------------------------------------------------------
 * parse – splits cmdLine into tokens and fills parseInfo
 * ------------------------------------------------------- */
parseInfo *parse(char *cmdLine) {
    parseInfo *info = malloc(sizeof(parseInfo));
    if (!info) { perror("malloc"); exit(1); }

    /* zero everything */
    memset(info, 0, sizeof(parseInfo));
    info->inputFile  = NULL;
    info->outputFile = NULL;
    info->hasPipe    = 0;
    info->argc       = 0;
    info->argc2      = 0;

    if (!cmdLine || strlen(cmdLine) == 0) return info;

    const char *cursor = cmdLine;
    int inRightSide = 0;

    while (1) {
        char *token = nextToken(&cursor);
        if (!token) break;

        if (strcmp(token, "|") == 0) {
            info->hasPipe = 1;
            inRightSide = 1;
            free(token);
            continue;
        }

        if (!inRightSide && strcmp(token, ">") == 0) {
            free(token);
            token = nextToken(&cursor);
            if (!token) {
                fprintf(stderr, "shell: syntax error near unexpected token `>'\n");
                break;
            }
            info->outputFile = token;
            continue;
        }

        if (!inRightSide && strcmp(token, "<") == 0) {
            free(token);
            token = nextToken(&cursor);
            if (!token) {
                fprintf(stderr, "shell: syntax error near unexpected token `<'\n");
                break;
            }
            info->inputFile = token;
            continue;
        }

        if (!inRightSide) {
            if (info->argc < MAX_ARGS - 1) {
                info->args[info->argc++] = token;
            } else {
                free(token);
            }
        } else {
            if (info->argc2 < MAX_ARGS - 1) {
                info->args2[info->argc2++] = token;
            } else {
                free(token);
            }
        }
    }

    if (info->hasPipe && (info->argc == 0 || info->argc2 == 0)) {
        fprintf(stderr, "shell: invalid pipe usage\n");
    }

    info->args[info->argc] = NULL;
    info->args2[info->argc2] = NULL;
    return info;
}

/* free all memory inside a parseInfo */
void freeParseInfo(parseInfo *info) {
    if (!info) return;
    for (int i = 0; i < info->argc;  i++) free(info->args[i]);
    for (int i = 0; i < info->argc2; i++) free(info->args2[i]);
    if (info->inputFile)  free(info->inputFile);
    if (info->outputFile) free(info->outputFile);
    free(info);
}

/* -------------------------------------------------------
 * main – the shell loop
 * ------------------------------------------------------- */
int main(int argc, char **argv) {
    int   childPid;
    char *cmdLine;
    parseInfo *info;

    printf("---Starting Custom Shell---\n");
    printf("----------------------------\n");
    initShellExecDir(argv[0]);

    while (1) {
        /* build prompt showing current directory */
        char cwd[MAX_PATH];
        if (getcwd(cwd, sizeof(cwd)) == NULL) strcpy(cwd, "?");

        char prompt[MAX_PATH + 4];
        snprintf(prompt, sizeof(prompt), "%s>> ", cwd);

        cmdLine = readline(prompt);
        if (!cmdLine) break;              /* Ctrl-D → EOF */
        if (strlen(cmdLine) == 0) { free(cmdLine); continue; }

        add_history(cmdLine);             /* readline history */

        info = parse(cmdLine);
        free(cmdLine);

        if (info->argc == 0) { freeParseInfo(info); continue; }

        /* built-in: cd and exit must run in the parent process */
        if (strcmp(info->args[0], "cd") == 0) {
            handle_cd(info);
            freeParseInfo(info);
            continue;
        }
        if (strcmp(info->args[0], "exit") == 0) {
            if (info->argc != 1) {
                fprintf(stderr, "exit: Usage: exit\n");
                freeParseInfo(info);
                continue;
            }
            handle_exit();
        }

        childPid = fork();
        if (childPid < 0) { perror("fork"); freeParseInfo(info); continue; }

        if (childPid == 0) {
            /* ---- child ---- */
            executeCommand(info);
            exit(0);
        } else {
            /* ---- parent ---- */
            waitpid(childPid, NULL, 0);
        }

        freeParseInfo(info);
    }

    return 0;
}
