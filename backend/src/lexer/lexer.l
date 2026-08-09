%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
static int s_scope_level = 0;
static int s_native    = 0;   /* 1 => lexing a native language (extended set) */
static int s_lang      = 0;   /* 0=mini-c, 1=c, 2=c++, 3=java */

/* ----------------------------------------------------------------
   Per-language keyword tables. Each language has its own reserved-word
   set so (for example) 'class'/'public' are keywords in Java/C++ but
   ordinary identifiers in C, and 'String' is a Java type keyword but an
   identifier in C/C++. Words without a dedicated token kind map to the
   generic TOK_KEYWORD.
   ---------------------------------------------------------------- */

typedef struct KWord { const char *word; TokenKind kind; } KWord;

static const KWord kMiniKw[] = {
    {"int",TOK_INT},{"float",TOK_FLOAT},{"bool",TOK_BOOL},{"char",TOK_CHAR},
    {"const",TOK_CONST},{"if",TOK_IF},{"else",TOK_ELSE},{"while",TOK_WHILE},
    {"for",TOK_FOR},{"return",TOK_RETURN},{"print",TOK_PRINT},{"read",TOK_READ},
    {"true",TOK_TRUE},{"false",TOK_FALSE},
};

static const KWord kCKw[] = {
    {"char",TOK_CHAR},{"double",TOK_DOUBLE},{"float",TOK_FLOAT},{"int",TOK_INT},
    {"long",TOK_LONG},{"short",TOK_SHORT},{"signed",TOK_SIGNED},{"unsigned",TOK_UNSIGNED},
    {"void",TOK_VOID},{"_Bool",TOK_BOOL},
    {"break",TOK_BREAK},{"case",TOK_CASE},{"continue",TOK_CONTINUE},{"default",TOK_DEFAULT},
    {"do",TOK_DO},{"else",TOK_ELSE},{"for",TOK_FOR},{"goto",TOK_GOTO},{"if",TOK_IF},
    {"return",TOK_RETURN},{"switch",TOK_SWITCH},{"while",TOK_WHILE},
    {"auto",TOK_KEYWORD},{"const",TOK_CONST},{"enum",TOK_ENUM},{"extern",TOK_EXTERN},
    {"inline",TOK_INLINE},{"register",TOK_KEYWORD},{"restrict",TOK_KEYWORD},
    {"sizeof",TOK_SIZEOF},{"static",TOK_STATIC},{"struct",TOK_STRUCT},{"typedef",TOK_TYPEDEF},
    {"union",TOK_UNION},{"volatile",TOK_KEYWORD},
    {"_Alignas",TOK_KEYWORD},{"_Alignof",TOK_KEYWORD},{"_Atomic",TOK_KEYWORD},
    {"_Generic",TOK_KEYWORD},{"_Noreturn",TOK_KEYWORD},{"_Static_assert",TOK_KEYWORD},
    {"_Thread_local",TOK_KEYWORD},
};

static const KWord kCppKw[] = {
    {"char",TOK_CHAR},{"double",TOK_DOUBLE},{"float",TOK_FLOAT},{"int",TOK_INT},
    {"long",TOK_LONG},{"short",TOK_SHORT},{"signed",TOK_SIGNED},{"unsigned",TOK_UNSIGNED},
    {"void",TOK_VOID},{"bool",TOK_BOOL},{"wchar_t",TOK_KEYWORD},
    {"break",TOK_BREAK},{"case",TOK_CASE},{"continue",TOK_CONTINUE},{"default",TOK_DEFAULT},
    {"do",TOK_DO},{"else",TOK_ELSE},{"for",TOK_FOR},{"goto",TOK_GOTO},{"if",TOK_IF},
    {"return",TOK_RETURN},{"switch",TOK_SWITCH},{"while",TOK_WHILE},
    {"auto",TOK_KEYWORD},{"const",TOK_CONST},{"enum",TOK_ENUM},{"extern",TOK_EXTERN},
    {"inline",TOK_INLINE},{"register",TOK_KEYWORD},{"restrict",TOK_KEYWORD},
    {"sizeof",TOK_SIZEOF},{"static",TOK_STATIC},{"struct",TOK_STRUCT},{"typedef",TOK_TYPEDEF},
    {"union",TOK_UNION},{"volatile",TOK_KEYWORD},
    {"class",TOK_CLASS},{"public",TOK_PUBLIC},{"private",TOK_PRIVATE},{"protected",TOK_PROTECTED},
    {"namespace",TOK_NAMESPACE},{"using",TOK_USING},{"template",TOK_TEMPLATE},
    {"virtual",TOK_VIRTUAL},{"friend",TOK_FRIEND},{"operator",TOK_OPERATOR},
    {"new",TOK_NEW},{"delete",TOK_DELETE},{"this",TOK_THIS},
    {"true",TOK_TRUE},{"false",TOK_FALSE},{"nullptr",TOK_KEYWORD},
    {"alignas",TOK_KEYWORD},{"alignof",TOK_KEYWORD},{"constexpr",TOK_KEYWORD},
    {"const_cast",TOK_KEYWORD},{"decltype",TOK_KEYWORD},{"dynamic_cast",TOK_KEYWORD},
    {"explicit",TOK_KEYWORD},{"export",TOK_KEYWORD},{"import",TOK_IMPORT},
    {"module",TOK_KEYWORD},{"mutable",TOK_KEYWORD},{"noexcept",TOK_KEYWORD},
    {"reinterpret_cast",TOK_KEYWORD},{"static_assert",TOK_KEYWORD},{"static_cast",TOK_KEYWORD},
    {"thread_local",TOK_KEYWORD},{"typeid",TOK_KEYWORD},{"typename",TOK_KEYWORD},
};

