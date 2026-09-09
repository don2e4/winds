#include "codegen_x86.h"
#include "regalloc.h"

static const char *k_arg_regs_64[] = { "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9" };

static int get_operand_reg(RegAlloc *ra, IROperand op) {
    if (ra && op.vreg > 0 && op.vreg <= ra->vreg_count) {
        return ra->vreg_to_reg[op.vreg];
    }
    return PHYS_REG_NONE;
}

static int get_operand_offset(RegAlloc *ra, IROperand op, int base_stack) {
    if (ra && op.vreg > 0 && op.vreg <= ra->vreg_count && ra->vreg_to_spill[op.vreg] != 0) {
        return ra->vreg_to_spill[op.vreg];
    }
    return -(base_stack + op.vreg * 8);
}

static void emit_operand_to_reg(FILE *out, const char *target_reg, IROperand op, RegAlloc *ra, int base_stack) {
    if (op.vreg > 0) {
        int pr = get_operand_reg(ra, op);
        if (pr >= 0) {
            const char *src_name = regalloc_reg_name_64((PhysReg)pr);
            if (strcmp(target_reg, src_name) != 0) {
                fprintf(out, "\tmovq\t%s, %s\n", src_name, target_reg);
            }
        } else {
            int off = get_operand_offset(ra, op, base_stack);
            fprintf(out, "\tmovq\t%d(%%rbp), %s\n", off, target_reg);
        }
    } else {
        if (op.imm == 0 && strcmp(target_reg, "%rax") == 0) {
            fprintf(out, "\txorl\t%%eax, %%eax\n");
        } else if (op.imm == 0 && strcmp(target_reg, "%rcx") == 0) {
            fprintf(out, "\txorl\t%%ecx, %%ecx\n");
        } else {
            fprintf(out, "\tmovq\t$%ld, %s\n", (long)op.imm, target_reg);
        }
    }
}

static void emit_operand_to_rax(FILE *out, IROperand op, RegAlloc *ra, int base_stack) {
    emit_operand_to_reg(out, "%rax", op, ra, base_stack);
}

static void emit_operand_to_rcx(FILE *out, IROperand op, RegAlloc *ra, int base_stack) {
    emit_operand_to_reg(out, "%rcx", op, ra, base_stack);
}

static void emit_store_from_reg(FILE *out, const char *src_reg, IROperand dest, RegAlloc *ra, int base_stack) {
    if (dest.vreg > 0) {
        int pr = get_operand_reg(ra, dest);
        if (pr >= 0) {
            const char *dst_name = regalloc_reg_name_64((PhysReg)pr);
            if (strcmp(dst_name, src_reg) != 0) {
                fprintf(out, "\tmovq\t%s, %s\n", src_reg, dst_name);
            }
        } else {
            int off = get_operand_offset(ra, dest, base_stack);
            fprintf(out, "\tmovq\t%s, %d(%%rbp)\n", src_reg, off);
        }
    }
}

static void emit_store_rax(FILE *out, IROperand dest, RegAlloc *ra, int base_stack) {
    emit_store_from_reg(out, "%rax", dest, ra, base_stack);
}

static void emit_store_rcx(FILE *out, IROperand dest, RegAlloc *ra, int base_stack) {
    emit_store_from_reg(out, "%rcx", dest, ra, base_stack);
}

static void emit_operand_to_xmm(FILE *out, const char *xmm, IROperand op, RegAlloc *ra, int base_stack) {
    emit_operand_to_rax(out, op, ra, base_stack);
    fprintf(out, "\t%s\t%%rax, %s\n", op.fp_size == 4 ? "movd" : "movq", xmm);
}

static void emit_xmm_to_operand(FILE *out, const char *xmm, IROperand dest, RegAlloc *ra, int base_stack) {
    fprintf(out, "\t%s\t%s, %%rax\n", dest.fp_size == 4 ? "movd" : "movq", xmm);
    emit_store_rax(out, dest, ra, base_stack);
}

