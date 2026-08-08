/* ============================================================
   IR phase: AST -> three-address code (TAC).

   Lowering rules (mini-c study subset):
     - expressions evaluate into temporaries t1..tN
     - control flow uses labels L1..LN plus goto / if_false
     - variable declarations emit a `declare` quad (type, name)
     - print/read/return map 1:1 onto their own quads

   The same walker feeds the optimizer (which rewrites the quad
   list) and the codegen phase (which turns quads into x86-64).
   ============================================================ */

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "strbuf.h"

DARRAY_DEFINE(IrQuad, IrQuadList)

typedef struct IrGen {
    IrQuadList quads;
    int next_index;
    int next_temp;
    int next_label;
} IrGen;

static char *new_temp(IrGen *g) {
    char buf[32];
    snprintf(buf, sizeof(buf), "t%d", g->next_temp++);
    return co1_strdup(buf);
}

static char *new_label(IrGen *g) {
    char buf[32];
    snprintf(buf, sizeof(buf), "L%d", g->next_label++);
    return co1_strdup(buf);
}

/* Emit a quad, taking ownership of the (possibly NULL) strings. */
static void emit(IrGen *g, const char *op, char *arg1, char *arg2, char *result) {
    IrQuad q;
    q.index = g->next_index++;
    q.op = op;
    q.arg1 = arg1;
    q.arg2 = arg2;
    q.result = result;
    IrQuadList_push(&g->quads, q);
}

/* Emit a quad copying string literals. */
static void emit_dup(IrGen *g, const char *op, const char *arg1,
                     const char *arg2, const char *result) {
    emit(g, op, arg1 ? co1_strdup(arg1) : NULL,
         arg2 ? co1_strdup(arg2) : NULL, result ? co1_strdup(result) : NULL);
}

static char *gen_expr(IrGen *g, const ASTNode *n);
static void gen_stmt(IrGen *g, const ASTNode *n);

/* ---------------------------------------------------------------- expressions */

static const char *binary_op_name(const char *src) {
    if (strcmp(src, "+") == 0)  return "add";
    if (strcmp(src, "-") == 0)  return "sub";
    if (strcmp(src, "*") == 0)  return "mul";
    if (strcmp(src, "/") == 0)  return "div";
    if (strcmp(src, "%") == 0)  return "mod";
    if (strcmp(src, "<") == 0)  return "lt";
    if (strcmp(src, "<=") == 0) return "le";
    if (strcmp(src, ">") == 0)  return "gt";
    if (strcmp(src, ">=") == 0) return "ge";
    if (strcmp(src, "==") == 0) return "eq";
    if (strcmp(src, "!=") == 0) return "ne";
    if (strcmp(src, "&&") == 0) return "and";
    if (strcmp(src, "||") == 0) return "or";
    return src;
}

/* Prefix ++/-- on a variable: updates the variable and yields its
   new value in a fresh temporary. */
static char *gen_prefix_incdec(IrGen *g, const char *name, const char *op) {
    char *t = new_temp(g);
    emit_dup(g, strcmp(op, "++") == 0 ? "add" : "sub", name, "1", name);
    emit_dup(g, "assign", name, NULL, t);
    return t;
}

/* Postfix x++ / x-- : yields the old value, then updates the variable. */
static char *gen_postfix_incdec(IrGen *g, const char *name, const char *op) {
    char *t = new_temp(g);
    emit_dup(g, "assign", name, NULL, t);
    emit_dup(g, strcmp(op, "++") == 0 ? "add" : "sub", name, "1", name);
    return t;
}

static char *gen_expr(IrGen *g, const ASTNode *n) {
    if (!n) {
        return co1_strdup("0");
    }

    if (strcmp(n->node_type, "IntLit") == 0) {
        const char *v = ast_get_prop(n, "value");
        return co1_strdup(v ? v : "0");
    }
    if (strcmp(n->node_type, "FloatLit") == 0) {
        const char *v = ast_get_prop(n, "value");
        return co1_strdup(v ? v : "0");
    }
    if (strcmp(n->node_type, "BoolLit") == 0) {
        const char *v = ast_get_prop(n, "value");
        return co1_strdup(v && strcmp(v, "1") == 0 ? "1" : "0");
    }
    if (strcmp(n->node_type, "Identifier") == 0) {
        const char *name = ast_get_prop(n, "name");
        return co1_strdup(name ? name : "?");
    }

    if (strcmp(n->node_type, "UnaryOp") == 0) {
        const char *op = ast_get_prop(n, "op");
        if (strcmp(op, "-") == 0) {
            char *operand = gen_expr(g, ast_child(n, 0));
            char *t = new_temp(g);
            emit(g, "neg", operand, NULL, t);
            return t;
        }
        if (strcmp(op, "!") == 0) {
            char *operand = gen_expr(g, ast_child(n, 0));
            char *t = new_temp(g);
            emit(g, "not", operand, NULL, t);
            return t;
        }
        if (strcmp(op, "++") == 0 || strcmp(op, "--") == 0) {
            const ASTNode *operand = ast_child(n, 0);
            const char *name = ast_get_prop(operand, "name");
            if (name) {
                return gen_prefix_incdec(g, name, op);
            }
            char *v = gen_expr(g, operand);
            char *t = new_temp(g);
            emit_dup(g, strcmp(op, "++") == 0 ? "add" : "sub", v, "1", v);
            emit(g, "assign", v, NULL, t);
            return t;
        }
    }

    if (strcmp(n->node_type, "IncDec") == 0) {
        const ASTNode *operand = ast_child(n, 0);
        const char *op = ast_get_prop(n, "op");
        const char *name = ast_get_prop(operand, "name");
        return gen_postfix_incdec(g, name ? name : "?", op);
    }

    if (strcmp(n->node_type, "BinaryOp") == 0) {
        const char *op = ast_get_prop(n, "op");
        char *l = gen_expr(g, ast_child(n, 0));
        char *r = gen_expr(g, ast_child(n, 1));
        char *t = new_temp(g);
        emit(g, binary_op_name(op), l, r, t);
        return t;
    }

    /* Unhandled node used as an expression: evaluate its single child if
       present so the pipeline can keep running on partial ASTs. */
    const ASTNode *only = ast_child(n, 0);
    if (only) {
        return gen_expr(g, only);
    }
    return co1_strdup("0");
}

