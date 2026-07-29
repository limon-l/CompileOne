#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "src/ast/ast.h"
#include "src/symbol_table/symbol_table.h"

// Semantic Analysis Core Functions
int analyze_semantics(ASTNode *root);
char *check_node_type(ASTNode *node);

#endif