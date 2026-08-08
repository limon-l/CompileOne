/* ============================================================
   Native-language IR phase: AST -> three-address code (TAC).

   Lowers the C / C++ / Java AST produced by the multi-language
   front end (frontend.c) into the same linear TAC stream the
   mini-c walker produces, extended with function, array, object,
   string and I/O instructions:

     func / param / endfunc     function boundaries (name, ret type)
     arg / call                 argument list + call site
     alloc                      heap allocation (arrays / objects)
     arr_load / arr_store       array element access (int / float)
     member_load / member_store constant-offset object field access
     string                     materialise a string literal address
     print_item / print_float_item / print_str_item / print_str_addr
                                newline-free printf pieces
     println                    emit a newline
     read / read_str            numeric / string input

   Methods are lowered with an implicit `this` parameter, object
   fields become constant-offset slots of an 8-byte-per-field heap
   block, and every call is label + argument list so the x86-64
   backend can emit a simple stack-based calling convention.
   ============================================================ */

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "strbuf.h"

/* ---------------------------------------------------------------- class table */

typedef struct FieldInfo {
    char *name;             /* owned */
    char *type;             /* owned: "int" | "float" | "str" | class name */
} FieldInfo;

typedef struct MethodInfo {
    char *name;             /* owned */
    char *ret;              /* owned: "int" | "float" | "void" | "str" | ... */
    int is_static;
    int is_constructor;
} MethodInfo;

typedef struct ClassInfo {
    char *name;             /* owned */
    FieldInfo *fields;
    size_t nfields;
    size_t cap_fields;
    MethodInfo *methods;
    size_t nmethods;
    size_t cap_methods;
} ClassInfo;

typedef struct ClassTable {
    ClassInfo *items;
    size_t len;
    size_t cap;
} ClassTable;

static ClassInfo *table_find_class(ClassTable *t, const char *name) {
    for (size_t i = 0; i < t->len; i++) {
        if (strcmp(t->items[i].name, name) == 0) {
            return &t->items[i];
        }
    }
    return NULL;
}

static FieldInfo *table_find_field(ClassInfo *c, const char *name) {
    for (size_t i = 0; i < c->nfields; i++) {
        if (strcmp(c->fields[i].name, name) == 0) {
            return &c->fields[i];
        }
    }
    return NULL;
}

static MethodInfo *table_find_method(ClassInfo *c, const char *name) {
    for (size_t i = 0; i < c->nmethods; i++) {
        if (strcmp(c->methods[i].name, name) == 0) {
            return &c->methods[i];
        }
    }
    return NULL;
}

/* ---------------------------------------------------------------- scopes */

typedef struct NameType {
    char *name;             /* owned */
    char *type;             /* owned */
} NameType;

typedef struct TypeScope {
    NameType *items;
    size_t len;
    size_t cap;
} TypeScope;

static void scope_push(TypeScope *s, const char *name, const char *type) {
    if (s->len == s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 16;
        s->items = (NameType *)realloc(s->items, nc * sizeof(NameType));
        s->cap = nc;
    }
    s->items[s->len].name = co1_strdup(name);
    s->items[s->len].type = co1_strdup(type);
    s->len++;
}

static const char *scope_get(const TypeScope *s, const char *name) {
    for (size_t i = s->len; i > 0; i--) {
        if (strcmp(s->items[i - 1].name, name) == 0) {
            return s->items[i - 1].type;
        }
    }
    return NULL;
}

static void scope_pop_to(TypeScope *s, size_t mark) {
    while (s->len > mark) {
        s->len--;
        free(s->items[s->len].name);
        free(s->items[s->len].type);
    }
}

/* loop contexts for break/continue targets */
typedef struct LoopCtx {
    char *break_label;
    char *continue_label;
} LoopCtx;

typedef struct LoopStack {
    LoopCtx *items;
    size_t len;
    size_t cap;
} LoopStack;

/* ---------------------------------------------------------------- codegen context */

typedef struct IrGen {
    IrQuadList quads;
    int next_index;
    int next_temp;
    int next_label;
    ClassTable classes;
    TypeScope scope;
    LoopStack loops;
    const char *cur_class;  /* class being generated (NULL at top level) */
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

static void emit_dup(IrGen *g, const char *op, const char *arg1,
                     const char *arg2, const char *result) {
    emit(g, op, arg1 ? co1_strdup(arg1) : NULL,
         arg2 ? co1_strdup(arg2) : NULL, result ? co1_strdup(result) : NULL);
}

/* Allocate a fresh temporary and remember its value type. */
static char *temp_typed(IrGen *g, const char *type) {
    char *t = new_temp(g);
    scope_push(&g->scope, t, type ? type : "int");
    return t;
}

/* ---------------------------------------------------------------- type helpers */

static int type_is_float(const char *t) {
    return t && strcmp(t, "float") == 0;
}

static int type_is_str(const char *t) {
    return t && (strcmp(t, "str") == 0);
}

static int type_is_arr(const char *t) {
    return t && strchr(t, '[') != NULL;
}

static const char *type_elem(const char *t) {
    static char buf[32];
    if (!t) {
        return "int";
    }
    const char *b = strchr(t, '[');
    if (!b) {
        return t;
    }
    size_t n = (size_t)(b - t);
    if (n >= sizeof(buf)) {
        n = sizeof(buf) - 1;
    }
    memcpy(buf, t, n);
    buf[n] = '\0';
    return buf;
}

static int base_is_float_type(const char *base) {
    return strcmp(base, "float") == 0 || strcmp(base, "double") == 0;
}

static int base_is_int_type(const char *base) {
    return strcmp(base, "int") == 0 || strcmp(base, "long") == 0 ||
           strcmp(base, "short") == 0 || strcmp(base, "char") == 0 ||
           strcmp(base, "bool") == 0 || strcmp(base, "boolean") == 0;
}

static int base_is_str_type(const char *base) {
    return strcmp(base, "String") == 0 || strcmp(base, "string") == 0;
}

/* Normalise a declared type into the IR type vocabulary. The buffer is
   only used for array types. */
static const char *normalize_type(const char *base, int is_pointer, int is_array) {
    static char buf[64];
    if (is_array) {
        if (base_is_str_type(base) || strcmp(base, "char") == 0) {
            snprintf(buf, sizeof(buf), "char[]");
            return buf;
        }
        if (base_is_float_type(base)) {
            snprintf(buf, sizeof(buf), "float[]");
            return buf;
        }
        snprintf(buf, sizeof(buf), "int[]");
        return buf;
    }
    if (is_pointer) {
        if (strcmp(base, "char") == 0) {
            return "str";
        }
        return "int[]"; /* opaque pointer treated as an int array */
    }
    if (base_is_str_type(base)) {
        return "str";
    }
    if (base_is_int_type(base)) {
        return "int";
    }
    if (base_is_float_type(base)) {
        return "float";
    }
    if (strcmp(base, "void") == 0) {
        return "void";
    }
    return base; /* class name */
}

static int type_needs_8byte(const char *t) {
    if (!t) {
        return 0;
    }
    return type_is_arr(t) || type_is_str(t) || strcmp(t, "obj") == 0;
}

/* ---------------------------------------------------------------- forward decls */

static char *gen_expr(IrGen *g, const ASTNode *n);
static void gen_stmt(IrGen *g, const ASTNode *n);

/* ---------------------------------------------------------------- helpers */

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

static const char *assign_binop(const char *op) {
    if (strcmp(op, "+=") == 0)  return "add";
    if (strcmp(op, "-=") == 0)  return "sub";
    if (strcmp(op, "*=") == 0)  return "mul";
    if (strcmp(op, "/=") == 0)  return "div";
    if (strcmp(op, "%=") == 0)  return "mod";
    return NULL;
}

/* Method / function label for the call table and the function itself. */
static char *func_label(const char *class_name, const char *name) {
    if (class_name && *class_name) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s__%s", class_name, name);
        return co1_strdup(buf);
    }
    return co1_strdup(name ? name : "?");
}

