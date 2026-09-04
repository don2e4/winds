#include "codegen_x86.h"

static const char *k_arg_regs_64[] = { "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9" };

static int get_vreg_offset(int vreg, int base_stack) {
    /* vreg 1 is at -(base_stack + 8), vreg 2 is at -(base_stack + 16)... */
    return -(base_stack + vreg * 8);
}

static void emit_operand_to_rax(FILE *out, IROperand op, int base_stack) {
    if (op.vreg > 0) {
        fprintf(out, "\tmovq\t%d(%%rbp), %%rax\n", get_vreg_offset(op.vreg, base_stack));
    } else {
        fprintf(out, "\tmovq\t$%ld, %%rax\n", (long)op.imm);
    }
}

static void emit_operand_to_rcx(FILE *out, IROperand op, int base_stack) {
    if (op.vreg > 0) {
        fprintf(out, "\tmovq\t%d(%%rbp), %%rcx\n", get_vreg_offset(op.vreg, base_stack));
    } else {
        fprintf(out, "\tmovq\t$%ld, %%rcx\n", (long)op.imm);
    }
}

static void emit_store_rax(FILE *out, IROperand dest, int base_stack) {
    fprintf(out, "\tmovq\t%%rax, %d(%%rbp)\n", get_vreg_offset(dest.vreg, base_stack));
}

