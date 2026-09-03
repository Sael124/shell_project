#include "shell.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    TOKEN_WORD,
    TOKEN_PIPE,
    TOKEN_INPUT,
    TOKEN_OUTPUT,
    TOKEN_APPEND,
    TOKEN_BACKGROUND,
    TOKEN_END,
    TOKEN_ERROR
} TokenType;

typedef struct {
    TokenType type;
    char *text;
} Token;

typedef struct {
    const char *cursor;
} Lexer;

static char *duplicate_string(const char *value) {
    char *copy = strdup(value);
    if (copy == NULL) {
        perror("strdup");
    }
    return copy;
}

static bool is_operator(char character) {
    return character == '|' || character == '<' || character == '>' ||
           character == '&';
}

static int append_character(char **buffer, size_t *length, size_t *capacity,
                            char character) {
    if (*length + 1 >= *capacity) {
        size_t new_capacity = *capacity == 0 ? 32 : *capacity * 2;
        char *resized = realloc(*buffer, new_capacity);
        if (resized == NULL) {
            return -1;
        }
        *buffer = resized;
        *capacity = new_capacity;
    }

    (*buffer)[(*length)++] = character;
    (*buffer)[*length] = '\0';
    return 0;
}

static Token make_simple_token(TokenType type) {
    Token token = {.type = type, .text = NULL};
    return token;
}

static Token next_token(Lexer *lexer) {
    while (isspace((unsigned char)*lexer->cursor)) {
        lexer->cursor++;
    }

    if (*lexer->cursor == '\0') {
        return make_simple_token(TOKEN_END);
    }
    if (*lexer->cursor == '|') {
        lexer->cursor++;
        return make_simple_token(TOKEN_PIPE);
    }
    if (*lexer->cursor == '<') {
        lexer->cursor++;
        return make_simple_token(TOKEN_INPUT);
    }
    if (*lexer->cursor == '&') {
        lexer->cursor++;
        return make_simple_token(TOKEN_BACKGROUND);
    }
    if (*lexer->cursor == '>') {
        lexer->cursor++;
        if (*lexer->cursor == '>') {
            lexer->cursor++;
            return make_simple_token(TOKEN_APPEND);
        }
        return make_simple_token(TOKEN_OUTPUT);
    }

    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    bool consumed = false;

    while (*lexer->cursor != '\0' &&
           !isspace((unsigned char)*lexer->cursor) &&
           !is_operator(*lexer->cursor)) {
        if (*lexer->cursor == '\\') {
            consumed = true;
            lexer->cursor++;
            if (*lexer->cursor == '\0') {
                free(buffer);
                Token token = make_simple_token(TOKEN_ERROR);
                token.text = duplicate_string("trailing escape character");
                return token;
            }
            if (append_character(&buffer, &length, &capacity,
                                 *lexer->cursor++) != 0) {
                free(buffer);
                return make_simple_token(TOKEN_ERROR);
            }
            continue;
        }

        if (*lexer->cursor == '\'' || *lexer->cursor == '"') {
            consumed = true;
            char quote = *lexer->cursor++;
            while (*lexer->cursor != '\0' && *lexer->cursor != quote) {
                if (quote == '"' && *lexer->cursor == '\\' &&
                    lexer->cursor[1] != '\0') {
                    char escaped = lexer->cursor[1];
                    if (escaped == '"' || escaped == '\\' ||
                        escaped == '$' || escaped == '`' ||
                        escaped == '\n') {
                        lexer->cursor++;
                    }
                }
                if (append_character(&buffer, &length, &capacity,
                                     *lexer->cursor++) != 0) {
                    free(buffer);
                    return make_simple_token(TOKEN_ERROR);
                }
            }
            if (*lexer->cursor != quote) {
                free(buffer);
                Token token = make_simple_token(TOKEN_ERROR);
                token.text = duplicate_string("unterminated quote");
                return token;
            }
            lexer->cursor++;
            continue;
        }

        consumed = true;
        if (append_character(&buffer, &length, &capacity,
                             *lexer->cursor++) != 0) {
            free(buffer);
            return make_simple_token(TOKEN_ERROR);
        }
    }

    if (!consumed) {
        free(buffer);
        return make_simple_token(TOKEN_ERROR);
    }
    if (buffer == NULL) {
        buffer = duplicate_string("");
    }

    Token token = {.type = TOKEN_WORD, .text = buffer};
    return token;
}

static void free_command(Command *command) {
    for (size_t index = 0; index < command->argc; index++) {
        free(command->argv[index]);
    }
    free(command->argv);
    free(command->input_path);
    free(command->output_path);
    memset(command, 0, sizeof(*command));
}

