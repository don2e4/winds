#include "opt.h"

/* Constant propagation entry */
typedef struct {
    bool is_const;
    int64_t val;
} ConstValue;

static void opt_dead_code_elimination(IRFunction *fn) {
    IRInst *inst = fn->first_inst;

    while (inst) {
        if (inst->op == IR_RET || inst->op == IR_JMP) {
            /* Remove all instructions until the next label */
            IRInst *dead = inst->next;
            while (dead && dead->op != IR_LABEL) {
                IRInst *next_dead = dead->next;
                /* Unlink dead instruction */
                if (dead->prev) dead->prev->next = dead->next;
                if (dead->next) dead->next->prev = dead->prev;
                if (dead == fn->last_inst) fn->last_inst = dead->prev;
                dead = next_dead;
            }
        }
        inst = inst->next;
    }
}

static void opt_jump_threading(IRFunction *fn) {
    IRInst *inst = fn->first_inst;

    while (inst) {
        /* If unconditional jump is immediately followed by the target label, remove the jump */
        if (inst->op == IR_JMP && inst->next && inst->next->op == IR_LABEL) {
            if (inst->dest.label && inst->next->dest.label &&
                strcmp(inst->dest.label, inst->next->dest.label) == 0) {
                IRInst *to_remove = inst;
                inst = inst->next;
                if (to_remove->prev) to_remove->prev->next = to_remove->next;
                if (to_remove->next) to_remove->next->prev = to_remove->prev;
                if (to_remove == fn->first_inst) fn->first_inst = to_remove->next;
                if (to_remove == fn->last_inst) fn->last_inst = to_remove->prev;
                continue;
            }
        }
        inst = inst->next;
    }
}

static void opt_constant_folding(IRFunction *fn, Arena *arena) {
    int max_vreg = fn->vreg_count + 16;
    ConstValue *consts = arena_alloc_zero(arena, sizeof(ConstValue) * max_vreg);

    for (IRInst *inst = fn->first_inst; inst != NULL; inst = inst->next) {
        if (inst->op == IR_LABEL) {
            /* Invalidate constants across labels to be safe */
            memset(consts, 0, sizeof(ConstValue) * max_vreg);
            continue;
        }

        if (inst->op == IR_IMM) {
            if (inst->dest.vreg >= 0 && inst->dest.vreg < max_vreg) {
                consts[inst->dest.vreg].is_const = true;
                consts[inst->dest.vreg].val = inst->src1.imm;
            }
            continue;
        }

        /* Check binary operations on constants */
        int r1 = inst->src1.vreg;
        int r2 = inst->src2.vreg;

        if (r1 >= 0 && r1 < max_vreg && r2 >= 0 && r2 < max_vreg &&
            consts[r1].is_const && consts[r2].is_const) {
            int64_t v1 = consts[r1].val;
            int64_t v2 = consts[r2].val;
            int64_t res = 0;
            bool folded = true;

            switch (inst->op) {
                case IR_ADD:    res = v1 + v2; break;
                case IR_SUB:    res = v1 - v2; break;
                case IR_MUL:    res = v1 * v2; break;
                case IR_DIV:    if (v2 != 0) res = v1 / v2; else folded = false; break;
                case IR_MOD:    if (v2 != 0) res = v1 % v2; else folded = false; break;
                case IR_AND:    res = v1 & v2; break;
                case IR_OR:     res = v1 | v2; break;
                case IR_XOR:    res = v1 ^ v2; break;
                case IR_SHL:    res = v1 << v2; break;
                case IR_SHR:    res = v1 >> v2; break;
                case IR_CMP_EQ: res = (v1 == v2) ? 1 : 0; break;
                case IR_CMP_NE: res = (v1 != v2) ? 1 : 0; break;
                case IR_CMP_LT: res = (v1 < v2) ? 1 : 0; break;
                case IR_CMP_LE: res = (v1 <= v2) ? 1 : 0; break;
                case IR_CMP_GT: res = (v1 > v2) ? 1 : 0; break;
                case IR_CMP_GE: res = (v1 >= v2) ? 1 : 0; break;
                default: folded = false; break;
            }

            if (folded) {
                inst->op = IR_IMM;
                inst->src1.imm = res;
                inst->src2.vreg = 0;
                if (inst->dest.vreg >= 0 && inst->dest.vreg < max_vreg) {
                    consts[inst->dest.vreg].is_const = true;
                    consts[inst->dest.vreg].val = res;
                }
            }
        }
    }
}

void opt_run_pipeline(IRModule *mod, OptOptions options) {
    if (options.level <= 0) return;

    for (IRFunction *fn = mod->functions; fn != NULL; fn = fn->next) {
        if (options.enable_const_fold || options.level >= 1) {
            opt_constant_folding(fn, mod->arena);
        }
        if (options.enable_dce || options.level >= 1) {
            opt_dead_code_elimination(fn);
            opt_jump_threading(fn);
        }
    }
}
