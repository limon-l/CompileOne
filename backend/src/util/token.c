#include "token.h"

#include <string.h>

#include "strbuf.h"

/* Concrete dynamic-array instantiations for the token lists. */
DARRAY_DEFINE(Token, TokenList)
DARRAY_DEFINE(LexError, LexErrorList)

/* ============================================================
   Token lexicon — the single backend source of truth for the
   display name, category, subtype, color and educational
   description of every mini-c token kind.
   (Python mirrors these tables in app/domain/lexicon.py.)
   ============================================================ */

typedef struct TokenInfo {
    TokenKind kind;
    const char *name;
    TokenCategory category;
    const char *subtype;
    const char *color;
    const char *description;
} TokenInfo;

static const TokenInfo kTokenInfo[] = {
    /* ---- keywords ---- */
    {TOK_INT,    "KW_INT",    CAT_TYPE,      "type-specifier", "#4ec9b0",
     "32-bit signed integer type specifier"},
    {TOK_FLOAT,  "KW_FLOAT",  CAT_TYPE,      "type-specifier", "#4ec9b0",
     "32-bit IEEE-754 floating-point type specifier"},
    {TOK_BOOL,   "KW_BOOL",   CAT_TYPE,      "type-specifier", "#4ec9b0",
     "1-byte boolean type specifier"},
    {TOK_CHAR,   "KW_CHAR",   CAT_TYPE,      "type-specifier", "#4ec9b0",
     "8-bit character type specifier"},
    {TOK_CONST,  "KW_CONST",  CAT_KEYWORD,   "qualifier",      "#569cd6",
     "Constant qualifier: variable cannot be reassigned"},
    {TOK_IF,     "KW_IF",     CAT_KEYWORD,   "control-flow",   "#569cd6",
     "Conditional branch keyword"},
    {TOK_ELSE,   "KW_ELSE",   CAT_KEYWORD,   "control-flow",   "#569cd6",
     "Alternate branch keyword"},
    {TOK_WHILE,  "KW_WHILE",  CAT_KEYWORD,   "control-flow",   "#569cd6",
     "Pre-test loop keyword"},
    {TOK_FOR,    "KW_FOR",    CAT_KEYWORD,   "control-flow",   "#569cd6",
     "Counter-controlled loop keyword"},
    {TOK_RETURN, "KW_RETURN", CAT_KEYWORD,   "control-flow",   "#569cd6",
     "Return-from-function keyword"},
    {TOK_PRINT,  "KW_PRINT",  CAT_KEYWORD,   "output",         "#569cd6",
     "Output statement keyword (educational)"},
    {TOK_READ,   "KW_READ",   CAT_KEYWORD,   "input",          "#569cd6",
     "Input statement keyword: reads a value from stdin into a variable"},
    {TOK_TRUE,   "BOOL_TRUE", CAT_LITERAL,   "boolean",        "#569cd6",
     "Boolean literal true"},
    {TOK_FALSE,  "BOOL_FALSE", CAT_LITERAL,  "boolean",        "#569cd6",
     "Boolean literal false"},

    /* ---- operators ---- */
    {TOK_ADD,    "OP_ADD",    CAT_OPERATOR,  "arithmetic",     "#d4d4d4",
     "Arithmetic addition operator"},
    {TOK_SUB,    "OP_SUB",    CAT_OPERATOR,  "arithmetic",     "#d4d4d4",
     "Arithmetic subtraction operator"},
    {TOK_MUL,    "OP_MUL",    CAT_OPERATOR,  "arithmetic",     "#d4d4d4",
     "Arithmetic multiplication operator"},
    {TOK_DIV,    "OP_DIV",    CAT_OPERATOR,  "arithmetic",     "#d4d4d4",
     "Arithmetic division operator"},
    {TOK_MOD,    "OP_MOD",    CAT_OPERATOR,  "arithmetic",     "#d4d4d4",
     "Arithmetic modulo operator"},
    {TOK_LE,     "OP_LE",     CAT_OPERATOR,  "relational",     "#d4d4d4",
     "Relational less-than-or-equal operator"},
    {TOK_GE,     "OP_GE",     CAT_OPERATOR,  "relational",     "#d4d4d4",
     "Relational greater-than-or-equal operator"},
    {TOK_EQ,     "OP_EQ",     CAT_OPERATOR,  "relational",     "#d4d4d4",
     "Equality comparison operator"},
    {TOK_NEQ,    "OP_NEQ",    CAT_OPERATOR,  "relational",     "#d4d4d4",
     "Inequality comparison operator"},
    {TOK_LT,     "OP_LT",     CAT_OPERATOR,  "relational",     "#d4d4d4",
     "Relational less-than operator"},
    {TOK_GT,     "OP_GT",     CAT_OPERATOR,  "relational",     "#d4d4d4",
     "Relational greater-than operator"},
    {TOK_AND,    "OP_AND",    CAT_OPERATOR,  "logical",        "#d4d4d4",
     "Logical AND operator"},
    {TOK_OR,     "OP_OR",     CAT_OPERATOR,  "logical",        "#d4d4d4",
     "Logical OR operator"},
    {TOK_NOT,    "OP_NOT",    CAT_OPERATOR,  "logical",        "#d4d4d4",
     "Logical NOT operator"},
    {TOK_ASSIGN, "OP_ASSIGN", CAT_OPERATOR,  "assignment",     "#d4d4d4",
     "Assignment operator"},
    {TOK_PLUSPLUS,  "OP_INC", CAT_OPERATOR,  "increment",      "#d4d4d4",
     "Pre/post increment operator"},
    {TOK_MINUSMINUS,"OP_DEC", CAT_OPERATOR,  "decrement",      "#d4d4d4",
     "Pre/post decrement operator"},

    /* ---- delimiters ---- */
    {TOK_SEMICOLON, "SEMICOLON", CAT_DELIMITER, "statement",   "#d4d4d4",
     "Statement terminator"},
    {TOK_LBRACE, "LBRACE", CAT_DELIMITER, "block", "#d4d4d4",
     "Block open delimiter"},
    {TOK_RBRACE, "RBRACE", CAT_DELIMITER, "block", "#d4d4d4",
     "Block close delimiter"},
    {TOK_LPAREN, "LPAREN", CAT_DELIMITER, "grouping", "#d4d4d4",
     "Grouping / call delimiter (open)"},
    {TOK_RPAREN, "RPAREN", CAT_DELIMITER, "grouping", "#d4d4d4",
     "Grouping / call delimiter (close)"},
    {TOK_COMMA,  "COMMA",   CAT_DELIMITER, "separator", "#d4d4d4",
     "Argument / expression separator"},
    {TOK_LBRACKET, "LBRACKET", CAT_DELIMITER, "array", "#d4d4d4",
     "Array subscript delimiter (open)"},
    {TOK_RBRACKET, "RBRACKET", CAT_DELIMITER, "array", "#d4d4d4",
     "Array subscript delimiter (close)"},

    /* ---- literals & identifiers ---- */
    {TOK_INT_LITERAL,    "INT_LITERAL",    CAT_LITERAL,   "integer",  "#b5cea8",
     "Integer literal constant"},
    {TOK_FLOAT_LITERAL,  "FLOAT_LITERAL",  CAT_LITERAL,   "floating", "#b5cea8",
     "Floating-point literal constant"},
    {TOK_STRING_LITERAL, "STRING_LITERAL", CAT_LITERAL,   "string",   "#ce9178",
     "String literal constant"},
    {TOK_IDENTIFIER,     "IDENTIFIER",     CAT_IDENTIFIER,"user-identifier", "#9cdcfe",
     "User-defined identifier"},

    /* ---- C/C++ & preprocessor extensions ---- */
    {TOK_DOT,     "OP_DOT",     CAT_OPERATOR,  "member",       "#d4d4d4",
     "Member/field access operator (C/C++)"},
    {TOK_COLON,   "OP_COLON",   CAT_OPERATOR,  "label",        "#d4d4d4",
     "Label / ternary separator (C/C++)"},
    {TOK_SCOPE,   "OP_SCOPE",   CAT_OPERATOR,  "scope",        "#d4d4d4",
     "C++ namespace/class scope-resolution operator"},
    {TOK_AMP,     "OP_AMP",     CAT_OPERATOR,  "bitwise",      "#d4d4d4",
     "Address-of or bitwise AND operator (C/C++)"},
    {TOK_SHL,     "OP_SHL",     CAT_OPERATOR,  "shift",        "#d4d4d4",
     "Left shift / stream-insertion operator"},
    {TOK_SHR,     "OP_SHR",     CAT_OPERATOR,  "shift",        "#d4d4d4",
     "Right shift / stream-extraction operator"},
    {TOK_ARROW,   "OP_ARROW",   CAT_OPERATOR,  "member",       "#d4d4d4",
     "Pointer member-access operator (C/C++)"},
    {TOK_QUESTION,"OP_QUESTION",CAT_OPERATOR,  "ternary",      "#d4d4d4",
     "Ternary conditional operator (C/C++)"},
    {TOK_TILDE,   "OP_TILDE",   CAT_OPERATOR,  "bitwise",      "#d4d4d4",
     "Bitwise complement operator (C/C++)"},
    {TOK_PREPROC, "PREPROC",    CAT_COMMENT,   "preprocessor", "#569cd6",
     "Preprocessor directive (skipped by the compiler)"},

    /* ---- special ---- */
    {TOK_COMMENT,  "COMMENT",  CAT_COMMENT, "comment",  "#6a9955",
     "Source comment (skipped by the parser)"},
    {TOK_LEX_ERROR, "LEX_ERROR", CAT_ERROR, "lexical",  "#f44747",
     "Character not valid in mini-c"},
};

