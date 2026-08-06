#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Symbol Table Entry Struct
typedef struct SymbolEntry
{
    char *name;               // Identifier Name
    char *type;               // "int", "float", "bool"
    int scope_level;          // Scope ID (0 = Global, 1 = Local, etc.)
    int line_declared;        // Line number where declared
    struct SymbolEntry *next; // Pointer to next symbol in the same scope
} SymbolEntry;

// Scope Table Struct (Linked list of entries in a scope)
typedef struct ScopeTable
{
    int scope_level;
    SymbolEntry *head;
    struct ScopeTable *parent; // Link to outer/parent scope
} ScopeTable;

// Function Prototypes
void init_symbol_table();
void enter_scope();
void exit_scope();
int insert_symbol(const char *name, const char *type, int line);
SymbolEntry *lookup_symbol(const char *name);
SymbolEntry *lookup_symbol_current_scope(const char *name);
void print_symbol_table();

#endif