static const KWord kJavaKw[] = {
    {"boolean",TOK_BOOLEAN},{"byte",TOK_BYTE},{"char",TOK_CHAR},{"double",TOK_DOUBLE},
    {"float",TOK_FLOAT},{"int",TOK_INT},{"long",TOK_LONG},{"short",TOK_SHORT},
    {"void",TOK_VOID},{"String",TOK_STRING},
    {"abstract",TOK_ABSTRACT},{"assert",TOK_KEYWORD},{"break",TOK_BREAK},{"case",TOK_CASE},
    {"catch",TOK_CATCH},{"class",TOK_CLASS},{"const",TOK_KEYWORD},{"continue",TOK_CONTINUE},
    {"default",TOK_DEFAULT},{"do",TOK_DO},{"else",TOK_ELSE},{"enum",TOK_ENUM},
    {"extends",TOK_EXTENDS},{"final",TOK_FINAL},{"finally",TOK_FINALLY},{"for",TOK_FOR},
    {"goto",TOK_KEYWORD},{"if",TOK_IF},{"implements",TOK_IMPLEMENTS},{"import",TOK_IMPORT},
    {"instanceof",TOK_INSTANCEOF},{"interface",TOK_INTERFACE},{"native",TOK_KEYWORD},
    {"new",TOK_NEW},{"package",TOK_PACKAGE},{"private",TOK_PRIVATE},{"protected",TOK_PROTECTED},
    {"public",TOK_PUBLIC},{"return",TOK_RETURN},{"static",TOK_STATIC},{"strictfp",TOK_KEYWORD},
    {"super",TOK_SUPER},{"switch",TOK_SWITCH},{"synchronized",TOK_SYNCHRONIZED},
    {"this",TOK_THIS},{"throw",TOK_THROW},{"throws",TOK_THROWS},{"transient",TOK_KEYWORD},
    {"try",TOK_TRY},{"volatile",TOK_KEYWORD},{"while",TOK_WHILE},
    {"true",TOK_TRUE},{"false",TOK_FALSE},{"null",TOK_NULL},
};

static TokenKind find_kw(const KWord *table, size_t n, const char *word) {
    for (size_t i = 0; i < n; i++) {
        if (strcmp(table[i].word, word) == 0) {
            return table[i].kind;
        }
    }
    return TOK_IDENTIFIER;
}

/* Classify the current yytext match against the active language's
   reserved-word set. Returns TOK_IDENTIFIER when it is not a keyword. */
static TokenKind classify_keyword(void) {
    switch (s_lang) {
        case 1:  return find_kw(kCKw, sizeof(kCKw)/sizeof(kCKw[0]), yytext);
        case 2:  return find_kw(kCppKw, sizeof(kCppKw)/sizeof(kCppKw[0]), yytext);
        case 3:  return find_kw(kJavaKw, sizeof(kJavaKw)/sizeof(kJavaKw[0]), yytext);
        default: return find_kw(kMiniKw, sizeof(kMiniKw)/sizeof(kMiniKw[0]), yytext);
    }
}

void lex_set_language(const char *lang) {
    s_lang = 0;
    if (lang && strcmp(lang, "c") == 0) {
        s_lang = 1;
    } else if (lang && strcmp(lang, "c++") == 0) {
        s_lang = 2;
    } else if (lang && strcmp(lang, "java") == 0) {
        s_lang = 3;
    }
    s_native = (s_lang != 0);
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
{ID}            { record_token(classify_keyword()); }

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