/* Resolve the class of `name` in scope; falls back to the type as-is. */
static const char *class_of(IrGen *g, const char *name) {
    const char *t = scope_get(&g->scope, name);
    if (!t) {
        return NULL;
    }
    if (table_find_class(&g->classes, t)) {
        return t;
    }
    return NULL;
}

/* ---------------------------------------------------------------- stores */

/* Store `value` into the lvalue `target` (Identifier / Index /
   MemberAccess). `value` is an owned string the caller relinquishes. */
static void gen_store(IrGen *g, const ASTNode *target, char *value) {
    if (!target) {
        free(value);
        return;
    }

    if (strcmp(target->node_type, "Identifier") == 0) {
        const char *name = ast_get_prop(target, "name");
        emit(g, "assign", value, NULL, co1_strdup(name ? name : "?"));
        return;
    }

    if (strcmp(target->node_type, "Index") == 0) {
        const ASTNode *base = ast_child(target, 0);
        const ASTNode *idx = ast_child(target, 1);
        const char *btype = base ? scope_get(&g->scope, ast_get_prop(base, "name")) : NULL;
        int is_float = base_is_float_type(type_elem(btype));
        char *b = gen_expr(g, base);
        char *i = gen_expr(g, idx);
        emit(g, is_float ? "arr_storef" : "arr_store", b, i, value);
        return;
    }

    if (strcmp(target->node_type, "MemberAccess") == 0) {
        const char *member = ast_get_prop(target, "member");
        const ASTNode *base = ast_child(target, 0);
        const char *base_name = base ? ast_get_prop(base, "name") : NULL;
        const char *cls = class_of(g, base_name);
        ClassInfo *ci = cls ? table_find_class(&g->classes, cls) : NULL;
        char offset[16];
        const char *ftype = "int";
        if (ci) {
            FieldInfo *f = table_find_field(ci, member);
            if (f) {
                size_t off = (size_t)(f - ci->fields);
                snprintf(offset, sizeof(offset), "%lu", (unsigned long)off);
                ftype = f->type;
            } else {
                snprintf(offset, sizeof(offset), "0");
            }
        } else {
            snprintf(offset, sizeof(offset), "0");
        }
        char *b = gen_expr(g, base);
        const char *op = type_is_float(ftype)   ? "member_storef"
                       : type_needs_8byte(ftype) ? "member_storeq"
                                                 : "member_store";
        emit(g, op, b, co1_strdup(offset), value);
        return;
    }

    free(value); /* unsupported lvalue: drop the computed value */
}

/* ---------------------------------------------------------------- expressions */

/* Look up a method's return type in the class table. */
static const char *method_ret(IrGen *g, const char *cls, const char *member) {
    ClassInfo *ci = cls ? table_find_class(&g->classes, cls) : NULL;
    if (ci) {
        MethodInfo *m = table_find_method(ci, member);
        if (m) {
            return m->ret;
        }
    }
    return "int";
}

