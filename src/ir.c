#include "ir.h"
#include "str.h"

IRModule *ir_module_create(Arena *arena) {
    IRModule *mod = arena_alloc_zero(arena, sizeof(IRModule));
    mod->arena = arena;
    mod->functions = NULL;
    mod->strings = NULL;
    mod->label_counter = 1;
    mod->str_counter = 1;
    return mod;
}

IRFunction *ir_function_create(IRModule *mod, const char *name, const char *mangled_name, int stack_size) {
    IRFunction *fn = arena_alloc_zero(mod->arena, sizeof(IRFunction));
    fn->name = name;
    fn->mangled_name = mangled_name ? mangled_name : name;
    fn->stack_size = stack_size;
    fn->first_inst = NULL;
    fn->last_inst = NULL;
    fn->vreg_count = 1;

    fn->next = mod->functions;
    mod->functions = fn;
    return fn;
}

void ir_emit(IRFunction *fn, IRInst *inst) {
    if (!fn->first_inst) {
        fn->first_inst = inst;
        fn->last_inst = inst;
    } else {
        fn->last_inst->next = inst;
        inst->prev = fn->last_inst;
        fn->last_inst = inst;
    }
}

static int alloc_vreg(IRFunction *fn) {
    return fn->vreg_count++;
}

static const char *gen_label(IRModule *mod, const char *prefix) {
    char buf[64];
    snprintf(buf, sizeof(buf), ".L_%s_%d", prefix, mod->label_counter++);
    return arena_strdup(mod->arena, buf);
}

static const char *add_string_literal(IRModule *mod, const char *data, size_t len) {
    char buf[64];
    snprintf(buf, sizeof(buf), ".LC_%d", mod->str_counter++);
    const char *lbl = arena_strdup(mod->arena, buf);

    IRStringLiteral *str = arena_alloc_zero(mod->arena, sizeof(IRStringLiteral));
    str->label = lbl;
    str->data = data;
    str->len = len;
    str->next = mod->strings;
    mod->strings = str;
    return lbl;
}

static IRInst *make_inst(Arena *arena, IROp op) {
    IRInst *inst = arena_alloc_zero(arena, sizeof(IRInst));
    inst->op = op;
    return inst;
}

static void emit_label(IRFunction *fn, Arena *arena, const char *label) {
    IRInst *inst = make_inst(arena, IR_LABEL);
    inst->dest.label = label;
    ir_emit(fn, inst);
}

static void emit_jmp(IRFunction *fn, Arena *arena, const char *label) {
    IRInst *inst = make_inst(arena, IR_JMP);
    inst->dest.label = label;
    ir_emit(fn, inst);
}

static void emit_jmp_if_zero(IRFunction *fn, Arena *arena, IROperand cond, const char *label) {
    IRInst *inst = make_inst(arena, IR_JMP_IF_ZERO);
    inst->src1 = cond;
    inst->dest.label = label;
    ir_emit(fn, inst);
}

/* Forward declarations */
static IROperand lower_expr(IRModule *mod, IRFunction *fn, ASTNode *expr);
static void lower_stmt(IRModule *mod, IRFunction *fn, ASTNode *stmt, const char *break_lbl, const char *cont_lbl);
static int lower_base_address(IRModule *mod, IRFunction *fn, ASTNode *object, bool is_arrow);

static int lower_base_address(IRModule *mod, IRFunction *fn, ASTNode *object, bool is_arrow) {
    Arena *arena = mod->arena;
    if (!object) return 0;

    if (object->kind == AST_VAR_REF) {
        Symbol *sym = object->var_ref.sym;
        if (is_arrow || (sym && (sym->is_ref || (sym->type && (sym->type->kind == TYPE_REF || sym->type->kind == TYPE_PTR))))) {
            /* Variable is a pointer or reference: stack slot holds the address of target */
            int ptr_vreg = alloc_vreg(fn);
            IRInst *load = make_inst(arena, IR_LOAD_STACK);
            load->dest.vreg = ptr_vreg;
            load->src1.offset = sym ? sym->stack_offset : 0;
            ir_emit(fn, load);
            return ptr_vreg;
        } else {
            /* Object is value on stack (e.g. Vector2D v; v.x or v.method()) */
            int addr_vreg = alloc_vreg(fn);
            IRInst *addr = make_inst(arena, IR_ADDR_STACK);
            addr->dest.vreg = addr_vreg;
            addr->src1.offset = sym ? sym->stack_offset : 0;
            ir_emit(fn, addr);
            return addr_vreg;
        }
    } else if (object->kind == AST_UNARY && object->unary.op == TOK_STAR) {
        /* (*ptr).field -> evaluate ptr */
        IROperand ptr = lower_expr(mod, fn, object->unary.operand);
        return ptr.vreg;
    } else if (object->kind == AST_MEMBER && !object->member.is_arrow && object->type && object->type->kind == TYPE_CLASS) {
        int parent_base = lower_base_address(mod, fn, object->member.object, object->member.is_arrow);
        int offset = object->member.field ? object->member.field->offset : 0;
        int res_vreg = alloc_vreg(fn);
        IRInst *add = make_inst(arena, IR_ADD);
        add->dest.vreg = res_vreg;
        add->src1.vreg = parent_base;
        add->src2.imm = offset;
        ir_emit(fn, add);
        return res_vreg;
    } else {
        IROperand obj = lower_expr(mod, fn, object);
        return obj.vreg;
    }
}

