# winds — Lightweight, High-Performance C++ Compiler

`winds` is an ultra-fast, lightweight C++ compiler targeting **x86_64 Linux**. Built in strict C11 with zero external LLVM dependencies, `winds` is designed to provide high-quality compilation at a fraction of the binary size and compile latency of Clang/GCC.

---

## 🚀 Key Highlights & Benchmarks

| Metric | `clang++` (LLVM 22.1) | `winds` | Advantage |
|---|:---:|:---:|:---:|
| **Frontend + Codegen (`-S`)** | `12.02 ms` | **`0.32 ms`** | **~38x – 40x Faster** |
| **Compiler Footprint** | `~196.0 MB` | **`325 KB`** | **~600x Lighter** |
| **Dependencies** | Massive LLVM libraries (`libLLVM`, `libclang-cpp`) | **Zero dependencies** (glibc only) | **100% Self-contained** |
| **Memory Management** | Heavy reference counting & heap allocations | **High-speed Bump Arena** | **Instant O(1) Tear-down** |

*(Benchmarks measured across 20 iterations on x86_64 Linux compiling `tests/03_classes.cpp`)*

---

## 🛠️ Supported C++ Language Features

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
7. **Optimizer (`src/opt.c`)**: Peephole constant folder and dead jump removal.
8. **x86_64 Codegen (`src/codegen_x86.c`)**: System V AMD64 ABI compliant register assignment (`%rdi`, `%rsi`, `%rdx`, `%rcx`, `%r8`, `%r9`), 16-byte aligned stack frame generation, and sized memory accesses (`movb`, `movl`, `movq`).
9. **Driver (`src/driver.c`)**: Clean command-line interface orchestrating the toolchain pipeline.

---

## 📦 Building and Testing

### Prerequisites
- GCC or Clang (to compile `winds` itself)
- Standard GNU Assembler (`as`) and Linker (`gcc` or `ld`)

### Build `winds`
```bash
make
```
The executable will be placed in `bin/winds`.

### Run Test Suite
```bash
make test
```
Runs the automated test suites verifying:
- `01_basics.cpp` — Variables, loops, precedence, conditionals.
- `02_functions.cpp` — Function overloading, pass-by-reference swap, recursion.
- `03_classes.cpp` — Classes, access control, methods, implicit `this`.
- `04_ctor_dtor.cpp` — Constructors, destructors, `new`, `delete`.
- `05_namespace.cpp` — Namespaces, scope resolution (`::`), `using namespace`.

### Run Benchmark
```bash
make benchmark
```
Directly compares compilation throughput and binary footprint against system `clang++`.

---

## 💻 CLI Usage

```bash
winds [options] <input.cpp>

Options:
  -o <file>      Place output into <file>
  -S             Emit assembly source code only (.s)
  -c             Compile to object file (.o)
  -O0, -O1, -O2  Optimization level (default: -O1)
  --emit-ast     Dump Abstract Syntax Tree to stdout
  --emit-ir      Dump Three-Address Intermediate Representation
  -v             Verbose output (shows subprocess commands)
  --help         Display available options
  --version      Display compiler version
```

### Examples
Compile directly to binary:
```bash
bin/winds main.cpp -o app
./app
```

Emit intermediate representation:
```bash
bin/winds --emit-ir main.cpp
```

Emit x86_64 assembly:
```bash
bin/winds -S main.cpp -o main.s
```

---

## 📄 License
MIT License. Developed with precision, performance, and simplicity.