/* Gen a call expression node. The callee is children[0]; arguments follow. */
static char *gen_call(IrGen *g, const ASTNode *n) {
    const ASTNode *callee = ast_child(n, 0);
    size_t arg_start = 1;
    char *base_value = NULL;
    const char *label = NULL;
    const char *ret = "int";

    if (callee && strcmp(callee->node_type, "MemberAccess") == 0) {
        const char *member = ast_get_prop(callee, "member");
        const ASTNode *base = ast_child(callee, 0);
        const char *base_name = base ? ast_get_prop(base, "name") : NULL;
        const char *cls = class_of(g, base_name);
        if (base_name && !cls && table_find_class(&g->classes, base_name)) {
            /* static call: ClassName.method(...) — no receiver */
            label = func_label(base_name, member);
            ret = method_ret(g, base_name, member);
        } else {
            /* instance call: receiver is the implicit `this` argument */
            if (!cls) {
                /* unknown receiver class: try to find any class with the
                   method so the pipeline keeps running */
                for (size_t i = 0; i < g->classes.len; i++) {
                    if (table_find_method(&g->classes.items[i], member)) {
                        cls = g->classes.items[i].name;
                        break;
                    }
                }
            }
            label = func_label(cls, member);
            ret = method_ret(g, cls, member);
            base_value = gen_expr(g, base);
        }
    } else if (callee && strcmp(callee->node_type, "Identifier") == 0) {
        const char *name = ast_get_prop(callee, "name");
        /* The front end collapses `ClassName.method` calls down to a bare
           identifier; re-associate them with the owning class so the call
           reaches the right method label. Free C functions have no class. */
        const char *mcls = NULL;
        for (size_t i = 0; i < g->classes.len; i++) {
            MethodInfo *m = table_find_method(&g->classes.items[i], name);
            if (m) {
                mcls = g->classes.items[i].name;
                ret = m->ret;
                break;
            }
        }
        label = func_label(mcls, name);
    }

    /* collect arguments into a flat list so the backend can push them
       right-to-left */
    size_t total = 0;
    char **vals = NULL;
    size_t cap = 0;
    if (base_value) {
        vals = (char **)malloc(sizeof(char *));
        vals[0] = base_value;
        total = 1;
        cap = 1;
    }
    for (size_t i = arg_start; i < n->nchildren; i++) {
        char *v = gen_expr(g, n->children[i]);
        if (total == cap) {
            cap = cap ? cap * 2 : 4;
            vals = (char **)realloc(vals, cap * sizeof(char *));
        }
        vals[total++] = v;
    }
    for (size_t i = 0; i < total; i++) {
        char idx[16];
        snprintf(idx, sizeof(idx), "%lu", (unsigned long)i);
        emit(g, "arg", vals[i], co1_strdup(idx), NULL);
    }
    free(vals);

    char nargs[16];
    snprintf(nargs, sizeof(nargs), "%lu", (unsigned long)total);
    char *t = temp_typed(g, ret);
    emit(g, "call", co1_strdup(label), co1_strdup(nargs), co1_strdup(t));
    return t;
}

static char *gen_new_object(IrGen *g, const ASTNode *n) {
    const char *cls = ast_get_prop(n, "class_name");
    ClassInfo *ci = table_find_class(&g->classes, cls);
    char count[16];
    snprintf(count, sizeof(count), "%lu", (unsigned long)(ci ? ci->nfields : 0));
    char *t = temp_typed(g, cls);
    emit(g, "alloc", co1_strdup(cls), co1_strdup(count), co1_strdup(t));

    /* constructor call: new Person("Alice", 30) -> alloc, call ctor(this, args) */
    size_t total = 1; /* this */
    size_t cap = 1 + n->nchildren;
    char **vals = (char **)malloc(cap * sizeof(char *));
    vals[0] = co1_strdup(t);
    for (size_t i = 0; i < n->nchildren; i++) {
        vals[total++] = gen_expr(g, n->children[i]);
    }
    for (size_t i = 0; i < total; i++) {
        char idx[16];
        snprintf(idx, sizeof(idx), "%lu", (unsigned long)i);
        emit(g, "arg", vals[i], co1_strdup(idx), NULL);
    }
    free(vals);
    char nargs[16];
    snprintf(nargs, sizeof(nargs), "%lu", (unsigned long)total);
    char *ctor = func_label(cls, cls);
    emit(g, "call", ctor, co1_strdup(nargs), NULL);
    return co1_strdup(t);
}

static char *gen_new_array(IrGen *g, const ASTNode *n) {
    const char *elem = ast_get_prop(n, "elem_type");
    const char *etype = normalize_type(elem, 0, 0);
    char *size = gen_expr(g, ast_child(n, 0));
    char *t = temp_typed(g, "int[]");
    emit(g, "alloc", co1_strdup(etype), size, co1_strdup(t));
    return co1_strdup(t);
}

static char *gen_init_list(IrGen *g, const ASTNode *n, const char *elem_type) {
    char count[16];
    snprintf(count, sizeof(count), "%lu", (unsigned long)n->nchildren);
    char *t = temp_typed(g, "int[]");
    emit(g, "alloc", co1_strdup(elem_type), co1_strdup(count), co1_strdup(t));
    for (size_t i = 0; i < n->nchildren; i++) {
        char idx[16];
        snprintf(idx, sizeof(idx), "%lu", (unsigned long)i);
        char *v = gen_expr(g, n->children[i]);
        emit(g, "arr_store", co1_strdup(t), co1_strdup(idx), v);
    }
    return co1_strdup(t);
}

static char *gen_binary(IrGen *g, const ASTNode *n) {
    const char *op = ast_get_prop(n, "op");
    char *l = gen_expr(g, ast_child(n, 0));
    char *r = gen_expr(g, ast_child(n, 1));

    /* Java string concatenation in a plain expression: keep the pieces in
       a single "concat" quad (the backend may lower it to nothing; the
       println flattening in the front end covers the common case) */
    const char *lt = scope_get(&g->scope, l);
    const char *rt = scope_get(&g->scope, r);
    if (strcmp(op, "+") == 0 && (type_is_str(lt) || type_is_str(rt))) {
        char *t = temp_typed(g, "str");
        emit(g, "concat", l, r, t);
        return t;
    }

    int is_float = type_is_float(lt) || type_is_float(rt);
    char *t = temp_typed(g, is_float ? "float" : "int");
    emit(g, binary_op_name(op), l, r, co1_strdup(t));
    return t;
}

