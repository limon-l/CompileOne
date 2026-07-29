#include "tac.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int temp_var_count = 0;
static int label_count = 0;

// Helper to generate temporary variables (t1, t2, ...)
char *new_temp()
{
    char *temp = (char *)malloc(10);
    sprintf(temp, "t%d", ++temp_var_count);
    return temp;
}

// Helper to generate labels (L1, L2, ...)
char *new_label()
{
    char *label = (char *)malloc(10);
    sprintf(label, "L%d", ++label_count);
    return label;
}

// Helper function to process expressions and return the holding variable
char *process_expr(ASTNode *node)
{
    if (!node)
        return "";

    char *result = (char *)malloc(20);

    switch (node->type)
    {
    case NODE_INT_LIT:
        sprintf(result, "%d", node->data.int_val);
        return result;

    case NODE_FLOAT_LIT:
        sprintf(result, "%.2f", node->data.float_val);
        return result;

    case NODE_BOOL_LIT:
        sprintf(result, "%s", node->data.bool_val ? "true" : "false");
        return result;

    case NODE_ID:
        strcpy(result, node->data.str_val);
        return result;

    case NODE_BINARY_OP:
    {
        char *left_val = process_expr(node->left);
        char *right_val = process_expr(node->right);
        char *temp = new_temp();
        printf("  %s = %s %s %s\n", temp, left_val, node->data.op, right_val);
        return temp;
    }
    default:
        return "";
    }
}

// Recursive function to process statements
void process_stmt(ASTNode *node)
{
    if (!node)
        return;

    switch (node->type)
    {
    case NODE_ASSIGN:
    {
        char *expr_val = process_expr(node->left);
        printf("  %s = %s\n", node->data.str_val, expr_val);
        break;
    }

    case NODE_IF:
    {
        char *cond_val = process_expr(node->left);
        char *label_false = new_label();
        char *label_end = new_label();

        printf("  ifFalse %s goto %s\n", cond_val, label_false);

        // Then block
        process_stmt(node->right);

        if (node->third)
        {
            printf("  goto %s\n", label_end);
        }

        printf("%s:\n", label_false);

        // Else block
        if (node->third)
        {
            process_stmt(node->third);
            printf("%s:\n", label_end);
        }
        break;
    }

    case NODE_WHILE:
    {
        char *label_start = new_label();
        char *label_end = new_label();

        printf("%s:\n", label_start);
        char *cond_val = process_expr(node->left);
        printf("  ifFalse %s goto %s\n", cond_val, label_end);

        // Body
        process_stmt(node->right);

        printf("  goto %s\n", label_start);
        printf("%s:\n", label_end);
        break;
    }

    case NODE_PRINT:
    {
        char *expr_val = process_expr(node->left);
        printf("  print %s\n", expr_val);
        break;
    }

    case NODE_BLOCK:
        process_stmt(node->left);
        break;

    case NODE_VAR_DECL:
        // variable declarations don't generally produce TAC directly
        break;

    default:
        break;
    }

    if (node->next)
    {
        process_stmt(node->next);
    }
}

// Main TAC Generator Function
void generate_tac(ASTNode *root)
{
    printf("\n--- Three-Address Code (TAC) Generation ---\n");
    process_stmt(root);
    printf("-------------------------------------------\n");
}