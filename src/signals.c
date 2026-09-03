#include "shell.h"

#include <signal.h>
#include <stddef.h>

static volatile sig_atomic_t child_signal_pending = 0;

static void handle_child_signal(int signal_number) {
    (void)signal_number;
    child_signal_pending = 1;
}

static void install_handler(int signal_number, void (*handler)(int),
                            int flags) {
    struct sigaction action;
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = flags;
    sigaction(signal_number, &action, NULL);
}

void install_shell_signal_handlers(void) {
    install_handler(SIGINT, SIG_IGN, SA_RESTART);
    install_handler(SIGQUIT, SIG_IGN, SA_RESTART);
    install_handler(SIGTSTP, SIG_IGN, SA_RESTART);
    install_handler(SIGTTIN, SIG_IGN, SA_RESTART);
    install_handler(SIGTTOU, SIG_IGN, SA_RESTART);
    install_handler(SIGCHLD, handle_child_signal, 0);
}

void restore_child_signal_handlers(void) {
    install_handler(SIGINT, SIG_DFL, 0);
    install_handler(SIGQUIT, SIG_DFL, 0);
    install_handler(SIGTSTP, SIG_DFL, 0);
    install_handler(SIGTTIN, SIG_DFL, 0);
    install_handler(SIGTTOU, SIG_DFL, 0);
    install_handler(SIGCHLD, SIG_DFL, 0);
}

bool consume_child_signal(void) {
    sigset_t block_set;
    sigset_t previous_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGCHLD);
    sigprocmask(SIG_BLOCK, &block_set, &previous_set);

    bool was_pending = child_signal_pending != 0;
    child_signal_pending = 0;
    sigprocmask(SIG_SETMASK, &previous_set, NULL);
    return was_pending;
}
