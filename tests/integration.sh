#!/usr/bin/env bash
set -euo pipefail

shell_path="${1:?usage: integration.sh <novash-path>}"
shell_path="$(realpath "$shell_path")"
temporary_directory="$(mktemp -d)"
trap 'rm -rf "$temporary_directory"' EXIT

pipeline_output="$("$shell_path" -c "printf 'hello\nworld\n' | grep world | tr a-z A-Z")"
[[ "$pipeline_output" == "WORLD" ]]

output_file="$temporary_directory/output.txt"
"$shell_path" -c "printf first > $output_file"
"$shell_path" -c "printf second >> $output_file"
[[ "$(cat "$output_file")" == "firstsecond" ]]

input_file="$temporary_directory/input.txt"
printf 'alpha\nbeta\n' > "$input_file"
[[ "$("$shell_path" -c "wc -l < $input_file" | tr -d ' ')" == "2" ]]

[[ "$("$shell_path" -c "pwd")" == "$(pwd)" ]]
"$shell_path" -c "help" | grep -q "NovaShell built-ins"
"$shell_path" -c "tree" | grep -q "src/"

set +e
"$shell_path" -c "definitely-not-a-real-command" >/dev/null 2>&1
missing_command_status=$?
"$shell_path" -c "echo unfinished |" >/dev/null 2>&1
syntax_error_status=$?
set -e

[[ "$missing_command_status" -eq 127 ]]
[[ "$syntax_error_status" -eq 2 ]]

job_output="$(printf 'sleep 30 &\njobs\n' | "$shell_path")"
printf '%s\n' "$job_output" | grep -q "Running"

echo "Integration tests passed."
