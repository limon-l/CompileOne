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
static int    s_native    = 0;   /* 1 => lexing C/C++/Java (extended set) */

void lex_set_language(const char *lang) {
    s_native = lang &&
               (strcmp(lang, "c") == 0 || strcmp(lang, "c++") == 0 ||
                strcmp(lang, "java") == 0);
}

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

/* Record a token from a substring of yytext (start, start+len). Used to
   re-emit the two base tokens of a compound assignment for mini-c. */
static void record_substr(TokenKind kind, size_t start, size_t len) {
    Token t = token_make(kind, yytext + start, len, ++s_token_id,
                         s_line, s_column,
                         (size_t)s_offset, (size_t)s_offset + len,
                         s_scope_level);
    TokenList_push(&s_tokens, t);
    lexer_advance(yytext + start, len);
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

"void"      { if (s_native) record_token(TOK_VOID); else record_token(TOK_IDENTIFIER); }
"double"    { if (s_native) record_token(TOK_DOUBLE); else record_token(TOK_IDENTIFIER); }
"do"        { if (s_native) record_token(TOK_DO); else record_token(TOK_IDENTIFIER); }
"break"     { if (s_native) record_token(TOK_BREAK); else record_token(TOK_IDENTIFIER); }
"continue"  { if (s_native) record_token(TOK_CONTINUE); else record_token(TOK_IDENTIFIER); }
"class"     { if (s_native) record_token(TOK_CLASS); else record_token(TOK_IDENTIFIER); }
"public"    { if (s_native) record_token(TOK_PUBLIC); else record_token(TOK_IDENTIFIER); }
"private"   { if (s_native) record_token(TOK_PRIVATE); else record_token(TOK_IDENTIFIER); }
"protected" { if (s_native) record_token(TOK_PROTECTED); else record_token(TOK_IDENTIFIER); }
"static"    { if (s_native) record_token(TOK_STATIC); else record_token(TOK_IDENTIFIER); }
"new"       { if (s_native) record_token(TOK_NEW); else record_token(TOK_IDENTIFIER); }
"this"      { if (s_native) record_token(TOK_THIS); else record_token(TOK_IDENTIFIER); }
"extends"   { if (s_native) record_token(TOK_EXTENDS); else record_token(TOK_IDENTIFIER); }
"implements" { if (s_native) record_token(TOK_IMPLEMENTS); else record_token(TOK_IDENTIFIER); }
"interface" { if (s_native) record_token(TOK_INTERFACE); else record_token(TOK_IDENTIFIER); }
"import"    { if (s_native) record_token(TOK_IMPORT); else record_token(TOK_IDENTIFIER); }
"package"   { if (s_native) record_token(TOK_PACKAGE); else record_token(TOK_IDENTIFIER); }
"namespace" { if (s_native) record_token(TOK_NAMESPACE); else record_token(TOK_IDENTIFIER); }
"using"     { if (s_native) record_token(TOK_USING); else record_token(TOK_IDENTIFIER); }
"super"     { if (s_native) record_token(TOK_SUPER); else record_token(TOK_IDENTIFIER); }
"final"     { if (s_native) record_token(TOK_FINAL); else record_token(TOK_IDENTIFIER); }
"abstract"  { if (s_native) record_token(TOK_ABSTRACT); else record_token(TOK_IDENTIFIER); }
"boolean"   { if (s_native) record_token(TOK_BOOLEAN); else record_token(TOK_IDENTIFIER); }
"byte"      { if (s_native) record_token(TOK_BYTE); else record_token(TOK_IDENTIFIER); }
"short"     { if (s_native) record_token(TOK_SHORT); else record_token(TOK_IDENTIFIER); }
"long"      { if (s_native) record_token(TOK_LONG); else record_token(TOK_IDENTIFIER); }
"null"      { if (s_native) record_token(TOK_NULL); else record_token(TOK_IDENTIFIER); }
"delete"    { if (s_native) record_token(TOK_DELETE); else record_token(TOK_IDENTIFIER); }
"sizeof"    { if (s_native) record_token(TOK_SIZEOF); else record_token(TOK_IDENTIFIER); }
"struct"    { if (s_native) record_token(TOK_STRUCT); else record_token(TOK_IDENTIFIER); }
"typedef"   { if (s_native) record_token(TOK_TYPEDEF); else record_token(TOK_IDENTIFIER); }
"enum"      { if (s_native) record_token(TOK_ENUM); else record_token(TOK_IDENTIFIER); }
"union"     { if (s_native) record_token(TOK_UNION); else record_token(TOK_IDENTIFIER); }
"switch"    { if (s_native) record_token(TOK_SWITCH); else record_token(TOK_IDENTIFIER); }
"case"      { if (s_native) record_token(TOK_CASE); else record_token(TOK_IDENTIFIER); }
"default"   { if (s_native) record_token(TOK_DEFAULT); else record_token(TOK_IDENTIFIER); }
"goto"      { if (s_native) record_token(TOK_GOTO); else record_token(TOK_IDENTIFIER); }
"extern"    { if (s_native) record_token(TOK_EXTERN); else record_token(TOK_IDENTIFIER); }
"signed"    { if (s_native) record_token(TOK_SIGNED); else record_token(TOK_IDENTIFIER); }
"unsigned"  { if (s_native) record_token(TOK_UNSIGNED); else record_token(TOK_IDENTIFIER); }
"template"  { if (s_native) record_token(TOK_TEMPLATE); else record_token(TOK_IDENTIFIER); }
"virtual"   { if (s_native) record_token(TOK_VIRTUAL); else record_token(TOK_IDENTIFIER); }
"friend"    { if (s_native) record_token(TOK_FRIEND); else record_token(TOK_IDENTIFIER); }
"inline"    { if (s_native) record_token(TOK_INLINE); else record_token(TOK_IDENTIFIER); }
"try"       { if (s_native) record_token(TOK_TRY); else record_token(TOK_IDENTIFIER); }
"catch"     { if (s_native) record_token(TOK_CATCH); else record_token(TOK_IDENTIFIER); }
"finally"   { if (s_native) record_token(TOK_FINALLY); else record_token(TOK_IDENTIFIER); }
"throw"     { if (s_native) record_token(TOK_THROW); else record_token(TOK_IDENTIFIER); }
"throws"    { if (s_native) record_token(TOK_THROWS); else record_token(TOK_IDENTIFIER); }
"instanceof" { if (s_native) record_token(TOK_INSTANCEOF); else record_token(TOK_IDENTIFIER); }
"synchronized" { if (s_native) record_token(TOK_SYNCHRONIZED); else record_token(TOK_IDENTIFIER); }
"operator"  { if (s_native) record_token(TOK_OPERATOR); else record_token(TOK_IDENTIFIER); }
"String"    { if (s_native) record_token(TOK_STRING); else record_token(TOK_IDENTIFIER); }

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
"+="    { if (s_native) record_token(TOK_PLUSEQ); else { record_substr(TOK_ADD, 0, 1); record_substr(TOK_ASSIGN, 1, 1); } }
"-="    { if (s_native) record_token(TOK_MINUSEQ); else { record_substr(TOK_SUB, 0, 1); record_substr(TOK_ASSIGN, 1, 1); } }
"*="    { if (s_native) record_token(TOK_STAREQ); else { record_substr(TOK_MUL, 0, 1); record_substr(TOK_ASSIGN, 1, 1); } }
"/="    { if (s_native) record_token(TOK_SLASHEQ); else { record_substr(TOK_DIV, 0, 1); record_substr(TOK_ASSIGN, 1, 1); } }
"%="    { if (s_native) record_token(TOK_PERCENTEQ); else { record_substr(TOK_MOD, 0, 1); record_substr(TOK_ASSIGN, 1, 1); } }
"&="    { if (s_native) record_token(TOK_ANDEQ); else { record_substr(TOK_AMP, 0, 1); record_substr(TOK_ASSIGN, 1, 1); } }
"|="    { if (s_native) record_token(TOK_OREQ); else { record_substr(TOK_PIPE, 0, 1); record_substr(TOK_ASSIGN, 1, 1); } }
"^="    { if (s_native) record_token(TOK_XOREQ); else { record_substr(TOK_CARET, 0, 1); record_substr(TOK_ASSIGN, 1, 1); } }
"<<="   { if (s_native) record_token(TOK_SHLASSIGN); else { record_substr(TOK_SHL, 0, 2); record_substr(TOK_ASSIGN, 2, 1); } }
">>="   { if (s_native) record_token(TOK_SHRASSIGN); else { record_substr(TOK_SHR, 0, 2); record_substr(TOK_ASSIGN, 2, 1); } }
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
"|"     { record_token(TOK_PIPE); }
"^"     { record_token(TOK_CARET); }
"."     { record_token(TOK_DOT); }
":"     { record_token(TOK_COLON); }
"?"     { record_token(TOK_QUESTION); }
"~"     { record_token(TOK_TILDE); }

"'"(\\.|[^'\\\r\n])"'"   { if (s_native) record_token(TOK_CHAR_LITERAL); else record_error("Unknown character"); }

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
