CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Wno-unused-parameter -MMD -MP -O2 -g -Iinclude
BIN_DIR = bin
BUILD_DIR = build

SRCS = src/arena.c \
       src/diag.c \
       src/str.c \
       src/lexer.c \
       src/ast.c \
       src/parser.c \
       src/sema.c \
       src/ir.c \
       src/opt.c \
       src/regalloc.c \
       src/codegen_x86.c \
       src/driver.c \
       src/main.c

PREFIX ?= $(HOME)/.local
INSTALL_BIN ?= $(PREFIX)/bin

OBJS = $(SRCS:src/%.c=$(BUILD_DIR)/%.o)
TARGET = $(BIN_DIR)/winds

.PHONY: all clean test benchmark install uninstall

all: $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

-include $(OBJS:.o=.d)

$(BIN_DIR) $(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) tests/*.out tests/*.s tests/*.o

test: $(TARGET)
	@echo "Running winds compiler test suite..."
	@$(TARGET) tests/01_basics.cpp -o tests/01_basics.out
	@tests/01_basics.out && echo "  [PASS] 01_basics"
	@$(TARGET) tests/02_functions.cpp -o tests/02_functions.out
	@tests/02_functions.out && echo "  [PASS] 02_functions"
	@$(TARGET) tests/03_classes.cpp -o tests/03_classes.out
	@tests/03_classes.out && echo "  [PASS] 03_classes"
	@$(TARGET) tests/04_ctor_dtor.cpp -o tests/04_ctor_dtor.out
	@tests/04_ctor_dtor.out && echo "  [PASS] 04_ctor_dtor"
	@$(TARGET) tests/05_namespace.cpp -o tests/05_namespace.out
	@tests/05_namespace.out && echo "  [PASS] 05_namespace"
	@$(TARGET) tests/06_headers.cpp -o tests/06_headers.out
	@tests/06_headers.out && echo "  [PASS] 06_headers"
	@$(TARGET) tests/07_optimizations.cpp -o tests/07_optimizations.out
	@tests/07_optimizations.out && echo "  [PASS] 07_optimizations"
	@tests/08_diagnostics.sh && echo "  [PASS] 08_diagnostics"
	@echo "All tests passed successfully!"

benchmark: $(TARGET)
	@python3 tests/benchmark.py $(TARGET)

install: $(TARGET)
	mkdir -p $(INSTALL_BIN)
	cp $(TARGET) $(INSTALL_BIN)/winds
	chmod 755 $(INSTALL_BIN)/winds
	@echo "winds successfully installed to $(INSTALL_BIN)/winds"

uninstall:
	rm -f $(INSTALL_BIN)/winds
	@echo "winds uninstalled from $(INSTALL_BIN)/winds"
