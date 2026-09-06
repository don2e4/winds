<p align="center">
  <img src="assets/logo_text.jpg" alt="winds" width="260" />
</p>

<p align="center">
  <strong>an ultra-fast, lightweight c++ compiler for x86_64 linux</strong>
  <br />
  <em>pure c11 implementation &bull; zero llvm bloat &bull; sub-millisecond compilation</em>
</p>

<p align="center">
  <a href="#license"><img src="https://img.shields.io/badge/license-mit-7aa2f7.svg" alt="license" /></a>
  <a href="#benchmarks"><img src="https://img.shields.io/badge/compile_time-0.53_ms-9ece6a.svg" alt="speed" /></a>
  <a href="#why-winds-is-small-and-fast"><img src="https://img.shields.io/badge/binary_size-255_kb-e0af68.svg" alt="size" /></a>
  <a href="#building-and-testing"><img src="https://img.shields.io/badge/tests-21_passing-9ece6a.svg" alt="tests" /></a>
</p>

---

winds is a small, focused c++ compiler built from the ground up in strict c11. it targets x86_64 linux with direct native code emission, skipping multi-gigabyte backends to turn c++ into assembly in a fraction of a millisecond.

designed for rapid iteration, modular systems programming, and instant feedback, winds gives you real object-oriented c++ without the waiting time.

## quick start

get up and running in seconds:

```bash
# build winds with make
make -j4

# or build with cmake
mkdir -p build && cd build && cmake .. && cmake --build .

# run a c++ file directly like a script
./bin/winds -run tests/01_basics.cpp

# or compile to an executable binary
./bin/winds tests/03_classes.cpp -o app
./app
```

## benchmarks

benchmarks measured on x86_64 linux across 20 iterations:

### 1. core compilation (`tests/03_classes.cpp`)

| metric | clang++ (llvm 22.1) | winds | advantage |
|---|:---|:---|:---|
| **frontend + codegen (`-s`)** | `11.89 ms` &nbsp; ▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰ | `0.53 ms` &nbsp; ▰ | **~22.3x faster** |
| **compiler footprint** | `~196 mb` &nbsp; ▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰ | `255 kb` &nbsp; ▰ | **~788x lighter** |
| **runtime dependencies** | libllvm, libclang-cpp, libc++ | glibc only | **100% self-contained** |
| **memory teardown** | recursive reference counting | contiguous bump arena | **instant constant-time exit** |

winds end-to-end compilation, assembly, and linking: `14.49 ms` minimum, `15.85 ms` average.

### 2. standard library compilation (`tests/15_std_library.cpp`)

compiling `<iostream>`, `<string>`, `<vector>`, `<utility>`, `<algorithm>`, `<cstdint>`, `<cassert>`:

| compiler | compile time (`-s`) | advantage |
|:---|:---|:---|
| **clang++ (system libc++)** | `173.70 ms` &nbsp; ▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰ | baseline |
| **winds (self-contained std)** | `2.55 ms` &nbsp; ▰ | **~68.0x faster** |

## why winds is small and fast

modern compilers often feel sluggish because they carry decades of legacy intermediate representation transformations, heavy class hierarchies, and massive dynamic libraries. winds takes a simpler, more deliberate path:

- **bump arena allocation**: all abstract syntax tree nodes, symbol entries, and intermediate instructions live in contiguous 128 kb blocks. memory allocations take a single pointer bump with zero lock contention, and process cleanup takes one single free call.
- **linear scan register allocator**: instead of costly graph coloring algorithms with complex spill-reloading passes, winds runs a fast linear scan over live intervals with loop extension and automatic callee-saved register tracking.
- **zero llvm dependencies**: no massive shared libraries to load into memory on each invocation. the entire compiler binary is only 255 kb.
- **direct native code emission**: the intermediate representation lowers straight to system v amd64 assembly without intermediate serialization steps.

## compilation pipeline

winds transforms c++ into native machine code through a streamlined multi-stage pipeline:

```
     source code (.cpp)
             │
             ▼
   lexer & string intern       fast tokenization, fnv-1a identifier pool
             │
             ▼
  recursive descent parser     operator-precedence expression grammar
             │
             ▼
    abstract syntax tree       compact node structures in arena memory
             │
             ▼
     semantic analyzer         type checking, namespaces, method resolution
             │
             ▼
   three-address code ir       linearized instruction stream
             │
             ▼
      optimizer passes         constant folding, copy prop, cfg simplification
             │
             ▼
   linear scan regalloc        physical register mapping & loop extension
             │
             ▼
      native codegen           system v amd64 assembly emission (.s)
             │
             ▼
    assembler & linker         gnu as and gcc/ld executable linking (.out)
```

## real code in winds

winds supports core object-oriented c++ features out of the box:

### classes and destructors

```cpp
#include <stdio.h>

class counter {
private:
    int count;
public:
    counter(int start) {
        this->count = start;
    }
    ~counter() {
        printf("counter finished at %d
", this->count);
    }
    void step(int delta) {
        this->count = this->count + delta;
    }
    int value() {
        return this->count;
    }
};

int main() {
    counter c(10);
    c.step(5);
    printf("current: %d
", c.value());
    return 0;
}
```

### executable scripts with shebang

make your c++ code runnable without a separate build step:

```cpp
#!/usr/bin/env winds -run
#include <stdio.h>

int main(int argc, char **argv) {
    printf("executed directly via winds script mode!
");
    for (int i = 1; i < argc; i = i + 1) {
        printf("argument %d: %s
", i, argv[i]);
    }
    return 0;
}
```

### references and pointers

```cpp
void swap(int &first, int &second) {
    int temp = first;
    first = second;
    second = temp;
}
```

## interactive feature tour

explore the underlying compiler components by expanding the sections below:

<details>
<summary><strong>templates & bespoke standard library</strong></summary>

- on-demand template class monomorphization (`template <typename t> class name { ... };`)
- variadic templates and parameter pack expansion (`template <typename... args>`)
- recursive template monomorphization and pack unrolling
- standard `<tuple>` (`std::tuple`, `std::make_tuple`) with contiguous stack layout
- self-contained `<iostream>` with stream chaining (`std::cout << val << std::endl;`)
- heap-managed `<string>` with dynamic resizing, concatenation, and streaming
- dynamic `<vector<t>>` container with `push_back`, `pop_back`, and array indexing
- generic `<utility>` (`std::pair`, `std::make_pair`, `std::swap`) and `<algorithm>` (`std::min`, `std::max`, `std::sort`)
- zero external compiler runtime dependencies

</details>

<details>
<summary><strong>function pointers & pointers to members</strong></summary>

- first-class function pointer declarations (`ret (*fp)(args)`) and indirect calls
- function pointer arrays, lookup tables, and callback dispatch architectures
- pointers to class data members (`type class::*ptr`) and member dereference operators (`.*`, `->*`)
- pointers to member functions (`ret (class::*mfp)(args)`) and indirect method invocation

</details>

<details>
<summary><strong>preprocessor & macro expansion</strong></summary>