void free_pipeline(Pipeline *pipeline) {
    if (pipeline == NULL) {
        return;
    }
    for (size_t index = 0; index < pipeline->count; index++) {
        free_command(&pipeline->commands[index]);
    }
    free(pipeline->commands);
    free(pipeline->source);
    memset(pipeline, 0, sizeof(*pipeline));
}

static int add_command(Pipeline *pipeline) {
    if (pipeline->count == pipeline->capacity) {
        size_t new_capacity = pipeline->capacity == 0 ? 4 :
                              pipeline->capacity * 2;
        Command *resized = realloc(pipeline->commands,
                                   new_capacity * sizeof(*resized));
        if (resized == NULL) {
            return -1;
        }
        pipeline->commands = resized;
        pipeline->capacity = new_capacity;
    }

    memset(&pipeline->commands[pipeline->count], 0, sizeof(Command));
    pipeline->count++;
    return 0;
}

static int add_argument(Command *command, char *argument) {
    if (command->argc + 1 >= command->capacity) {
        size_t new_capacity = command->capacity == 0 ? 8 :
                              command->capacity * 2;
        char **resized = realloc(command->argv,
                                 new_capacity * sizeof(*resized));
        if (resized == NULL) {
            return -1;
        }
        command->argv = resized;
        command->capacity = new_capacity;
    }

    command->argv[command->argc++] = argument;
    command->argv[command->argc] = NULL;
    return 0;
}

static int set_error(char **error_message, const char *message) {
    *error_message = duplicate_string(message);
    return -1;
}

int parse_pipeline(const char *line, Pipeline *pipeline, char **error_message) {
    memset(pipeline, 0, sizeof(*pipeline));
    *error_message = NULL;

    if (line == NULL) {
        return set_error(error_message, "input line is null");
    }

    pipeline->source = duplicate_string(line);
    if (pipeline->source == NULL || add_command(pipeline) != 0) {
        free_pipeline(pipeline);
        return set_error(error_message, "out of memory");
    }

    Lexer lexer = {.cursor = line};
    Command *current = &pipeline->commands[0];
    bool saw_token = false;

    for (;;) {
        Token token = next_token(&lexer);
        if (token.type == TOKEN_ERROR) {
            const char *message = token.text != NULL ? token.text :
                                  "out of memory";
            int result = set_error(error_message, message);
            free(token.text);
            free_pipeline(pipeline);
            return result;
        }
        if (token.type == TOKEN_END) {
            break;
        }
        saw_token = true;
        if (token.type == TOKEN_WORD) {
            if (add_argument(current, token.text) != 0) {
                free(token.text);
                free_pipeline(pipeline);
                return set_error(error_message, "out of memory");
            }
            continue;
        }
        if (token.type == TOKEN_PIPE) {
            if (current->argc == 0) {
                free_pipeline(pipeline);
                return set_error(error_message,
                                 "expected a command before '|'");
            }
            if (add_command(pipeline) != 0) {
                free_pipeline(pipeline);
                return set_error(error_message, "out of memory");
            }
            current = &pipeline->commands[pipeline->count - 1];
            continue;
        }
        if (token.type == TOKEN_BACKGROUND) {
            if (current->argc == 0) {
                free_pipeline(pipeline);
                return set_error(error_message,
                                 "expected a command before '&'");
            }
            Token following = next_token(&lexer);
            if (following.type != TOKEN_END) {
                free(following.text);
                free_pipeline(pipeline);
                return set_error(error_message,
                                 "'&' is only valid at the end of a command");
            }
            pipeline->background = true;
            break;
        }

        Token path = next_token(&lexer);
        if (path.type != TOKEN_WORD) {
            free(path.text);
            free_pipeline(pipeline);
            return set_error(error_message,
                             "redirection requires a file path");
        }

        if (token.type == TOKEN_INPUT) {
            if (current->input_path != NULL) {
                free(path.text);
                free_pipeline(pipeline);
                return set_error(error_message,
                                 "duplicate input redirection");
            }
            current->input_path = path.text;
        } else {
            if (current->output_path != NULL) {
                free(path.text);
                free_pipeline(pipeline);
                return set_error(error_message,
                                 "duplicate output redirection");
            }
            current->output_path = path.text;
            current->append_output = token.type == TOKEN_APPEND;
        }
    }

    if (pipeline->count == 1 && pipeline->commands[0].argc == 0) {
        free_pipeline(pipeline);
        if (saw_token) {
            return set_error(error_message, "expected a command");
        }
        return 1;
    }
    if (current->argc == 0) {
        free_pipeline(pipeline);
        return set_error(error_message, "expected a command after '|'");
    }

    return 0;
}