static char *gen_unary(IrGen *g, const ASTNode *n) {
    const char *op = ast_get_prop(n, "op");
    const ASTNode *operand = ast_child(n, 0);

    if (strcmp(op, "++") == 0 || strcmp(op, "--") == 0) {
        if (operand && strcmp(operand->node_type, "Identifier") == 0) {
            const char *name = ast_get_prop(operand, "name");
            char *t = temp_typed(g, "int");
            emit_dup(g, strcmp(op, "++") == 0 ? "add" : "sub", name, "1", name);
            emit_dup(g, "assign", name, NULL, t);
            return t;
        }
        char *v = gen_expr(g, operand);
        char *t = temp_typed(g, "int");
        emit(g, strcmp(op, "++") == 0 ? "add" : "sub", v, co1_strdup("1"), co1_strdup(t));
        return t;
    }

    if (strcmp(op, "-") == 0) {
        char *v = gen_expr(g, operand);
        const char *vt = scope_get(&g->scope, v);
        char *t = temp_typed(g, type_is_float(vt) ? "float" : "int");
        emit(g, "neg", v, NULL, co1_strdup(t));
        return t;
    }

    if (strcmp(op, "!") == 0) {
        char *v = gen_expr(g, operand);
        char *t = temp_typed(g, "int");
        emit(g, "not", v, NULL, co1_strdup(t));
        return t;
    }

    return gen_expr(g, operand);
}

static char *gen_index(IrGen *g, const ASTNode *n) {
    const ASTNode *base = ast_child(n, 0);
    const ASTNode *idx = ast_child(n, 1);
    const char *bname = base ? ast_get_prop(base, "name") : NULL;
    const char *btype = bname ? scope_get(&g->scope, bname) : NULL;
    int is_float = base_is_float_type(type_elem(btype));
    char *b = gen_expr(g, base);
    char *i = gen_expr(g, idx);
    char *t = temp_typed(g, is_float ? "float" : "int");
    emit(g, is_float ? "arr_loadf" : "arr_load", b, i, co1_strdup(t));
    return t;
}

static char *gen_member(IrGen *g, const ASTNode *n) {
    const char *member = ast_get_prop(n, "member");
    const ASTNode *base = ast_child(n, 0);
    const char *base_name = base ? ast_get_prop(base, "name") : NULL;
    const char *cls = class_of(g, base_name);
    ClassInfo *ci = cls ? table_find_class(&g->classes, cls) : NULL;
    char offset[16];
    const char *ftype = "int";
    if (ci) {
        FieldInfo *f = table_find_field(ci, member);
        if (f) {
            size_t off = (size_t)(f - ci->fields);
            snprintf(offset, sizeof(offset), "%lu", (unsigned long)off);
            ftype = f->type;
        } else {
            snprintf(offset, sizeof(offset), "0");
        }
    } else {
        snprintf(offset, sizeof(offset), "0");
    }
    char *b = gen_expr(g, base);
    char *t = temp_typed(g, ftype);
    const char *op = type_is_float(ftype)   ? "member_loadf"
                   : type_needs_8byte(ftype) ? "member_loadq"
                                             : "member_load";
    emit(g, op, b, co1_strdup(offset), co1_strdup(t));
    return t;
}

static char *gen_expr(IrGen *g, const ASTNode *n) {
    if (!n) {
        return co1_strdup("0");
    }

    if (strcmp(n->node_type, "IntLit") == 0) {
        return co1_strdup(ast_get_prop(n, "value") ? ast_get_prop(n, "value") : "0");
    }
    if (strcmp(n->node_type, "FloatLit") == 0) {
        return co1_strdup(ast_get_prop(n, "value") ? ast_get_prop(n, "value") : "0");
    }
    if (strcmp(n->node_type, "CharLit") == 0) {
        const char *v = ast_get_prop(n, "value");
        /* value is a quoted single char like "'A'"; render its numeric code */
        if (v && v[0] == '\'' && v[2] == '\'' && v[1]) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", (unsigned char)v[1]);
            return co1_strdup(buf);
        }
        return co1_strdup(v ? v : "0");
    }
    if (strcmp(n->node_type, "BoolLit") == 0) {
        const char *v = ast_get_prop(n, "value");
        return co1_strdup(v && strcmp(v, "1") == 0 ? "1" : "0");
    }
    if (strcmp(n->node_type, "NullLit") == 0) {
        return co1_strdup("0");
    }
    if (strcmp(n->node_type, "Identifier") == 0) {
        return co1_strdup(ast_get_prop(n, "name") ? ast_get_prop(n, "name") : "?");
    }
    if (strcmp(n->node_type, "StringLit") == 0) {
        const char *lit = ast_get_prop(n, "value");
        if (!lit) {
            lit = n->token ? n->token->lexeme : "\"\"";
        }
        char *t = temp_typed(g, "str");
        emit(g, "string", co1_strdup(lit), NULL, co1_strdup(t));
        return t;
    }

    if (strcmp(n->node_type, "BinaryOp") == 0) {
        return gen_binary(g, n);
    }
    if (strcmp(n->node_type, "UnaryOp") == 0) {
        return gen_unary(g, n);
    }
    if (strcmp(n->node_type, "IncDec") == 0) {
        const ASTNode *operand = ast_child(n, 0);
        const char *name = operand ? ast_get_prop(operand, "name") : NULL;
        const char *op = ast_get_prop(n, "op");
        char *t = temp_typed(g, "int");
        emit_dup(g, "assign", name, NULL, t); /* old value */
        emit_dup(g, strcmp(op, "++") == 0 ? "add" : "sub", name, "1", name);
        return t;
    }
    if (strcmp(n->node_type, "Index") == 0) {
        return gen_index(g, n);
    }
    if (strcmp(n->node_type, "MemberAccess") == 0) {
        return gen_member(g, n);
    }
    if (strcmp(n->node_type, "Call") == 0) {
        return gen_call(g, n);
    }
    if (strcmp(n->node_type, "NewObject") == 0) {
        return gen_new_object(g, n);
    }
    if (strcmp(n->node_type, "NewArray") == 0) {
        return gen_new_array(g, n);
    }
    if (strcmp(n->node_type, "InitList") == 0) {
        return gen_init_list(g, n, "int");
    }
    if (strcmp(n->node_type, "AssignLvalue") == 0) {
        /* assignment used as an expression: store, then yield the value */
        const ASTNode *target = ast_child(n, 0);
        char *v = gen_expr(g, ast_child(n, 1));
        char *copy = co1_strdup(v);
        gen_store(g, target, v);
        return copy;
    }

    /* statement-like nodes can reach the expression walker when they are
       wrapped in ExprStmt; route them through the statement walker so
       their side effects are preserved */
    if (strcmp(n->node_type, "Assign") == 0 ||
        strcmp(n->node_type, "Println") == 0 ||
        strcmp(n->node_type, "PrintNoLn") == 0 ||
        strcmp(n->node_type, "Printf") == 0 ||
        strcmp(n->node_type, "CoutStream") == 0 ||
        strcmp(n->node_type, "Read") == 0 ||
        strcmp(n->node_type, "Return") == 0) {
        gen_stmt(g, n);
        return co1_strdup("0");
    }

    const ASTNode *only = ast_child(n, 0);
    if (only) {
        return gen_expr(g, only);
    }
    return co1_strdup("0");
}

