#ifndef WINDS_OPT_H
#define WINDS_OPT_H

#include "ir.h"

/* Optimization options */
typedef struct {
    int level; /* 0: None, 1: Basic, 2: Aggressive */
    bool enable_dce;
    bool enable_const_fold;
    bool enable_const_prop;
    bool enable_copy_prop;
    bool enable_algebraic;
    bool enable_cfg_opt;
    bool enable_unreachable;
} OptOptions;

/* Individual optimization passes */
bool opt_constant_propagation(IRFunction *fn, Arena *arena);
bool opt_copy_propagation(IRFunction *fn, Arena *arena);
bool opt_algebraic_simplification(IRFunction *fn);
bool opt_cfg_optimization(IRFunction *fn);
bool opt_unreachable_block_removal(IRFunction *fn, Arena *arena);
bool opt_dead_code_elimination(IRFunction *fn, Arena *arena);

/* Run optimization pipeline on IR module */
void opt_run_pipeline(IRModule *mod, OptOptions options);

#endif /* WINDS_OPT_H */
