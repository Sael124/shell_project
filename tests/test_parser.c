#include "shell.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Pipeline parse_successfully(const char *line) {
    Pipeline pipeline;
    char *error = NULL;
    int result = parse_pipeline(line, &pipeline, &error);
    if (result != 0) {
        fprintf(stderr, "Unexpected parse failure for '%s': %s\n", line,
                error == NULL ? "empty input" : error);
        free(error);
        abort();
    }
    return pipeline;
}

static void test_simple_command(void) {
    Pipeline pipeline = parse_successfully("echo hello");
    assert(pipeline.count == 1);
    assert(pipeline.commands[0].argc == 2);
    assert(strcmp(pipeline.commands[0].argv[0], "echo") == 0);
    assert(strcmp(pipeline.commands[0].argv[1], "hello") == 0);
    assert(pipeline.commands[0].argv[2] == NULL);
    free_pipeline(&pipeline);
}

static void test_quotes_and_escapes(void) {
    Pipeline pipeline =
        parse_successfully("printf '%s %s' \"hello world\" escaped\\ value \"\"");
    assert(pipeline.commands[0].argc == 5);
    assert(strcmp(pipeline.commands[0].argv[1], "%s %s") == 0);
    assert(strcmp(pipeline.commands[0].argv[2], "hello world") == 0);
    assert(strcmp(pipeline.commands[0].argv[3], "escaped value") == 0);
    assert(strcmp(pipeline.commands[0].argv[4], "") == 0);
    free_pipeline(&pipeline);

    pipeline = parse_successfully("echo \"keep \\q and \\\"quotes\\\"\"");
    assert(pipeline.commands[0].argc == 2);
    assert(strcmp(pipeline.commands[0].argv[1], "keep \\q and \"quotes\"") == 0);
    free_pipeline(&pipeline);
}

static void test_pipeline_and_redirections(void) {
    Pipeline pipeline =
        parse_successfully("cat < input.txt | grep value | wc -l >> count.txt &");
    assert(pipeline.count == 3);
    assert(pipeline.background);
    assert(strcmp(pipeline.commands[0].input_path, "input.txt") == 0);
    assert(strcmp(pipeline.commands[1].argv[0], "grep") == 0);
    assert(strcmp(pipeline.commands[2].output_path, "count.txt") == 0);
    assert(pipeline.commands[2].append_output);
    free_pipeline(&pipeline);
}

static void expect_error(const char *line) {
    Pipeline pipeline;
    char *error = NULL;
    assert(parse_pipeline(line, &pipeline, &error) < 0);
    assert(error != NULL);
    free(error);
}

static void test_syntax_errors(void) {
    expect_error("| cat");
    expect_error("cat |");
    expect_error("echo hello & wc");
    expect_error("cat >");
    expect_error("echo 'unterminated");
    expect_error("cat < first < second");
    expect_error("> output.txt");
    expect_error("&");
}

static void test_empty_input(void) {
    Pipeline pipeline;
    char *error = NULL;
    assert(parse_pipeline("   ", &pipeline, &error) == 1);
    assert(error == NULL);
}

int main(void) {
    test_simple_command();
    test_quotes_and_escapes();
    test_pipeline_and_redirections();
    test_syntax_errors();
    test_empty_input();
    puts("Parser tests passed.");
    return 0;
}
