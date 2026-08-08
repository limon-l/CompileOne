#ifndef CO1_PARSER_H
#define CO1_PARSER_H

#include <stddef.h>

#include "json_writer.h"
#include "token.h"

/* ============================================================
   mini-c recursive-descent parser.

   The parser consumes the token array produced by the flex lexer
   and builds two artifacts in one pass:

     - a Concrete Syntax Tree (CST) : every grammar rule reduction
       is a node, terminals carry their token. This is the "parse
       tree" a compiler-construction course draws.

     - an Abstract Syntax Tree (AST): the semantic structure (decls,
       assigns, if/while/for, print/read, expressions) that the
       semantic, IR, optimizer and codegen phases consume.

   Both trees are serialised to JSON by the driver phase commands
   (parse -> parse_tree.json, ast -> ast.json).
   ============================================================ */

/* ------------------------------- CST ------------------------------- */

typedef struct CSTNode {
    const char *rule_name;      /* non-terminal production name */
    Token *token;               /* terminal node: the anchored token */
    struct CSTNode **children;
    size_t child_count;
} CSTNode;

/* ------------------------------- AST ------------------------------- */

typedef struct ASTProp {
    const char *key;
    char *value;                /* raw value (unescaped string or number) */
    int is_string;              /* 1 => serialise with JSON escaping */
} ASTProp;

typedef struct ASTNode {
    int id;                     /* 1-based node id (source-map key) */
    const char *node_type;      /* "Program", "VarDecl", "BinaryOp", ... */
    Token *token;               /* anchor token (may be NULL) */
    ASTProp *props;
    size_t nprops;
    struct ASTNode **children;
    size_t nchildren;
} ASTNode;

/* ---------------------------- diagnostics --------------------------- */

typedef struct SyntaxError {
    int line;
    int column;
    char *message;              /* owned */
} SyntaxError;

DARRAY_DECLARE(SyntaxError, SyntaxErrorList)

/* ------------------------------ result ------------------------------ */

typedef struct ParseResult {
    CSTNode *cst_root;
    ASTNode *ast_root;
    SyntaxErrorList errors;     /* empty => syntactically valid */
} ParseResult;

/* Parse `tokens` into CST + AST. On success both roots are non-NULL.
   If the token list contains lexical errors the parser still attempts
   recovery, but `errors` is populated and the caller should halt the
   pipeline. Ownership of every node is transferred to the caller. */
int parse_tokens(TokenList *tokens, ParseResult *out);

void parse_result_free(ParseResult *r);

/* ---------------------------- free helpers --------------------------- */

void cst_free(CSTNode *node);
void ast_free(ASTNode *node);

/* --------------------------- JSON serializers ------------------------- */

/* Serialise the CST root as the "root" value of parse_tree.json.
   `w` must already be inside the enclosing artifact object. */
void cst_to_json(JsonWriter *w, const CSTNode *node);

/* Serialise the AST root as the "root" value of ast.json. */
void ast_to_json(JsonWriter *w, const ASTNode *node);

/* ------------------------------ AST accessors -------------------------- */

const char *ast_get_prop(const ASTNode *node, const char *key);
const char *ast_child_name(const ASTNode *node);      /* Identifier "name" */
const ASTNode *ast_child(const ASTNode *node, size_t idx);

/* Attach a string attribute on an AST node (used by the semantic pass
   to annotate inferred types). Appends; callers should not duplicate
   a key that already exists. */
void ast_annotate(ASTNode *node, const char *key, const char *value);

#endif /* CO1_PARSER_H */