- parameterized macros (`#define fn(x, y) ...`)
- macro argument stringification (`#param`)
- token pasting and concatenation (`##`)
- multiline macro continuation via backslash (`\`)
- macro undefinition via `#undef`
- preprocessor conditional branches (`#if`, `#elif`, `#else`, `#endif`, `#ifdef`, `#ifndef`)

</details>

<details>
<summary><strong>object-oriented programming & memory lifecycle</strong></summary>

- `class` and `struct` declarations with member offset alignments
- access modifiers: `public`, `private`, `protected`
- stack constructors and automatic destructors on scope exit
- dynamic heap allocation with `new` and `delete`
- member methods with implicit and explicit `this` pointer resolution
- out-of-line method definitions (`classname::method(...)`)
- pass-by-reference (`type&`) and pass-by-pointer (`type*`)
- parameter-count and type-aware function and method overloading

</details>

<details>
<summary><strong>system v amd64 calling convention compliance</strong></summary>

- full support for functions with 7 or more arguments: the first six arguments travel in registers (`%rdi`, `%rsi`, `%rdx`, `%rcx`, `%r8`, `%r9`), while arguments 7 and beyond are passed on the stack frame (`16(%rbp)`, `24(%rbp)`, etc.)
- 16-byte caller stack alignment (`subq` / `addq`) for crash-free compatibility with glibc vector and variadic functions
- callee-saved register preservation (`%rbx`, `%r12` through `%r15`) across deep recursion and call sites

</details>

<details>
<summary><strong>optimizer passes (-o1, -o2)</strong></summary>

- **control-flow graph simplification**: branch inversion over jumps, identical branch target folding, consecutive label deduplication, and dead block elimination
- **constant propagation & folding**: evaluates compile-time arithmetic and prunes conditional branches
- **copy propagation**: removes redundant register assignments and assignment chains
- **algebraic simplification**: applies mathematical identities (`x + 0`, `x * 0`, `x * 1`, `x - x`, `x ^ x`, `x & 0`, `x == x`)
- **unreachable block removal**: computes basic block reachability and discards code after returns or unconditional branches

</details>

<details>
<summary><strong>developer diagnostics & error reporting</strong></summary>

- visual diagnostics with line gutters (` | `) and precise column underline markers (`^^^^^`)
- actionable help notes suggesting missing semicolons, delimiters, or include flags
- levenshtein distance suggestions for typos in variable names and class members (e.g. suggesting `counter` when you type `countr`)
- warning controls: `-wall`, `-wextra` for unused variable detection, and `-werror` to treat warnings as hard build errors
- colored output support via `-fdiagnostics-color=always|never|auto`

</details>

<details>
<summary><strong>build system integration & scripting</strong></summary>

- direct script execution via `-run` with argument forwarding and exit code propagation
- automatic makefile dependency generation via `-mmd`, `-mp`, and `-mf <file>`
- header include path management with `-i <dir>`
- single-inclusion protection with `#pragma once` and `#ifndef` guards
- cross-compilation toolchain dispatching via `--target=<triple>`, `--sysroot=<path>`, and `--cross-prefix=<prefix>`

</details>

<details>
<summary><strong>command-line reference</strong></summary>

| flag | description |
|---|---|
| `-o <file>` | write output binary or object file to `<file>` |
| `-s` | compile only to assembly (`.s`) |
| `-c` | compile and assemble without linking (`.o`) |
| `-run` | compile and execute directly as a script |
| `-mmd` | generate makefile dependency file (`.d`) |
| `-mp` | add phony targets for header dependencies |
| `-mf <file>` | write dependency output to specified `<file>` |
| `-wall`, `-wextra` | enable compiler warnings (such as unused variables) |
| `-werror` | treat warnings as hard compilation errors |
| `-o0`, `-o1`, `-o2` | set optimization level (default: `-o1`) |
| `-i <dir>` | add directory to header search path |
| `-Dname[=value]`, `-Uname` | define or undefine a preprocessor macro |
| `-L<dir>`, `-l<name>`, `-Wl,...` | pass library search and linker options |
| `--target=<triple>` | specify target architecture (default: `x86_64-linux-gnu`) |
| `--sysroot=<path>` | specify system root directory for headers and libraries |
| `--cross-prefix=<p>` | specify cross-toolchain binary prefix |
| `--print-target-triple` | display target triple and exit |
| `--emit-ast` | dump abstract syntax tree to stdout |
| `--emit-ir` | dump three-address intermediate code to stdout |
| `-v`, `--verbose` | print stage execution details and timing |
| `--help` | print help summary |
| `--version` | print compiler version |

</details>

## building and testing

### prerequisites

- gcc or clang (to build the compiler binary itself)
- standard gnu assembler (`as`) and linker (`gcc` or `ld`)
- cmake 3.16+ (optional, standard make is supported)

### building with make

```bash
# build binary to bin/winds
make -j4

# run automated test suites
make test

# run benchmark against clang++
make benchmark

# install binary to ~/.local/bin
make install
```

### building with cmake

```bash
# configure and build
mkdir -p build && cd build && cmake .. && cmake --build .

# execute all test cases via ctest
ctest --test-dir build --output-on-failure
```

### test suites

winds includes 21 automated test suites covering syntax, semantics, standard library, and code generation:

- `01_basics.cpp` &mdash; variables, arithmetic precedence, loops, and branches
- `02_functions.cpp` &mdash; function overloading, pass-by-reference, and recursion
- `03_classes.cpp` &mdash; classes, access modifiers, member methods, and `this`
- `04_ctor_dtor.cpp` &mdash; constructors, destructors, `new`, and `delete`
- `05_namespace.cpp` &mdash; namespaces, nested scopes, and `using namespace`
- `06_headers.cpp` &mdash; `#include`, `#pragma once`, and guard macros
- `07_optimizations.cpp` &mdash; constant folding, copy propagation, algebraic simplification, and control-flow jumps
- `08_diagnostics.sh` &mdash; caret underline diagnostics and typo suggestions
- `09_abi.cpp` &mdash; 7+ argument passing, callee-saved preservation, and 16-byte stack alignment
- `10_dependencies.sh` &mdash; `-mmd`, `-mp`, and `-mf` dependency rules
- `11_warnings_and_run.sh` &mdash; `-wall` warnings, `-werror` escalation, and `-run` execution
- `12_operator_overload.cpp` &mdash; member and stream operator overloading (`<<`, `>>`, `[]`, `+`, `==`)
- `13_typedef.cpp` &mdash; type aliases via `typedef` and `using`
- `14_templates.cpp` &mdash; template class monomorphization (`box<t>`, `pair<t1, t2>`)
- `15_std_library.cpp` &mdash; self-contained standard library containers and streams
- `16_function_pointers.cpp` &mdash; function pointers, indirect calls, and callback dispatch tables
- `17_pointers_to_members.cpp` &mdash; pointers to data members and member functions (`.*`, `->*`)
- `18_macros.cpp` &mdash; parameterized macros, stringification (`#`), token pasting (`##`), `#undef`, `#if`/`#elif`
- `19_variadic_templates.cpp` &mdash; variadic templates, parameter packs, recursive monomorphization, and standard `<tuple>`
- `20_multifile.sh` &mdash; multiple translation units, command-line macros, external objects, and C ABI linking
- `21_c_compat.c` &mdash; C linkage, `do` loops, conditional/comma expressions, and curated C headers

`make corpus-test` checks an unchanged, pinned zlib 1.3.1 translation unit by compiling it with winds, linking it to a gcc-built C harness, and validating its checksum output. Set `ZLIB_SOURCE` to an existing checkout for an offline run.

## roadmap

- [x] **phase 1**: script execution mode (`-run`), shebang support, makefile dependency tracking (`-mmd`, `-mp`, `-mf`), warning controls (`-wall`, `-wextra`, `-werror`), colored diagnostics
- [x] **phase 2**: operator overloading, type aliases (`typedef`, `using`), template class monomorphization, 12 self-contained standard library headers (`<iostream>`, `<string>`, `<vector>`, etc.)
- [x] **phase 3**: function pointers, pointers to members, parameterized preprocessor macros, variadic templates and standard tuple
- [x] **phase 4a**: multi-file builds, external object/library linking, C linkage, curated libc headers, and the first pinned real-world zlib corpus gate
- [ ] **phase 4b**: complete the corpus-driven C foundation required by full zlib, cJSON, and SQLite builds
- [ ] **phase 4c**: compile pugixml without exceptions, RTTI, or the system C++ standard library

## license

distributed under the mit license.
