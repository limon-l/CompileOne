#include "semantic.h"

static int semantic_errors = 0;

// Helper function: Type checking for expressions
char *check_node_type(ASTNode *node)
{
    if (!node)
        return "unknown";

    switch (node->type)
    {
    case NODE_INT_LIT:
        return "int";

    case NODE_FLOAT_LIT:
        return "float";

    case NODE_BOOL_LIT:
        return "bool";

    case NODE_ID:
    {
        SymbolEntry *symbol = lookup_symbol(node->data.str_val);
        if (!symbol)
        {
            printf("[SEMANTIC ERROR] Undeclared variable '%s' used!\n", node->data.str_val);
            semantic_errors++;
            return "unknown";
        }
        return symbol->type;
    }

    case NODE_BINARY_OP:
    {
        char *left_type = check_node_type(node->left);
        char *right_type = check_node_type(node->right);

        // Arithmetic Operations
        if (strcmp(node->data.op, "+") == 0 || strcmp(node->data.op, "-") == 0 ||
            strcmp(node->data.op, "*") == 0 || strcmp(node->data.op, "/") == 0)
        {

            if (strcmp(left_type, "int") == 0 && strcmp(right_type, "int") == 0)
            {
                return "int";
            }
            if (strcmp(left_type, "float") == 0 && strcmp(right_type, "float") == 0)
            {
                return "float";
            }

            if (strcmp(left_type, "unknown") != 0 && strcmp(right_type, "unknown") != 0)
            {
                printf("[SEMANTIC ERROR] Type mismatch in operation '%s': cannot perform on '%s' and '%s'\n",
                       node->data.op, left_type, right_type);
                semantic_errors++;
            }
            return "unknown";
        }

        // Relational Operations
        if (strcmp(node->data.op, "<") == 0 || strcmp(node->data.op, ">") == 0 ||
            strcmp(node->data.op, "<=") == 0 || strcmp(node->data.op, ">=") == 0 ||
            strcmp(node->data.op, "==") == 0 || strcmp(node->data.op, "!=") == 0)
        {

            if (strcmp(left_type, right_type) != 0 &&
                strcmp(left_type, "unknown") != 0 && strcmp(right_type, "unknown") != 0)
            {
                printf("[SEMANTIC ERROR] Type mismatch in comparison '%s': comparing '%s' with '%s'\n",
                       node->data.op, left_type, right_type);
                semantic_errors++;
            }
            return "bool"; // Comparisons result in boolean
        }

        // Logical Operations
        if (strcmp(node->data.op, "&&") == 0 || strcmp(node->data.op, "||") == 0)
        {
            if (strcmp(left_type, "bool") != 0 || strcmp(right_type, "bool") != 0)
            {
                printf("[SEMANTIC ERROR] Logical operator '%s' requires boolean operands!\n", node->data.op);
                semantic_errors++;
            }
            return "bool";
        }
        break;
    }

    default:
        return "unknown";
    }

    return "unknown";
}

// AST Traversal for Semantic Validation
void traverse_and_check(ASTNode *node)
{
    if (!node)
        return;

    switch (node->type)
    {
    case NODE_VAR_DECL:
    {
        char *type_name = node->data.str_val;
        char *var_name = node->left->data.str_val;

        // Symbol Table-এ ইনসার্ট করার চেষ্টা
        if (!insert_symbol(var_name, type_name, 0))
        {
            printf("[SEMANTIC ERROR] Redeclaration of variable '%s' in the same scope!\n", var_name);
            semantic_errors++;
        }
        break;
    }

    case NODE_ASSIGN:
    {
        char *var_name = node->data.str_val;
        SymbolEntry *symbol = lookup_symbol(var_name);

        if (!symbol)
        {
            printf("[SEMANTIC ERROR] Cannot assign to undeclared variable '%s'!\n", var_name);
            semantic_errors++;
        }
        else
        {
            char *expr_type = check_node_type(node->left);
            if (strcmp(symbol->type, expr_type) != 0 && strcmp(expr_type, "unknown") != 0)
            {
                printf("[SEMANTIC ERROR] Type mismatch in assignment: Cannot assign '%s' to variable '%s' of type '%s'\n",
                       expr_type, var_name, symbol->type);
                semantic_errors++;
            }
        }
        break;
    }

    case NODE_IF:
    {
        char *cond_type = check_node_type(node->left);
        if (strcmp(cond_type, "bool") != 0 && strcmp(cond_type, "unknown") != 0)
        {
            printf("[SEMANTIC ERROR] Condition in 'if' statement must be boolean, found '%s'\n", cond_type);
            semantic_errors++;
        }
        traverse_and_check(node->right); // Then block
        if (node->third)
            traverse_and_check(node->third); // Else block
        break;
    }

    case NODE_WHILE:
    {
        char *cond_type = check_node_type(node->left);
        if (strcmp(cond_type, "bool") != 0 && strcmp(cond_type, "unknown") != 0)
        {
            printf("[SEMANTIC ERROR] Condition in 'while' loop must be boolean, found '%s'\n", cond_type);
            semantic_errors++;
        }
        traverse_and_check(node->right); // Body
        break;
    }

    case NODE_PRINT:
    {
        check_node_type(node->left);
        break;
    }

    case NODE_BLOCK:
    {
        enter_scope(); // New block scope
        traverse_and_check(node->left);
        exit_scope(); // Exit block scope
        break;
    }

    default:
        break;
    }

    // Process next statement in the statement list
    if (node->type != NODE_BLOCK)
    {
        traverse_and_check(node->next);
    }
}

// Main Driver Function for Semantic Analysis
int analyze_semantics(ASTNode *root)
{
    semantic_errors = 0;
    init_symbol_table(); // Global scope initialize
    printf("\n--- Starting Semantic Analysis Phase ---\n");

    traverse_and_check(root);

    if (semantic_errors == 0)
    {
        printf("[SUCCESS] Semantic Analysis Passed with 0 Errors!\n");
        return 1;
    }
    else
    {
        printf("[FAILURE] Semantic Analysis Failed with %d Error(s).\n", semantic_errors);
        return 0;
    }
}