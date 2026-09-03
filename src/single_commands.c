#include "../headers/globals.h"

/* -------------------------------------------------------
 * executeCommand – dispatch to the right handler
 * ------------------------------------------------------- */
void executeCommand(parseInfo *info) {
    if (info->argc == 0) return;

    char *cmd = info->args[0];

    if (info->inputFile) {
        int inFd = open(info->inputFile, O_RDONLY);
        if (inFd < 0) {
            perror("open");
            return;
        }
        if (dup2(inFd, STDIN_FILENO) < 0) {
            perror("dup2");
            close(inFd);
            return;
        }
        close(inFd);
    }

    /* output redirection (ls -l > file, etc.) */
    if (info->outputFile) {
        handle_redirect_output(info);
        return;
    }

    /* pipe */
    if (info->hasPipe) {
        handle_pipe(info);
        return;
    }

    if      (strcmp(cmd, "pwd")   == 0) {
        if (info->argc != 1) fprintf(stderr, "pwd: Usage: pwd\n");
        else handle_pwd();
    }
    else if (strcmp(cmd, "cd")    == 0) handle_cd(info);   /* fallback */
    else if (strcmp(cmd, "cat")   == 0) handle_cat(info);
    else if (strcmp(cmd, "nano")  == 0) handle_nano(info);
    else if (strcmp(cmd, "wc")    == 0) handle_wc(info);
    else if (strcmp(cmd, "cp")    == 0) handle_cp(info);
    else if (strcmp(cmd, "clear") == 0) {
        if (info->argc != 1) fprintf(stderr, "clear: Usage: clear\n");
        else handle_clear();
    }
    else if (strcmp(cmd, "grep")  == 0) handle_grep(info);
    else if (strcmp(cmd, "ls")    == 0) handle_ls(info);
    else if (strcmp(cmd, "tree")  == 0) {
        if (info->argc != 1) fprintf(stderr, "tree: Usage: tree\n");
        else handle_tree();
    }
    else if (strcmp(cmd, "exit")  == 0) {
        if (info->argc != 1) fprintf(stderr, "exit: Usage: exit\n");
        else handle_exit();
    }
    else {
        /* try to run as external command */
        execvp(cmd, info->args);
        /* if execvp returns, the command was not found */
        fprintf(stderr, "shell: %s: command not found\n", cmd);
        exit(1);
    }
}

/* -------------------------------------------------------
 * pwd
 * ------------------------------------------------------- */
void handle_pwd() {
    /* keep behavior close to shell builtins: no extra arguments */
    /* argc check handled before dispatch since this handler has no parseInfo */
    char cwd[MAX_PATH];
    if (getcwd(cwd, sizeof(cwd)) != NULL)
        printf("%s\n", cwd);
    else
        perror("pwd");
}

/* -------------------------------------------------------
 * cd
 * ------------------------------------------------------- */
void handle_cd(parseInfo *info) {
    if (info->argc < 2) {
        fprintf(stderr, "cd: missing argument\nUsage: cd <path>\n");
        return;
    }
    if (info->argc > 2) {
        fprintf(stderr, "cd: too many arguments\nUsage: cd <path>\n");
        return;
    }
    if (chdir(info->args[1]) != 0)
        perror("cd");
}

/* -------------------------------------------------------
 * cat  (cat <filename>  OR  cat > <filename> handled via redirect)
 * ------------------------------------------------------- */
void handle_cat(parseInfo *info) {
    if (info->argc != 2) {
        fprintf(stderr, "cat: Usage: cat <filename>\n");
        return;
    }
    execvp("cat", info->args);
    perror("cat");
    exit(1);
}

/* -------------------------------------------------------
 * nano  (opens nano editor for the given file)
 * ------------------------------------------------------- */
void handle_nano(parseInfo *info) {
    if (info->argc != 2) {
        fprintf(stderr, "nano: Usage: nano <filename>\n");
        return;
    }
    execvp("nano", info->args);
    perror("nano");
    exit(1);
}

/* -------------------------------------------------------
 * wc / wc -l / wc -c / wc -w
 * ------------------------------------------------------- */