static IROperand lower_expr(IRModule *mod, IRFunction *fn, ASTNode *expr) {
    Arena *arena = mod->arena;
    IROperand res = {0};

    if (!expr) return res;

    switch (expr->kind) {
        case AST_LIT_INT: {
            res.vreg = alloc_vreg(fn);
            IRInst *inst = make_inst(arena, IR_IMM);
            inst->dest = res;
            inst->src1.imm = expr->int_val;
            ir_emit(fn, inst);
            return res;
        }

        case AST_LIT_STR: {
            const char *lbl = add_string_literal(mod, expr->str_lit.val, expr->str_lit.len);
            res.vreg = alloc_vreg(fn);
            IRInst *inst = make_inst(arena, IR_STR);
            inst->dest = res;
            inst->src1.label = lbl;
            ir_emit(fn, inst);
            return res;
        }

        case AST_LIT_BOOL: {
            res.vreg = alloc_vreg(fn);
            IRInst *inst = make_inst(arena, IR_IMM);
            inst->dest = res;
            inst->src1.imm = expr->bool_val ? 1 : 0;
            ir_emit(fn, inst);
            return res;
        }

        case AST_LIT_NULLPTR: {
            res.vreg = alloc_vreg(fn);
            IRInst *inst = make_inst(arena, IR_IMM);
            inst->dest = res;
            inst->src1.imm = 0;
            ir_emit(fn, inst);
            return res;
        }

        case AST_THIS: {
            /* 'this' is at RBP - 8 (first param of method) */
            res.vreg = alloc_vreg(fn);
            IRInst *inst = make_inst(arena, IR_LOAD_STACK);
            inst->dest = res;
            inst->src1.offset = -8;
            ir_emit(fn, inst);
            return res;
        }

        case AST_VAR_REF: {
            Symbol *sym = expr->var_ref.sym;
            if (!sym) return res;

            res.vreg = alloc_vreg(fn);
            if (sym->is_ref) {
                /* Reference: load target pointer first, then load value from pointer */
                int ptr_vreg = alloc_vreg(fn);
                IRInst *load_ptr = make_inst(arena, IR_LOAD_STACK);
                load_ptr->dest.vreg = ptr_vreg;
                load_ptr->src1.offset = sym->stack_offset;
                ir_emit(fn, load_ptr);

                IRInst *load_val = make_inst(arena, IR_LOAD);
                load_val->dest = res;
                load_val->src1.vreg = ptr_vreg;
                load_val->src2.offset = 0;
                ir_emit(fn, load_val);
            } else {
                IRInst *inst = make_inst(arena, IR_LOAD_STACK);
                inst->dest = res;
                inst->src1.offset = sym->stack_offset;
                ir_emit(fn, inst);
            }
            return res;
        }

        case AST_MEMBER: {
            int base_vreg = lower_base_address(mod, fn, expr->member.object, expr->member.is_arrow);
            int offset = expr->member.field ? expr->member.field->offset : 0;
            int sz = expr->member.field && expr->member.field->type ? (int)expr->member.field->type->size : 8;
            res.vreg = alloc_vreg(fn);

            IRInst *load = make_inst(arena, IR_LOAD);
            load->dest = res;
            load->src1.vreg = base_vreg;
            load->src2.offset = offset;
            load->size = sz;
            ir_emit(fn, load);
            return res;
        }

        case AST_BINARY: {
            IROperand left = lower_expr(mod, fn, expr->binary.left);
            IROperand right = lower_expr(mod, fn, expr->binary.right);
            res.vreg = alloc_vreg(fn);

            IROp op = IR_ADD;
            switch (expr->binary.op) {
                case TOK_PLUS:     op = IR_ADD; break;
                case TOK_MINUS:    op = IR_SUB; break;
                case TOK_STAR:     op = IR_MUL; break;
                case TOK_SLASH:    op = IR_DIV; break;
                case TOK_PERCENT:  op = IR_MOD; break;
                case TOK_AMP:      op = IR_AND; break;
                case TOK_PIPE:     op = IR_OR; break;
                case TOK_CARET:    op = IR_XOR; break;
                case TOK_SHL:      op = IR_SHL; break;
                case TOK_SHR:      op = IR_SHR; break;
                case TOK_EQ_EQ:    op = IR_CMP_EQ; break;
                case TOK_EXCL_EQ:  op = IR_CMP_NE; break;
                case TOK_LESS:     op = IR_CMP_LT; break;
                case TOK_LESS_EQ:  op = IR_CMP_LE; break;
                case TOK_GREATER:  op = IR_CMP_GT; break;
                case TOK_GREATER_EQ: op = IR_CMP_GE; break;
                default:           op = IR_ADD; break;
            }

            IRInst *inst = make_inst(arena, op);
            inst->dest = res;
            inst->src1 = left;
            inst->src2 = right;
            ir_emit(fn, inst);
            return res;
        }

        case AST_UNARY: {
            SourceLoc loc = expr->loc;
            (void)loc;
            if (expr->unary.op == TOK_AMP) {
                /* Address-of &x */
                if (expr->unary.operand->kind == AST_VAR_REF) {
                    Symbol *sym = expr->unary.operand->var_ref.sym;
                    res.vreg = alloc_vreg(fn);
                    IRInst *inst = make_inst(arena, IR_ADDR_STACK);
                    inst->dest = res;
                    inst->src1.offset = sym ? sym->stack_offset : 0;
                    ir_emit(fn, inst);
                    return res;
                }
            } else if (expr->unary.op == TOK_STAR) {
                /* Dereference *ptr */
                IROperand ptr = lower_expr(mod, fn, expr->unary.operand);
                res.vreg = alloc_vreg(fn);
                IRInst *inst = make_inst(arena, IR_LOAD);
                inst->dest = res;
                inst->src1 = ptr;
                inst->src2.offset = 0;
                ir_emit(fn, inst);
                return res;
            } else if (expr->unary.op == TOK_MINUS) {
                IROperand opnd = lower_expr(mod, fn, expr->unary.operand);
                res.vreg = alloc_vreg(fn);
                IRInst *inst = make_inst(arena, IR_SUB);
                inst->dest = res;
                inst->src1.imm = 0;
                inst->src2 = opnd;
                ir_emit(fn, inst);
                return res;
            } else if (expr->unary.op == TOK_EXCL) {
                IROperand opnd = lower_expr(mod, fn, expr->unary.operand);
                res.vreg = alloc_vreg(fn);
                IRInst *inst = make_inst(arena, IR_CMP_EQ);
                inst->dest = res;
                inst->src1 = opnd;
                inst->src2.imm = 0;
                ir_emit(fn, inst);
                return res;
            } else if (expr->unary.op == TOK_INC || expr->unary.op == TOK_DEC) {
                /* ++x or x++ */
                if (expr->unary.operand->kind == AST_VAR_REF) {
                    Symbol *sym = expr->unary.operand->var_ref.sym;
                    int old_vreg = alloc_vreg(fn);
                    IRInst *load = make_inst(arena, IR_LOAD_STACK);
                    load->dest.vreg = old_vreg;
                    load->src1.offset = sym->stack_offset;
                    ir_emit(fn, load);

                    int new_vreg = alloc_vreg(fn);
                    IRInst *math = make_inst(arena, expr->unary.op == TOK_INC ? IR_ADD : IR_SUB);
                    math->dest.vreg = new_vreg;
                    math->src1.vreg = old_vreg;
                    math->src2.imm = 1;
                    ir_emit(fn, math);

                    IRInst *store = make_inst(arena, IR_STORE_STACK);
                    store->dest.offset = sym->stack_offset;
                    store->src1.vreg = new_vreg;
                    ir_emit(fn, store);

                    res.vreg = expr->unary.is_prefix ? new_vreg : old_vreg;
                    return res;
                }
            }
            break;
        }

        case AST_ASSIGN: {
            IROperand val = lower_expr(mod, fn, expr->assign.value);

            /* Assignment to variable */
            if (expr->assign.target->kind == AST_VAR_REF) {
                Symbol *sym = expr->assign.target->var_ref.sym;
                if (sym) {
                    if (sym->is_ref) {
                        /* Store through reference pointer */
                        int ptr_vreg = alloc_vreg(fn);
                        IRInst *load_ptr = make_inst(arena, IR_LOAD_STACK);
                        load_ptr->dest.vreg = ptr_vreg;
                        load_ptr->src1.offset = sym->stack_offset;
                        ir_emit(fn, load_ptr);

                        IRInst *store = make_inst(arena, IR_STORE);
                        store->dest.vreg = ptr_vreg;
                        store->dest.offset = 0;
                        store->src1 = val;
                        ir_emit(fn, store);
                    } else {
                        IRInst *store = make_inst(arena, IR_STORE_STACK);
                        store->dest.offset = sym->stack_offset;
                        store->src1 = val;
                        ir_emit(fn, store);
                    }
                }
                return val;
            }

            /* Assignment to member: obj->field = val or obj.field = val */
            if (expr->assign.target->kind == AST_MEMBER) {
                ASTNode *member = expr->assign.target;
                int base_vreg = lower_base_address(mod, fn, member->member.object, member->member.is_arrow);
                int offset = member->member.field ? member->member.field->offset : 0;
                int sz = member->member.field && member->member.field->type ? (int)member->member.field->type->size : 8;

                IRInst *store = make_inst(arena, IR_STORE);
                store->dest.vreg = base_vreg;
                store->dest.offset = offset;
                store->src1 = val;
                store->size = sz;
                ir_emit(fn, store);
                return val;
            }

            /* Assignment through dereference: *ptr = val */
            if (expr->assign.target->kind == AST_UNARY && expr->assign.target->unary.op == TOK_STAR) {
                IROperand ptr = lower_expr(mod, fn, expr->assign.target->unary.operand);
                IRInst *store = make_inst(arena, IR_STORE);
                store->dest.vreg = ptr.vreg;
                store->dest.offset = 0;
                store->src1 = val;
                ir_emit(fn, store);
                return val;
            }
            break;
        }

        case AST_CALL: {
            int total_args = expr->call.arg_count + (expr->call.is_method ? 1 : 0);
            IROperand *call_args = arena_alloc(arena, sizeof(IROperand) * (total_args > 0 ? total_args : 1));
            int arg_idx = 0;

            if (expr->call.is_method) {
                /* Pass object as first argument 'this' */
                int base_vreg = lower_base_address(mod, fn, expr->call.object, expr->call.is_arrow);
                call_args[arg_idx++].vreg = base_vreg;
            }

            TypeParam *param_type_iter = NULL;
            if (expr->call.callee_sym && expr->call.callee_sym->type &&
                expr->call.callee_sym->type->kind == TYPE_FUNC) {
                param_type_iter = expr->call.callee_sym->type->func.params;
            }

            for (int i = 0; i < expr->call.arg_count; i++) {
                Type *expected_t = param_type_iter ? param_type_iter->type : NULL;
                if (param_type_iter) param_type_iter = param_type_iter->next;

                if (expected_t && expected_t->kind == TYPE_REF) {
                    if (expr->call.args[i]->kind == AST_VAR_REF) {
                        Symbol *asym = expr->call.args[i]->var_ref.sym;
                        if (asym && (asym->is_ref || (asym->type && asym->type->kind == TYPE_REF))) {
                            /* Already a reference: load the pointer from stack slot */
                            int ptr_vreg = alloc_vreg(fn);
                            IRInst *load = make_inst(arena, IR_LOAD_STACK);
                            load->dest.vreg = ptr_vreg;
                            load->src1.offset = asym->stack_offset;
                            ir_emit(fn, load);
                            call_args[arg_idx++].vreg = ptr_vreg;
                        } else {
                            /* Stack variable: compute &(rbp + offset) */
                            int addr_vreg = alloc_vreg(fn);
                            IRInst *addr = make_inst(arena, IR_ADDR_STACK);
                            addr->dest.vreg = addr_vreg;
                            addr->src1.offset = asym ? asym->stack_offset : 0;
                            ir_emit(fn, addr);
                            call_args[arg_idx++].vreg = addr_vreg;
                        }
                    } else if (expr->call.args[i]->kind == AST_MEMBER) {
                        ASTNode *memb = expr->call.args[i];
                        int base_vreg = lower_base_address(mod, fn, memb->member.object, memb->member.is_arrow);
                        int offset = memb->member.field ? memb->member.field->offset : 0;
                        int mem_addr = alloc_vreg(fn);
                        IRInst *add = make_inst(arena, IR_ADD);
                        add->dest.vreg = mem_addr;
                        add->src1.vreg = base_vreg;
                        add->src2.imm = offset;
                        ir_emit(fn, add);
                        call_args[arg_idx++].vreg = mem_addr;
                    } else {
                        call_args[arg_idx++] = lower_expr(mod, fn, expr->call.args[i]);
                    }
                } else {
                    call_args[arg_idx++] = lower_expr(mod, fn, expr->call.args[i]);
                }
            }

            res.vreg = alloc_vreg(fn);
            IRInst *call = make_inst(arena, IR_CALL);
            call->dest = res;
            call->src1.label = expr->call.mangled_name ? expr->call.mangled_name : expr->call.name;
            call->call_args = call_args;
            call->call_arg_count = total_args;
            ir_emit(fn, call);
            return res;
        }

        case AST_NEW: {
            Type *t = expr->new_expr.target_type;
            size_t sz = t->size > 0 ? t->size : 8;

            /* Call malloc(sz) */
            IROperand sz_op;
            sz_op.vreg = alloc_vreg(fn);
            IRInst *imm = make_inst(arena, IR_IMM);
            imm->dest = sz_op;
            imm->src1.imm = (int64_t)sz;
            ir_emit(fn, imm);

            IROperand *margs = arena_alloc(arena, sizeof(IROperand));
            margs[0] = sz_op;

            int ptr_vreg = alloc_vreg(fn);
            IRInst *mcall = make_inst(arena, IR_CALL);
            mcall->dest.vreg = ptr_vreg;
            mcall->src1.label = str_intern("malloc");
            mcall->call_args = margs;
            mcall->call_arg_count = 1;
            ir_emit(fn, mcall);

            /* If class has constructor, invoke constructor(ptr, args) */
            if (t->kind == TYPE_CLASS) {
                TypeParam *cparams_head = NULL;
                TypeParam **cparams_tail = &cparams_head;
                for (int i = 0; i < expr->new_expr.arg_count; i++) {
                    TypeParam *tp = arena_alloc_zero(arena, sizeof(TypeParam));
                    tp->type = expr->new_expr.args[i]->type ? expr->new_expr.args[i]->type : g_type_int;
                    *cparams_tail = tp;
                    cparams_tail = &tp->next;
                }
                const char *ctor_name = mangle_function_name(arena, t->name, t->name, cparams_head, true, false);

                int cargs_count = 1 + expr->new_expr.arg_count;
                IROperand *cargs = arena_alloc(arena, sizeof(IROperand) * cargs_count);
                cargs[0].vreg = ptr_vreg;

                for (int i = 0; i < expr->new_expr.arg_count; i++) {
                    cargs[1 + i] = lower_expr(mod, fn, expr->new_expr.args[i]);
                }

                IRInst *ccall = make_inst(arena, IR_CALL);
                ccall->dest.vreg = alloc_vreg(fn);
                ccall->src1.label = ctor_name;
                ccall->call_args = cargs;
                ccall->call_arg_count = cargs_count;
                ir_emit(fn, ccall);
            }

            res.vreg = ptr_vreg;
            return res;
        }

        case AST_DELETE: {
            IROperand target = lower_expr(mod, fn, expr->delete_expr.target);

            Type *target_t = expr->delete_expr.target ? expr->delete_expr.target->type : NULL;
            if (target_t && target_t->kind == TYPE_PTR && target_t->ptr.base && target_t->ptr.base->kind == TYPE_CLASS) {
                Type *cls = target_t->ptr.base;
                const char *dtor_name = mangle_function_name(arena, cls->name, cls->name, NULL, false, true);
                IROperand *dargs = arena_alloc(arena, sizeof(IROperand));
                dargs[0] = target;
                IRInst *dcall = make_inst(arena, IR_CALL);
                dcall->dest.vreg = alloc_vreg(fn);
                dcall->src1.label = dtor_name;
                dcall->call_args = dargs;
                dcall->call_arg_count = 1;
                ir_emit(fn, dcall);
            }

            /* Call free(target) */
            IROperand *fargs = arena_alloc(arena, sizeof(IROperand));
            fargs[0] = target;

            IRInst *fcall = make_inst(arena, IR_CALL);
            fcall->dest.vreg = alloc_vreg(fn);
            fcall->src1.label = str_intern("free");
            fcall->call_args = fargs;
            fcall->call_arg_count = 1;
            ir_emit(fn, fcall);
            return res;
        }

        default:
            break;
    }

    return res;
}

