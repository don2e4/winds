#include "opt.h"

/* Helper: Unlink instruction from function's doubly-linked list */
static void remove_inst(IRFunction *fn, IRInst *inst) {
    if (!fn || !inst) return;
    if (inst->prev) inst->prev->next = inst->next;
    if (inst->next) inst->next->prev = inst->prev;
    if (inst == fn->first_inst) fn->first_inst = inst->next;
    if (inst == fn->last_inst) fn->last_inst = inst->prev;
}

/* =========================================================================
 * 1. Constant Propagation & Constant Folding
 * ========================================================================= */

typedef struct {
    bool is_const;
    int64_t val;
} ConstVal;

typedef struct {
    int offset;
    int64_t val;
    bool is_const;
} StackConstEntry;

#define MAX_STACK_CONSTS 64

typedef struct {
    StackConstEntry entries[MAX_STACK_CONSTS];
    int count;
} StackConstTable;

static void stack_const_clear(StackConstTable *table) {
    table->count = 0;
}

static void stack_const_set(StackConstTable *table, int offset, int64_t val) {
    for (int i = 0; i < table->count; i++) {
        if (table->entries[i].offset == offset) {
            table->entries[i].val = val;
            table->entries[i].is_const = true;
            return;
        }
    }
    if (table->count < MAX_STACK_CONSTS) {
        table->entries[table->count].offset = offset;
        table->entries[table->count].val = val;
        table->entries[table->count].is_const = true;
        table->count++;
    }
}

static void stack_const_invalidate(StackConstTable *table, int offset) {
    for (int i = 0; i < table->count; i++) {
        if (table->entries[i].offset == offset) {
            table->entries[i].is_const = false;
            return;
        }
    }
}

static bool stack_const_get(StackConstTable *table, int offset, int64_t *out_val) {
    for (int i = 0; i < table->count; i++) {
        if (table->entries[i].offset == offset && table->entries[i].is_const) {
            *out_val = table->entries[i].val;
            return true;
        }
    }
    return false;
}

