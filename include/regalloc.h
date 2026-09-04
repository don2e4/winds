#ifndef WINDS_REGALLOC_H
#define WINDS_REGALLOC_H

#include "winds.h"
#include "ir.h"
#include "arena.h"

/* Physical registers available for allocation */
typedef enum {
    PHYS_REG_NONE = -1,
    PHYS_REG_RBX = 0,  /* Callee-saved */
    PHYS_REG_R12 = 1,  /* Callee-saved */
    PHYS_REG_R13 = 2,  /* Callee-saved */
    PHYS_REG_R14 = 3,  /* Callee-saved */
    PHYS_REG_R15 = 4,  /* Callee-saved */
    PHYS_REG_R10 = 5,  /* Caller-saved scratch */
    PHYS_REG_R11 = 6,  /* Caller-saved scratch */
    PHYS_REG_COUNT = 7
} PhysReg;

/* Live interval for a virtual register */
typedef struct {
    int vreg;
    int start_inst;
    int end_inst;
    bool crosses_call;
    int assigned_reg;  /* PHYS_REG_* or PHYS_REG_NONE */
    int spill_offset;  /* Offset from %rbp if spilled */
} LiveInterval;

/* Register allocation result for a function */
typedef struct {
    int vreg_count;
    int *vreg_to_reg;       /* vreg -> PhysReg (-1 if spilled or unused) */
    int *vreg_to_spill;     /* vreg -> stack offset (if spilled) */
    bool used_regs[PHYS_REG_COUNT]; /* Which physical registers were allocated */
    int num_spills;         /* Number of spilled vregs */
    int spill_space;        /* Total stack space reserved for spills */
    int callee_save_space;  /* Total stack space reserved for saved callee regs */
    int callee_save_offsets[PHYS_REG_COUNT]; /* Stack offsets for saved callee regs */
} RegAlloc;

/* Run linear scan register allocation on a function */
RegAlloc *regalloc_run(IRFunction *fn, Arena *arena, int local_stack_base);

/* Register name getters */
const char *regalloc_reg_name_64(PhysReg reg);
const char *regalloc_reg_name_32(PhysReg reg);
const char *regalloc_reg_name_8(PhysReg reg);
bool regalloc_is_callee_saved(PhysReg reg);

#endif /* WINDS_REGALLOC_H */