static void codegen_function(IRFunction *fn, FILE *out, Arena *arena) {
    int local_stack = (fn->stack_size + 15) & ~15;

    /* Run register allocation on function */
    RegAlloc *ra = regalloc_run(fn, arena, local_stack);

    int total_stack = ((local_stack + ra->callee_save_space + ra->spill_space) + 15) & ~15;
    if (total_stack < 32) total_stack = 32;

    const char *func_name = fn->mangled_name ? fn->mangled_name : fn->name;

    fprintf(out, "\n\t.text\n");
    if (fn->is_global) fprintf(out, "\t.globl\t%s\n", func_name);
    fprintf(out, "\t.type\t%s, @function\n", func_name);
    fprintf(out, "%s:\n", func_name);
    fprintf(out, "\t.cfi_startproc\n");
    fprintf(out, "\tpushq\t%%rbp\n");
    fprintf(out, "\t.cfi_def_cfa_offset 16\n");
    fprintf(out, "\t.cfi_offset 6, -16\n");
    fprintf(out, "\tmovq\t%%rsp, %%rbp\n");
    fprintf(out, "\t.cfi_def_cfa_register 6\n");
    fprintf(out, "\tsubq\t$%d, %%rsp\n", total_stack);

    if (fn->va_save_offset) {
        const char *args[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
        for (int i = 0; i < 6; i++) fprintf(out, "\tmovq\t%s, %d(%%rbp)\n", args[i], fn->va_save_offset + i * 8);
        for (int i = 0; i < 8; i++) fprintf(out, "\tmovups\t%%xmm%d, %d(%%rbp)\n", i, fn->va_save_offset + 48 + i * 16);
    }

    /* Save used callee-saved registers into their reserved stack slots */
    for (int r = PHYS_REG_RBX; r <= PHYS_REG_R15; r++) {
        if (ra->used_regs[r]) {
            fprintf(out, "\tmovq\t%s, %d(%%rbp)\n", regalloc_reg_name_64((PhysReg)r), ra->callee_save_offsets[r]);
        }
    }

    char epilogue_label[128];
    snprintf(epilogue_label, sizeof(epilogue_label), ".L_ret_%s", func_name);

    int *uses = arena_alloc_zero(arena, sizeof(int) * fn->vreg_count);
    for (IRInst *i = fn->first_inst; i; i = i->next) {
        if (i->src1.vreg > 0) uses[i->src1.vreg]++;
        if (i->src2.vreg > 0) uses[i->src2.vreg]++;
        if (i->op == IR_STORE && i->dest.vreg > 0) uses[i->dest.vreg]++;
        for (int j = 0; j < i->call_arg_count; j++)
            if (i->call_args[j].vreg > 0) uses[i->call_args[j].vreg]++;
    }
    for (IRInst *inst = fn->first_inst; inst != NULL; inst = inst->next) {
        switch (inst->op) {
            case IR_LABEL:
                fprintf(out, "%s:\n", inst->dest.label);
                break;

            case IR_IMM: {
                int pr = get_operand_reg(ra, inst->dest);
                if (pr >= 0) {
                    if (inst->src1.imm == 0) {
                        const char *r32 = regalloc_reg_name_32((PhysReg)pr);
                        fprintf(out, "\txorl\t%s, %s\n", r32, r32);
                    } else {
                        fprintf(out, "\tmovq\t$%ld, %s\n", (long)inst->src1.imm, regalloc_reg_name_64((PhysReg)pr));
                    }
                } else {
                    if (inst->src1.imm == 0) {
                        fprintf(out, "\txorl\t%%eax, %%eax\n");
                    } else {
                        fprintf(out, "\tmovq\t$%ld, %%rax\n", (long)inst->src1.imm);
                    }
                    emit_store_rax(out, inst->dest, ra, local_stack);
                }
                break;
            }

            case IR_STR: {
                int pr = get_operand_reg(ra, inst->dest);
                if (pr >= 0) {
                    fprintf(out, "\tleaq\t%s(%%rip), %s\n", inst->src1.label, regalloc_reg_name_64((PhysReg)pr));
                } else {
                    fprintf(out, "\tleaq\t%s(%%rip), %%rax\n", inst->src1.label);
                    emit_store_rax(out, inst->dest, ra, local_stack);
                }
                break;
            }

            case IR_MOV: {
                int pd = get_operand_reg(ra, inst->dest);
                int ps = (inst->src1.vreg > 0) ? get_operand_reg(ra, inst->src1) : -1;
                if (pd >= 0 && ps >= 0) {
                    if (pd != ps) {
                        fprintf(out, "\tmovq\t%s, %s\n", regalloc_reg_name_64((PhysReg)ps), regalloc_reg_name_64((PhysReg)pd));
                    }
                } else if (pd >= 0 && inst->src1.vreg == 0) {
                    fprintf(out, "\tmovq\t$%ld, %s\n", (long)inst->src1.imm, regalloc_reg_name_64((PhysReg)pd));
                } else {
                    emit_operand_to_rax(out, inst->src1, ra, local_stack);
                    emit_store_rax(out, inst->dest, ra, local_stack);
                }
                break;
            }

            case IR_LOAD_STACK: {
                int pd = get_operand_reg(ra, inst->dest);
                if (pd >= 0) {
                    fprintf(out, "\tmovq\t%d(%%rbp), %s\n", inst->src1.offset, regalloc_reg_name_64((PhysReg)pd));
                } else {
                    fprintf(out, "\tmovq\t%d(%%rbp), %%rax\n", inst->src1.offset);
                    emit_store_rax(out, inst->dest, ra, local_stack);
                }
                break;
            }

            case IR_STORE_STACK:
                if (inst->src1.vreg == -1) {
                    /* Incoming argument register */
                    int arg_num = (int)inst->src1.imm;
                    if (arg_num >= 0 && arg_num < 6) {
                        fprintf(out, "\tmovq\t%s, %d(%%rbp)\n", k_arg_regs_64[arg_num], inst->dest.offset);
                    }
                } else if (inst->src1.vreg == -3) {
                    int arg_num = (int)inst->src1.imm;
                    fprintf(out, "\t%s\t%%xmm%d, %d(%%rbp)\n", inst->src1.fp_size == 4 ? "movss" : "movsd", arg_num, inst->dest.offset);
                } else if (inst->src1.vreg == -2) {
                    /* Incoming stack argument from caller: 16(%rbp), 24(%rbp), etc. */
                    int incoming_stack_off = (int)inst->src1.imm;
                    fprintf(out, "\tmovq\t%d(%%rbp), %%rax\n", incoming_stack_off);
                    fprintf(out, "\tmovq\t%%rax, %d(%%rbp)\n", inst->dest.offset);
                } else {
                    int ps = (inst->src1.vreg > 0) ? get_operand_reg(ra, inst->src1) : -1;
                    if (ps >= 0) {
                        fprintf(out, "\tmovq\t%s, %d(%%rbp)\n", regalloc_reg_name_64((PhysReg)ps), inst->dest.offset);
                    } else {
                        emit_operand_to_rax(out, inst->src1, ra, local_stack);
                        fprintf(out, "\tmovq\t%%rax, %d(%%rbp)\n", inst->dest.offset);
                    }
                }
                break;

            case IR_ADDR_STACK: {
                int pd = get_operand_reg(ra, inst->dest);
                if (pd >= 0) {
                    fprintf(out, "\tleaq\t%d(%%rbp), %s\n", inst->src1.offset, regalloc_reg_name_64((PhysReg)pd));
                } else {
                    fprintf(out, "\tleaq\t%d(%%rbp), %%rax\n", inst->src1.offset);
                    emit_store_rax(out, inst->dest, ra, local_stack);
                }
                break;
            }

            case IR_LOAD:
                emit_operand_to_rax(out, inst->src1, ra, local_stack);
                if (inst->size == 1) {
                    fprintf(out, inst->dest.is_unsigned ? "\tmovzbq\t%d(%%rax), %%rcx\n" : "\tmovsbq\t%d(%%rax), %%rcx\n", inst->src2.offset);
                } else if (inst->size == 2) {
                    fprintf(out, inst->dest.is_unsigned ? "\tmovzwq\t%d(%%rax), %%rcx\n" : "\tmovswq\t%d(%%rax), %%rcx\n", inst->src2.offset);
                } else if (inst->size == 4) {
                    fprintf(out, inst->dest.is_unsigned ? "\tmovl\t%d(%%rax), %%ecx\n" : "\tmovslq\t%d(%%rax), %%rcx\n", inst->src2.offset);
                } else {
                    fprintf(out, "\tmovq\t%d(%%rax), %%rcx\n", inst->src2.offset);
                }
                emit_store_rcx(out, inst->dest, ra, local_stack);
                break;

            case IR_STORE:
                emit_operand_to_rcx(out, inst->src1, ra, local_stack); /* val in rcx */
                emit_operand_to_rax(out, inst->dest, ra, local_stack); /* ptr in rax */
                if (inst->size == 1) {
                    fprintf(out, "\tmovb\t%%cl, %d(%%rax)\n", inst->dest.offset);
                } else if (inst->size == 2) {
                    fprintf(out, "\tmovw\t%%cx, %d(%%rax)\n", inst->dest.offset);
                } else if (inst->size == 4) {
                    fprintf(out, "\tmovl\t%%ecx, %d(%%rax)\n", inst->dest.offset);
                } else {
                    fprintf(out, "\tmovq\t%%rcx, %d(%%rax)\n", inst->dest.offset);
                }
                break;

            case IR_ADD:
                if (inst->src1.fp_size) {
                    emit_operand_to_xmm(out, "%xmm0", inst->src1, ra, local_stack);
                    emit_operand_to_xmm(out, "%xmm1", inst->src2, ra, local_stack);
                    fprintf(out, "\t%s\t%%xmm1, %%xmm0\n", inst->src1.fp_size == 4 ? "addss" : "addsd");
                    emit_xmm_to_operand(out, "%xmm0", inst->dest, ra, local_stack);
                    break;
                }
                emit_operand_to_rax(out, inst->src1, ra, local_stack);
                if (!inst->src2.vreg && inst->src2.imm == 1) {
                    fprintf(out, "\tincq\t%%rax\n");
                    emit_store_rax(out, inst->dest, ra, local_stack);
                    break;
                }
                emit_operand_to_rcx(out, inst->src2, ra, local_stack);
                fprintf(out, "\taddq\t%%rcx, %%rax\n");
                emit_store_rax(out, inst->dest, ra, local_stack);
                break;

            case IR_SUB:
                if (inst->src1.fp_size) {
                    emit_operand_to_xmm(out, "%xmm0", inst->src1, ra, local_stack);
                    emit_operand_to_xmm(out, "%xmm1", inst->src2, ra, local_stack);
                    fprintf(out, "\t%s\t%%xmm1, %%xmm0\n", inst->src1.fp_size == 4 ? "subss" : "subsd");
                    emit_xmm_to_operand(out, "%xmm0", inst->dest, ra, local_stack);
                    break;
                }
                emit_operand_to_rax(out, inst->src1, ra, local_stack);
                if (!inst->src2.vreg && inst->src2.imm == 1) {
                    fprintf(out, "\tdecq\t%%rax\n");
                    emit_store_rax(out, inst->dest, ra, local_stack);
                    break;
                }
                emit_operand_to_rcx(out, inst->src2, ra, local_stack);
                fprintf(out, "\tsubq\t%%rcx, %%rax\n");
                emit_store_rax(out, inst->dest, ra, local_stack);
                break;

            case IR_MUL:
                if (inst->src1.fp_size) {
                    emit_operand_to_xmm(out, "%xmm0", inst->src1, ra, local_stack);
                    emit_operand_to_xmm(out, "%xmm1", inst->src2, ra, local_stack);
                    fprintf(out, "\t%s\t%%xmm1, %%xmm0\n", inst->src1.fp_size == 4 ? "mulss" : "mulsd");
                    emit_xmm_to_operand(out, "%xmm0", inst->dest, ra, local_stack);
                    break;
                }
                emit_operand_to_rax(out, inst->src1, ra, local_stack);
                emit_operand_to_rcx(out, inst->src2, ra, local_stack);
                fprintf(out, "\timulq\t%%rcx, %%rax\n");
                emit_store_rax(out, inst->dest, ra, local_stack);
                break;

            case IR_DIV:
                if (inst->src1.fp_size) {
                    emit_operand_to_xmm(out, "%xmm0", inst->src1, ra, local_stack);
                    emit_operand_to_xmm(out, "%xmm1", inst->src2, ra, local_stack);
                    fprintf(out, "\t%s\t%%xmm1, %%xmm0\n", inst->src1.fp_size == 4 ? "divss" : "divsd");
                    emit_xmm_to_operand(out, "%xmm0", inst->dest, ra, local_stack);
                    break;
                }
                emit_operand_to_rax(out, inst->src1, ra, local_stack);
                emit_operand_to_rcx(out, inst->src2, ra, local_stack);
                fprintf(out, inst->is_unsigned ? "\txorq\t%%rdx, %%rdx\n\tdivq\t%%rcx\n" : "\tcqto\n\tidivq\t%%rcx\n");
                emit_store_rax(out, inst->dest, ra, local_stack);
                break;

            case IR_MOD:
                emit_operand_to_rax(out, inst->src1, ra, local_stack);
                emit_operand_to_rcx(out, inst->src2, ra, local_stack);
                fprintf(out, inst->is_unsigned ? "\txorq\t%%rdx, %%rdx\n\tdivq\t%%rcx\n" : "\tcqto\n\tidivq\t%%rcx\n");
                fprintf(out, "\tmovq\t%%rdx, %%rax\n");
                emit_store_rax(out, inst->dest, ra, local_stack);
                break;

            case IR_AND:
                emit_operand_to_rax(out, inst->src1, ra, local_stack);
                emit_operand_to_rcx(out, inst->src2, ra, local_stack);
                fprintf(out, "\tandq\t%%rcx, %%rax\n");
                emit_store_rax(out, inst->dest, ra, local_stack);
                break;

            case IR_OR:
                emit_operand_to_rax(out, inst->src1, ra, local_stack);
                emit_operand_to_rcx(out, inst->src2, ra, local_stack);
                fprintf(out, "\torq\t%%rcx, %%rax\n");
                emit_store_rax(out, inst->dest, ra, local_stack);
                break;

            case IR_XOR:
                emit_operand_to_rax(out, inst->src1, ra, local_stack);
                emit_operand_to_rcx(out, inst->src2, ra, local_stack);
                fprintf(out, "\txorq\t%%rcx, %%rax\n");
                emit_store_rax(out, inst->dest, ra, local_stack);
                break;

            case IR_SHL:
                emit_operand_to_rax(out, inst->src1, ra, local_stack);
                if (inst->src2.vreg == 0) {
                    fprintf(out, "\tshlq\t$%ld, %%rax\n", (long)inst->src2.imm);
                } else {
                    emit_operand_to_rcx(out, inst->src2, ra, local_stack);
                    fprintf(out, "\tshlq\t%%cl, %%rax\n");
                }
                emit_store_rax(out, inst->dest, ra, local_stack);
                break;

            case IR_SHR:
                emit_operand_to_rax(out, inst->src1, ra, local_stack);
                if (inst->src2.vreg == 0) {
                    fprintf(out, inst->is_unsigned ? "\tshrq\t$%ld, %%rax\n" : "\tsarq\t$%ld, %%rax\n", (long)inst->src2.imm);
                } else {
                    emit_operand_to_rcx(out, inst->src2, ra, local_stack);
                    fprintf(out, inst->is_unsigned ? "\tshrq\t%%cl, %%rax\n" : "\tsarq\t%%cl, %%rax\n");
                }
                emit_store_rax(out, inst->dest, ra, local_stack);
                break;

            case IR_CMP_EQ:
            case IR_CMP_NE:
            case IR_CMP_LT:
            case IR_CMP_LE:
            case IR_CMP_GT:
            case IR_CMP_GE: {
                if (inst->src1.fp_size) {
                    emit_operand_to_xmm(out, "%xmm0", inst->src1, ra, local_stack);
                    emit_operand_to_xmm(out, "%xmm1", inst->src2, ra, local_stack);
                    fprintf(out, "\t%s\t%%xmm1, %%xmm0\n", inst->src1.fp_size == 4 ? "ucomiss" : "ucomisd");
                    const char *fp_cc[] = {"sete", "setne", "setb", "setbe", "seta", "setae"};
                    fprintf(out, "\t%s\t%%al\n\tmovzbq\t%%al, %%rax\n", fp_cc[inst->op - IR_CMP_EQ]);
                    emit_store_rax(out, inst->dest, ra, local_stack);
                    break;
                }
                emit_operand_to_rax(out, inst->src1, ra, local_stack);
                emit_operand_to_rcx(out, inst->src2, ra, local_stack);
                fprintf(out, "\tcmpq\t%%rcx, %%rax\n");

                IRInst *branch = inst->next;
                if (branch && inst->dest.vreg > 0 && uses[inst->dest.vreg] == 1 &&
                    branch->src1.vreg == inst->dest.vreg &&
                    (branch->op == IR_JMP_IF_ZERO || branch->op == IR_JMP_IF_NOT_ZERO)) {
                    const char *normal[] = {"je", "jne", "jl", "jle", "jg", "jge"};
                    const char *inverse[] = {"jne", "je", "jge", "jg", "jle", "jl"};
                    const char *unsigned_normal[] = {"je", "jne", "jb", "jbe", "ja", "jae"};
                    const char *unsigned_inverse[] = {"jne", "je", "jae", "ja", "jbe", "jb"};
                    int cc = inst->op - IR_CMP_EQ;
                    const char **conditions = branch->op == IR_JMP_IF_ZERO
                                            ? (inst->is_unsigned ? unsigned_inverse : inverse)
                                            : (inst->is_unsigned ? unsigned_normal : normal);
                    fprintf(out, "\t%s\t%s\n", conditions[cc], branch->dest.label);
                    inst = branch;
                    break;
                }
                const char *set_cc = "sete";
                switch (inst->op) {
                    case IR_CMP_EQ: set_cc = "sete"; break;
                    case IR_CMP_NE: set_cc = "setne"; break;
                    case IR_CMP_LT: set_cc = "setl"; break;
                    case IR_CMP_LE: set_cc = "setle"; break;
                    case IR_CMP_GT: set_cc = "setg"; break;
                    case IR_CMP_GE: set_cc = "setge"; break;
                    default: break;
                }
                if (inst->is_unsigned && inst->op >= IR_CMP_LT)
                    set_cc = (const char *[]){"setb", "setbe", "seta", "setae"}[inst->op - IR_CMP_LT];
                fprintf(out, "\t%s\t%%al\n", set_cc);
                fprintf(out, "\tmovzbq\t%%al, %%rax\n");
                emit_store_rax(out, inst->dest, ra, local_stack);
                break;
            }

            case IR_JMP:
                fprintf(out, "\tjmp\t%s\n", inst->dest.label);
                break;

            case IR_JMP_IF_ZERO: {
                int pr = get_operand_reg(ra, inst->src1);
                if (pr >= 0) {
                    fprintf(out, "\ttestq\t%s, %s\n", regalloc_reg_name_64((PhysReg)pr), regalloc_reg_name_64((PhysReg)pr));
                } else {
                    emit_operand_to_rax(out, inst->src1, ra, local_stack);
                    fprintf(out, "\ttestq\t%%rax, %%rax\n");
                }
                fprintf(out, "\tjz\t%s\n", inst->dest.label);
                break;
            }

            case IR_JMP_IF_NOT_ZERO: {
                int pr = get_operand_reg(ra, inst->src1);
                if (pr >= 0) {
                    fprintf(out, "\ttestq\t%s, %s\n", regalloc_reg_name_64((PhysReg)pr), regalloc_reg_name_64((PhysReg)pr));
                } else {
                    emit_operand_to_rax(out, inst->src1, ra, local_stack);
                    fprintf(out, "\ttestq\t%%rax, %%rax\n");
                }
                fprintf(out, "\tjnz\t%s\n", inst->dest.label);
                break;
            }

            case IR_CALL: {
                if (inst->src1.label && !strcmp(inst->src1.label, "__winds_va_arg_gp")) {
                    emit_operand_to_rax(out, inst->call_args[0], ra, local_stack);
                    fprintf(out, "\tmovl\t(%%rax), %%ecx\n\tcmpl\t$48, %%ecx\n");
                    fprintf(out, "\tjae\t.L_va_stack_%s_%d\n", func_name, inst->dest.vreg);
                    fprintf(out, "\taddl\t$8, (%%rax)\n\taddq\t16(%%rax), %%rcx\n");
                    fprintf(out, "\tjmp\t.L_va_done_%s_%d\n.L_va_stack_%s_%d:\n", func_name, inst->dest.vreg, func_name, inst->dest.vreg);
                    fprintf(out, "\tmovq\t8(%%rax), %%rcx\n\taddq\t$8, 8(%%rax)\n");
                    fprintf(out, ".L_va_done_%s_%d:\n", func_name, inst->dest.vreg);
                    emit_store_rcx(out, inst->dest, ra, local_stack);
                    break;
                }
                if (inst->src1.label && !strcmp(inst->src1.label, "__winds_va_start")) {
                    emit_operand_to_rax(out, inst->call_args[0], ra, local_stack);
                    int gp = fn->named_arg_count < 6 ? fn->named_arg_count * 8 : 48;
                    int stack = fn->named_arg_count > 6 ? (fn->named_arg_count - 6) * 8 : 0;
                    fprintf(out, "\tmovl\t$%d, (%%rax)\n\tmovl\t$48, 4(%%rax)\n", gp);
                    fprintf(out, "\tleaq\t%d(%%rbp), %%rcx\n\tmovq\t%%rcx, 8(%%rax)\n", 16 + stack);
                    fprintf(out, "\tleaq\t%d(%%rbp), %%rcx\n\tmovq\t%%rcx, 16(%%rax)\n", fn->va_save_offset);
                    break;
                }
                int gp_count = 0, fp_count = 0, n_stack = 0;
                for (int i = 0; i < inst->call_arg_count; i++) {
                    if (inst->call_args[i].on_stack) n_stack++;
                    else if (inst->call_args[i].fp_size) { if (fp_count++ >= 8) n_stack++; }
                    else if (gp_count++ >= 6) n_stack++;
                }
                int stack_arg_space = 0;
                if (n_stack > 0) {
                    stack_arg_space = ((n_stack * 8) + 15) & ~15;
                    fprintf(out, "\tsubq\t$%d, %%rsp\n", stack_arg_space);
                }

                gp_count = fp_count = 0;
                int stack_index = 0;
                for (int i = 0; i < inst->call_arg_count; i++) {
                    IROperand arg = inst->call_args[i];
                    if (!arg.on_stack && arg.fp_size && fp_count < 8) emit_operand_to_xmm(out, fp_count++ == 0 ? "%xmm0" : fp_count == 2 ? "%xmm1" : fp_count == 3 ? "%xmm2" : fp_count == 4 ? "%xmm3" : fp_count == 5 ? "%xmm4" : fp_count == 6 ? "%xmm5" : fp_count == 7 ? "%xmm6" : "%xmm7", arg, ra, local_stack);
                    else if (!arg.on_stack && !arg.fp_size && gp_count < 6) emit_operand_to_reg(out, k_arg_regs_64[gp_count++], arg, ra, local_stack);
                    else {
                        emit_operand_to_rax(out, arg, ra, local_stack);
                        fprintf(out, "\tmovq\t%%rax, %d(%%rsp)\n", stack_index++ * 8);
                    }
                }

                if (inst->src1.label != NULL) {
                    /* Clear %al for variadic function calls */
                    fprintf(out, "\tmovb\t$%d, %%al\n", fp_count);
                    fprintf(out, "\tcall\t%s@PLT\n", inst->src1.label);
                } else {
                    emit_operand_to_reg(out, "%r11", inst->src1, ra, local_stack);
                    /* Clear %al for variadic function calls */
                    fprintf(out, "\tmovb\t$%d, %%al\n", fp_count);
                    fprintf(out, "\tcall\t*%%r11\n");
                }

                if (stack_arg_space > 0) {
                    fprintf(out, "\taddq\t$%d, %%rsp\n", stack_arg_space);
                }

                if (inst->dest.fp_size) emit_xmm_to_operand(out, "%xmm0", inst->dest, ra, local_stack);
                else emit_store_rax(out, inst->dest, ra, local_stack);
                break;
            }

            case IR_RET:
                if (inst->src1.fp_size) emit_operand_to_xmm(out, "%xmm0", inst->src1, ra, local_stack);
                else emit_operand_to_rax(out, inst->src1, ra, local_stack);
                fprintf(out, "\tjmp\t%s\n", epilogue_label);
                break;

            case IR_CAST:
                if (!inst->src1.fp_size && inst->dest.fp_size) {
                    emit_operand_to_rax(out, inst->src1, ra, local_stack);
                    fprintf(out, "\t%s\t%%rax, %%xmm0\n", inst->dest.fp_size == 4 ? "cvtsi2ssq" : "cvtsi2sdq");
                    emit_xmm_to_operand(out, "%xmm0", inst->dest, ra, local_stack);
                } else if (inst->src1.fp_size && !inst->dest.fp_size) {
                    emit_operand_to_xmm(out, "%xmm0", inst->src1, ra, local_stack);
                    fprintf(out, "\t%s\t%%xmm0, %%rax\n", inst->src1.fp_size == 4 ? "cvttss2siq" : "cvttsd2siq");
                    emit_store_rax(out, inst->dest, ra, local_stack);
                } else if (!inst->src1.fp_size && !inst->dest.fp_size) {
                    emit_operand_to_rax(out, inst->src1, ra, local_stack);
                    if (inst->size == 1) fprintf(out, inst->dest.is_unsigned ? "\tmovzbq\t%%al, %%rax\n" : "\tmovsbq\t%%al, %%rax\n");
                    else if (inst->size == 2) fprintf(out, inst->dest.is_unsigned ? "\tmovzwq\t%%ax, %%rax\n" : "\tmovswq\t%%ax, %%rax\n");
                    else if (inst->size == 4) fprintf(out, inst->dest.is_unsigned ? "\tmovl\t%%eax, %%eax\n" : "\tmovslq\t%%eax, %%rax\n");
                    emit_store_rax(out, inst->dest, ra, local_stack);
                } else {
                    emit_operand_to_xmm(out, "%xmm0", inst->src1, ra, local_stack);
                    fprintf(out, "\t%s\t%%xmm0, %%xmm0\n", inst->src1.fp_size == 4 ? "cvtss2sd" : "cvtsd2ss");
                    emit_xmm_to_operand(out, "%xmm0", inst->dest, ra, local_stack);
                }
                break;

            case IR_ADDR_GLOBAL:
                fprintf(out, "\tleaq\t%s(%%rip), %%rax\n", inst->src1.label);
                emit_store_rax(out, inst->dest, ra, local_stack);
                break;

            case IR_LOAD_GLOBAL:
                if (inst->size == 1) fprintf(out, inst->dest.is_unsigned ? "\tmovzbq\t%s(%%rip), %%rax\n" : "\tmovsbq\t%s(%%rip), %%rax\n", inst->src1.label);
                else if (inst->size == 2) fprintf(out, inst->dest.is_unsigned ? "\tmovzwq\t%s(%%rip), %%rax\n" : "\tmovswq\t%s(%%rip), %%rax\n", inst->src1.label);
                else if (inst->size == 4) fprintf(out, inst->dest.is_unsigned ? "\tmovl\t%s(%%rip), %%eax\n" : "\tmovslq\t%s(%%rip), %%rax\n", inst->src1.label);
                else fprintf(out, "\tmovq\t%s(%%rip), %%rax\n", inst->src1.label);
                emit_store_rax(out, inst->dest, ra, local_stack);
                break;

            case IR_STORE_GLOBAL:
                emit_operand_to_rax(out, inst->src1, ra, local_stack);
                if (inst->size == 1) fprintf(out, "\tmovb\t%%al, %s(%%rip)\n", inst->dest.label);
                else if (inst->size == 2) fprintf(out, "\tmovw\t%%ax, %s(%%rip)\n", inst->dest.label);
                else if (inst->size == 4) fprintf(out, "\tmovl\t%%eax, %s(%%rip)\n", inst->dest.label);
                else fprintf(out, "\tmovq\t%%rax, %s(%%rip)\n", inst->dest.label);
                break;

            default:
                break;
        }
    }

    fprintf(out, "%s:\n", epilogue_label);
    /* Restore used callee-saved registers in reverse order */
    for (int r = PHYS_REG_R15; r >= PHYS_REG_RBX; r--) {
        if (ra->used_regs[r]) {
            fprintf(out, "\tmovq\t%d(%%rbp), %s\n", ra->callee_save_offsets[r], regalloc_reg_name_64((PhysReg)r));
        }
    }
    fprintf(out, "\tmovq\t%%rbp, %%rsp\n");
    fprintf(out, "\tpopq\t%%rbp\n");
    fprintf(out, "\t.cfi_def_cfa 7, 8\n");
    fprintf(out, "\tret\n");
    fprintf(out, "\t.cfi_endproc\n");
}

bool codegen_x86_emit(IRModule *mod, FILE *out) {
    if (!mod || !out) return false;

    /* Emit header metadata */
    fprintf(out, "\t.file\t\"winds_generated.cpp\"\n");

    /* Emit string constants in .rodata */
    if (mod->strings) {
        fprintf(out, "\t.section\t.rodata\n");
        for (IRStringLiteral *s = mod->strings; s != NULL; s = s->next) {
            fprintf(out, "%s:\n", s->label);
            fprintf(out, "\t.string\t\"");
            for (size_t i = 0; i < s->len; i++) {
                char c = s->data[i];
                if (c == '\n') fprintf(out, "\\n");
                else if (c == '\t') fprintf(out, "\\t");
                else if (c == '\"') fprintf(out, "\\\"");
                else if (c == '\\') fprintf(out, "\\\\");
                else if ((unsigned char)c < 32 || (unsigned char)c >= 127)
                    fprintf(out, "\\%03o", (unsigned char)c);
                else fputc(c, out);
            }
            fprintf(out, "\"\n");
        }
    }

    /* Emit global variables */
    for (IRGlobalVar *g = mod->globals; g != NULL; g = g->next) {
        if (g->is_init) {
            const char *visibility = g->is_internal ? "" : "\t.globl\t%s\n";
            if (!g->is_internal) fprintf(out, visibility, g->name);
            if (g->init_label) {
                fprintf(out, "\t.data\n\t.align 8\n\t.type\t%s, @object\n\t.size\t%s, %zu\n%s:\n\t.quad\t%s\n",
                        g->name, g->name, g->size, g->name, g->init_label);
            } else if (g->init_values) {
                fprintf(out, "\t.data\n\t.align %d\n\t.type\t%s, @object\n\t.size\t%s, %zu\n%s:\n",
                        g->elem_size >= 8 ? 8 : g->elem_size >= 4 ? 4 : g->elem_size >= 2 ? 2 : 1,
                        g->name, g->name, g->size, g->name);
                size_t initialized = 0;
                for (int i = 0; i < g->init_count; i++) {
                    int size = g->init_sizes ? g->init_sizes[i] : g->elem_size;
                    int offset = g->init_offsets ? g->init_offsets[i] : i * g->elem_size;
                    const char *directive = size == 1 ? ".byte" : size == 2 ? ".short" :
                                            size == 4 ? ".long" : ".quad";
                    if ((size_t)offset > initialized) fprintf(out, "\t.zero\t%zu\n", (size_t)offset - initialized);
                    if (g->init_labels && g->init_labels[i])
                        fprintf(out, "\t%s\t%s\n", directive, g->init_labels[i]);
                    else
                        fprintf(out, "\t%s\t%ld\n", directive, (long)g->init_values[i]);
                    initialized = (size_t)offset + (size_t)size;
                }
                if (initialized < g->size) fprintf(out, "\t.zero\t%zu\n", g->size - initialized);
            } else {
                const char *directive = g->size == 1 ? ".byte" : g->size == 4 ? ".long" : ".quad";
                fprintf(out, "\t.data\n\t.align %zu\n\t.type\t%s, @object\n\t.size\t%s, %zu\n%s:\n\t%s\t%ld\n",
                        g->size, g->name, g->name, g->size, g->name, directive, (long)g->init_val);
            }
        } else {
            if (!g->is_internal) fprintf(out, "\t.globl\t%s\n", g->name);
            fprintf(out, "\t.bss\n\t.align 8\n\t.type\t%s, @object\n\t.size\t%s, %zu\n%s:\n\t.zero\t%zu\n",
                    g->name, g->name, g->size, g->name, g->size);
        }
    }

    /* Reverse function list for natural top-down ordering */
    IRFunction *prev = NULL;
    IRFunction *curr = mod->functions;
    while (curr) {
        IRFunction *next = curr->next;
        curr->next = prev;
        prev = curr;
        curr = next;
    }
    mod->functions = prev;

    for (IRFunction *fn = mod->functions; fn != NULL; fn = fn->next) {
        codegen_function(fn, out, mod->arena);
    }

    /* GNU stack note marking non-executable stack */
    fprintf(out, "\n\t.section\t.note.GNU-stack,\"\",@progbits\n");
    return true;
}