bool opt_constant_propagation(IRFunction *fn, Arena *arena) {
    if (!fn) return false;

    bool changed = false;
    int max_vreg = fn->vreg_count + 32;
    ConstVal *consts = arena_alloc_zero(arena, sizeof(ConstVal) * max_vreg);
    StackConstTable stack_table;
    stack_const_clear(&stack_table);

    int *def_count = arena_alloc_zero(arena, sizeof(int) * max_vreg);
    for (IRInst *inst = fn->first_inst; inst != NULL; inst = inst->next) {
        if (inst->dest.vreg > 0 && inst->dest.vreg < max_vreg) {
            def_count[inst->dest.vreg]++;
        }
    }

    for (IRInst *inst = fn->first_inst; inst != NULL; inst = inst->next) {
        if (inst->op == IR_LABEL) {
            /* Control flow merge - invalidate stack constants */
            stack_const_clear(&stack_table);
            continue;
        }

        if (inst->op == IR_CALL || inst->op == IR_STORE) {
            /* Potential memory side effects - clear stack constants */
            stack_const_clear(&stack_table);

            if (inst->op == IR_CALL) {
                for (int i = 0; i < inst->call_arg_count; i++) {
                    int r = inst->call_args[i].vreg;
                    if (r > 0 && r < max_vreg && consts[r].is_const) {
                        inst->call_args[i].vreg = 0;
                        inst->call_args[i].imm = consts[r].val;
                        changed = true;
                    }
                }
            } else if (inst->op == IR_STORE) {
                int r = inst->src1.vreg;
                if (r > 0 && r < max_vreg && consts[r].is_const) {
                    inst->src1.vreg = 0;
                    inst->src1.imm = consts[r].val;
                    changed = true;
                }
            }
            continue;
        }

        if (inst->op == IR_ADDR_STACK) {
            stack_const_invalidate(&stack_table, inst->src1.offset);
            continue;
        }

        if (inst->op == IR_STORE_STACK) {
            if (inst->src1.vreg == 0) {
                stack_const_set(&stack_table, inst->dest.offset, inst->src1.imm);
            } else if (inst->src1.vreg > 0 && inst->src1.vreg < max_vreg && consts[inst->src1.vreg].is_const) {
                int64_t val = consts[inst->src1.vreg].val;
                inst->src1.vreg = 0;
                inst->src1.imm = val;
                stack_const_set(&stack_table, inst->dest.offset, val);
                changed = true;
            } else {
                stack_const_invalidate(&stack_table, inst->dest.offset);
            }
            continue;
        }

        if (inst->op == IR_LOAD_STACK) {
            int64_t val = 0;
            if (stack_const_get(&stack_table, inst->src1.offset, &val)) {
                inst->op = IR_IMM;
                inst->src1.vreg = 0;
                inst->src1.imm = val;
                inst->src1.label = NULL;
                inst->src1.offset = 0;
                if (inst->dest.vreg > 0 && inst->dest.vreg < max_vreg && def_count[inst->dest.vreg] == 1) {
                    consts[inst->dest.vreg].is_const = true;
                    consts[inst->dest.vreg].val = val;
                }
                changed = true;
                continue;
            }
        }

        if (inst->op == IR_IMM) {
            if (inst->dest.vreg > 0 && inst->dest.vreg < max_vreg && def_count[inst->dest.vreg] == 1) {
                consts[inst->dest.vreg].is_const = true;
                consts[inst->dest.vreg].val = inst->src1.imm;
            }
            continue;
        }

        if (inst->op == IR_MOV) {
            int r = inst->src1.vreg;
            if (r > 0 && r < max_vreg && consts[r].is_const) {
                inst->op = IR_IMM;
                inst->src1.vreg = 0;
                inst->src1.imm = consts[r].val;
                if (inst->dest.vreg > 0 && inst->dest.vreg < max_vreg && def_count[inst->dest.vreg] == 1) {
                    consts[inst->dest.vreg].is_const = true;
                    consts[inst->dest.vreg].val = consts[r].val;
                }
                changed = true;
                continue;
            } else if (inst->src1.vreg == 0) {
                inst->op = IR_IMM;
                if (inst->dest.vreg > 0 && inst->dest.vreg < max_vreg && def_count[inst->dest.vreg] == 1) {
                    consts[inst->dest.vreg].is_const = true;
                    consts[inst->dest.vreg].val = inst->src1.imm;
                }
                changed = true;
                continue;
            }
        }

        if (inst->op == IR_RET) {
            int r = inst->src1.vreg;
            if (r > 0 && r < max_vreg && consts[r].is_const) {
                inst->src1.vreg = 0;
                inst->src1.imm = consts[r].val;
                changed = true;
            }
            continue;
        }

        if (inst->op == IR_JMP_IF_ZERO || inst->op == IR_JMP_IF_NOT_ZERO) {
            int r = inst->src1.vreg;
            if (r > 0 && r < max_vreg && consts[r].is_const) {
                inst->src1.vreg = 0;
                inst->src1.imm = consts[r].val;
                changed = true;
            }
            continue;
        }

        /* Binary arithmetic / comparison operations */
        if (inst->op >= IR_ADD && inst->op <= IR_CMP_GE) {
            int r1 = inst->src1.vreg;
            if (r1 > 0 && r1 < max_vreg && consts[r1].is_const) {
                inst->src1.vreg = 0;
                inst->src1.imm = consts[r1].val;
                changed = true;
            }

            int r2 = inst->src2.vreg;
            if (r2 > 0 && r2 < max_vreg && consts[r2].is_const) {
                inst->src2.vreg = 0;
                inst->src2.imm = consts[r2].val;
                changed = true;
            }

            if (inst->src1.vreg == 0 && inst->src2.vreg == 0) {
                int64_t v1 = inst->src1.imm;
                int64_t v2 = inst->src2.imm;
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
                    case IR_SHL:    res = v1 << (v2 & 63); break;
                    case IR_SHR:    res = v1 >> (v2 & 63); break;
                    case IR_CMP_EQ: res = (v1 == v2) ? 1 : 0; break;
                    case IR_CMP_NE: res = (v1 != v2) ? 1 : 0; break;
                    case IR_CMP_LT: res = (v1 < v2) ? 1 : 0; break;
                    case IR_CMP_LE: res = (v1 <= v2) ? 1 : 0; break;
                    case IR_CMP_GT: res = (v1 > v2) ? 1 : 0; break;
                    case IR_CMP_GE: res = (v1 >= v2) ? 1 : 0; break;
                    default:        folded = false; break;
                }

                if (folded) {
                    inst->op = IR_IMM;
                    inst->src1.vreg = 0;
                    inst->src1.imm = res;
                    inst->src2.vreg = 0;
                    inst->src2.imm = 0;
                    if (inst->dest.vreg > 0 && inst->dest.vreg < max_vreg) {
                        consts[inst->dest.vreg].is_const = true;
                        consts[inst->dest.vreg].val = res;
                    }
                    changed = true;
                    continue;
                }
            }
        }
    }

    return changed;
}