/* ---------------------------------------------------------------- print / input */

static char *dup_quoted(const char *text, size_t n) {
    StrBuf sb;
    strbuf_init(&sb);
    strbuf_append_char(&sb, '"');
    strbuf_append_len(&sb, text, n);
    strbuf_append_char(&sb, '"');
    char *out = co1_strdup(strbuf_cstr(&sb));
    strbuf_free(&sb);
    return out;
}

/* Emit one flattened print item (no trailing newline). */
static void emit_print_item(IrGen *g, const ASTNode *item) {
    if (!item) {
        return;
    }
    if (strcmp(item->node_type, "StringLit") == 0) {
        const char *lit = ast_get_prop(item, "value");
        if (!lit) {
            lit = item->token ? item->token->lexeme : "\"\"";
        }
        emit_dup(g, "print_str_item", lit, NULL, NULL);
        return;
    }
    char *v = gen_expr(g, item);
    const char *vt = scope_get(&g->scope, v);
    if (type_is_str(vt)) {
        emit(g, "print_str_addr", v, NULL, NULL);
    } else if (type_is_float(vt)) {
        emit(g, "print_float_item", v, NULL, NULL);
    } else {
        emit(g, "print_item", v, NULL, NULL);
    }
}

static void gen_print_items(IrGen *g, const ASTNode *n, int add_newline) {
    for (size_t i = 0; i < n->nchildren; i++) {
        emit_print_item(g, n->children[i]);
    }
    if (add_newline) {
        emit(g, "println", NULL, NULL, NULL);
    }
}

/* Lower a C printf call, splitting the format string into literal
   segments and conversion items. */
static void gen_printf(IrGen *g, const ASTNode *n) {
    const char *fmt = ast_get_prop(n, "format");
    if (!fmt) {
        return;
    }
    size_t flen = strlen(fmt);
    const char *s = fmt;
    if (flen >= 2 && fmt[0] == '"' && fmt[flen - 1] == '"') {
        s = fmt + 1;
        flen -= 2;
    }

    StrBuf text;
    strbuf_init(&text);
    size_t arg_idx = 0;

    for (size_t i = 0; i < flen; i++) {
        char c = s[i];
        if (c == '\\' && i + 1 < flen) { /* keep escape sequences verbatim */
            strbuf_append_char(&text, c);
            strbuf_append_char(&text, s[++i]);
            continue;
        }
        if (c != '%' || i + 1 >= flen) {
            strbuf_append_char(&text, c);
            continue;
        }
        char conv = s[i + 1];
        if (conv == '%') {
            strbuf_append_char(&text, '%');
            i++;
            continue;
        }
        /* flush pending literal text */
        if (strbuf_len(&text) > 0) {
            char *q = dup_quoted(strbuf_cstr(&text), strbuf_len(&text));
            emit(g, "print_str_item", q, NULL, NULL);
            strbuf_clear(&text);
        }
        /* skip conversion flags/width/precision */
        size_t j = i + 1;
        while (j < flen && strchr("-+ #0*.123456789hlLzj", s[j])) {
            j++;
        }
        if (j < flen) {
            conv = s[j];
        }
        const ASTNode *arg = (arg_idx < n->nchildren) ? n->children[arg_idx++] : NULL;
        if (conv == 's') {
            if (arg && strcmp(arg->node_type, "StringLit") == 0) {
                const char *lit = ast_get_prop(arg, "value");
                if (!lit) {
                    lit = arg->token ? arg->token->lexeme : "\"\"";
                }
                emit_dup(g, "print_str_item", lit, NULL, NULL);
            } else if (arg) {
                char *v = gen_expr(g, arg);
                emit(g, "print_str_addr", v, NULL, NULL);
            }
        } else {
            char *v = arg ? gen_expr(g, arg) : co1_strdup("0");
            const char *vt = scope_get(&g->scope, v);
            if (type_is_float(vt)) {
                emit(g, "print_float_item", v, NULL, NULL);
            } else {
                emit(g, "print_item", v, NULL, NULL);
            }
        }
        i = j;
    }
    if (strbuf_len(&text) > 0) {
        char *q = dup_quoted(strbuf_cstr(&text), strbuf_len(&text));
        emit(g, "print_str_item", q, NULL, NULL);
    }
    strbuf_free(&text);
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
    LoopCtx *top = g->loops.len ? &g->loops.items[g->loops.len - 1] : NULL;
    (void)top;

    if (g->loops.len == g->loops.cap) {
        size_t nc = g->loops.cap ? g->loops.cap * 2 : 8;
        g->loops.items = (LoopCtx *)realloc(g->loops.items, nc * sizeof(LoopCtx));
        g->loops.cap = nc;
    }
    g->loops.items[g->loops.len].break_label = co1_strdup(l_end);
    g->loops.items[g->loops.len].continue_label = co1_strdup(l_cond);
    g->loops.len++;

    emit(g, "label", NULL, NULL, l_cond);
    char *cond = gen_expr(g, ast_child(n, 0));
    emit(g, "if_false", cond, NULL, l_end);
    gen_stmt(g, ast_child(n, 1));
    emit(g, "goto", NULL, NULL, l_cond);
    emit(g, "label", NULL, NULL, l_end);

    g->loops.len--;
    free(g->loops.items[g->loops.len].break_label);
    free(g->loops.items[g->loops.len].continue_label);
}