static void lower_stmt(IRModule *mod, IRFunction *fn, ASTNode *stmt, const char *break_lbl, const char *cont_lbl) {
    Arena *arena = mod->arena;
    if (!stmt) return;

    switch (stmt->kind) {
        case AST_STMT_EXPR:
            lower_expr(mod, fn, stmt->stmt_expr.expr);
            break;

        case AST_STMT_BLOCK:
            for (int i = 0; i < stmt->block.count; i++) {
                lower_stmt(mod, fn, stmt->block.stmts[i], break_lbl, cont_lbl);
            }
            break;

        case AST_STMT_VAR_DECL: {
            Symbol *sym = stmt->var_decl.sym;
            if (!sym) break;

            if (sym->is_ref) {
                /* Reference: target address must be stored in stack pointer slot */
                if (stmt->var_decl.init) {
                    IROperand addr;
                    if (stmt->var_decl.init->kind == AST_VAR_REF) {
                        Symbol *target_sym = stmt->var_decl.init->var_ref.sym;
                        addr.vreg = alloc_vreg(fn);
                        IRInst *ainst = make_inst(arena, IR_ADDR_STACK);
                        ainst->dest = addr;
                        ainst->src1.offset = target_sym ? target_sym->stack_offset : 0;
                        ir_emit(fn, ainst);
                    } else {
                        addr = lower_expr(mod, fn, stmt->var_decl.init);
                    }

                    IRInst *store = make_inst(arena, IR_STORE_STACK);
                    store->dest.offset = sym->stack_offset;
                    store->src1 = addr;
                    ir_emit(fn, store);
                }
            } else if (stmt->var_decl.init) {
                /* Check if init is constructor call on local stack variable: Foo f(1, 2); */
                if (stmt->var_decl.init->kind == AST_NEW && stmt->var_decl.var_type->kind == TYPE_CLASS) {
                    Type *ct = stmt->var_decl.var_type;
                    int addr_vreg = alloc_vreg(fn);
                    IRInst *ainst = make_inst(arena, IR_ADDR_STACK);
                    ainst->dest.vreg = addr_vreg;
                    ainst->src1.offset = sym->stack_offset;
                    ir_emit(fn, ainst);

                    TypeParam *cparams_head = NULL;
                    TypeParam **cparams_tail = &cparams_head;
                    for (int i = 0; i < stmt->var_decl.init->new_expr.arg_count; i++) {
                        TypeParam *tp = arena_alloc_zero(arena, sizeof(TypeParam));
                        tp->type = stmt->var_decl.init->new_expr.args[i]->type ? stmt->var_decl.init->new_expr.args[i]->type : g_type_int;
                        *cparams_tail = tp;
                        cparams_tail = &tp->next;
                    }

                    const char *ctor_name = mangle_function_name(arena, ct->name, ct->name, cparams_head, true, false);
                    int cargs_count = 1 + stmt->var_decl.init->new_expr.arg_count;
                    IROperand *cargs = arena_alloc(arena, sizeof(IROperand) * cargs_count);
                    cargs[0].vreg = addr_vreg;

                    for (int i = 0; i < stmt->var_decl.init->new_expr.arg_count; i++) {
                        cargs[1 + i] = lower_expr(mod, fn, stmt->var_decl.init->new_expr.args[i]);
                    }

                    IRInst *ccall = make_inst(arena, IR_CALL);
                    ccall->dest.vreg = alloc_vreg(fn);
                    ccall->src1.label = ctor_name;
                    ccall->call_args = cargs;
                    ccall->call_arg_count = cargs_count;
                    ir_emit(fn, ccall);
                } else {
                    IROperand val = lower_expr(mod, fn, stmt->var_decl.init);
                    IRInst *store = make_inst(arena, IR_STORE_STACK);
                    store->dest.offset = sym->stack_offset;
                    store->src1 = val;
                    ir_emit(fn, store);
                }
            }
            break;
        }

        case AST_STMT_IF: {
            const char *else_lbl = stmt->if_stmt.else_branch ? gen_label(mod, "else") : NULL;
            const char *end_lbl = gen_label(mod, "endif");

            IROperand cond = lower_expr(mod, fn, stmt->if_stmt.cond);
            emit_jmp_if_zero(fn, arena, cond, else_lbl ? else_lbl : end_lbl);

            lower_stmt(mod, fn, stmt->if_stmt.then_branch, break_lbl, cont_lbl);

            if (else_lbl) {
                emit_jmp(fn, arena, end_lbl);
                emit_label(fn, arena, else_lbl);
                lower_stmt(mod, fn, stmt->if_stmt.else_branch, break_lbl, cont_lbl);
            }

            emit_label(fn, arena, end_lbl);
            break;
        }

        case AST_STMT_WHILE: {
            const char *loop_start = gen_label(mod, "while_start");
            const char *loop_end = gen_label(mod, "while_end");

            emit_label(fn, arena, loop_start);
            IROperand cond = lower_expr(mod, fn, stmt->while_stmt.cond);
            emit_jmp_if_zero(fn, arena, cond, loop_end);

            lower_stmt(mod, fn, stmt->while_stmt.body, loop_end, loop_start);
            emit_jmp(fn, arena, loop_start);

            emit_label(fn, arena, loop_end);
            break;
        }

        case AST_STMT_FOR: {
            const char *loop_start = gen_label(mod, "for_start");
            const char *loop_step = gen_label(mod, "for_step");
            const char *loop_end = gen_label(mod, "for_end");

            if (stmt->for_stmt.init) {
                lower_stmt(mod, fn, stmt->for_stmt.init, break_lbl, cont_lbl);
            }

            emit_label(fn, arena, loop_start);
            if (stmt->for_stmt.cond) {
                IROperand cond = lower_expr(mod, fn, stmt->for_stmt.cond);
                emit_jmp_if_zero(fn, arena, cond, loop_end);
            }

            lower_stmt(mod, fn, stmt->for_stmt.body, loop_end, loop_step);

            emit_label(fn, arena, loop_step);
            if (stmt->for_stmt.step) {
                lower_expr(mod, fn, stmt->for_stmt.step);
            }
            emit_jmp(fn, arena, loop_start);

            emit_label(fn, arena, loop_end);
            break;
        }

        case AST_STMT_RETURN: {
            IROperand val = {0};
            if (stmt->ret_stmt.expr) {
                val = lower_expr(mod, fn, stmt->ret_stmt.expr);
            }
            IRInst *ret = make_inst(arena, IR_RET);
            ret->src1 = val;
            ir_emit(fn, ret);
            break;
        }

        case AST_STMT_BREAK:
            if (break_lbl) emit_jmp(fn, arena, break_lbl);
            break;

        case AST_STMT_CONTINUE:
            if (cont_lbl) emit_jmp(fn, arena, cont_lbl);
            break;

        default:
            break;
    }
}

