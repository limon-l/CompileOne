#ifndef CO1_FRONTEND_H
#define CO1_FRONTEND_H

#include "parser.h"
#include "token.h"

/* ============================================================
   Multi-language C-family front end (C / C++ / Java).

   The native languages share one recursive-descent parser that
   builds a real Concrete Syntax Tree (CST) and an Abstract Syntax
   Tree (AST) in a single pass, exactly as the mini-c parser does.
   The grammar covers the common C-family subset plus the
   language-specific extensions:

     C   : functions, prototypes, globals, printf/scanf, arrays
     C++ : classes, methods, constructors, inheritance, namespaces,
           iostream (cout/cin) and using-directives
     Java: classes, methods, constructors, inheritance, packages,
           imports, arrays, try/catch, String and System.out I/O

   Both trees use the same CSTNode/ASTNode shapes as the mini-c
   parser so every downstream phase (semantic, IR, optimizer,
   codegen) and every JSON serializer is shared.
   ============================================================ */

/* True when `language` is handled by this front end. */
int lang_is_native(const char *language);

/* Reclassify word tokens for a native language (demotes mini-c-only
   keywords such as print/read that are plain identifiers in C/C++).
   Modifies tokens in place; a no-op for non-native languages. */
void lang_reclassify_tokens(TokenList *tokens, const char *language);

/* Parse `tokens` (already classified) into CST + AST for a native
   language. Mirrors parse_tokens(): on success both roots are
   non-NULL; `errors` collects every syntax diagnostic. */
int lang_parse_tokens(TokenList *tokens, const char *language, ParseResult *out);

#endif /* CO1_FRONTEND_H */