static void gen_do_while(IrGen *g, const ASTNode *n) {
    char *l_top = new_label(g);
    char *l_cond = new_label(g);
    char *l_end = new_label(g);

    if (g->loops.len == g->loops.cap) {
        size_t nc = g->loops.cap ? g->loops.cap * 2 : 8;
        g->loops.items = (LoopCtx *)realloc(g->loops.items, nc * sizeof(LoopCtx));
        g->loops.cap = nc;
    }
    g->loops.items[g->loops.len].break_label = co1_strdup(l_end);
    g->loops.items[g->loops.len].continue_label = co1_strdup(l_cond);
    g->loops.len++;

    emit(g, "label", NULL, NULL, l_top);
    gen_stmt(g, ast_child(n, 0));       /* body */
    emit(g, "label", NULL, NULL, l_cond);
    char *cond = gen_expr(g, ast_child(n, 1));
    emit(g, "if_false", cond, NULL, l_end);
    emit(g, "goto", NULL, NULL, l_top);
    emit(g, "label", NULL, NULL, l_end);

    g->loops.len--;
    free(g->loops.items[g->loops.len].break_label);
    free(g->loops.items[g->loops.len].continue_label);
}

static void gen_for(IrGen *g, const ASTNode *n) {
    char *l_body = new_label(g);
    char *l_step = new_label(g);
    char *l_cond = new_label(g);
    char *l_end = new_label(g);

    if (g->loops.len == g->loops.cap) {
        size_t nc = g->loops.cap ? g->loops.cap * 2 : 8;
        g->loops.items = (LoopCtx *)realloc(g->loops.items, nc * sizeof(LoopCtx));
        g->loops.cap = nc;
    }
    g->loops.items[g->loops.len].break_label = co1_strdup(l_end);
    g->loops.items[g->loops.len].continue_label = co1_strdup(l_step);
    g->loops.len++;

    if (n->nchildren >= 1) {
        gen_stmt(g, ast_child(n, 0));   /* init */
    }
    emit(g, "goto", NULL, NULL, l_cond);
    emit(g, "label", NULL, NULL, l_body);
    if (n->nchildren >= 4) {
        gen_stmt(g, ast_child(n, 3));   /* body */
    }
    emit(g, "label", NULL, NULL, l_step);
    if (n->nchildren >= 3) {
        char *step = gen_expr(g, ast_child(n, 2));
        free(step);
    }
    emit(g, "goto", NULL, NULL, l_cond);
    emit(g, "label", NULL, NULL, l_cond);
    if (n->nchildren >= 2) {
        char *cond = gen_expr(g, ast_child(n, 1));
        emit(g, "if_false", cond, NULL, l_end);
    }
    emit(g, "goto", NULL, NULL, l_body);
    emit(g, "label", NULL, NULL, l_end);

    g->loops.len--;
    free(g->loops.items[g->loops.len].break_label);
    free(g->loops.items[g->loops.len].continue_label);
}

static void gen_assign(IrGen *g, const ASTNode *n) {
    const char *name = ast_get_prop(n, "name");
    const char *op = ast_get_prop(n, "op");
    char *value = gen_expr(g, ast_child(n, 0));
    const char *bin = assign_binop(op);
    if (bin) {
        char *t = temp_typed(g, "int");
        emit(g, bin, co1_strdup(name), value, t);
        emit_dup(g, "assign", t, NULL, name);
    } else {
        emit(g, "assign", value, NULL, co1_strdup(name ? name : "?"));
    }
}

static void gen_assign_lvalue(IrGen *g, const ASTNode *n) {
    const ASTNode *target = ast_child(n, 0);
    const char *op = ast_get_prop(n, "op");
    const ASTNode *val_node = ast_child(n, 1);
    const char *bin = assign_binop(op);
    if (bin && target && strcmp(target->node_type, "Identifier") == 0) {
        /* a = a op b */
        const char *name = ast_get_prop(target, "name");
        char *value = gen_expr(g, val_node);
        char *t = temp_typed(g, "int");
        emit(g, bin, co1_strdup(name), value, t);
        emit_dup(g, "assign", t, NULL, name);
        return;
    }
    if (bin) {
        /* a[i] op= v  =>  a[i] = a[i] op v */
        char *lhs = gen_expr(g, target);
        char *rhs = gen_expr(g, val_node);
        char *t = temp_typed(g, "int");
        emit(g, bin, lhs, rhs, t);
        gen_store(g, target, t);
        return;
    }
    char *value = gen_expr(g, val_node);
    gen_store(g, target, value);
}

static void gen_var_decl(IrGen *g, const ASTNode *n) {
    const char *name = ast_get_prop(n, "name");
    const char *base = ast_get_prop(n, "type_name");
    const char *is_array = ast_get_prop(n, "is_array");
    const char *is_pointer = ast_get_prop(n, "is_pointer");
    const char *array_size = ast_get_prop(n, "array_size");
    const char *type = normalize_type(base ? base : "int",
                                      is_pointer && strcmp(is_pointer, "true") == 0,
                                      is_array && strcmp(is_array, "true") == 0);

    emit_dup(g, "declare", type, NULL, name);
    scope_push(&g->scope, name, type);

    const ASTNode *init = ast_child(n, 0);
    if (!init) {
        /* C-style char name[100]; still needs heap backing so scanf can
           write into it */
        if (strcmp(type, "char[]") == 0) {
            const char *sz = array_size && array_size[0] != '[' ? array_size : "100";
            char *t = temp_typed(g, "char[]");
            emit_dup(g, "alloc", "char", sz, t);
            emit_dup(g, "assign", t, NULL, name);
        }
        return;
    }

    if (strcmp(type, "char[]") == 0 &&
        strcmp(init->node_type, "StringLit") == 0) {
        const char *lit = ast_get_prop(init, "value");
        if (!lit) {
            lit = init->token ? init->token->lexeme : "\"\"";
        }
        char *t = temp_typed(g, "str");
        emit(g, "string", co1_strdup(lit), NULL, co1_strdup(t));
        emit_dup(g, "assign", t, NULL, name);
        return;
    }

    if (strcmp(init->node_type, "InitList") == 0) {
        const char *elem = type_elem(type);
        char count[16];
        snprintf(count, sizeof(count), "%lu", (unsigned long)init->nchildren);
        char *t = temp_typed(g, type);
        emit_dup(g, "alloc", elem, count, t);
        emit_dup(g, "assign", t, NULL, name);
        for (size_t i = 0; i < init->nchildren; i++) {
            char idx[16];
            snprintf(idx, sizeof(idx), "%lu", (unsigned long)i);
            char *v = gen_expr(g, init->children[i]);
            emit_dup(g, base_is_float_type(elem) ? "arr_storef" : "arr_store",
                     name, idx, v);
        }
        return;
    }

    char *value = gen_expr(g, init);
    emit(g, "assign", value, NULL, co1_strdup(name));
}