/* ---------------------------------------------------------------- statements */

static void gen_block(IrGen *g, const ASTNode *n) {
    for (size_t i = 0; i < n->nchildren; i++) {
        gen_stmt(g, n->children[i]);
    }
}

static void gen_if(IrGen *g, const ASTNode *n) {
    char *cond = gen_expr(g, ast_child(n, 0));
    char *l_else = new_label(g);
    char *l_end = new_label(g);

    emit(g, "if_false", cond, NULL, l_else);
    gen_stmt(g, ast_child(n, 1));
    emit(g, "goto", NULL, NULL, l_end);
    emit(g, "label", NULL, NULL, l_else);
    if (n->nchildren >= 3) {
        gen_stmt(g, ast_child(n, 2));
    }
    emit(g, "label", NULL, NULL, l_end);
}

static void gen_while(IrGen *g, const ASTNode *n) {
    char *l_cond = new_label(g);
    char *l_end = new_label(g);

    emit(g, "label", NULL, NULL, l_cond);
    char *cond = gen_expr(g, ast_child(n, 0));
    emit(g, "if_false", cond, NULL, l_end);
    gen_stmt(g, ast_child(n, 1));
    emit(g, "goto", NULL, NULL, l_cond);
    emit(g, "label", NULL, NULL, l_end);
}

static void gen_for(IrGen *g, const ASTNode *n) {
    char *l_body = new_label(g);
    char *l_step = new_label(g);
    char *l_cond = new_label(g);
    char *l_end = new_label(g);

    /* init clause (declaration or assignment), optional */
    if (n->nchildren >= 1) {
        gen_stmt(g, ast_child(n, 0));
    }
    emit(g, "goto", NULL, NULL, l_cond);
    emit(g, "label", NULL, NULL, l_body);
    /* body */
    if (n->nchildren >= 4) {
        gen_stmt(g, ast_child(n, 3));
    }
    emit(g, "label", NULL, NULL, l_step);
    /* step clause (expression), optional */
    if (n->nchildren >= 3) {
        char *step = gen_expr(g, ast_child(n, 2));
        free(step);
    }
    emit(g, "goto", NULL, NULL, l_cond);
    emit(g, "label", NULL, NULL, l_cond);
    /* condition, optional (missing condition => infinite loop) */
    if (n->nchildren >= 2) {
        char *cond = gen_expr(g, ast_child(n, 1));
        emit(g, "if_false", cond, NULL, l_end);
    }
    emit(g, "goto", NULL, NULL, l_body);
    emit(g, "label", NULL, NULL, l_end);
}