/* =========================================================================
 * 2. Copy Propagation
 * ========================================================================= */

static int find_copy_root(int *copy_map, int r) {
    int curr = r;
    while (curr > 0 && copy_map[curr] > 0 && copy_map[curr] != curr) {
        curr = copy_map[curr];
    }
    return curr;
}

bool opt_copy_propagation(IRFunction *fn, Arena *arena) {
    if (!fn) return false;

    bool changed = false;
    int max_vreg = fn->vreg_count + 32;
    int *copy_map = arena_alloc_zero(arena, sizeof(int) * max_vreg);
    for (int i = 0; i < max_vreg; i++) {
        copy_map[i] = i;
    }

    /* Pass 1: Build copy mapping and remove redundant self-moves */
    for (IRInst *inst = fn->first_inst; inst != NULL; ) {
        if (inst->op == IR_MOV) {
            int d = inst->dest.vreg;
            int s = inst->src1.vreg;

            if (d > 0 && s > 0 && d < max_vreg && s < max_vreg) {
                if (d == s) {
                    IRInst *next = inst->next;
                    remove_inst(fn, inst);
                    inst = next;
                    changed = true;
                    continue;
                }
                int root = find_copy_root(copy_map, s);
                if (root > 0 && root != d) {
                    copy_map[d] = root;
                }
            }
        }
        inst = inst->next;
    }

    /* Pass 2: Rewrite uses of copied registers */
    for (IRInst *inst = fn->first_inst; inst != NULL; inst = inst->next) {
        /* Rewrite src1 */
        if (inst->op != IR_LABEL && inst->op != IR_STR && inst->op != IR_ADDR_STACK) {
            int r1 = inst->src1.vreg;
            if (r1 > 0 && r1 < max_vreg) {
                int root = find_copy_root(copy_map, r1);
                if (root > 0 && root != r1) {
                    inst->src1.vreg = root;
                    changed = true;
                }
            }
        }

        /* Rewrite src2 */
        if (inst->op >= IR_LOAD && inst->op <= IR_CMP_GE) {
            int r2 = inst->src2.vreg;
            if (r2 > 0 && r2 < max_vreg) {
                int root = find_copy_root(copy_map, r2);
                if (root > 0 && root != r2) {
                    inst->src2.vreg = root;
                    changed = true;
                }
            }
        }

        /* Rewrite call_args */
        if (inst->op == IR_CALL) {
            for (int i = 0; i < inst->call_arg_count; i++) {
                int r = inst->call_args[i].vreg;
                if (r > 0 && r < max_vreg) {
                    int root = find_copy_root(copy_map, r);
                    if (root > 0 && root != r) {
                        inst->call_args[i].vreg = root;
                        changed = true;
                    }
                }
            }
        }

        /* Rewrite pointer in IR_STORE */
        if (inst->op == IR_STORE) {
            int d = inst->dest.vreg;
            if (d > 0 && d < max_vreg) {
                int root = find_copy_root(copy_map, d);
                if (root > 0 && root != d) {
                    inst->dest.vreg = root;
                    changed = true;
                }
            }
        }
    }

    return changed;
}

/* =========================================================================
 * 3. Simple Algebraic Simplification
 * ========================================================================= */