static void gen_read(IrGen *g, const ASTNode *n) {
    for (size_t i = 0; i < n->nchildren; i++) {
        const ASTNode *target = n->children[i];
        const char *name = ast_get_prop(target, "name");
        if (!name) {
            continue;
        }
        const char *t = scope_get(&g->scope, name);
        if (strcmp(t ? t : "", "char[]") == 0) {
            emit_dup(g, "read_str", NULL, NULL, name);
        } else {
            emit_dup(g, "read", NULL, NULL, name);
        }
    }
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
    if (strcmp(n->node_type, "Empty") == 0 ||
        strcmp(n->node_type, "CtorInit") == 0) {
        return;
    }
    if (strcmp(n->node_type, "VarDecl") == 0) {
        gen_var_decl(g, n);
        return;
    }
    if (strcmp(n->node_type, "Assign") == 0) {
        gen_assign(g, n);
        return;
    }
    if (strcmp(n->node_type, "AssignLvalue") == 0) {
        gen_assign_lvalue(g, n);
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
    if (strcmp(n->node_type, "DoWhile") == 0) {
        gen_do_while(g, n);
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
    if (strcmp(n->node_type, "Break") == 0) {
        if (g->loops.len > 0) {
            emit_dup(g, "goto", NULL, NULL,
                     g->loops.items[g->loops.len - 1].break_label);
        }
        return;
    }
    if (strcmp(n->node_type, "Continue") == 0) {
        if (g->loops.len > 0) {
            emit_dup(g, "goto", NULL, NULL,
                     g->loops.items[g->loops.len - 1].continue_label);
        }
        return;
    }
    if (strcmp(n->node_type, "Println") == 0) {
        gen_print_items(g, n, 1);
        return;
    }
    if (strcmp(n->node_type, "PrintNoLn") == 0) {
        gen_print_items(g, n, 0);
        return;
    }
    if (strcmp(n->node_type, "CoutStream") == 0) {
        gen_print_items(g, n, 0);
        return;
    }
    if (strcmp(n->node_type, "Printf") == 0) {
        gen_printf(g, n);
        return;
    }
    if (strcmp(n->node_type, "Read") == 0) {
        gen_read(g, n);
        return;
    }
    if (strcmp(n->node_type, "ExprStmt") == 0) {
        char *v = gen_expr(g, ast_child(n, 0));
        free(v);
        return;
    }
    if (strcmp(n->node_type, "IncDec") == 0 ||
        strcmp(n->node_type, "UnaryOp") == 0) {
        char *v = gen_expr(g, n);
        free(v);
        return;
    }
    if (strcmp(n->node_type, "Throw") == 0) {
        return;
    }
}

/* ---------------------------------------------------------------- functions */

static void gen_function(IrGen *g, const ASTNode *n) {
    const char *name = ast_get_prop(n, "name");
    if (!name) {
        return;
    }
    const char *is_proto = ast_get_prop(n, "prototype");
    if (is_proto && strcmp(is_proto, "true") == 0) {
        return; /* declaration only */
    }

    const char *class_name = ast_get_prop(n, "class_name");
    const char *is_static = ast_get_prop(n, "is_static");
    const char *return_type = ast_get_prop(n, "return_type");
    const char *ret = normalize_type(return_type ? return_type : "int", 0, 0);

    char *label = func_label(class_name, name);
    emit(g, "func", co1_strdup(ret), co1_strdup(name), label);

    size_t scope_mark = g->scope.len;
    int param_index = 0;
    char idx[16];

    const char *prev_class = g->cur_class;
    g->cur_class = class_name;

    int is_instance = class_name && *class_name &&
                      !(is_static && strcmp(is_static, "true") == 0);
    if (is_instance) {
        emit_dup(g, "param", "obj", "0", "this");
        scope_push(&g->scope, "this", "obj");
        param_index++;
    }

    for (size_t i = 0; i < n->nchildren; i++) {
        const ASTNode *child = n->children[i];
        if (strcmp(child->node_type, "ParamDecl") == 0) {
            const char *pname = ast_get_prop(child, "name");
            const char *pbase = ast_get_prop(child, "type_name");
            const char *pis_array = ast_get_prop(child, "is_array");
            const char *pis_ptr = ast_get_prop(child, "is_pointer");
            const char *ptype = normalize_type(
                pbase ? pbase : "int",
                pis_ptr && strcmp(pis_ptr, "true") == 0,
                pis_array && strcmp(pis_array, "true") == 0);
            snprintf(idx, sizeof(idx), "%d", param_index++);
            emit_dup(g, "param", ptype, idx, pname);
            scope_push(&g->scope, pname, ptype);
        } else if (strcmp(child->node_type, "CtorInit") == 0) {
            /* C++ ctor init list: this->member = value */
            const char *member = ast_get_prop(child, "member");
            char offset[16];
            const char *ftype = "int";
            ClassInfo *ci = table_find_class(&g->classes, class_name);
            if (ci) {
                FieldInfo *f = table_find_field(ci, member);
                if (f) {
                    snprintf(offset, sizeof(offset), "%lu", (unsigned long)(f - ci->fields));
                    ftype = f->type;
                } else {
                    snprintf(offset, sizeof(offset), "0");
                }
            } else {
                snprintf(offset, sizeof(offset), "0");
            }
            char *v = gen_expr(g, ast_child(child, 0));
            const char *op = type_is_float(ftype)   ? "member_storef"
                           : type_needs_8byte(ftype) ? "member_storeq"
                                                     : "member_store";
            emit(g, op, co1_strdup("this"), co1_strdup(offset), v);
        } else if (strcmp(child->node_type, "Block") == 0) {
            gen_block(g, child);
        }
    }

    g->cur_class = prev_class;
    scope_pop_to(&g->scope, scope_mark);
    emit(g, "endfunc", NULL, NULL, NULL);
}

/* ---------------------------------------------------------------- program scan */

static void collect_class(IrGen *g, const ASTNode *cls) {
    const char *name = ast_get_prop(cls, "name");
    if (!name || table_find_class(&g->classes, name)) {
        return;
    }
    if (g->classes.len == g->classes.cap) {
        size_t nc = g->classes.cap ? g->classes.cap * 2 : 4;
        g->classes.items = (ClassInfo *)realloc(g->classes.items, nc * sizeof(ClassInfo));
        g->classes.cap = nc;
    }
    ClassInfo *ci = &g->classes.items[g->classes.len];
    memset(ci, 0, sizeof(*ci));
    ci->name = co1_strdup(name);
    g->classes.len++;

    for (size_t i = 0; i < cls->nchildren; i++) {
        const ASTNode *c = cls->children[i];
        if (strcmp(c->node_type, "FieldDecl") == 0) {
            if (ci->nfields == ci->cap_fields) {
                size_t nc = ci->cap_fields ? ci->cap_fields * 2 : 4;
                ci->fields = (FieldInfo *)realloc(ci->fields, nc * sizeof(FieldInfo));
                ci->cap_fields = nc;
            }
            FieldInfo *f = &ci->fields[ci->nfields++];
            const char *fbase = ast_get_prop(c, "type_name");
            const char *farr = ast_get_prop(c, "is_array");
            f->name = co1_strdup(ast_get_prop(c, "name"));
            f->type = co1_strdup(normalize_type(
                fbase ? fbase : "int", 0, farr && strcmp(farr, "true") == 0));
        } else if (strcmp(c->node_type, "MethodDef") == 0 ||
                   strcmp(c->node_type, "FunctionDef") == 0) {
            const char *mname = ast_get_prop(c, "name");
            if (!mname) {
                continue;
            }
            if (ci->nmethods == ci->cap_methods) {
                size_t nc = ci->cap_methods ? ci->cap_methods * 2 : 4;
                ci->methods = (MethodInfo *)realloc(ci->methods, nc * sizeof(MethodInfo));
                ci->cap_methods = nc;
            }
            MethodInfo *m = &ci->methods[ci->nmethods++];
            const char *rbase = ast_get_prop(c, "return_type");
            const char *mstatic = ast_get_prop(c, "is_static");
            const char *mctor = ast_get_prop(c, "is_constructor");
            m->name = co1_strdup(mname);
            m->ret = co1_strdup(normalize_type(rbase ? rbase : "void", 0, 0));
            m->is_static = mstatic && strcmp(mstatic, "true") == 0;
            m->is_constructor = mctor && strcmp(mctor, "true") == 0;
        }
    }
}

/* ---------------------------------------------------------------- public API */

void ir_build_lang(const ASTNode *program, IrQuadList *out) {
    IrGen g;
    memset(&g, 0, sizeof(g));
    g.next_index = 1;
    g.next_temp = 1;
    g.next_label = 1;

    /* pass 1: class layouts */
    for (size_t i = 0; i < program->nchildren; i++) {
        const ASTNode *child = program->children[i];
        if (strcmp(child->node_type, "ClassDef") == 0) {
            collect_class(&g, child);
        }
    }

    /* pass 2: function bodies */
    for (size_t i = 0; i < program->nchildren; i++) {
        const ASTNode *child = program->children[i];
        if (strcmp(child->node_type, "ClassDef") == 0) {
            for (size_t j = 0; j < child->nchildren; j++) {
                const ASTNode *m = child->children[j];
                if (strcmp(m->node_type, "MethodDef") == 0 ||
                    strcmp(m->node_type, "FunctionDef") == 0) {
                    gen_function(&g, m);
                }
            }
        } else if (strcmp(child->node_type, "FunctionDef") == 0) {
            gen_function(&g, child);
        }
    }

    /* pass 3: top-level (global) declarations — emitted at the very end;
       the backend emits them as a global-init section */
    for (size_t i = 0; i < program->nchildren; i++) {
        const ASTNode *child = program->children[i];
        if (strcmp(child->node_type, "VarDecl") == 0 ||
            strcmp(child->node_type, "Block") == 0) {
            gen_block(&g, child);
        }
    }

    *out = g.quads;

    for (size_t i = 0; i < g.classes.len; i++) {
        ClassInfo *ci = &g.classes.items[i];
        free(ci->name);
        for (size_t j = 0; j < ci->nfields; j++) {
            free(ci->fields[j].name);
            free(ci->fields[j].type);
        }
        free(ci->fields);
        for (size_t j = 0; j < ci->nmethods; j++) {
            free(ci->methods[j].name);
            free(ci->methods[j].ret);
        }
        free(ci->methods);
    }
    free(g.classes.items);
    while (g.scope.len > 0) {
        g.scope.len--;
        free(g.scope.items[g.scope.len].name);
        free(g.scope.items[g.scope.len].type);
    }
    free(g.scope.items);
    for (size_t i = 0; i < g.loops.len; i++) {
        free(g.loops.items[i].break_label);
        free(g.loops.items[i].continue_label);
    }
    free(g.loops.items);
}
