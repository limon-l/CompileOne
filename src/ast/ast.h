#ifndef AST_H
#define AST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum
{
    NODE_INT_LIT,
    NODE_FLOAT_LIT,
    NODE_BOOL_LIT,
    NODE_ID,
    NODE_BINARY_OP,
    NODE_ASSIGN,
    NODE_VAR_DECL,
    NODE_IF,
    NODE_WHILE,
    NODE_PRINT,
    NODE_BLOCK,
    NODE_STMT_LIST
} NodeType;

typedef struct ASTNode
{
    NodeType type;
    union
    {
        int int_val;
        float float_val;
        int bool_val;
        char *str_val;
        char *op;
    } data;

    struct ASTNode *left;
    struct ASTNode *right;
    struct ASTNode *third; // for IF-ELSE
    struct ASTNode *next;  // for linking statements
} ASTNode;

// Constructor Helper Functions
ASTNode *create_int_node(int val);
ASTNode *create_float_node(float val);
ASTNode *create_bool_node(int val);
ASTNode *create_id_node(const char *name);
ASTNode *create_binary_node(const char *op, ASTNode *left, ASTNode *right);
ASTNode *create_assign_node(const char *id_name, ASTNode *expr);
ASTNode *create_var_decl_node(const char *type_name, const char *id_name);
ASTNode *create_if_node(ASTNode *cond, ASTNode *then_block, ASTNode *else_block);
ASTNode *create_while_node(ASTNode *cond, ASTNode *body);
ASTNode *create_print_node(ASTNode *expr);
ASTNode *create_block_node(ASTNode *stmt_list);

ASTNode *append_stmt(ASTNode *head, ASTNode *stmt);
void print_ast(ASTNode *node, int indent);
void free_ast(ASTNode *node);

#endif