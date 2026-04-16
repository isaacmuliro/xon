CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c99

SRC_DIR := src
INC_DIR := include
TEST_DIR := tests

TARGET := xon
LIB_DYLIB := libxon.dylib
LIB_SO := libxon.so
TEST_BIN := /tmp/xon_test_suite

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
LIB_TARGET := $(LIB_DYLIB)
LIB_FLAGS := -dynamiclib
else
LIB_TARGET := $(LIB_SO)
LIB_FLAGS := -shared -fPIC
endif

.PHONY: all parser cli lib example test clean

all: parser cli lib example

parser:
	./tools/lemon $(SRC_DIR)/xon.lemon

cli: parser
	$(CC) $(CFLAGS) -I$(INC_DIR) -o $(TARGET) \
		$(SRC_DIR)/main.c $(SRC_DIR)/xon_api.c $(SRC_DIR)/lexer.c $(SRC_DIR)/logger.c

lib: parser
	$(CC) $(LIB_FLAGS) $(CFLAGS) -I$(INC_DIR) -o $(LIB_TARGET) \
		$(SRC_DIR)/xon_api.c $(SRC_DIR)/lexer.c $(SRC_DIR)/logger.c

example: lib
	$(CC) $(CFLAGS) -I$(INC_DIR) -o example_lib examples/use_library.c -L. -lxon

test: cli lib
	$(CC) $(CFLAGS) -I$(INC_DIR) -o $(TEST_BIN) \
		$(TEST_DIR)/test_suite.c $(SRC_DIR)/xon_api.c $(SRC_DIR)/lexer.c $(SRC_DIR)/logger.c
	$(TEST_BIN)
	python3 $(TEST_DIR)/test_python.py

clean:
	rm -f $(TARGET) $(LIB_DYLIB) $(LIB_SO) example_lib $(TEST_BIN)
