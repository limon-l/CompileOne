%{
#include <stdio.h>
#include <stdlib.h>

#include "lexer.h"
#include "strbuf.h"
#include "token.h"

/* ------------------------------------------------------------------
   Scan state. The cursor (s_line, s_column, s_offset) always points at
   the START of the current match; every rule calls lexer_advance()
   after recording its token, moving the cursor across the matched text.
   ------------------------------------------------------------------ */
int lex_collect_comments = 1;

static TokenList    s_tokens;
static LexErrorList s_errors;
static int    s_token_id  = 0;
static int    s_line      = 1;
static int    s_column    = 1;
static long   s_offset    = 0;
static int    s_scope_level = 0;

extern FILE *yyin;
extern int yylex(void);

static void lexer_advance(const char *text, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (text[i] == '\n') {
            s_line++;
            s_column = 1;
        } else {
            s_column++;
        }
    }
    s_offset += (long)n;
}

static void record_token(TokenKind kind) {
    Token t = token_make(kind, yytext, yyleng, ++s_token_id,
                         s_line, s_column,
                         (size_t)s_offset, (size_t)s_offset + yyleng,
                         s_scope_level);
    TokenList_push(&s_tokens, t);
    lexer_advance(yytext, yyleng);
}

static void record_error(const char *message) {
    LexError e;
    e.line    = s_line;
    e.column  = s_column;
    e.lexeme  = co1_strdup(yytext);
    e.message = message;
    LexErrorList_push(&s_errors, e);

    Token t = token_make(TOK_LEX_ERROR, yytext, yyleng, ++s_token_id,
                         s_line, s_column,
                         (size_t)s_offset, (size_t)s_offset + yyleng,
                         s_scope_level);
    TokenList_push(&s_tokens, t);
    lexer_advance(yytext, yyleng);
}

static void skip(void) {
    lexer_advance(yytext, yyleng);
}
%}

%option noyywrap
%option noinput
%option nounput

DIGIT      [0-9]
LETTER     [a-zA-Z_]
ID         {LETTER}({LETTER}|{DIGIT})*
INT_LIT    {DIGIT}+
FLOAT_LIT  {DIGIT}+\.{DIGIT}+
WS         [ \t]+
NEWLINE    (\r\n|[\n\r])

%%

{WS}                    { skip(); }
{NEWLINE}               { skip(); }

"//"[^\r\n]*              { if (lex_collect_comments) record_token(TOK_COMMENT); else skip(); }
"/*"([^*]|"*"+[^*/])*"*"+"/" { if (lex_collect_comments) record_token(TOK_COMMENT); else skip(); }
"/*"                    { record_error("Unterminated block comment"); }
^[ \t]*"#"[^\r\n]*        { record_token(TOK_PREPROC); }
\"(\\.|[^\"\\\r\n])*\"     { record_token(TOK_STRING_LITERAL); }
\"(\\.|[^\"\\\r\n])*       { record_error("Unterminated string literal"); }

"int"       { record_token(TOK_INT); }
"float"     { record_token(TOK_FLOAT); }
"bool"      { record_token(TOK_BOOL); }
"char"      { record_token(TOK_CHAR); }
"const"     { record_token(TOK_CONST); }
"if"        { record_token(TOK_IF); }
"else"      { record_token(TOK_ELSE); }
"while"     { record_token(TOK_WHILE); }
"for"       { record_token(TOK_FOR); }
"return"    { record_token(TOK_RETURN); }
"print"     { record_token(TOK_PRINT); }
"read"      { record_token(TOK_READ); }
"true"      { record_token(TOK_TRUE); }
"false"     { record_token(TOK_FALSE); }

"<="    { record_token(TOK_LE); }
">="    { record_token(TOK_GE); }
"=="    { record_token(TOK_EQ); }
"!="    { record_token(TOK_NEQ); }
"&&"    { record_token(TOK_AND); }
"||"    { record_token(TOK_OR); }
"++"    { record_token(TOK_PLUSPLUS); }
"--"    { record_token(TOK_MINUSMINUS); }
"->"    { record_token(TOK_ARROW); }
"<<"    { record_token(TOK_SHL); }
">>"    { record_token(TOK_SHR); }
"::"    { record_token(TOK_SCOPE); }
"+"     { record_token(TOK_ADD); }
"-"     { record_token(TOK_SUB); }
"*"     { record_token(TOK_MUL); }
"/"     { record_token(TOK_DIV); }
"%"     { record_token(TOK_MOD); }
"<"     { record_token(TOK_LT); }
">"     { record_token(TOK_GT); }
"!"     { record_token(TOK_NOT); }
"="     { record_token(TOK_ASSIGN); }
"&"     { record_token(TOK_AMP); }
"."     { record_token(TOK_DOT); }
":"     { record_token(TOK_COLON); }
"?"     { record_token(TOK_QUESTION); }
"~"     { record_token(TOK_TILDE); }

";"     { record_token(TOK_SEMICOLON); }
","     { record_token(TOK_COMMA); }
"("     { record_token(TOK_LPAREN); }
")"     { record_token(TOK_RPAREN); }
"["     { record_token(TOK_LBRACKET); }
"]"     { record_token(TOK_RBRACKET); }
"{"     { record_token(TOK_LBRACE); s_scope_level++; }
"}"     { record_token(TOK_RBRACE); s_scope_level--; if (s_scope_level < 0) s_scope_level = 0; }

{FLOAT_LIT}     { record_token(TOK_FLOAT_LITERAL); }
{INT_LIT}       { record_token(TOK_INT_LITERAL); }
{ID}            { record_token(TOK_IDENTIFIER); }

.               { record_error("Unknown character"); }

%%

int lex_file(const char *input_path, TokenList *tokens, LexErrorList *errors) {
    /* Open in text mode so Windows CRLF line endings are translated
       to a single '\n' and token offsets remain stable across platforms. */
    FILE *fp = fopen(input_path, "r");
    if (!fp) {
        return -1;
    }

    s_tokens = (TokenList){0};
    s_errors = (LexErrorList){0};
    s_token_id    = 0;
    s_line        = 1;
    s_column      = 1;
    s_offset      = 0;
    s_scope_level = 0;

    yyin = fp;
    while (yylex() != 0) {
        /* tokens are collected inside the scanner actions */
    }
    fclose(fp);

    *tokens = s_tokens;
    *errors = s_errors;
    return 0;
}
