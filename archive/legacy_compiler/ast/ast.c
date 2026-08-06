#include "ast.h"

ASTNode *create_int_node(int val)
{
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    node->type = NODE_INT_LIT;
    node->data.int_val = val;
    return node;
}

ASTNode *create_float_node(float val)
{
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    node->type = NODE_FLOAT_LIT;
    node->data.float_val = val;
    return node;
}

ASTNode *create_bool_node(int val)
{
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    node->type = NODE_BOOL_LIT;
    node->data.bool_val = val;
    return node;
}

ASTNode *create_id_node(const char *name)
{
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    node->type = NODE_ID;
    node->data.str_val = strdup(name);
    return node;
}

ASTNode *create_binary_node(const char *op, ASTNode *left, ASTNode *right)
{
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    node->type = NODE_BINARY_OP;
    node->data.op = strdup(op);
    node->left = left;
    node->right = right;
    return node;
}

ASTNode *create_assign_node(const char *id_name, ASTNode *expr)
{
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    node->type = NODE_ASSIGN;
    node->data.str_val = strdup(id_name);
    node->left = expr;
    return node;
}

ASTNode *create_var_decl_node(const char *type_name, const char *id_name)
{
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    node->type = NODE_VAR_DECL;
    node->data.str_val = strdup(type_name);
    node->left = create_id_node(id_name);
    return node;
}

ASTNode *create_if_node(ASTNode *cond, ASTNode *then_block, ASTNode *else_block)
{
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    node->type = NODE_IF;
    node->left = cond;
    node->right = then_block;
    node->third = else_block;
    return node;
}

ASTNode *create_while_node(ASTNode *cond, ASTNode *body)
{
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    node->type = NODE_WHILE;
    node->left = cond;
    node->right = body;
    return node;
}

ASTNode *create_print_node(ASTNode *expr)
{
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    node->type = NODE_PRINT;
    node->left = expr;
    return node;
}

ASTNode *create_block_node(ASTNode *stmt_list)
{
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    node->type = NODE_BLOCK;
    node->left = stmt_list;
    return node;
}

ASTNode *append_stmt(ASTNode *head, ASTNode *stmt)
{
    if (!head)
        return stmt;
    ASTNode *curr = head;
    while (curr->next)
        curr = curr->next;
    curr->next = stmt;
    return head;
}

void print_ast(ASTNode *node, int indent)
{
    if (!node)
        return;

    for (int i = 0; i < indent; i++)
        printf("  ");

    switch (node->type)
    {
    case NODE_INT_LIT:
        printf("IntLiteral: %d\n", node->data.int_val);
        break;
    case NODE_FLOAT_LIT:
        printf("FloatLiteral: %.2f\n", node->data.float_val);
        break;
    case NODE_BOOL_LIT:
        printf("BoolLiteral: %s\n", node->data.bool_val ? "true" : "false");
        break;
    case NODE_ID:
        printf("Identifier: %s\n", node->data.str_val);
        break;
    case NODE_BINARY_OP:
        printf("BinaryOp (%s)\n", node->data.op);
        print_ast(node->left, indent + 1);
        print_ast(node->right, indent + 1);
        break;
    case NODE_ASSIGN:
        printf("AssignStmt (=) -> Var: %s\n", node->data.str_val);
        print_ast(node->left, indent + 1);
        break;
    case NODE_VAR_DECL:
        printf("VarDecl (Type: %s)\n", node->data.str_val);
        print_ast(node->left, indent + 1);
        break;
    case NODE_IF:
        printf("IfStmt\n");
        for (int i = 0; i < indent + 1; i++)
            printf("  ");
        printf("[Condition]:\n");
        print_ast(node->left, indent + 2);
        for (int i = 0; i < indent + 1; i++)
            printf("  ");
        printf("[Then Block]:\n");
        print_ast(node->right, indent + 2);
        if (node->third)
        {
            for (int i = 0; i < indent + 1; i++)
                printf("  ");
            printf("[Else Block]:\n");
            print_ast(node->third, indent + 2);
        }
        break;
    case NODE_WHILE:
        printf("WhileStmt\n");
        for (int i = 0; i < indent + 1; i++)
            printf("  ");
        printf("[Condition]:\n");
        print_ast(node->left, indent + 2);
        for (int i = 0; i < indent + 1; i++)
            printf("  ");
        printf("[Body]:\n");
        print_ast(node->right, indent + 2);
        break;
    case NODE_PRINT:
        printf("PrintStmt\n");
        print_ast(node->left, indent + 1);
        break;
    case NODE_BLOCK:
        printf("BlockStmt {\n");
        print_ast(node->left, indent + 1);
        for (int i = 0; i < indent; i++)
            printf("  ");
        printf("}\n");
        break;
    default:
        printf("Unknown Node\n");
    }

    if (node->next)
    {
        print_ast(node->next, indent);
    }
}

void free_ast(ASTNode *node)
{
    if (!node)
        return;
    if (node->type == NODE_ID || node->type == NODE_ASSIGN || node->type == NODE_VAR_DECL)
    {
        free(node->data.str_val);
    }
    if (node->type == NODE_BINARY_OP)
    {
        free(node->data.op);
    }

    free_ast(node->left);
    free_ast(node->right);
    free_ast(node->third);
    free_ast(node->next);
    free(node);
}