static void codegen_function(IRFunction *fn, FILE *out) {
    int local_stack = (fn->stack_size + 15) & ~15;
    int vreg_space = ((fn->vreg_count + 1) * 8 + 15) & ~15;
    int total_stack = local_stack + vreg_space;
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

    char epilogue_label[128];
    snprintf(epilogue_label, sizeof(epilogue_label), ".L_ret_%s", func_name);

    for (IRInst *inst = fn->first_inst; inst != NULL; inst = inst->next) {
        switch (inst->op) {
            case IR_LABEL:
                fprintf(out, "%s:\n", inst->dest.label);
                break;

            case IR_IMM:
                fprintf(out, "\tmovq\t$%ld, %%rax\n", (long)inst->src1.imm);
                emit_store_rax(out, inst->dest, local_stack);
                break;

            case IR_STR:
                fprintf(out, "\tleaq\t%s(%%rip), %%rax\n", inst->src1.label);
                emit_store_rax(out, inst->dest, local_stack);
                break;

            case IR_MOV:
                emit_operand_to_rax(out, inst->src1, local_stack);
                emit_store_rax(out, inst->dest, local_stack);
                break;

            case IR_LOAD_STACK:
                fprintf(out, "\tmovq\t%d(%%rbp), %%rax\n", inst->src1.offset);
                emit_store_rax(out, inst->dest, local_stack);
                break;

            case IR_STORE_STACK:
                if (inst->src1.vreg == -1) {
                    /* Incoming argument register */
                    int arg_num = (int)inst->src1.imm;
                    if (arg_num >= 0 && arg_num < 6) {
                        fprintf(out, "\tmovq\t%s, %d(%%rbp)\n", k_arg_regs_64[arg_num], inst->dest.offset);
                    }
                } else {
                    emit_operand_to_rax(out, inst->src1, local_stack);
                    fprintf(out, "\tmovq\t%%rax, %d(%%rbp)\n", inst->dest.offset);
                }
                break;

            case IR_ADDR_STACK:
                fprintf(out, "\tleaq\t%d(%%rbp), %%rax\n", inst->src1.offset);
                emit_store_rax(out, inst->dest, local_stack);
                break;

            case IR_LOAD:
                emit_operand_to_rax(out, inst->src1, local_stack);
                if (inst->size == 1) {
                    fprintf(out, "\tmovsbq\t%d(%%rax), %%rcx\n", inst->src2.offset);
                } else if (inst->size == 4) {
                    fprintf(out, "\tmovslq\t%d(%%rax), %%rcx\n", inst->src2.offset);
                } else {
                    fprintf(out, "\tmovq\t%d(%%rax), %%rcx\n", inst->src2.offset);
                }
                fprintf(out, "\tmovq\t%%rcx, %d(%%rbp)\n", get_vreg_offset(inst->dest.vreg, local_stack));
                break;

            case IR_STORE:
                emit_operand_to_rcx(out, inst->src1, local_stack); /* val in rcx */
                fprintf(out, "\tmovq\t%d(%%rbp), %%rax\n", get_vreg_offset(inst->dest.vreg, local_stack)); /* ptr in rax */
                if (inst->size == 1) {
                    fprintf(out, "\tmovb\t%%cl, %d(%%rax)\n", inst->dest.offset);
                } else if (inst->size == 4) {
                    fprintf(out, "\tmovl\t%%ecx, %d(%%rax)\n", inst->dest.offset);
                } else {
                    fprintf(out, "\tmovq\t%%rcx, %d(%%rax)\n", inst->dest.offset);
                }
                break;

            case IR_ADD:
                emit_operand_to_rax(out, inst->src1, local_stack);
                emit_operand_to_rcx(out, inst->src2, local_stack);
                fprintf(out, "\taddq\t%%rcx, %%rax\n");
                emit_store_rax(out, inst->dest, local_stack);
                break;

            case IR_SUB:
                emit_operand_to_rax(out, inst->src1, local_stack);
                emit_operand_to_rcx(out, inst->src2, local_stack);
                fprintf(out, "\tsubq\t%%rcx, %%rax\n");
                emit_store_rax(out, inst->dest, local_stack);
                break;

            case IR_MUL:
                emit_operand_to_rax(out, inst->src1, local_stack);
                emit_operand_to_rcx(out, inst->src2, local_stack);
                fprintf(out, "\timulq\t%%rcx, %%rax\n");
                emit_store_rax(out, inst->dest, local_stack);
                break;

            case IR_DIV:
                emit_operand_to_rax(out, inst->src1, local_stack);
                emit_operand_to_rcx(out, inst->src2, local_stack);
                fprintf(out, "\tcqto\n");
                fprintf(out, "\tidivq\t%%rcx\n");
                emit_store_rax(out, inst->dest, local_stack);
                break;

            case IR_MOD:
                emit_operand_to_rax(out, inst->src1, local_stack);
                emit_operand_to_rcx(out, inst->src2, local_stack);
                fprintf(out, "\tcqto\n");
                fprintf(out, "\tidivq\t%%rcx\n");
                fprintf(out, "\tmovq\t%%rdx, %%rax\n");
                emit_store_rax(out, inst->dest, local_stack);
                break;

            case IR_AND:
                emit_operand_to_rax(out, inst->src1, local_stack);
                emit_operand_to_rcx(out, inst->src2, local_stack);
                fprintf(out, "\tandq\t%%rcx, %%rax\n");
                emit_store_rax(out, inst->dest, local_stack);
                break;

            case IR_OR:
                emit_operand_to_rax(out, inst->src1, local_stack);
                emit_operand_to_rcx(out, inst->src2, local_stack);
                fprintf(out, "\torq\t%%rcx, %%rax\n");
                emit_store_rax(out, inst->dest, local_stack);
                break;

            case IR_XOR:
                emit_operand_to_rax(out, inst->src1, local_stack);
                emit_operand_to_rcx(out, inst->src2, local_stack);
                fprintf(out, "\txorq\t%%rcx, %%rax\n");
                emit_store_rax(out, inst->dest, local_stack);
                break;

            case IR_SHL:
                emit_operand_to_rax(out, inst->src1, local_stack);
                emit_operand_to_rcx(out, inst->src2, local_stack);
                fprintf(out, "\tshlq\t%%cl, %%rax\n");
                emit_store_rax(out, inst->dest, local_stack);
                break;

            case IR_SHR:
                emit_operand_to_rax(out, inst->src1, local_stack);
                emit_operand_to_rcx(out, inst->src2, local_stack);
                fprintf(out, "\tsarq\t%%cl, %%rax\n");
                emit_store_rax(out, inst->dest, local_stack);
                break;

            case IR_CMP_EQ:
            case IR_CMP_NE:
            case IR_CMP_LT:
            case IR_CMP_LE:
            case IR_CMP_GT:
            case IR_CMP_GE: {
                emit_operand_to_rax(out, inst->src1, local_stack);
                emit_operand_to_rcx(out, inst->src2, local_stack);
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
                emit_store_rax(out, inst->dest, local_stack);
                break;
            }

            case IR_JMP:
                fprintf(out, "\tjmp\t%s\n", inst->dest.label);
                break;

            case IR_JMP_IF_ZERO:
                emit_operand_to_rax(out, inst->src1, local_stack);
                fprintf(out, "\ttestq\t%%rax, %%rax\n");
                fprintf(out, "\tjz\t%s\n", inst->dest.label);
                break;

            case IR_JMP_IF_NOT_ZERO:
                emit_operand_to_rax(out, inst->src1, local_stack);
                fprintf(out, "\ttestq\t%%rax, %%rax\n");
                fprintf(out, "\tjnz\t%s\n", inst->dest.label);
                break;

            case IR_CALL: {
                /* System V ABI: first 6 integer/pointer args in rdi, rsi, rdx, rcx, r8, r9 */
                for (int i = 0; i < inst->call_arg_count && i < 6; i++) {
                    emit_operand_to_rax(out, inst->call_args[i], local_stack);
                    fprintf(out, "\tmovq\t%%rax, %s\n", k_arg_regs_64[i]);
                }

                /* Clear %al for variadic function calls */
                fprintf(out, "\txorl\t%%eax, %%eax\n");
                fprintf(out, "\tcall\t%s@PLT\n", inst->src1.label);
                emit_store_rax(out, inst->dest, local_stack);
                break;
            }

            case IR_RET:
                emit_operand_to_rax(out, inst->src1, local_stack);
                fprintf(out, "\tjmp\t%s\n", epilogue_label);
                break;

            default:
                break;
        }
    }

    fprintf(out, "%s:\n", epilogue_label);
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

    /* Emit functions in reverse order so declarations match module */
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
        codegen_function(fn, out);
    }

    /* GNU stack note marking non-executable stack */
    fprintf(out, "\n\t.section\t.note.GNU-stack,\"\",@progbits\n");
    return true;
}
