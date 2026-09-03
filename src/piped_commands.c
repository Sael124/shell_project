#include "../headers/globals.h"

/* -------------------------------------------------------
 * handle_pipe – run  cmd1 | cmd2
 * ------------------------------------------------------- */
void handle_pipe(parseInfo *info) {
    int pipefd[2];

    if (pipe(pipefd) < 0) { perror("pipe"); exit(1); }

    /* --- first child: runs left-side command, writes to pipe --- */
    pid_t pid1 = fork();
    if (pid1 < 0) { perror("fork"); exit(1); }

    if (pid1 == 0) {
        /* redirect stdout → write-end of pipe */
        close(pipefd[0]);                      /* close read end */
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        execvp(info->args[0], info->args);
        fprintf(stderr, "shell: %s: command not found\n", info->args[0]);
        exit(1);
    }

    /* --- second child: runs right-side command, reads from pipe --- */
    pid_t pid2 = fork();
    if (pid2 < 0) { perror("fork"); exit(1); }

    if (pid2 == 0) {
        /* redirect stdin → read-end of pipe */
        close(pipefd[1]);                      /* close write end */
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);

        execvp(info->args2[0], info->args2);
        fprintf(stderr, "shell: %s: command not found\n", info->args2[0]);
        exit(1);
    }

    /* parent: close both ends and wait for both children */
    close(pipefd[0]);
    close(pipefd[1]);
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
}

/* -------------------------------------------------------
 * handle_redirect_output – run cmd, send stdout to file
 * e.g.  ls -l > output.txt
 * ------------------------------------------------------- */
void handle_redirect_output(parseInfo *info) {
    /* open (create/truncate) the output file */
    int fd = open(info->outputFile,
                  O_WRONLY | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) { perror("open"); exit(1); }

    dup2(fd, STDOUT_FILENO);   /* redirect stdout → file */
    close(fd);

    execvp(info->args[0], info->args);
    fprintf(stderr, "shell: %s: command not found\n", info->args[0]);
    exit(1);
}
