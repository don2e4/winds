#ifndef WINDS_OPT_H
#define WINDS_OPT_H

#include "ir.h"

/* Optimization options */
typedef struct {
    int level; /* 0: None, 1: Basic, 2: Aggressive */
    bool enable_dce;
    bool enable_const_fold;
} OptOptions;

/* Run optimization pipeline on IR module */
void opt_run_pipeline(IRModule *mod, OptOptions options);

#endif /* WINDS_OPT_H */
