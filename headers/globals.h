#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_ARGS     64
#define MAX_PATH     1024
#define MAX_LINE     1024

/* parseInfo struct holds parsed command information */
typedef struct {
    char *args[MAX_ARGS];   /* array of argument strings */
    int  argc;              /* number of arguments       */
    char *inputFile;        /* input  redirection file   */
    char *outputFile;       /* output redirection file   */
    int  hasPipe;           /* 1 if command contains '|' */
    char *args2[MAX_ARGS];  /* args for right side of pipe */
    int  argc2;
} parseInfo;

/* Function declarations - single_commands.c */
void executeCommand(parseInfo *info);
void handle_pwd();
void handle_cd(parseInfo *info);
void handle_cat(parseInfo *info);
void handle_nano(parseInfo *info);
void handle_wc(parseInfo *info);
void handle_cp(parseInfo *info);
void handle_clear();
void handle_grep(parseInfo *info);
void handle_ls(parseInfo *info);
void handle_exit();
void handle_tree();

/* Function declarations - piped_commands.c */
void handle_pipe(parseInfo *info);
void handle_redirect_output(parseInfo *info);

/* Function declarations - shell.c */
parseInfo *parse(char *cmdLine);
void freeParseInfo(parseInfo *info);
void initShellExecDir(char *argv0);
extern char g_shellExecDir[MAX_PATH];

#endif /* GLOBALS_H */
