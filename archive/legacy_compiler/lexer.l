%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.tab.h" // Bison generated header file

extern int line_num;
int line_num = 1;
%}

%option noyywrap
%option noinput
%option nounput

DIGIT       [0-9]
LETTER      [a-zA-Z_]
ID          {LETTER}({LETTER}|{DIGIT})*
INT_LIT     {DIGIT}+
FLOAT_LIT   {DIGIT}+\.{DIGIT}+

%%

[ \t\r]+    { /* Ignore whitespace */ }
\n          { line_num++; }
"//".*      { /* Single-line comment */ }

"int"       { return KW_INT; }
"float"     { return KW_FLOAT; }
"bool"      { return KW_BOOL; }
"if"        { return KW_IF; }
"else"      { return KW_ELSE; }
"while"     { return KW_WHILE; }
"print"     { return KW_PRINT; }
"true"      { return BOOL_TRUE; }
"false"     { return BOOL_FALSE; }

"+"         { return OP_ADD; }
"-"         { return OP_SUB; }
"*"         { return OP_MUL; }
"/"         { return OP_DIV; }
"%"         { return OP_MOD; }

"<="        { return OP_LE; }
">="        { return OP_GE; }
"=="        { return OP_EQ; }
"!="        { return OP_NEQ; }
"<"         { return OP_LT; }
">"         { return OP_GT; }

"&&"        { return OP_AND; }
"||"        { return OP_OR; }
"!"         { return OP_NOT; }

"="         { return OP_ASSIGN; }

";"         { return SEMICOLON; }
"{"         { return LBRACE; }
"}"         { return RBRACE; }
"("         { return LPAREN; }
")"         { return RPAREN; }

{INT_LIT}   { yylval.int_val = atoi(yytext); return INT_LITERAL; }
{FLOAT_LIT} { yylval.float_val = atof(yytext); return FLOAT_LITERAL; }
{ID}        { yylval.str_val = strdup(yytext); return IDENTIFIER; }

.           { printf("[LEXICAL ERROR] Line %d: Unknown character '%s'\n", line_num, yytext); }

%%