CC := gcc
CPPFLAGS := -Iinclude -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wformat=2
LDLIBS := -lreadline

TARGET := bin/novash
SOURCES := src/main.c src/parser.c src/executor.c src/builtins.c src/jobs.c src/signals.c
OBJECTS := $(SOURCES:src/%.c=build/%.o)
TEST_TARGET := bin/test_parser

.PHONY: all debug sanitize test clean integration

all: $(TARGET)

$(TARGET): $(OBJECTS) | bin
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJECTS) -o $@ $(LDLIBS)

build/%.o: src/%.c include/shell.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TEST_TARGET): tests/test_parser.c src/parser.c include/shell.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_parser.c src/parser.c -o $@

integration: $(TARGET)
	bash tests/integration.sh ./$(TARGET)

build bin:
	mkdir -p $@

debug:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CFLAGS) -O0 -g3" all

sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CFLAGS) -O1 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer" \
		LDFLAGS="-fsanitize=address,undefined" all

test: all $(TEST_TARGET)
	./$(TEST_TARGET)
	bash tests/integration.sh ./$(TARGET)

clean:
	rm -rf build bin