bool opt_algebraic_simplification(IRFunction *fn) {
    if (!fn) return false;

    bool changed = false;

    for (IRInst *inst = fn->first_inst; inst != NULL; inst = inst->next) {
        if (inst->op < IR_ADD || inst->op > IR_CMP_GE) continue;

        bool is_imm1 = (inst->src1.vreg == 0);
        int64_t imm1 = inst->src1.imm;
        bool is_imm2 = (inst->src2.vreg == 0);
        int64_t imm2 = inst->src2.imm;
        bool same_reg = (inst->src1.vreg > 0 && inst->src1.vreg == inst->src2.vreg);

        switch (inst->op) {
            case IR_ADD:
                /* x + 0 => x */
                if (is_imm2 && imm2 == 0) {
                    inst->op = IR_MOV;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                /* 0 + x => x */
                else if (is_imm1 && imm1 == 0) {
                    inst->op = IR_MOV;
                    inst->src1 = inst->src2;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                break;

            case IR_SUB:
                /* x - 0 => x */
                if (is_imm2 && imm2 == 0) {
                    inst->op = IR_MOV;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                /* x - x => 0 */
                else if (same_reg) {
                    inst->op = IR_IMM;
                    inst->src1.vreg = 0;
                    inst->src1.imm = 0;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                break;

            case IR_MUL:
                /* x * 0 or 0 * x => 0 */
                if ((is_imm2 && imm2 == 0) || (is_imm1 && imm1 == 0)) {
                    inst->op = IR_IMM;
                    inst->src1.vreg = 0;
                    inst->src1.imm = 0;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                /* x * 1 => x */
                else if (is_imm2 && imm2 == 1) {
                    inst->op = IR_MOV;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                /* 1 * x => x */
                else if (is_imm1 && imm1 == 1) {
                    inst->op = IR_MOV;
                    inst->src1 = inst->src2;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                break;

            case IR_DIV:
                /* x / 1 => x */
                if (is_imm2 && imm2 == 1) {
                    inst->op = IR_MOV;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                /* 0 / x => 0 (when x != 0) */
                else if (is_imm1 && imm1 == 0 && (!is_imm2 || imm2 != 0)) {
                    inst->op = IR_IMM;
                    inst->src1.vreg = 0;
                    inst->src1.imm = 0;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                /* x / x => 1 */
                else if (same_reg) {
                    inst->op = IR_IMM;
                    inst->src1.vreg = 0;
                    inst->src1.imm = 1;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                break;

            case IR_MOD:
                /* x % 1 => 0 */
                if (is_imm2 && imm2 == 1) {
                    inst->op = IR_IMM;
                    inst->src1.vreg = 0;
                    inst->src1.imm = 0;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                /* x % x => 0 */
                else if (same_reg) {
                    inst->op = IR_IMM;
                    inst->src1.vreg = 0;
                    inst->src1.imm = 0;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                /* 0 % x => 0 */
                else if (is_imm1 && imm1 == 0 && (!is_imm2 || imm2 != 0)) {
                    inst->op = IR_IMM;
                    inst->src1.vreg = 0;
                    inst->src1.imm = 0;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                break;

            case IR_AND:
                /* x & 0 or 0 & x => 0 */
                if ((is_imm2 && imm2 == 0) || (is_imm1 && imm1 == 0)) {
                    inst->op = IR_IMM;
                    inst->src1.vreg = 0;
                    inst->src1.imm = 0;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                /* x & x => x */
                else if (same_reg) {
                    inst->op = IR_MOV;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                /* x & -1 => x */
                else if (is_imm2 && imm2 == -1) {
                    inst->op = IR_MOV;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                /* -1 & x => x */
                else if (is_imm1 && imm1 == -1) {
                    inst->op = IR_MOV;
                    inst->src1 = inst->src2;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                break;

            case IR_OR:
                /* x | 0 => x */
                if (is_imm2 && imm2 == 0) {
                    inst->op = IR_MOV;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                /* 0 | x => x */
                else if (is_imm1 && imm1 == 0) {
                    inst->op = IR_MOV;
                    inst->src1 = inst->src2;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                /* x | x => x */
                else if (same_reg) {
                    inst->op = IR_MOV;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                /* x | -1 or -1 | x => -1 */
                else if ((is_imm2 && imm2 == -1) || (is_imm1 && imm1 == -1)) {
                    inst->op = IR_IMM;
                    inst->src1.vreg = 0;
                    inst->src1.imm = -1;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                break;

            case IR_XOR:
                /* x ^ 0 => x */
                if (is_imm2 && imm2 == 0) {
                    inst->op = IR_MOV;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                /* 0 ^ x => x */
                else if (is_imm1 && imm1 == 0) {
                    inst->op = IR_MOV;
                    inst->src1 = inst->src2;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                /* x ^ x => 0 */
                else if (same_reg) {
                    inst->op = IR_IMM;
                    inst->src1.vreg = 0;
                    inst->src1.imm = 0;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                break;

            case IR_SHL:
            case IR_SHR:
                /* x << 0 or x >> 0 => x */
                if (is_imm2 && imm2 == 0) {
                    inst->op = IR_MOV;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                /* 0 << x or 0 >> x => 0 */
                else if (is_imm1 && imm1 == 0) {
                    inst->op = IR_IMM;
                    inst->src1.vreg = 0;
                    inst->src1.imm = 0;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                break;

            case IR_CMP_EQ:
                if (same_reg) {
                    inst->op = IR_IMM;
                    inst->src1.vreg = 0;
                    inst->src1.imm = 1;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                break;

            case IR_CMP_NE:
                if (same_reg) {
                    inst->op = IR_IMM;
                    inst->src1.vreg = 0;
                    inst->src1.imm = 0;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                break;

            case IR_CMP_LT:
                if (same_reg) {
                    inst->op = IR_IMM;
                    inst->src1.vreg = 0;
                    inst->src1.imm = 0;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                break;

            case IR_CMP_LE:
                if (same_reg) {
                    inst->op = IR_IMM;
                    inst->src1.vreg = 0;
                    inst->src1.imm = 1;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                break;

            case IR_CMP_GT:
                if (same_reg) {
                    inst->op = IR_IMM;
                    inst->src1.vreg = 0;
                    inst->src1.imm = 0;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                break;

            case IR_CMP_GE:
                if (same_reg) {
                    inst->op = IR_IMM;
                    inst->src1.vreg = 0;
                    inst->src1.imm = 1;
                    inst->src2 = (IROperand){0};
                    changed = true;
                }
                break;

            default:
                break;
        }
    }

    return changed;
}

/* =========================================================================
 * 4. Basic CFG Optimization
 * ========================================================================= */

static const char *thread_jump_target(IRFunction *fn, const char *target) {
    if (!target) return NULL;
    const char *curr = target;
    for (int hop = 0; hop < 8; hop++) {
        IRInst *lbl = NULL;
        for (IRInst *i = fn->first_inst; i != NULL; i = i->next) {
            if (i->op == IR_LABEL && i->dest.label && strcmp(i->dest.label, curr) == 0) {
                lbl = i;
                break;
            }
        }
        if (!lbl || !lbl->next || lbl->next->op != IR_JMP) break;
        if (!lbl->next->dest.label) break;
        if (strcmp(lbl->next->dest.label, curr) == 0) break; /* Avoid self-loop */
        curr = lbl->next->dest.label;
    }
    return curr;
}

bool opt_cfg_optimization(IRFunction *fn) {
    if (!fn) return false;

    bool changed = false;

    /* 1. Simplify constant conditional branches */
    for (IRInst *inst = fn->first_inst; inst != NULL; ) {
        if (inst->op == IR_JMP_IF_ZERO) {
            if (inst->src1.vreg == 0) {
                if (inst->src1.imm == 0) {
                    inst->op = IR_JMP;
                    inst->src1 = (IROperand){0};
                    changed = true;
                } else {
                    IRInst *next = inst->next;
                    remove_inst(fn, inst);
                    inst = next;
                    changed = true;
                    continue;
                }
            }
        } else if (inst->op == IR_JMP_IF_NOT_ZERO) {
            if (inst->src1.vreg == 0) {
                if (inst->src1.imm != 0) {
                    inst->op = IR_JMP;
                    inst->src1 = (IROperand){0};
                    changed = true;
                } else {
                    IRInst *next = inst->next;
                    remove_inst(fn, inst);
                    inst = next;
                    changed = true;
                    continue;
                }
            }
        }
        inst = inst->next;
    }

    /* 2. Dead jumps (jump to immediately following instruction/label) */
    for (IRInst *inst = fn->first_inst; inst != NULL; ) {
        if (inst->op == IR_JMP) {
            if (inst->next && inst->next->op == IR_LABEL &&
                inst->dest.label && inst->next->dest.label &&
                strcmp(inst->dest.label, inst->next->dest.label) == 0) {
                IRInst *next = inst->next;
                remove_inst(fn, inst);
                inst = next;
                changed = true;
                continue;
            }
        } else if (inst->op == IR_JMP_IF_ZERO || inst->op == IR_JMP_IF_NOT_ZERO) {
            if (inst->next && inst->next->op == IR_LABEL &&
                inst->dest.label && inst->next->dest.label &&
                strcmp(inst->dest.label, inst->next->dest.label) == 0) {
                IRInst *next = inst->next;
                remove_inst(fn, inst);
                inst = next;
                changed = true;
                continue;
            }
        }
        inst = inst->next;
    }

    /* 3. Jump threading / branch chaining */
    for (IRInst *inst = fn->first_inst; inst != NULL; inst = inst->next) {
        if (inst->op == IR_JMP || inst->op == IR_JMP_IF_ZERO || inst->op == IR_JMP_IF_NOT_ZERO) {
            const char *target = thread_jump_target(fn, inst->dest.label);
            if (target && target != inst->dest.label && strcmp(target, inst->dest.label) != 0) {
                inst->dest.label = target;
                changed = true;
            }
        }
    }

    /* 4. Unused label removal */
    for (IRInst *inst = fn->first_inst; inst != NULL; ) {
        if (inst->op == IR_LABEL && inst->dest.label) {
            bool referenced = false;
            for (IRInst *other = fn->first_inst; other != NULL; other = other->next) {
                if (other->op == IR_JMP || other->op == IR_JMP_IF_ZERO || other->op == IR_JMP_IF_NOT_ZERO) {
                    if (other->dest.label && strcmp(other->dest.label, inst->dest.label) == 0) {
                        referenced = true;
                        break;
                    }
                }
            }
            if (!referenced) {
                /* Safe to remove only if control can fall through into it */
                if (inst->prev == NULL || (inst->prev->op != IR_JMP && inst->prev->op != IR_RET)) {
                    IRInst *next = inst->next;
                    remove_inst(fn, inst);
                    inst = next;
                    changed = true;
                    continue;
                }
            }
        }
        inst = inst->next;
    }

    return changed;
}

/* =========================================================================
 * 4b. CFG Simplification Pass
 * ========================================================================= */

bool opt_cfg_simplify(IRFunction *fn) {
    if (!fn || !fn->first_inst) return false;

    bool changed = false;

    /* 1. Invert conditional branch over unconditional jump */
    for (IRInst *inst = fn->first_inst; inst != NULL; ) {
        if (inst->op == IR_JMP_IF_ZERO || inst->op == IR_JMP_IF_NOT_ZERO) {
            IRInst *next = inst->next;
            if (next && next->op == IR_JMP) {
                IRInst *target_lbl = next->next;
                if (target_lbl && target_lbl->op == IR_LABEL &&
                    inst->dest.label && target_lbl->dest.label &&
                    strcmp(inst->dest.label, target_lbl->dest.label) == 0) {
                    /* Invert condition and jump directly to the unconditional jump's target */
                    inst->op = (inst->op == IR_JMP_IF_ZERO) ? IR_JMP_IF_NOT_ZERO : IR_JMP_IF_ZERO;
                    inst->dest.label = next->dest.label;
                    remove_inst(fn, next);
                    changed = true;
                    continue;
                }
            }
        }
        inst = inst->next;
    }

    /* 2. Fold identical conditional and unconditional branch destinations */
    for (IRInst *inst = fn->first_inst; inst != NULL; ) {
        if (inst->op == IR_JMP_IF_ZERO || inst->op == IR_JMP_IF_NOT_ZERO) {
            IRInst *next = inst->next;
            if (next && next->op == IR_JMP &&
                inst->dest.label && next->dest.label &&
                strcmp(inst->dest.label, next->dest.label) == 0) {
                /* Both branches jump to the same destination */
                IRInst *to_remove = inst;
                inst = inst->next;
                remove_inst(fn, to_remove);
                changed = true;
                continue;
            }
        }
        inst = inst->next;
    }

    /* 3. Consecutive labels deduplication */
    for (IRInst *inst = fn->first_inst; inst != NULL; ) {
        if (inst->op == IR_LABEL && inst->next && inst->next->op == IR_LABEL) {
            const char *keep_lbl = inst->dest.label;
            const char *remove_lbl = inst->next->dest.label;
            if (keep_lbl && remove_lbl && strcmp(keep_lbl, remove_lbl) != 0) {
                /* Replace all references to remove_lbl with keep_lbl */
                for (IRInst *other = fn->first_inst; other != NULL; other = other->next) {
                    if ((other->op == IR_JMP || other->op == IR_JMP_IF_ZERO || other->op == IR_JMP_IF_NOT_ZERO) &&
                        other->dest.label && strcmp(other->dest.label, remove_lbl) == 0) {
                        other->dest.label = keep_lbl;
                    }
                }
                remove_inst(fn, inst->next);
                changed = true;
                continue;
            }
        }
        inst = inst->next;
    }

    /* 4. Remove dead instructions between unconditional jump or return and next label */
    for (IRInst *inst = fn->first_inst; inst != NULL; inst = inst->next) {
        if (inst->op == IR_JMP || inst->op == IR_RET) {
            IRInst *dead = inst->next;
            while (dead && dead->op != IR_LABEL) {
                IRInst *next_dead = dead->next;
                remove_inst(fn, dead);
                dead = next_dead;
                changed = true;
            }
        }
    }

    return changed;
}

/* =========================================================================
 * 5. Unreachable Block Removal
 * ========================================================================= */

typedef struct {
    int id;
    IRInst *start;
    IRInst *end;
    const char *label;
    bool reachable;
} BasicBlockInfo;

bool opt_unreachable_block_removal(IRFunction *fn, Arena *arena) {
    if (!fn || !fn->first_inst) return false;

    bool changed = false;

    /* Step 1: Remove instructions immediately following unconditional JMP or RET up to next label */
    for (IRInst *inst = fn->first_inst; inst != NULL; inst = inst->next) {
        if (inst->op == IR_RET || inst->op == IR_JMP) {
            IRInst *dead = inst->next;
            while (dead && dead->op != IR_LABEL) {
                IRInst *next_dead = dead->next;
                remove_inst(fn, dead);
                dead = next_dead;
                changed = true;
            }
        }
    }

    /* Step 2: Basic block level reachability analysis */
    int bb_count = 0;
    for (IRInst *i = fn->first_inst; i != NULL; i = i->next) {
        if (i == fn->first_inst || i->op == IR_LABEL) {
            bb_count++;
        }
    }
    if (bb_count == 0) return changed;

    BasicBlockInfo *blocks = arena_alloc_zero(arena, sizeof(BasicBlockInfo) * bb_count);
    int curr_bb = -1;

    for (IRInst *i = fn->first_inst; i != NULL; i = i->next) {
        if (i == fn->first_inst || i->op == IR_LABEL) {
            curr_bb++;
            blocks[curr_bb].id = curr_bb;
            blocks[curr_bb].start = i;
            blocks[curr_bb].end = i;
            blocks[curr_bb].reachable = (curr_bb == 0); /* Entry block is reachable */
            if (i->op == IR_LABEL) {
                blocks[curr_bb].label = i->dest.label;
            }
        } else {
            blocks[curr_bb].end = i;
        }
    }

    /* Propagate reachability across control flow edges */
    bool bb_changed = true;
    while (bb_changed) {
        bb_changed = false;
        for (int b = 0; b < bb_count; b++) {
            if (!blocks[b].reachable) continue;

            /* Check all jumps within this basic block */
            for (IRInst *curr = blocks[b].start; curr != NULL; ) {
                if (curr->op == IR_JMP || curr->op == IR_JMP_IF_ZERO || curr->op == IR_JMP_IF_NOT_ZERO) {
                    if (curr->dest.label) {
                        for (int t = 0; t < bb_count; t++) {
                            if (blocks[t].label && strcmp(blocks[t].label, curr->dest.label) == 0) {
                                if (!blocks[t].reachable) {
                                    blocks[t].reachable = true;
                                    bb_changed = true;
                                }
                                break;
                            }
                        }
                    }
                }
                if (curr == blocks[b].end) break;
                curr = curr->next;
            }

            /* Does this block fall through to block b + 1? */
            IRInst *last = blocks[b].end;
            if (last && last->op != IR_RET && last->op != IR_JMP) {
                if (b + 1 < bb_count && !blocks[b + 1].reachable) {
                    blocks[b + 1].reachable = true;
                    bb_changed = true;
                }
            }
        }
    }

    /* Remove all instructions in unreachable blocks */
    for (int b = 0; b < bb_count; b++) {
        if (!blocks[b].reachable) {
            IRInst *curr = blocks[b].start;
            IRInst *end = blocks[b].end;
            while (curr) {
                IRInst *next = curr->next;
                bool at_end = (curr == end);
                remove_inst(fn, curr);
                changed = true;
                if (at_end) break;
                curr = next;
            }
        }
    }

    return changed;
}

/* =========================================================================
 * 6. Dead Code Elimination
 * ========================================================================= */

bool opt_dead_code_elimination(IRFunction *fn, Arena *arena) {
    if (!fn) return false;

    bool changed = false;
    int max_vreg = fn->vreg_count + 32;
    int *use_count = arena_alloc_zero(arena, sizeof(int) * max_vreg);

    /* Count usages of each vreg */
    for (IRInst *inst = fn->first_inst; inst != NULL; inst = inst->next) {
        if (inst->op != IR_LABEL && inst->op != IR_STR && inst->op != IR_ADDR_STACK) {
            int r1 = inst->src1.vreg;
            if (r1 > 0 && r1 < max_vreg) use_count[r1]++;
        }
        if (inst->op >= IR_LOAD && inst->op <= IR_CMP_GE) {
            int r2 = inst->src2.vreg;
            if (r2 > 0 && r2 < max_vreg) use_count[r2]++;
        }
        if (inst->op == IR_STORE) {
            int d = inst->dest.vreg;
            if (d > 0 && d < max_vreg) use_count[d]++;
        }
        if (inst->op == IR_CALL) {
            for (int i = 0; i < inst->call_arg_count; i++) {
                int r = inst->call_args[i].vreg;
                if (r > 0 && r < max_vreg) use_count[r]++;
            }
        }
    }

    /* Remove pure instructions whose dest vreg is never used */
    for (IRInst *inst = fn->first_inst; inst != NULL; ) {
        bool is_pure = (inst->op == IR_IMM || inst->op == IR_STR || inst->op == IR_MOV ||
                        inst->op == IR_LOAD_STACK || inst->op == IR_ADDR_STACK ||
                        (inst->op >= IR_ADD && inst->op <= IR_CMP_GE));

        if (is_pure && inst->dest.vreg > 0 && inst->dest.vreg < max_vreg) {
            if (use_count[inst->dest.vreg] == 0) {
                IRInst *next = inst->next;
                remove_inst(fn, inst);
                inst = next;
                changed = true;
                continue;
            }
        }
        inst = inst->next;
    }

    return changed;
}

/* =========================================================================
 * Optimization Pipeline
 * ========================================================================= */

void opt_run_pipeline(IRModule *mod, OptOptions options) {
    if (options.level <= 0) return;

    for (IRFunction *fn = mod->functions; fn != NULL; fn = fn->next) {
        int max_iters = (options.level >= 2) ? 10 : 4;
        for (int iter = 0; iter < max_iters; iter++) {
            bool changed = false;

            if (options.enable_const_prop || options.enable_const_fold || options.level >= 1) {
                changed |= opt_constant_propagation(fn, mod->arena);
            }
            if (options.enable_copy_prop || options.level >= 1) {
                changed |= opt_copy_propagation(fn, mod->arena);
            }
            if (options.enable_algebraic || options.level >= 1) {
                changed |= opt_algebraic_simplification(fn);
            }
            if (options.enable_cfg_opt || options.level >= 1) {
                changed |= opt_cfg_optimization(fn);
            }
            if (options.enable_cfg_simplify || options.enable_cfg_opt || options.level >= 1) {
                changed |= opt_cfg_simplify(fn);
            }
            if (options.enable_unreachable || options.level >= 1) {
                changed |= opt_unreachable_block_removal(fn, mod->arena);
            }
            if (options.enable_dce || options.level >= 1) {
                changed |= opt_dead_code_elimination(fn, mod->arena);
            }

            if (!changed) break;
        }
    }
}
