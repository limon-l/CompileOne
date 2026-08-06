#include <stdio.h>
#include "src/symbol_table/symbol_table.h"

int main()
{
    printf("===========================================\n");
    printf("     Symbol Table & Scope Management Test  \n");
    printf("===========================================\n");

    init_symbol_table();

    insert_symbol("x", "int", 1);
    insert_symbol("flag", "bool", 3);

    print_symbol_table();

    printf("[INFO] Entering new block ({ ... })...\n");
    enter_scope();
    insert_symbol("y", "float", 6);
    insert_symbol("x", "int", 7);

    print_symbol_table();

    printf("[LOOKUP TEST]: Searching 'y' -> %s\n", lookup_symbol("y") ? "FOUND" : "NOT FOUND");
    printf("[LOOKUP TEST]: Searching 'flag' -> %s\n", lookup_symbol("flag") ? "FOUND" : "NOT FOUND");

    printf("[INFO] Exiting block (} ...)\n");
    exit_scope();

    print_symbol_table();

    printf("[LOOKUP TEST]: Searching 'y' after exiting scope -> %s\n",
           lookup_symbol("y") ? "FOUND" : "NOT FOUND");

    return 0;
}