static void gen_stmt(IrGen *g, const ASTNode *n) {
    if (!n) {
        return;
    }

    if (strcmp(n->node_type, "Program") == 0 ||
        strcmp(n->node_type, "Block") == 0) {
        gen_block(g, n);
        return;
    }

    if (strcmp(n->node_type, "VarDecl") == 0) {
        const char *type = ast_get_prop(n, "type_name");
        const char *name = ast_get_prop(n, "name");
        const char *is_const = ast_get_prop(n, "const");
        if (name) {
            char *res = co1_strdup(name);
            if (is_const && strcmp(is_const, "true") == 0) {
                emit_dup(g, "declare", type ? type : "int", "const", res);
            } else {
                emit_dup(g, "declare", type ? type : "int", NULL, res);
            }
        }
        if (n->nchildren >= 1) {
            char *init = gen_expr(g, ast_child(n, 0));
            emit(g, "assign", init, NULL, co1_strdup(name ? name : "?"));
        }
        return;
    }

    if (strcmp(n->node_type, "Assign") == 0) {
        const char *name = ast_get_prop(n, "name");
        char *value = gen_expr(g, ast_child(n, 0));
        emit(g, "assign", value, NULL, co1_strdup(name ? name : "?"));
        return;
    }

    if (strcmp(n->node_type, "Read") == 0) {
        const char *name = ast_get_prop(n, "name");
        emit_dup(g, "read", NULL, NULL, name ? name : "?");
        return;
    }

    if (strcmp(n->node_type, "Print") == 0) {
        const ASTNode *arg = ast_child(n, 0);
        if (arg && strcmp(arg->node_type, "StringLit") == 0 && arg->token) {
            emit_dup(g, "print_str", arg->token->lexeme, NULL, NULL);
        } else {
            char *v = gen_expr(g, arg);
            emit(g, "print", v, NULL, NULL);
        }
        return;
    }

    if (strcmp(n->node_type, "If") == 0) {
        gen_if(g, n);
        return;
    }

    if (strcmp(n->node_type, "While") == 0) {
        gen_while(g, n);
        return;
    }

    if (strcmp(n->node_type, "For") == 0) {
        gen_for(g, n);
        return;
    }

    if (strcmp(n->node_type, "Return") == 0) {
        if (n->nchildren >= 1) {
            char *v = gen_expr(g, ast_child(n, 0));
            emit(g, "return", v, NULL, NULL);
        } else {
            emit(g, "return", NULL, NULL, NULL);
        }
        return;
    }

    if (strcmp(n->node_type, "IncDec") == 0 ||
        strcmp(n->node_type, "UnaryOp") == 0) {
        /* statement form: evaluate and discard */
        char *v = gen_expr(g, n);
        free(v);
        return;
    }
}

/* ---------------------------------------------------------------- public API */

void ir_build(const ASTNode *program, IrQuadList *out) {
    IrGen g;
    g.quads = (IrQuadList){0};
    g.next_index = 1;
    g.next_temp = 1;
    g.next_label = 1;

    gen_stmt(&g, program);

    *out = g.quads;
}

void ir_list_free(IrQuadList *quads) {
    for (size_t i = 0; i < quads->len; i++) {
        free(quads->items[i].arg1);
        free(quads->items[i].arg2);
        free(quads->items[i].result);
    }
    IrQuadList_free(quads);
}

char *ir_quad_text(const IrQuad *q) {
    StrBuf sb;
    strbuf_init(&sb);

    if (strcmp(q->op, "label") == 0) {
        strbuf_append(&sb, q->result ? q->result : "?");
        strbuf_append_char(&sb, ':');
    } else if (strcmp(q->op, "goto") == 0) {
        strbuf_append(&sb, "goto ");
        strbuf_append(&sb, q->result ? q->result : "?");
    } else if (strcmp(q->op, "if_false") == 0) {
        strbuf_append(&sb, "if_false ");
        strbuf_append(&sb, q->arg1 ? q->arg1 : "?");
        strbuf_append(&sb, " goto ");
        strbuf_append(&sb, q->result ? q->result : "?");
    } else if (strcmp(q->op, "declare") == 0) {
        strbuf_append(&sb, "declare ");
        strbuf_append(&sb, q->arg1 ? q->arg1 : "int");
        if (q->arg2) {
            strbuf_append_char(&sb, ' ');
            strbuf_append(&sb, q->arg2);
        }
        strbuf_append_char(&sb, ' ');
        strbuf_append(&sb, q->result ? q->result : "?");
    } else if (q->result) {
        strbuf_append(&sb, q->result);
        strbuf_append(&sb, " = ");
        strbuf_append(&sb, q->arg1 ? q->arg1 : "?");
        if (q->arg2) {
            strbuf_append_char(&sb, ' ');
            strbuf_append(&sb, q->op);
            strbuf_append_char(&sb, ' ');
            strbuf_append(&sb, q->arg2);
        }
    } else if (q->arg1) {
        strbuf_append(&sb, q->op);
        strbuf_append_char(&sb, ' ');
        strbuf_append(&sb, q->arg1);
        if (q->arg2) {
            strbuf_append_char(&sb, ' ');
            strbuf_append(&sb, q->arg2);
        }
    } else {
        strbuf_append(&sb, q->op);
    }

    char *out = co1_strdup(strbuf_cstr(&sb));
    strbuf_free(&sb);
    return out;
}

char *ir_render(const IrQuadList *quads) {
    StrBuf sb;
    strbuf_init(&sb);
    for (size_t i = 0; i < quads->len; i++) {
        char idx[16];
        snprintf(idx, sizeof(idx), "%3d  ", quads->items[i].index);
        strbuf_append(&sb, idx);
        char *text = ir_quad_text(&quads->items[i]);
        strbuf_append(&sb, text);
        strbuf_append_char(&sb, '\n');
        free(text);
    }
    char *out = co1_strdup(strbuf_cstr(&sb));
    strbuf_free(&sb);
    return out;
}
