============================================================
  Custom Shell - Operating Systems Assignment 2
============================================================

HOW TO COMPILE
--------------
gcc src/shell.c src/single_commands.c src/piped_commands.c -o shell -lreadline

HOW TO RUN
----------
./shell

SUPPORTED COMMANDS
------------------
  pwd                   - print current working directory
  cd <path>             - change directory
  cat <filename>        - display file contents
  nano <filename>       - open nano text editor
  wc <filename>         - word/line/char count
  wc -l <filename>      - count lines
  wc -c <filename>      - count bytes
  wc -w <filename>      - count words
  cp <src> <dst>        - copy a file
  clear                 - clear the terminal screen
  grep <pattern> <file> - search for pattern in file
  grep -c <pattern> <file> - count matching lines
  ls                    - list directory contents
  ls -l                 - long listing format
  ls -l > <file>        - redirect output to file
  tree                  - display directory tree
  exit                  - quit the shell

PIPE:      cmd1 | cmd2
REDIRECT:  cmd > file
============================================================
