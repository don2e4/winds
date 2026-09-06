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
	rm -rf $(BUILD_DIR) $(BIN_DIR) tests/*.out tests/*.s tests/*.o tests/*.d

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
	@$(TARGET) tests/09_abi.cpp -o tests/09_abi.out
	@tests/09_abi.out >/dev/null && echo "  [PASS] 09_abi"
	@tests/10_dependencies.sh && echo "  [PASS] 10_dependencies"
	@tests/11_warnings_and_run.sh && echo "  [PASS] 11_warnings_and_run"
	@$(TARGET) tests/12_operator_overload.cpp -o tests/12_operator_overload.out
	@tests/12_operator_overload.out >/dev/null && echo "  [PASS] 12_operator_overload"
	@$(TARGET) tests/13_typedef.cpp -o tests/13_typedef.out
	@tests/13_typedef.out >/dev/null && echo "  [PASS] 13_typedef"
	@$(TARGET) tests/14_templates.cpp -o tests/14_templates.out
	@tests/14_templates.out >/dev/null && echo "  [PASS] 14_templates"
	@$(TARGET) tests/15_std_library.cpp -o tests/15_std_library.out
	@tests/15_std_library.out >/dev/null && echo "  [PASS] 15_std_library"
	@$(TARGET) tests/16_function_pointers.cpp -o tests/16_function_pointers.out
	@tests/16_function_pointers.out >/dev/null && echo "  [PASS] 16_function_pointers"
	@$(TARGET) tests/17_pointers_to_members.cpp -o tests/17_pointers_to_members.out
	@tests/17_pointers_to_members.out >/dev/null && echo "  [PASS] 17_pointers_to_members"
	@$(TARGET) tests/18_macros.cpp -o tests/18_macros.out
	@tests/18_macros.out >/dev/null && echo "  [PASS] 18_macros"
	@$(TARGET) tests/19_variadic_templates.cpp -o tests/19_variadic_templates.out
	@tests/19_variadic_templates.out >/dev/null && echo "  [PASS] 19_variadic_templates"
	@echo "All tests passed successfully!"

benchmark: $(TARGET)
	@python3 tests/benchmark.py $(TARGET)

install: $(TARGET)
	mkdir -p $(INSTALL_BIN)
	cp $(TARGET) $(INSTALL_BIN)/winds
	chmod 755 $(INSTALL_BIN)/winds
	mkdir -p $(PREFIX)/include/winds/std
	cp -r include/winds/std/* $(PREFIX)/include/winds/std/
	@echo "winds successfully installed to $(INSTALL_BIN)/winds"

uninstall:
	rm -f $(INSTALL_BIN)/winds
	rm -rf $(PREFIX)/include/winds
	@echo "winds uninstalled from $(INSTALL_BIN)/winds"
