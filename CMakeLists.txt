cmake_minimum_required(VERSION 3.16)
project(winds VERSION 0.2.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter -O2 -g)
endif()

set(WINDS_SOURCES
    src/arena.c
    src/diag.c
    src/str.c
    src/lexer.c
    src/ast.c
    src/parser.c
    src/sema.c
    src/ir.c
    src/opt.c
    src/regalloc.c
    src/codegen_x86.c
    src/driver.c
    src/main.c
)

add_executable(winds ${WINDS_SOURCES})

target_include_directories(winds PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

include(GNUInstallDirs)
install(TARGETS winds
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/winds/std
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/winds
)

include(CTest)
if(BUILD_TESTING)
    find_program(BASH_EXECUTABLE bash REQUIRED)

    set(TEST_CASES
        01_basics
        02_functions
        03_classes
        04_ctor_dtor
        05_namespace
        06_headers
        07_optimizations
        09_abi
        12_operator_overload
        13_typedef
        14_templates
        15_std_library
        16_function_pointers
        17_pointers_to_members
        18_macros
        19_variadic_templates
    )

    foreach(TC ${TEST_CASES})
        add_test(
            NAME test_${TC}
            COMMAND ${BASH_EXECUTABLE} -c "$<TARGET_FILE:winds> tests/${TC}.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/${TC}.out && ${CMAKE_CURRENT_BINARY_DIR}/${TC}.out"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
    endforeach()

    add_test(
        NAME test_08_diagnostics
        COMMAND ${BASH_EXECUTABLE} -c "WINDS=$<TARGET_FILE:winds> bash tests/08_diagnostics.sh"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_test(
        NAME test_10_dependencies
        COMMAND ${BASH_EXECUTABLE} -c "WINDS=$<TARGET_FILE:winds> bash tests/10_dependencies.sh"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_test(
        NAME test_11_warnings_and_run
        COMMAND ${BASH_EXECUTABLE} -c "WINDS=$<TARGET_FILE:winds> bash tests/11_warnings_and_run.sh"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
