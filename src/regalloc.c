#include "regalloc.h"

const char *regalloc_reg_name_64(PhysReg reg) {
    switch (reg) {
        case PHYS_REG_RBX: return "%rbx";
        case PHYS_REG_R12: return "%r12";
        case PHYS_REG_R13: return "%r13";
        case PHYS_REG_R14: return "%r14";
        case PHYS_REG_R15: return "%r15";
        case PHYS_REG_R10: return "%r10";
        case PHYS_REG_R11: return "%r11";
        default: return "%rax";
    }
}

const char *regalloc_reg_name_32(PhysReg reg) {
    switch (reg) {
        case PHYS_REG_RBX: return "%ebx";
        case PHYS_REG_R12: return "%r12d";
        case PHYS_REG_R13: return "%r13d";
        case PHYS_REG_R14: return "%r14d";
        case PHYS_REG_R15: return "%r15d";
        case PHYS_REG_R10: return "%r10d";
        case PHYS_REG_R11: return "%r11d";
        default: return "%eax";
    }
}

const char *regalloc_reg_name_8(PhysReg reg) {
    switch (reg) {
        case PHYS_REG_RBX: return "%bl";
        case PHYS_REG_R12: return "%r12b";
        case PHYS_REG_R13: return "%r13b";
        case PHYS_REG_R14: return "%r14b";
        case PHYS_REG_R15: return "%r15b";
        case PHYS_REG_R10: return "%r10b";
        case PHYS_REG_R11: return "%r11b";
        default: return "%al";
    }
}

bool regalloc_is_callee_saved(PhysReg reg) {
    return reg >= PHYS_REG_RBX && reg <= PHYS_REG_R15;
}

typedef struct {
    const char *label;
    int index;
} LabelPos;

static int find_label_pos(LabelPos *labels, int count, const char *name) {
    if (!name) return -1;
    for (int i = 0; i < count; i++) {
        if (labels[i].label && strcmp(labels[i].label, name) == 0) {
            return labels[i].index;
        }
    }
    return -1;
}

typedef struct {
    int vreg;
    PhysReg reg;
    int end_inst;
} ActiveEntry;

static int compare_interval_start(const void *a, const void *b) {
    const LiveInterval *ia = *(const LiveInterval * const *)a;
    const LiveInterval *ib = *(const LiveInterval * const *)b;
    return ia->start_inst - ib->start_inst;
}

static void update_vreg_range(LiveInterval *intervals, int vreg, int max_vreg, int inst_idx) {
    if (vreg > 0 && vreg <= max_vreg) {
        if (intervals[vreg].start_inst == -1) {
            intervals[vreg].start_inst = inst_idx;
        }
        if (intervals[vreg].end_inst < inst_idx) {
            intervals[vreg].end_inst = inst_idx;
        }
    }
}