static const size_t kTokenInfoCount = sizeof(kTokenInfo) / sizeof(kTokenInfo[0]);

static const TokenInfo *find_info(TokenKind kind) {
    for (size_t i = 0; i < kTokenInfoCount; i++) {
        if (kTokenInfo[i].kind == kind) {
            return &kTokenInfo[i];
        }
    }
    return &kTokenInfo[kTokenInfoCount - 1]; /* TOK_LEX_ERROR fallback */
}

const char *token_name(TokenKind kind) {
    return find_info(kind)->name;
}

TokenKind token_from_name(const char *name) {
    for (size_t i = 0; i < kTokenInfoCount; i++) {
        if (strcmp(kTokenInfo[i].name, name) == 0) {
            return kTokenInfo[i].kind;
        }
    }
    return TOK_LEX_ERROR;
}

TokenCategory token_category(TokenKind kind) {
    return find_info(kind)->category;
}

const char *token_subtype(TokenKind kind) {
    return find_info(kind)->subtype;
}

const char *token_color(TokenKind kind) {
    return find_info(kind)->color;
}

const char *token_description(TokenKind kind) {
    return find_info(kind)->description;
}

static const char *const kCategoryNames[CAT_COUNT] = {
    "keyword", "type", "identifier", "literal",
    "operator", "delimiter", "comment", "error",
};

const char *token_category_name(TokenCategory cat) {
    if (cat < 0 || cat >= CAT_COUNT) {
        return "unknown";
    }
    return kCategoryNames[cat];
}

Token token_make(TokenKind kind, const char *lexeme, size_t len,
                 int id, int line, int column, size_t offset_start, size_t offset_end,
                 int scope_level) {
    Token t;
    t.id = id;
    t.line = line;
    t.column = column;
    t.lexeme = (char *)malloc(len + 1);
    memcpy(t.lexeme, lexeme, len);
    t.lexeme[len] = '\0';
    t.kind = kind;
    t.category = token_category(kind);
    t.subtype = token_subtype(kind);
    t.length = len;
    t.scope_level = scope_level;
    t.color = token_color(kind);
    t.description = token_description(kind);
    t.offset_start = offset_start;
    t.offset_end = offset_end;
    return t;
}

void token_free(Token *t) {
    free(t->lexeme);
    t->lexeme = NULL;
}

void lex_error_free(LexError *e) {
    free(e->lexeme);
    e->lexeme = NULL;
}
