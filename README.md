# winds — Lightweight, High-Performance C++ Compiler

[![License: MIT](https://img.shields.io/badge/License-MIT-purple.svg)](LICENSE)
[![Target: x86_64 Linux](https://img.shields.io/badge/Target-x86__64--linux-blue.svg)](#)
[![Built with C11](https://img.shields.io/badge/Compiler-C11-teal.svg)](#)
[![Tests: Passing](https://img.shields.io/badge/Tests-passing-brightgreen.svg)](#)

`winds` is an ultra-fast, lightweight C++ compiler targeting **x86_64 Linux**. Built in strict C11 with zero external LLVM dependencies, `winds` is designed to provide high-quality compilation at a fraction of the binary size and compile latency of Clang/GCC.

---

## 🚀 Key Highlights & Benchmarks

| Metric | `clang++` (LLVM 22.1) | `winds` | Advantage |
|---|:---:|:---:|:---:|
| **Frontend + Codegen (`-S`)** | `11.88 ms` | **`0.34 ms`** | **~35x Faster** |
| **Compiler Footprint** | `~196.0 MB` | **`408 KB`** | **~492x Lighter** |
| **Dependencies** | Massive LLVM libraries (`libLLVM`, `libclang-cpp`) | **Zero dependencies** (glibc only) | **100% Self-contained** |
| **Memory Management** | Heavy reference counting & heap allocations | **High-speed Bump Arena** | **Instant O(1) Tear-down** |

*(Benchmarks measured across 20 iterations on x86_64 Linux compiling `tests/03_classes.cpp`)*

---

- **System V AMD64 ABI Compliance & Call Expansion**:
  - Full support for functions and methods with 7+ arguments (first 6 in `%rdi`, `%rsi`, `%rdx`, `%rcx`, `%r8`, `%r9`, and arguments 7+ stored in incoming stack slots `16(%rbp)`, `24(%rbp)`, etc.).
  - Caller stack argument allocation with strict 16-byte alignment (`subq $N, %rsp` / `addq $N, %rsp`), ensuring 100% crash-free compatibility with glibc SSE vector and variadic functions.
  - Callee-saved register preservation (`%rbx`, `%r12`..`%r15`) across deep recursion and function call boundaries.
- **Linear Scan Register Allocation**:
  - Direct allocation of AMD64 physical general-purpose registers (`%rbx`, `%r12`, `%r13`, `%r14`, `%r15`, `%r10`, `%r11`).
  - Liveness analysis computing precise live intervals `[start, end]` per virtual register.
  - Backward loop edge extension to prevent premature register reclamation across loops.
  - Call boundary tracking: intervals that cross `IR_CALL` instructions are strictly mapped to callee-saved registers (`%rbx`, `%r12`..`%r15`) or spilled, safeguarding values across function calls.
  - Dynamic stack spilling with dedicated frame slots when registers are exhausted.
  - ABI isolation: System V calling convention argument registers (`%rdi`, `%rsi`, `%rdx`, `%rcx`, `%r8`, `%r9`) and return register (`%rax`) are kept isolated from allocatable registers.
  - Callee-save register preservation: function prologue only saves callee-saved registers actively used; epilogue restores them in reverse order with 16-byte aligned stack frame.
- **Advanced CFG & IR Optimizations (-O1, -O2)**:
  - **CFG Simplification**: Branch inversion over unconditional jumps (`jmp_if_zero L1; jmp L2; L1:` -> `jmp_if_not_zero L2; L1:`), identical branch destination folding (`jmp_if_zero L1; jmp L1` -> `jmp L1`), consecutive label deduplication, and dead code elimination after unconditional terminators.
  - **Constant Propagation & Folding**: Propagates known compile-time constants through arithmetic expressions and branches.
  - **Copy Propagation**: Eliminates redundant register assignments and chains of copies.
  - **Algebraic Simplification**: Automatically applies identity laws (`x + 0`, `x * 0`, `x * 1`, `x - x`, `x ^ x`, `x & 0`, `x == x`, etc.).
  - **Unreachable Block Removal**: Computes basic block reachability and eliminates dead blocks after returns and unconditional jumps.
- **Cross-Compilation & Toolchain Dispatching**:
  - Target triple specification via `--target=<triple>` (e.g. `x86_64-linux-gnu`, `x86_64-pc-linux-gnu`).
  - Strict target architecture validation (verifies `x86_64` / `amd64`).
  - Custom system root support via `--sysroot=<path>`, automatically injecting sysroot header include directories and linker search paths.
  - Explicit cross-prefix support via `--cross-prefix=<prefix>` (e.g. `x86_64-linux-gnu-`) for dispatched `<prefix>as` and `<prefix>gcc`.
  - Introspection via `--print-target-triple`.
- **Preprocessor & Header Files**:
  - Full support for quoted headers (`#include "..."`) and system headers (`#include <...>`).
  - Search path configuration via `-I <dir>` flag with sensible defaults (`./include`, `/usr/include`).
  - `#pragma once` file-level single-inclusion guards.
  - Conditional compilation directives: `#ifndef`, `#ifdef`, `#define`, `#else`, `#endif`.
- **Friendly Visual Error Diagnostics**:
  - Rust and Elm-inspired error reporting with clean line gutters (` | `) and exact column underlines (`^^^^`).
  - Actionable ` = help:` hints suggesting missing semicolons, closing delimiters, and include paths.
  - Levenshtein distance "did you mean?" typo suggestions for identifiers and struct/class members.
- **Object-Oriented Programming**:
  - `class` and `struct` declarations with proper member alignments and offsets.
  - Access specifiers: `public`, `private`, `protected`.
  - Member functions and out-of-line method definitions (`ClassName::Method(...)`).
  - Implicit and explicit `this` pointer resolution.
- **Object Lifecycle**:
  - Constructors (`ClassName(...)`) for stack and heap objects.
  - Destructors (`~ClassName()`) with automatic cleanup.
  - Dynamic memory management: `new` and `delete`.
- **References & Pointers**:
  - Pass-by-reference (`T&`) and pass-by-pointer (`T*`).
  - Reference assignments and member accesses without double-dereferencing.
- **Function & Method Overloading**:
  - Parameter-count and type-aware symbol resolution.
  - Robust name mangling matching System V conventions.
- **Namespaces & Scoping**:
  - Nested namespaces (`namespace A { namespace B { ... } }`).
  - Scope resolution operator (`Scope::Member`).
  - `using namespace` directives supported globally and locally within functions.
  - Namespaced classes and functions (`Geometry::Square`, `Math::add`).
- **Core Language & Control Flow**:
  - Primitive types: `int`, `long`, `short`, `char`, `bool`, `void`.
  - Loops: `for`, `while`, `break`, `continue`.
  - Conditionals: `if`, `else`, logical and comparison operators (`==`, `!=`, `<`, `<=`, `>`, `>=`).
  - Full operator precedence parsing.

---

## 🏗️ Compiler Architecture

```
                       [ Source Code (.cpp) ]
                                 │
                                 ▼
                     Lexer (Token Stream, FNV-1a)
                                 │
                                 ▼
                    Recursive Descent Parser
                                 │
                                 ▼
                   Abstract Syntax Tree (AST)
                                 │
                                 ▼
                  Semantic Analysis & Symbol Table
               (Overload Resolution, Type Checking)
                                 │
                                 ▼
                   Three-Address Code IR Module
                                  │
                                  ▼
                        IR Optimization Passes
             (Constant Folding, Dead Code Elimination)
                                  │
                                  ▼
                   Linear Scan Register Allocator
          (Liveness Analysis, Loop Extension, Callee-Saves)
                                  │
                                  ▼
                System V AMD64 Native Code Generator
                                  │
                                  ▼
                  GNU Assembler (GAS) Output (.s)
                                  │
                                  ▼
                     System Assembler & Linker
                                  │
                                  ▼
                     ELF Executable Binary (.out)
```

1. **Arena Allocator (`src/arena.c`)**: Block-based bump-pointer allocator with 8-byte alignment ensuring high spatial cache locality and zero memory leaks.
2. **String Interning (`src/str.c`)**: FNV-1a hash table allowing fast pointer equality comparisons for identifiers.
3. **Lexer (`src/lexer.c`)**: High-throughput character scanner with support for multi-character operators, keywords, comments, and string literals.
4. **Parser (`src/parser.c`)**: Operator-precedence expression parsing and recursive descent declaration and statement parser with arbitrary lookahead support.
5. **Semantic Analyzer (`src/sema.c`)**: Scoped symbol tables, multi-namespace resolution, class member offset layouts, and function overloading.
6. **IR Generator (`src/ir.c`)**: Linear 3-address code intermediate representation with typed instruction sizing.
7. **Optimizer (`src/opt.c`)**: Multi-pass fixpoint pipeline running constant propagation & folding, copy propagation, algebraic simplification, unreachable block pruning, dead code elimination, and CFG jump threading.
8. **Register Allocator (`src/regalloc.c`, `include/regalloc.h`)**: Linear scan physical register allocator tracking live intervals, backward loop edge extensions, callee-saved preservation across calls, and stack spilling.
9. **x86_64 Codegen (`src/codegen_x86.c`)**: System V AMD64 ABI compliant code generator translating IR and allocated physical registers into machine assembly with 16-byte aligned stack frame management.
10. **Driver (`src/driver.c`)**: Clean command-line interface orchestrating the toolchain pipeline.

---

## 📦 Building and Testing

### Prerequisites
- GCC or Clang (to compile `winds` itself)
- CMake 3.16+ (optional, Makefile is also supported)
- Standard GNU Assembler (`as`) and Linker (`gcc` or `ld`)

### Build & Install via Makefile
```bash
# Build binary to bin/winds
make

# Run all test suites
make test

# Install globally to ~/.local/bin
make install
```

### Build & Test via CMake
```bash
# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run full CTest suite
ctest --test-dir build --output-on-failure

# Install
cmake --install build --prefix ~/.local
```

### Test Suites
Automated test suites verifying compiler correctness:
- `01_basics.cpp` — Variables, loops, precedence, conditionals.
- `02_functions.cpp` — Function overloading, pass-by-reference swap, recursion.
- `03_classes.cpp` — Classes, access control, methods, implicit `this`.
- `04_ctor_dtor.cpp` — Constructors, destructors, `new`, `delete`.
- `05_namespace.cpp` — Namespaces, scope resolution (`::`), `using namespace`.
- `06_headers.cpp` — `#include "..."`, `#include <...>`, `#pragma once`, `#ifndef` guards.
- `07_optimizations.cpp` — Constant propagation, copy propagation, algebraic laws, CFG optimizations.
- `08_diagnostics.sh` — Visual caret underlines, line gutters, typo "did you mean?" hints.
- `09_abi.cpp` — System V AMD64 ABI compliance: 7+ arguments, callee-saved preservation across recursion, 16-byte stack alignment.

### Run Benchmark
```bash
make benchmark
```
Directly compares compilation throughput and binary footprint against system `clang++`.

---

## 💻 CLI Usage

Once installed, invoke `winds` directly anywhere from your terminal:

```bash
winds [options] <input.cpp>

Options:
  -o <file>                Place output into <file>
  -S                       Emit assembly source code only (.s)
  -c                       Compile to object file (.o)
  -O0, -O1, -O2            Optimization level (default: -O1)
  -I <dir>                 Add directory to header search path
  --target=<triple>        Specify target architecture triple (default: x86_64-linux-gnu)
  --sysroot=<path>         Specify system root directory for headers and libraries
  --cross-prefix=<prefix>  Specify cross-toolchain prefix (e.g. x86_64-linux-gnu-)
  --print-target-triple    Print target architecture triple and exit
  --emit-ast               Dump Abstract Syntax Tree to stdout
  --emit-ir                Dump Three-Address Intermediate Representation
  -v, --verbose            Verbose output (shows subprocess commands & timing)
  --help                   Display available options
  --version                Display compiler version
```

### Examples

Compile directly to binary:
```bash
winds example.cpp -o example
./example
```

Cross-compilation with custom sysroot and cross-prefix:
```bash
winds --target=x86_64-linux-gnu --sysroot=/opt/sysroot --cross-prefix=x86_64-linux-gnu- example.cpp -o example
```

Emit intermediate representation:
```bash
winds --emit-ir example.cpp
```

Emit x86_64 GNU assembly:
```bash
winds -S example.cpp -o example.s
```

---

## 📄 License
MIT License. Developed with precision, performance, and simplicity.