RegAlloc *regalloc_run(IRFunction *fn, Arena *arena, int local_stack_base) {
    if (!fn) return NULL;

    RegAlloc *ra = arena_alloc_zero(arena, sizeof(RegAlloc));
    ra->vreg_count = fn->vreg_count;
    int max_vreg = fn->vreg_count + 16;
    ra->vreg_to_reg = arena_alloc_zero(arena, sizeof(int) * max_vreg);
    ra->vreg_to_spill = arena_alloc_zero(arena, sizeof(int) * max_vreg);

    for (int i = 0; i < max_vreg; i++) {
        ra->vreg_to_reg[i] = PHYS_REG_NONE;
    }

    /* Count instructions */
    int inst_count = 0;
    bool has_calls = false;
    for (IRInst *i = fn->first_inst; i != NULL; i = i->next) {
        inst_count++;
        if (i->op == IR_CALL) has_calls = true;
    }

    if (inst_count == 0 || fn->vreg_count == 0) {
        return ra;
    }

    /* Array of instructions for indexed access */
    IRInst **inst_array = arena_alloc(arena, sizeof(IRInst *) * inst_count);
    int idx = 0;
    for (IRInst *i = fn->first_inst; i != NULL; i = i->next) {
        inst_array[idx++] = i;
    }

    /* Record label positions */
    LabelPos *labels = arena_alloc(arena, sizeof(LabelPos) * inst_count);
    int label_count = 0;
    for (int i = 0; i < inst_count; i++) {
        if (inst_array[i]->op == IR_LABEL && inst_array[i]->dest.label) {
            labels[label_count].label = inst_array[i]->dest.label;
            labels[label_count].index = i;
            label_count++;
        }
    }

    /* Initialize live intervals */
    LiveInterval *intervals = arena_alloc_zero(arena, sizeof(LiveInterval) * max_vreg);
    for (int v = 0; v < max_vreg; v++) {
        intervals[v].vreg = v;
        intervals[v].start_inst = -1;
        intervals[v].end_inst = -1;
        intervals[v].assigned_reg = PHYS_REG_NONE;
    }

    /* Pass 1: Compute def and use points for all virtual registers */
    for (int i = 0; i < inst_count; i++) {
        IRInst *inst = inst_array[i];

        /* Destination definition (or pointer read for IR_STORE) */
        if (inst->dest.vreg > 0) {
            update_vreg_range(intervals, inst->dest.vreg, fn->vreg_count, i);
        }

        /* Operand src1 */
        if (inst->src1.vreg > 0) {
            update_vreg_range(intervals, inst->src1.vreg, fn->vreg_count, i);
        }

        /* Operand src2 */
        if (inst->src2.vreg > 0) {
            update_vreg_range(intervals, inst->src2.vreg, fn->vreg_count, i);
        }

        /* Call arguments */
        if (inst->op == IR_CALL) {
            for (int a = 0; a < inst->call_arg_count; a++) {
                if (inst->call_args[a].vreg > 0) {
                    update_vreg_range(intervals, inst->call_args[a].vreg, fn->vreg_count, i);
                }
            }
        }
    }

    /* Pass 2: Extend live ranges across loops (backward jumps) */
    bool loop_changed = true;
    while (loop_changed) {
        loop_changed = false;
        for (int j = 0; j < inst_count; j++) {
            IRInst *inst = inst_array[j];
            if (inst->op == IR_JMP || inst->op == IR_JMP_IF_ZERO || inst->op == IR_JMP_IF_NOT_ZERO) {
                if (inst->dest.label) {
                    int target_idx = find_label_pos(labels, label_count, inst->dest.label);
                    if (target_idx >= 0 && target_idx < j) {
                        for (int v = 1; v <= fn->vreg_count; v++) {
                            if (intervals[v].start_inst >= 0 &&
                                intervals[v].start_inst <= j &&
                                intervals[v].end_inst >= target_idx) {
                                if (intervals[v].end_inst < j) {
                                    intervals[v].end_inst = j;
                                    loop_changed = true;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    /* Pass 3: Identify live intervals that cross function calls */
    if (has_calls) {
        int *call_prefix = arena_alloc(arena, sizeof(int) * ((size_t)inst_count + 1));
        call_prefix[0] = 0;
        for (int i = 0; i < inst_count; i++) {
            call_prefix[i + 1] = call_prefix[i] + (inst_array[i]->op == IR_CALL);
        }
        for (int v = 1; v <= fn->vreg_count; v++) {
            if (intervals[v].start_inst >= 0) {
                /* Calls at either endpoint count, including a call's result. */
                intervals[v].crosses_call =
                    call_prefix[intervals[v].end_inst + 1] > call_prefix[intervals[v].start_inst];
            }
        }
    }

    /* Pass 4: Linear Scan Register Allocation */
    int valid_count = 0;
    for (int v = 1; v <= fn->vreg_count; v++) {
        if (intervals[v].start_inst >= 0) {
            valid_count++;
        }
    }

    if (valid_count > 0) {
        LiveInterval **sorted_intervals = arena_alloc(arena, sizeof(LiveInterval *) * valid_count);
        int si = 0;
        for (int v = 1; v <= fn->vreg_count; v++) {
            if (intervals[v].start_inst >= 0) {
                sorted_intervals[si++] = &intervals[v];
            }
        }

        qsort(sorted_intervals, valid_count, sizeof(LiveInterval *), compare_interval_start);

        ActiveEntry active[PHYS_REG_COUNT];
        int active_count = 0;
        bool reg_in_use[PHYS_REG_COUNT] = {0};

        for (int i = 0; i < valid_count; i++) {
            LiveInterval *curr = sorted_intervals[i];

            /* Expire old intervals whose end_inst is strictly before curr->start_inst */
            for (int a = 0; a < active_count; ) {
                if (active[a].end_inst < curr->start_inst) {
                    reg_in_use[active[a].reg] = false;
                    for (int k = a; k < active_count - 1; k++) {
                        active[k] = active[k + 1];
                    }
                    active_count--;
                } else {
                    a++;
                }
            }

            /* Choose an available physical register */
            PhysReg chosen_reg = PHYS_REG_NONE;

            if (curr->crosses_call) {
                /* Callee-saved registers only */
                for (int r = PHYS_REG_RBX; r <= PHYS_REG_R15; r++) {
                    if (!reg_in_use[r]) {
                        chosen_reg = (PhysReg)r;
                        break;
                    }
                }
            } else {
                /* Prefer caller-saved scratch registers (R10, R11) first */
                if (!reg_in_use[PHYS_REG_R10]) {
                    chosen_reg = PHYS_REG_R10;
                } else if (!reg_in_use[PHYS_REG_R11]) {
                    chosen_reg = PHYS_REG_R11;
                } else {
                    /* Then callee-saved registers */
                    for (int r = PHYS_REG_RBX; r <= PHYS_REG_R15; r++) {
                        if (!reg_in_use[r]) {
                            chosen_reg = (PhysReg)r;
                            break;
                        }
                    }
                }
            }

            if (chosen_reg != PHYS_REG_NONE) {
                curr->assigned_reg = chosen_reg;
                reg_in_use[chosen_reg] = true;
                ra->used_regs[chosen_reg] = true;

                active[active_count].vreg = curr->vreg;
                active[active_count].reg = chosen_reg;
                active[active_count].end_inst = curr->end_inst;
                active_count++;
            } else {
                /* Spilled to stack */
                curr->assigned_reg = PHYS_REG_NONE;
            }
        }
    }

    /* Transfer assignments to RegAlloc result */
    for (int v = 1; v <= fn->vreg_count; v++) {
        ra->vreg_to_reg[v] = intervals[v].assigned_reg;
    }

    /* Assign callee-saved register backup stack slots */
    int callee_saved_count = 0;
    for (int r = PHYS_REG_RBX; r <= PHYS_REG_R15; r++) {
        if (ra->used_regs[r]) {
            callee_saved_count++;
            ra->callee_save_offsets[r] = -(local_stack_base + callee_saved_count * 8);
        }
    }
    ra->callee_save_space = callee_saved_count * 8;

    /* Assign spill slots for spilled virtual registers */
    int spill_idx = 0;
    for (int v = 1; v <= fn->vreg_count; v++) {
        if (ra->vreg_to_reg[v] == PHYS_REG_NONE && intervals[v].start_inst >= 0) {
            spill_idx++;
            ra->vreg_to_spill[v] = -(local_stack_base + ra->callee_save_space + spill_idx * 8);
        }
    }
    ra->num_spills = spill_idx;
    ra->spill_space = spill_idx * 8;

    return ra;
}
