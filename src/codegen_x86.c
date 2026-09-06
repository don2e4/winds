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

static void codegen_function(IRFunction *fn, FILE *out, Arena *arena) {
    int local_stack = (fn->stack_size + 15) & ~15;

    /* Run register allocation on function */
    RegAlloc *ra = regalloc_run(fn, arena, local_stack);

    int total_stack = ((local_stack + ra->callee_save_space + ra->spill_space) + 15) & ~15;
    if (total_stack < 32) total_stack = 32;

    const char *func_name = fn->mangled_name ? fn->mangled_name : fn->name;

    fprintf(out, "\n\t.text\n");
    fprintf(out, "\t.globl\t%s\n", func_name);
    fprintf(out, "\t.type\t%s, @function\n", func_name);
    fprintf(out, "%s:\n", func_name);
    fprintf(out, "\t.cfi_startproc\n");
    fprintf(out, "\tpushq\t%%rbp\n");
    fprintf(out, "\t.cfi_def_cfa_offset 16\n");
    fprintf(out, "\t.cfi_offset 6, -16\n");
    fprintf(out, "\tmovq\t%%rsp, %%rbp\n");
    fprintf(out, "\t.cfi_def_cfa_register 6\n");
    fprintf(out, "\tsubq\t$%d, %%rsp\n", total_stack);

    /* Save used callee-saved registers into their reserved stack slots */
    for (int r = PHYS_REG_RBX; r <= PHYS_REG_R15; r++) {
        if (ra->used_regs[r]) {
            fprintf(out, "\tmovq\t%s, %d(%%rbp)\n", regalloc_reg_name_64((PhysReg)r), ra->callee_save_offsets[r]);
        }
    }

    char epilogue_label[128];
    snprintf(epilogue_label, sizeof(epilogue_label), ".L_ret_%s", func_name);

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
                    fprintf(out, "\tmovsbq\t%d(%%rax), %%rcx\n", inst->src2.offset);
                } else if (inst->size == 4) {
                    fprintf(out, "\tmovslq\t%d(%%rax), %%rcx\n", inst->src2.offset);
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
                } else if (inst->size == 4) {
                    fprintf(out, "\tmovl\t%%ecx, %d(%%rax)\n", inst->dest.offset);
                } else {
                    fprintf(out, "\tmovq\t%%rcx, %d(%%rax)\n", inst->dest.offset);
                }
                break;

            case IR_ADD:
                emit_operand_to_rax(out, inst->src1, ra, local_stack);
                emit_operand_to_rcx(out, inst->src2, ra, local_stack);
                fprintf(out, "\taddq\t%%rcx, %%rax\n");
                emit_store_rax(out, inst->dest, ra, local_stack);
                break;

            case IR_SUB:
                emit_operand_to_rax(out, inst->src1, ra, local_stack);
                emit_operand_to_rcx(out, inst->src2, ra, local_stack);
                fprintf(out, "\tsubq\t%%rcx, %%rax\n");
                emit_store_rax(out, inst->dest, ra, local_stack);
                break;

            case IR_MUL:
                emit_operand_to_rax(out, inst->src1, ra, local_stack);
                emit_operand_to_rcx(out, inst->src2, ra, local_stack);
                fprintf(out, "\timulq\t%%rcx, %%rax\n");
                emit_store_rax(out, inst->dest, ra, local_stack);
                break;

            case IR_DIV:
                emit_operand_to_rax(out, inst->src1, ra, local_stack);
                emit_operand_to_rcx(out, inst->src2, ra, local_stack);
                fprintf(out, "\tcqto\n");
                fprintf(out, "\tidivq\t%%rcx\n");
                emit_store_rax(out, inst->dest, ra, local_stack);
                break;

            case IR_MOD:
                emit_operand_to_rax(out, inst->src1, ra, local_stack);
                emit_operand_to_rcx(out, inst->src2, ra, local_stack);
                fprintf(out, "\tcqto\n");
                fprintf(out, "\tidivq\t%%rcx\n");
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
                    fprintf(out, "\tsarq\t$%ld, %%rax\n", (long)inst->src2.imm);
                } else {
                    emit_operand_to_rcx(out, inst->src2, ra, local_stack);
                    fprintf(out, "\tsarq\t%%cl, %%rax\n");
                }
                emit_store_rax(out, inst->dest, ra, local_stack);
                break;

            case IR_CMP_EQ:
            case IR_CMP_NE:
            case IR_CMP_LT:
            case IR_CMP_LE:
            case IR_CMP_GT:
            case IR_CMP_GE: {
                emit_operand_to_rax(out, inst->src1, ra, local_stack);
                emit_operand_to_rcx(out, inst->src2, ra, local_stack);
                fprintf(out, "\tcmpq\t%%rcx, %%rax\n");

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
                int n_stack = (inst->call_arg_count > 6) ? (inst->call_arg_count - 6) : 0;
                int stack_arg_space = 0;
                if (n_stack > 0) {
                    stack_arg_space = ((n_stack * 8) + 15) & ~15;
                    fprintf(out, "\tsubq\t$%d, %%rsp\n", stack_arg_space);
                    for (int i = 6; i < inst->call_arg_count; i++) {
                        emit_operand_to_rax(out, inst->call_args[i], ra, local_stack);
                        fprintf(out, "\tmovq\t%%rax, %d(%%rsp)\n", (i - 6) * 8);
                    }
                }

                /* System V ABI: first 6 integer/pointer args in rdi, rsi, rdx, rcx, r8, r9 */
                for (int i = 0; i < inst->call_arg_count && i < 6; i++) {
                    emit_operand_to_reg(out, k_arg_regs_64[i], inst->call_args[i], ra, local_stack);
                }

                if (inst->src1.label != NULL) {
                    /* Clear %al for variadic function calls */
                    fprintf(out, "\txorl\t%%eax, %%eax\n");
                    fprintf(out, "\tcall\t%s@PLT\n", inst->src1.label);
                } else {
                    emit_operand_to_reg(out, "%r11", inst->src1, ra, local_stack);
                    /* Clear %al for variadic function calls */
                    fprintf(out, "\txorl\t%%eax, %%eax\n");
                    fprintf(out, "\tcall\t*%%r11\n");
                }

                if (stack_arg_space > 0) {
                    fprintf(out, "\taddq\t$%d, %%rsp\n", stack_arg_space);
                }

                emit_store_rax(out, inst->dest, ra, local_stack);
                break;
            }

            case IR_RET:
                emit_operand_to_rax(out, inst->src1, ra, local_stack);
                fprintf(out, "\tjmp\t%s\n", epilogue_label);
                break;

            case IR_ADDR_GLOBAL:
                fprintf(out, "\tleaq\t%s(%%rip), %%rax\n", inst->src1.label);
                emit_store_rax(out, inst->dest, ra, local_stack);
                break;

            case IR_LOAD_GLOBAL:
                fprintf(out, "\tmovq\t%s(%%rip), %%rax\n", inst->src1.label);
                emit_store_rax(out, inst->dest, ra, local_stack);
                break;

            case IR_STORE_GLOBAL:
                emit_operand_to_rax(out, inst->src1, ra, local_stack);
                fprintf(out, "\tmovq\t%%rax, %s(%%rip)\n", inst->dest.label);
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
                else fputc(c, out);
            }
            fprintf(out, "\"\n");
        }
    }

    /* Emit global variables */
    for (IRGlobalVar *g = mod->globals; g != NULL; g = g->next) {
        if (g->is_init) {
            if (g->init_label) {
                fprintf(out, "\t.globl\t%s\n\t.data\n\t.align 8\n\t.type\t%s, @object\n\t.size\t%s, %zu\n%s:\n\t.quad\t%s\n",
                        g->name, g->name, g->name, g->size, g->name, g->init_label);
            } else {
                fprintf(out, "\t.globl\t%s\n\t.data\n\t.align 8\n\t.type\t%s, @object\n\t.size\t%s, %zu\n%s:\n\t.quad\t%ld\n",
                        g->name, g->name, g->name, g->size, g->name, (long)g->init_val);
            }
        } else {
            fprintf(out, "\t.globl\t%s\n\t.bss\n\t.align 8\n\t.type\t%s, @object\n\t.size\t%s, %zu\n%s:\n\t.zero\t%zu\n",
                    g->name, g->name, g->name, g->size, g->name, g->size);
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