static void lower_function(IRModule *mod, ASTNode *fn_node) {
    if (!fn_node->func_decl.body) return;

    IRFunction *fn = ir_function_create(mod, fn_node->func_decl.name,
                                        fn_node->func_decl.mangled_name,
                                        fn_node->func_decl.stack_size);

    /* System V AMD64 ABI: function arguments are in RDI, RSI, RDX, RCX, R8, R9.
       Move parameters from registers into stack slots. */
    int reg_idx = 0;
    if (fn_node->func_decl.is_method) {
        /* Param 0 is 'this', stored at -8(%rbp) */
        IRInst *st = make_inst(mod->arena, IR_STORE_STACK);
        st->dest.offset = -8;
        st->src1.vreg = -1; /* Special flag indicating incoming arg register 0 */
        st->src1.imm = reg_idx++;
        ir_emit(fn, st);
    }

    for (int i = 0; i < fn_node->func_decl.param_count; i++) {
        ASTNode *pnode = fn_node->func_decl.params[i];
        Symbol *psym = pnode->var_decl.sym;
        if (psym) {
            IRInst *st = make_inst(mod->arena, IR_STORE_STACK);
            st->dest.offset = psym->stack_offset;
            st->src1.vreg = -1; /* Special flag indicating incoming arg register */
            st->src1.imm = reg_idx++;
            ir_emit(fn, st);
        }
    }

    lower_stmt(mod, fn, fn_node->func_decl.body, NULL, NULL);

    /* Ensure final return */
    if (!fn->last_inst || fn->last_inst->op != IR_RET) {
        IRInst *ret = make_inst(mod->arena, IR_RET);
        ret->src1.imm = 0;
        ir_emit(fn, ret);
    }
}

