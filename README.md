# NovaShell

NovaShell is a compact Unix shell written in C for Linux and WSL. It is a
portfolio-oriented systems programming project that demonstrates process
creation, inter-process communication, file descriptor management, signal
handling, terminal control, and background job management.

The project started as an operating systems assignment and was redesigned into
a modular, testable shell rather than a fixed wrapper around a small set of
commands.

## Highlights

- Executes any program available through `PATH`
- Supports pipelines of arbitrary length
- Supports input, output, and append redirection
- Runs jobs in the foreground or background
- Implements process groups and terminal foreground ownership
- Handles stopped jobs through `jobs`, `fg`, and `bg`
- Preserves the shell when `Ctrl+C` or `Ctrl+Z` is sent to a foreground job
- Parses single quotes, double quotes, and escaped characters
- Provides interactive history through GNU Readline
- Includes unit tests, integration tests, sanitizers, and Linux CI

## Example

```console
$ ./bin/novash
NovaShell - a small POSIX shell. Type 'help' to get started.

novash$ printf "alpha\nbeta\nalpha\n" | grep alpha | wc -l
2

novash$ sleep 30 &
[1] 24801

novash$ jobs
[1] Running  sleep 30 &

novash$ printf "first\n" > notes.txt
novash$ printf "second\n" >> notes.txt
novash$ cat < notes.txt
first
second
```

## Supported syntax

| Syntax | Purpose |
| --- | --- |
| `command arg` | Run an external command or built-in |
| `a \| b \| c` | Connect commands with pipes |
| `command < file` | Redirect standard input |
| `command > file` | Replace a file with standard output |
| `command >> file` | Append standard output to a file |
| `command &` | Run a pipeline in the background |
| `'literal text'` | Preserve characters literally |
| `"quoted text"` | Group text into one argument |
| `escaped\ value` | Escape the next character |

NovaShell intentionally does not invoke another shell to interpret input.
Tokens are parsed locally and programs are started with `execvp`, which avoids
accidental shell expansion and keeps process behavior explicit.

## Built-in commands

- `cd [directory]` — change the current working directory
- `pwd` — print the current working directory
- `tree [directory]` — recursively display a sorted directory tree
- `history` — display the current Readline history
- `jobs` — list running, stopped, and recently completed jobs
- `fg %job` — continue a job in the foreground
- `bg %job` — continue a stopped job in the background
- `clear` — clear the terminal
- `help` — summarize available features
- `exit [status]` — leave the shell with an optional exit status

Commands that change shell state, such as `cd`, are executed in the shell
process when used as a standalone foreground command. Built-ins inside a
pipeline run in a child process, matching normal Unix shell semantics.

## Architecture

```text
input
  |
  v
lexer and parser  --->  Pipeline
                          |
                          v
                     executor
                     /      \
              built-in    fork + execvp
                              |
                       process group
                       /     |     \
                    pipe   pipe   redirect
```

The implementation is separated by responsibility:

- `src/main.c` owns the interactive and non-interactive shell loops.
- `src/parser.c` tokenizes input and builds pipelines with per-command
  redirection metadata.
- `src/executor.c` creates pipes, forks children, connects file descriptors,
  forms process groups, and dispatches commands.
- `src/jobs.c` tracks process state and implements foreground/background job
  transitions.
- `src/signals.c` separates shell signal policy from child signal policy.
- `src/builtins.c` implements commands that must run inside NovaShell.
- `include/shell.h` defines shared data structures and module interfaces.

Every parsed pipeline owns its allocated arguments and paths. `free_pipeline`
releases that ownership after execution. Every child closes all inherited pipe
descriptors after `dup2`, preventing descriptor leaks and pipelines that never
receive EOF.

## Requirements

- Linux or WSL
- GCC or another C11-compatible compiler
- GNU Make
- GNU Readline development headers
- Bash for the integration test script

Install dependencies on Ubuntu or Debian:

```bash
sudo apt update
sudo apt install build-essential libreadline-dev
```

## Build and run

```bash
make
./bin/novash
```

Run one command without starting an interactive session:

```bash
./bin/novash -c "printf 'hello\n' | tr a-z A-Z"
```

Additional build targets:

```bash
make debug       # Build with debug symbols
make sanitize    # Build with AddressSanitizer and UndefinedBehaviorSanitizer
make clean       # Remove generated files
```

## Tests

```bash
make test
```

The test suite includes:

- Parser unit tests for quoting, escaping, redirections, pipelines, background
  commands, empty input, and invalid syntax
- Integration tests for external commands, multi-stage pipelines, file
  redirection, built-ins, exit statuses, and background job registration
- A GitHub Actions workflow that builds and tests the project on Linux

## Job control design

All processes in a pipeline are assigned to the same process group. For a
foreground pipeline, NovaShell gives that group ownership of the terminal with
`tcsetpgrp` and waits for it to finish or stop. The terminal is then returned
to the shell's process group.

NovaShell ignores interactive terminal signals while it is waiting. Child
processes restore default handlers before running a built-in or calling
`execvp`, so `Ctrl+C` and `Ctrl+Z` affect the foreground job instead of the
shell itself. Background state is collected with non-blocking `waitpid` calls,
preventing zombie processes.

## Current scope

NovaShell focuses on core POSIX process management. It does not currently
implement environment variable expansion, globbing, command substitution,
logical operators such as `&&`, or persistent history. These are deliberately
left as future extensions so the current code remains small enough to study.

## License

No license has been selected yet. Add one before distributing modified copies
outside a portfolio or coursework context.
