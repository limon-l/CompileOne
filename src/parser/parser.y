%{
#include <stdio.h>
#include <stdlib.h>
#include "src/ast/ast.h"
#include "src/semantic/semantic.h"
#include "src/semantic/semantic.h"
#include "src/codegen/tac.h"

extern int line_num;
extern int yylex();
extern FILE *yyin;

void yyerror(const char *s);

ASTNode *ast_root = NULL; // Root pointer for generated AST
%}

/* Union definition including ASTNode pointer */
%union {
    int int_val;
    float float_val;
    char *str_val;
    struct ASTNode *node;
}

/* Tokens */
%token KW_INT KW_FLOAT KW_BOOL KW_IF KW_ELSE KW_WHILE KW_PRINT
%token BOOL_TRUE BOOL_FALSE
%token OP_ADD OP_SUB OP_MUL OP_DIV OP_MOD
%token OP_LE OP_GE OP_EQ OP_NEQ OP_LT OP_GT
%token OP_AND OP_OR OP_NOT OP_ASSIGN
%token SEMICOLON LBRACE RBRACE LPAREN RPAREN
%token <int_val> INT_LITERAL
%token <float_val> FLOAT_LITERAL
%token <str_val> IDENTIFIER

/* Non-terminals Types */
%type <node> program stmt_list stmt var_decl assign_stmt if_stmt while_stmt print_stmt block_stmt expr
%type <str_val> type

/* Precedence */
%left OP_OR
%left OP_AND
%left OP_EQ OP_NEQ
%left OP_LT OP_GT OP_LE OP_GE
%left OP_ADD OP_SUB
%left OP_MUL OP_DIV OP_MOD
%right OP_NOT

%nonassoc LOWER_THAN_ELSE
%nonassoc KW_ELSE

%%

program:
    stmt_list { ast_root = $1; }
;

stmt_list:
    stmt_list stmt { $$ = append_stmt($1, $2); }
  | /* empty */    { $$ = NULL; }
;

stmt:
    var_decl     { $$ = $1; }
  | assign_stmt  { $$ = $1; }
  | if_stmt      { $$ = $1; }
  | while_stmt   { $$ = $1; }
  | print_stmt   { $$ = $1; }
  | block_stmt   { $$ = $1; }
;

type:
    KW_INT   { $$ = "int"; }
  | KW_FLOAT { $$ = "float"; }
  | KW_BOOL  { $$ = "bool"; }
;

var_decl:
    type IDENTIFIER SEMICOLON { $$ = create_var_decl_node($1, $2); }
;

assign_stmt:
    IDENTIFIER OP_ASSIGN expr SEMICOLON { $$ = create_assign_node($1, $3); }
;

if_stmt:
    KW_IF LPAREN expr RPAREN stmt %prec LOWER_THAN_ELSE { $$ = create_if_node($3, $5, NULL); }
  | KW_IF LPAREN expr RPAREN stmt KW_ELSE stmt          { $$ = create_if_node($3, $5, $7); }
;

while_stmt:
    KW_WHILE LPAREN expr RPAREN stmt { $$ = create_while_node($3, $5); }
;

print_stmt:
    KW_PRINT expr SEMICOLON { $$ = create_print_node($2); }
;

block_stmt:
    LBRACE stmt_list RBRACE { $$ = create_block_node($2); }
;

expr:
    expr OP_ADD expr  { $$ = create_binary_node("+", $1, $3); }
  | expr OP_SUB expr  { $$ = create_binary_node("-", $1, $3); }
  | expr OP_MUL expr  { $$ = create_binary_node("*", $1, $3); }
  | expr OP_DIV expr  { $$ = create_binary_node("/", $1, $3); }
  | expr OP_MOD expr  { $$ = create_binary_node("%", $1, $3); }
  | expr OP_EQ expr   { $$ = create_binary_node("==", $1, $3); }
  | expr OP_NEQ expr  { $$ = create_binary_node("!=", $1, $3); }
  | expr OP_LT expr   { $$ = create_binary_node("<", $1, $3); }
  | expr OP_GT expr   { $$ = create_binary_node(">", $1, $3); }
  | expr OP_LE expr   { $$ = create_binary_node("<=", $1, $3); }
  | expr OP_GE expr   { $$ = create_binary_node(">=", $1, $3); }
  | expr OP_AND expr  { $$ = create_binary_node("&&", $1, $3); }
  | expr OP_OR expr   { $$ = create_binary_node("||", $1, $3); }
  | OP_NOT expr       { $$ = create_binary_node("!", $2, NULL); }
  | LPAREN expr RPAREN { $$ = $2; }
  | IDENTIFIER        { $$ = create_id_node($1); }
  | INT_LITERAL       { $$ = create_int_node($1); }
  | FLOAT_LITERAL     { $$ = create_float_node($1); }
  | BOOL_TRUE         { $$ = create_bool_node(1); }
  | BOOL_FALSE        { $$ = create_bool_node(0); }
;

%%

void yyerror(const char *s) {
    fprintf(stderr, "[SYNTAX ERROR] Line %d: %s\n", line_num, s);
}

int main(int argc, char **argv) {
    if (argc > 1) {
        FILE *fp = fopen(argv[1], "r");
        if (!fp) {
            perror("File opening failed");
            return 1;
        }
        yyin = fp;
    }

    printf("===========================================\n");
    printf("        Compiler Execution Pipeline        \n");
    printf("===========================================\n");

    if (yyparse() == 0 && ast_root != NULL) {
        printf("\n[1] Syntax Analysis Passed. AST Built Successfully.\n");

        if (analyze_semantics(ast_root)) {
            printf("\n[2] Semantic Checks Passed. Proceeding to Code Generation...\n");
            
            generate_tac(ast_root);
            
            printf("\n[SUCCESS] Intermediate Code Generation Completed!\n");
        } else {
            printf("\n[STOP] Compilation Halted due to Semantic Errors.\n");
        }

        free_ast(ast_root);
    } else {
        printf("\n[STOP] Compilation Halted due to Syntax Errors.\n");
    }

    return 0;
}