void handle_wc(parseInfo *info) {
    if (info->argc == 2) {
        execvp("wc", info->args);
        perror("wc");
        exit(1);
    }

    if (info->argc == 3 &&
        (strcmp(info->args[1], "-l") == 0 ||
         strcmp(info->args[1], "-c") == 0 ||
         strcmp(info->args[1], "-w") == 0)) {
        execvp("wc", info->args);
        perror("wc");
        exit(1);
    }

    fprintf(stderr, "wc: Usage: wc <filename> | wc -l|-c|-w <filename>\n");
}

/* -------------------------------------------------------
 * cp <src> <dst>
 * ------------------------------------------------------- */
void handle_cp(parseInfo *info) {
    if (info->argc != 3) {
        fprintf(stderr, "cp: Usage: cp <src> <dst>\n");
        return;
    }
    execvp("cp", info->args);
    perror("cp");
    exit(1);
}

/* -------------------------------------------------------
 * clear
 * ------------------------------------------------------- */
void handle_clear() {
    printf("\033[H\033[J");   /* ANSI escape: move home + erase screen */
    fflush(stdout);
}

/* -------------------------------------------------------
 * grep / grep -c
 * ------------------------------------------------------- */
void handle_grep(parseInfo *info) {
    if (info->argc == 3) {
        execvp("grep", info->args);
        perror("grep");
        exit(1);
    }

    if (info->argc == 4 && strcmp(info->args[1], "-c") == 0) {
        execvp("grep", info->args);
        perror("grep");
        exit(1);
    }

    fprintf(stderr, "grep: Usage: grep <pattern> <file> | grep -c <pattern> <file>\n");
}

/* -------------------------------------------------------
 * ls / ls -l / ls -l > file
 * ------------------------------------------------------- */
void handle_ls(parseInfo *info) {
    if (info->argc == 1) {
        execvp("ls", info->args);
        perror("ls");
        exit(1);
    }

    if (info->argc == 2 && strcmp(info->args[1], "-l") == 0) {
        execvp("ls", info->args);
        perror("ls");
        exit(1);
    }

    fprintf(stderr, "ls: Usage: ls | ls -l | ls -l > <file>\n");
}

/* -------------------------------------------------------
 * tree – compile tree.c and run it
 * ------------------------------------------------------- */
void handle_tree() {
    char cwd[MAX_PATH];
    if (getcwd(cwd, sizeof(cwd)) == NULL) { perror("getcwd"); return; }

    if (g_shellExecDir[0] == '\0') {
        fprintf(stderr, "tree: could not resolve shell executable directory\n");
        return;
    }

    /* Step 1: compile tree.c → tree_bin */
    char treeSrc[MAX_PATH * 2], treeBin[MAX_PATH * 2];
    snprintf(treeSrc, sizeof(treeSrc), "%s/tree.c", g_shellExecDir);
    snprintf(treeBin, sizeof(treeBin), "%s/tree_bin", g_shellExecDir);

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return; }
    if (pid == 0) {
        /* child: compile */
        char *gccArgs[] = { "gcc", treeSrc, "-o", treeBin, NULL };
        execvp("gcc", gccArgs);
        perror("gcc");
        exit(1);
    }
    int compileStatus = 0;
    waitpid(pid, &compileStatus, 0);
    if (!WIFEXITED(compileStatus) || WEXITSTATUS(compileStatus) != 0) {
        fprintf(stderr, "tree: failed to compile tree.c\n");
        return;
    }

    /* Step 2: run tree_bin with cwd as argument */
    pid = fork();
    if (pid < 0) { perror("fork"); return; }
    if (pid == 0) {
        char *treeArgs[] = { treeBin, cwd, NULL };
        execvp(treeBin, treeArgs);
        perror("tree");
        exit(1);
    }
    waitpid(pid, NULL, 0);
}

/* -------------------------------------------------------
 * exit
 * ------------------------------------------------------- */
void handle_exit() {
    printf("Goodbye!\n");
    exit(0);
}
