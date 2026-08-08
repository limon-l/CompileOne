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

    /* ---- C / C++ / Java keywords (native language front end) ---- */
    TOK_VOID,        /* void */
    TOK_DOUBLE,      /* double */
    TOK_DO,          /* do */
    TOK_BREAK,       /* break */
    TOK_CONTINUE,    /* continue */
    TOK_CLASS,       /* class (C++ / Java) */
    TOK_PUBLIC,      /* public */
    TOK_PRIVATE,     /* private */
    TOK_PROTECTED,   /* protected */
    TOK_STATIC,      /* static */
    TOK_NEW,         /* new */
    TOK_THIS,        /* this */
    TOK_EXTENDS,     /* extends (Java) */
    TOK_IMPLEMENTS,  /* implements (Java) */
    TOK_INTERFACE,   /* interface (Java) */
    TOK_IMPORT,      /* import (Java) */
    TOK_PACKAGE,     /* package (Java) */
    TOK_NAMESPACE,   /* namespace (C++) */
    TOK_USING,       /* using (C++) */
    TOK_SUPER,       /* super (Java) */
    TOK_FINAL,       /* final (Java) */
    TOK_ABSTRACT,    /* abstract (Java) */
    TOK_BOOLEAN,     /* boolean (Java) */
    TOK_BYTE,        /* byte (Java) */
    TOK_SHORT,       /* short */
    TOK_LONG,        /* long */
    TOK_NULL,        /* null (Java) */
    TOK_DELETE,      /* delete (C++) */
    TOK_SIZEOF,      /* sizeof (C/C++) */
    TOK_STRUCT,      /* struct (C/C++) */
    TOK_TYPEDEF,     /* typedef (C/C++) */
    TOK_ENUM,        /* enum (C/C++) */
    TOK_UNION,       /* union (C/C++) */
    TOK_SWITCH,      /* switch */
    TOK_CASE,        /* case */
    TOK_DEFAULT,     /* default */
    TOK_GOTO,        /* goto (C/C++) */
    TOK_EXTERN,      /* extern (C/C++) */
    TOK_SIGNED,      /* signed (C/C++) */
    TOK_UNSIGNED,    /* unsigned (C/C++) */
    TOK_TEMPLATE,    /* template (C++) */
    TOK_VIRTUAL,     /* virtual (C++) */
    TOK_FRIEND,      /* friend (C++) */
    TOK_INLINE,      /* inline (C/C++) */
    TOK_TRY,         /* try (Java) */
    TOK_CATCH,       /* catch (Java) */
    TOK_FINALLY,     /* finally (Java) */
    TOK_THROW,       /* throw (Java) */
    TOK_THROWS,      /* throws (Java) */
    TOK_INSTANCEOF,  /* instanceof (Java) */
    TOK_SYNCHRONIZED,/* synchronized (Java) */
    TOK_OPERATOR,    /* operator (C++) */
    TOK_STRING,      /* String (Java type specifier) */

    /* ---- C-family literals & operators ---- */
    TOK_CHAR_LITERAL,/* 'a' */
    TOK_PLUSEQ,      /* += */
    TOK_MINUSEQ,     /* -= */
    TOK_STAREQ,      /* *= */
    TOK_SLASHEQ,     /* /= */
    TOK_PERCENTEQ,   /* %= */
    TOK_ANDEQ,       /* &= */
    TOK_OREQ,        /* |= */
    TOK_XOREQ,       /* ^= */
    TOK_SHLASSIGN,   /* <<= */
    TOK_SHRASSIGN,   /* >>= */
    TOK_PIPE,        /* | */
    TOK_CARET,       /* ^ */

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
