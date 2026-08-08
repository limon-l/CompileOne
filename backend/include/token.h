#ifndef CO1_TOKEN_H
#define CO1_TOKEN_H

#include <stddef.h>

#include "darray.h"

/* Token kinds for the mini-c study language.
   Numeric values deliberately start at 258 (Bison's first terminal code)
   so the same header can seed the future parser.tab.h mapping. */
typedef enum TokenKind {
    TOK_EOF = 0,

    /* keywords */
    TOK_INT = 258,
    TOK_FLOAT,
    TOK_BOOL,
    TOK_CHAR,
    TOK_CONST,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_RETURN,
    TOK_PRINT,
    TOK_READ,
    TOK_TRUE,
    TOK_FALSE,

    /* operators */
    TOK_ADD,
    TOK_SUB,
    TOK_MUL,
    TOK_DIV,
    TOK_MOD,
    TOK_LE,
    TOK_GE,
    TOK_EQ,
    TOK_NEQ,
    TOK_LT,
    TOK_GT,
    TOK_AND,
    TOK_OR,
    TOK_NOT,
    TOK_ASSIGN,
    TOK_PLUSPLUS,
    TOK_MINUSMINUS,

    /* delimiters */
    TOK_SEMICOLON,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_COMMA,
    TOK_LBRACKET,
    TOK_RBRACKET,

    /* literals & identifiers */
    TOK_INT_LITERAL,
    TOK_FLOAT_LITERAL,
    TOK_STRING_LITERAL,
    TOK_IDENTIFIER,

    /* C/C++ & preprocessor extensions (tolerated by the study lexer so
       real C/C++ sources can be tokenized without spurious errors) */
    TOK_DOT,         /* . member access / struct field */
    TOK_COLON,       /* : label / ternary separator */
    TOK_SCOPE,       /* :: C++ scope resolution */
    TOK_AMP,         /* & address-of / bitwise AND */
    TOK_SHL,         /* << left shift / stream out */
    TOK_SHR,         /* >> right shift / stream in */
    TOK_ARROW,       /* -> pointer member access */
    TOK_QUESTION,    /* ? ternary condition */
    TOK_TILDE,       /* ~ bitwise complement */
    TOK_PREPROC,     /* # preprocessor directive line */

    /* special */
    TOK_COMMENT,
    TOK_LEX_ERROR
} TokenKind;

typedef enum TokenCategory {
    CAT_KEYWORD = 0,
    CAT_TYPE,
    CAT_IDENTIFIER,
    CAT_LITERAL,
    CAT_OPERATOR,
    CAT_DELIMITER,
    CAT_COMMENT,
    CAT_ERROR,
    CAT_COUNT
} TokenCategory;

typedef struct Token {
    int id;             /* 1-based token sequence number */
    int line;
    int column;         /* 1-based column of first character */
    char *lexeme;       /* owned copy of yytext */
    TokenKind kind;
    TokenCategory category;
    const char *subtype;    /* static string, e.g. "type-specifier" */
    size_t length;
    int scope_level;        /* lexical brace depth (0 = global) */
    const char *color;      /* static string, e.g. "#569cd6" */
    const char *description;/* static string */
    size_t offset_start;    /* byte offset of first char in source */
    size_t offset_end;      /* byte offset one past last char */
} Token;

typedef struct LexError {
    int line;
    int column;
    char *lexeme;       /* owned */
    const char *message;/* static string */
} LexError;

DARRAY_DECLARE(Token, TokenList)
DARRAY_DECLARE(LexError, LexErrorList)

/* Static lexicon accessors (all return static storage). */
const char *token_name(TokenKind kind);              /* "KW_INT", "IDENTIFIER" ... */
const char *token_category_name(TokenCategory cat);  /* "keyword", "operator" ... */
const char *token_subtype(TokenKind kind);
const char *token_color(TokenKind kind);
const char *token_description(TokenKind kind);
TokenCategory token_category(TokenKind kind);
TokenKind token_from_name(const char *name);         /* reverse of token_name() */

/* Build a token that owns a copy of `lexeme`. */
Token token_make(TokenKind kind, const char *lexeme, size_t len,
                 int id, int line, int column, size_t offset_start, size_t offset_end,
                 int scope_level);

void token_free(Token *t);
void lex_error_free(LexError *e);

#endif /* CO1_TOKEN_H */
