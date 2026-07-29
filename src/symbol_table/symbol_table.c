#include "symbol_table.h"

static ScopeTable *current_scope = NULL;
static int global_scope_counter = 0;

void init_symbol_table()
{
    current_scope = (ScopeTable *)malloc(sizeof(ScopeTable));
    current_scope->scope_level = 0;
    current_scope->head = NULL;
    current_scope->parent = NULL;
}

void enter_scope()
{
    global_scope_counter++;
    ScopeTable *new_scope = (ScopeTable *)malloc(sizeof(ScopeTable));
    new_scope->scope_level = global_scope_counter;
    new_scope->head = NULL;
    new_scope->parent = current_scope;
    current_scope = new_scope;
}

void exit_scope()
{
    if (!current_scope)
        return;

    ScopeTable *temp_scope = current_scope;
    current_scope = current_scope->parent;

    SymbolEntry *curr = temp_scope->head;
    while (curr)
    {
        SymbolEntry *next = curr->next;
        free(curr->name);
        free(curr->type);
        free(curr);
        curr = next;
    }
    free(temp_scope);
}

SymbolEntry *lookup_symbol_current_scope(const char *name)
{
    if (!current_scope)
        return NULL;

    SymbolEntry *curr = current_scope->head;
    while (curr)
    {
        if (strcmp(curr->name, name) == 0)
        {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

SymbolEntry *lookup_symbol(const char *name)
{
    ScopeTable *scope_iter = current_scope;
    while (scope_iter)
    {
        SymbolEntry *curr = scope_iter->head;
        while (curr)
        {
            if (strcmp(curr->name, name) == 0)
            {
                return curr; // Symbol found!
            }
            curr = curr->next;
        }
        scope_iter = scope_iter->parent; // Try parent scope
    }
    return NULL; // Symbol Not Found
}

int insert_symbol(const char *name, const char *type, int line)
{
    if (lookup_symbol_current_scope(name) != NULL)
    {
        return 0; // Failure: Redeclaration Error in same scope!
    }

    SymbolEntry *entry = (SymbolEntry *)malloc(sizeof(SymbolEntry));
    entry->name = strdup(name);
    entry->type = strdup(type);
    entry->scope_level = current_scope->scope_level;
    entry->line_declared = line;
    entry->next = current_scope->head;
    current_scope->head = entry;

    return 1; // Success
}

void print_symbol_table()
{
    printf("\n============ SYMBOL TABLE SNAPSHOT ============\n");
    ScopeTable *scope_iter = current_scope;
    while (scope_iter)
    {
        printf("--- Scope Level: %d ---\n", scope_iter->scope_level);
        SymbolEntry *curr = scope_iter->head;
        if (!curr)
        {
            printf("  (Empty Scope)\n");
        }
        while (curr)
        {
            printf("  [ Name: %-10s | Type: %-6s | Line: %-3d ]\n",
                   curr->name, curr->type, curr->line_declared);
            curr = curr->next;
        }
        scope_iter = scope_iter->parent;
    }
    printf("===============================================\n\n");
}