static void lower_decl(IRModule *mod, ASTNode *decl) {
    if (!decl) return;
    if (decl->kind == AST_DECL_FUNC) {
        lower_function(mod, decl);
    } else if (decl->kind == AST_DECL_CLASS) {
        for (int m = 0; m < decl->class_decl.method_count; m++) {
            lower_function(mod, decl->class_decl.methods[m]);
        }
    } else if (decl->kind == AST_DECL_NAMESPACE) {
        for (int d = 0; d < decl->ns_decl.count; d++) {
            lower_decl(mod, decl->ns_decl.decls[d]);
        }
    }
}

void ir_build_from_ast(IRModule *mod, ASTNode *program) {
    if (!program || program->kind != AST_PROGRAM) return;

    for (int i = 0; i < program->program.count; i++) {
        lower_decl(mod, program->program.decls[i]);
    }
}

static void print_ir_operand(IROperand op) {
    if (op.vreg > 0) {
        printf("v%d", op.vreg);
    } else {
        printf("%ld", (long)op.imm);
    }
}

void ir_dump(IRModule *mod) {
    printf("=== WINDS IR MODULE ===\n");
    for (IRFunction *fn = mod->functions; fn != NULL; fn = fn->next) {
        printf("function %s (mangled: %s, stack: %d):\n", fn->name, fn->mangled_name, fn->stack_size);
        for (IRInst *inst = fn->first_inst; inst != NULL; inst = inst->next) {
            switch (inst->op) {
                case IR_LABEL:
                    printf("%s:\n", inst->dest.label);
                    break;
                case IR_IMM:
                    printf("  v%d = %ld\n", inst->dest.vreg, (long)inst->src1.imm);
                    break;
                case IR_STR:
                    printf("  v%d = string(%s)\n", inst->dest.vreg, inst->src1.label);
                    break;
                case IR_MOV:
                    printf("  v%d = ", inst->dest.vreg);
                    print_ir_operand(inst->src1);
                    printf("\n");
                    break;
                case IR_LOAD_STACK:
                    printf("  v%d = [rbp %d]\n", inst->dest.vreg, inst->src1.offset);
                    break;
                case IR_STORE_STACK:
                    if (inst->src1.vreg == -1) {
                        printf("  [rbp %d] = arg_%ld\n", inst->dest.offset, (long)inst->src1.imm);
                    } else {
                        printf("  [rbp %d] = ", inst->dest.offset);
                        print_ir_operand(inst->src1);
                        printf("\n");
                    }
                    break;
                case IR_ADDR_STACK:
                    printf("  v%d = &(rbp %d)\n", inst->dest.vreg, inst->src1.offset);
                    break;
                case IR_LOAD:
                    printf("  v%d = [v%d + %d]\n", inst->dest.vreg, inst->src1.vreg, inst->src2.offset);
                    break;
                case IR_STORE:
                    printf("  [v%d + %d] = ", inst->dest.vreg, inst->dest.offset);
                    print_ir_operand(inst->src1);
                    printf("\n");
                    break;
                case IR_ADD:
                case IR_SUB:
                case IR_MUL:
                case IR_DIV:
                case IR_MOD:
                case IR_AND:
                case IR_OR:
                case IR_XOR:
                case IR_SHL:
                case IR_SHR:
                case IR_CMP_EQ:
                case IR_CMP_NE:
                case IR_CMP_LT:
                case IR_CMP_LE:
                case IR_CMP_GT:
                case IR_CMP_GE: {
                    const char *op_sym = "+";
                    switch (inst->op) {
                        case IR_ADD: op_sym = "+"; break;
                        case IR_SUB: op_sym = "-"; break;
                        case IR_MUL: op_sym = "*"; break;
                        case IR_DIV: op_sym = "/"; break;
                        case IR_MOD: op_sym = "%"; break;
                        case IR_AND: op_sym = "&"; break;
                        case IR_OR:  op_sym = "|"; break;
                        case IR_XOR: op_sym = "^"; break;
                        case IR_SHL: op_sym = "<<"; break;
                        case IR_SHR: op_sym = ">>"; break;
                        case IR_CMP_EQ: op_sym = "=="; break;
                        case IR_CMP_NE: op_sym = "!="; break;
                        case IR_CMP_LT: op_sym = "<"; break;
                        case IR_CMP_LE: op_sym = "<="; break;
                        case IR_CMP_GT: op_sym = ">"; break;
                        case IR_CMP_GE: op_sym = ">="; break;
                        default: break;
                    }
                    printf("  v%d = ", inst->dest.vreg);
                    print_ir_operand(inst->src1);
                    printf(" %s ", op_sym);
                    print_ir_operand(inst->src2);
                    printf("\n");
                    break;
                }
                case IR_JMP:
                    printf("  jmp %s\n", inst->dest.label);
                    break;
                case IR_JMP_IF_ZERO:
                    printf("  jz ");
                    print_ir_operand(inst->src1);
                    printf(" -> %s\n", inst->dest.label);
                    break;
                case IR_JMP_IF_NOT_ZERO:
                    printf("  jnz ");
                    print_ir_operand(inst->src1);
                    printf(" -> %s\n", inst->dest.label);
                    break;
                case IR_CALL:
                    printf("  v%d = call %s (args: %d)\n", inst->dest.vreg, inst->src1.label, inst->call_arg_count);
                    break;
                case IR_RET:
                    printf("  ret ");
                    print_ir_operand(inst->src1);
                    printf("\n");
                    break;
                default:
                    printf("  inst (%d)\n", inst->op);
                    break;
            }
        }
        printf("\n");
    }
}
