"""Language keyword and color lexicon.

This is the source of truth for editor syntax-coloring and for any UI
that needs per-token styling. It is a static mirror of token data that
is authoritatively defined in the backend compiler.
"""

from __future__ import annotations

# VS Code Dark+ palette
COLOR_TYPE = "#4ec9b0"        # int, float, bool, char
COLOR_KEYWORD = "#569cd6"     # if, else, while, return, print
COLOR_IDENTIFIER = "#9cdcfe"
COLOR_NUMBER = "#b5cea8"
COLOR_STRING = "#ce9178"
COLOR_COMMENT = "#6a9955"
COLOR_OPERATOR = "#d4d4d4"
COLOR_DELIMITER = "#d4d4d4"
COLOR_ERROR = "#f44747"

# --- Mini-C ---
MINIC_KEYWORDS = {
    # Types
    "int": COLOR_TYPE, "float": COLOR_TYPE, "bool": COLOR_TYPE, "char": COLOR_TYPE,
    # Keywords
    "const": COLOR_KEYWORD, "if": COLOR_KEYWORD, "else": COLOR_KEYWORD,
    "while": COLOR_KEYWORD, "for": COLOR_KEYWORD, "return": COLOR_KEYWORD,
    "print": COLOR_KEYWORD, "true": COLOR_KEYWORD, "false": COLOR_KEYWORD,
}

# --- C (C11) ---
C_KEYWORDS = {
    # Types
    "char": COLOR_TYPE, "double": COLOR_TYPE, "float": COLOR_TYPE, "int": COLOR_TYPE,
    "long": COLOR_TYPE, "short": COLOR_TYPE, "signed": COLOR_TYPE,
    "unsigned": COLOR_TYPE, "void": COLOR_TYPE, "_Bool": COLOR_TYPE,
    "_Complex": COLOR_TYPE, "_Imaginary": COLOR_TYPE,
    # Keywords
    "auto": COLOR_KEYWORD, "break": COLOR_KEYWORD, "case": COLOR_KEYWORD,
    "const": COLOR_KEYWORD, "continue": COLOR_KEYWORD, "default": COLOR_KEYWORD,
    "do": COLOR_KEYWORD, "else": COLOR_KEYWORD, "enum": COLOR_KEYWORD,
    "extern": COLOR_KEYWORD, "for": COLOR_KEYWORD, "goto": COLOR_KEYWORD,
    "if": COLOR_KEYWORD, "inline": COLOR_KEYWORD, "register": COLOR_KEYWORD,
    "restrict": COLOR_KEYWORD, "return": COLOR_KEYWORD, "sizeof": COLOR_KEYWORD,
    "static": COLOR_KEYWORD, "struct": COLOR_KEYWORD, "switch": COLOR_KEYWORD,
    "typedef": COLOR_KEYWORD, "union": COLOR_KEYWORD, "volatile": COLOR_KEYWORD,
    "while": COLOR_KEYWORD, "_Alignas": COLOR_KEYWORD, "_Alignof": COLOR_KEYWORD,
    "_Atomic": COLOR_KEYWORD, "_Generic": COLOR_KEYWORD, "_Noreturn": COLOR_KEYWORD,
    "_Static_assert": COLOR_KEYWORD, "_Thread_local": COLOR_KEYWORD,
}

# --- C++ (C++20) ---
CPP_KEYWORDS = {
    # Types
    "bool": COLOR_TYPE, "char": COLOR_TYPE, "char8_t": COLOR_TYPE,
    "char16_t": COLOR_TYPE, "char32_t": COLOR_TYPE, "double": COLOR_TYPE,
    "float": COLOR_TYPE, "int": COLOR_TYPE, "long": COLOR_TYPE, "short": COLOR_TYPE,
    "signed": COLOR_TYPE, "unsigned": COLOR_TYPE, "void": COLOR_TYPE,
    "wchar_t": COLOR_TYPE,
    # Keywords
    "alignas": COLOR_KEYWORD, "alignof": COLOR_KEYWORD, "and": COLOR_KEYWORD,
    "and_eq": COLOR_KEYWORD, "asm": COLOR_KEYWORD, "atomic_cancel": COLOR_KEYWORD,
    "atomic_commit": COLOR_KEYWORD, "atomic_noexcept": COLOR_KEYWORD,
    "auto": COLOR_KEYWORD, "bitand": COLOR_KEYWORD, "bitor": COLOR_KEYWORD,
    "break": COLOR_KEYWORD, "case": COLOR_KEYWORD, "catch": COLOR_KEYWORD,
    "class": COLOR_KEYWORD, "compl": COLOR_KEYWORD, "concept": COLOR_KEYWORD,
    "const": COLOR_KEYWORD, "consteval": COLOR_KEYWORD, "constexpr": COLOR_KEYWORD,
    "constinit": COLOR_KEYWORD, "const_cast": COLOR_KEYWORD,
    "continue": COLOR_KEYWORD, "co_await": COLOR_KEYWORD, "co_return": COLOR_KEYWORD,
    "co_yield": COLOR_KEYWORD, "decltype": COLOR_KEYWORD, "default": COLOR_KEYWORD,
    "delete": COLOR_KEYWORD, "do": COLOR_KEYWORD, "dynamic_cast": COLOR_KEYWORD,
    "else": COLOR_KEYWORD, "enum": COLOR_KEYWORD, "explicit": COLOR_KEYWORD,
    "export": COLOR_KEYWORD, "extern": COLOR_KEYWORD, "false": COLOR_KEYWORD,
    "for": COLOR_KEYWORD, "friend": COLOR_KEYWORD, "goto": COLOR_KEYWORD,
    "if": COLOR_KEYWORD, "inline": COLOR_KEYWORD, "import": COLOR_KEYWORD,
    "module": COLOR_KEYWORD, "mutable": COLOR_KEYWORD, "namespace": COLOR_KEYWORD,
    "new": COLOR_KEYWORD, "noexcept": COLOR_KEYWORD, "not": COLOR_KEYWORD,
    "not_eq": COLOR_KEYWORD, "nullptr": COLOR_KEYWORD, "operator": COLOR_KEYWORD,
    "or": COLOR_KEYWORD, "or_eq": COLOR_KEYWORD, "private": COLOR_KEYWORD,
    "protected": COLOR_KEYWORD, "public": COLOR_KEYWORD, "reflexpr": COLOR_KEYWORD,
    "register": COLOR_KEYWORD, "reinterpret_cast": COLOR_KEYWORD,
    "requires": COLOR_KEYWORD, "return": COLOR_KEYWORD, "sizeof": COLOR_KEYWORD,
    "static": COLOR_KEYWORD, "static_assert": COLOR_KEYWORD,
    "static_cast": COLOR_KEYWORD, "struct": COLOR_KEYWORD,
    "switch": COLOR_KEYWORD, "synchronized": COLOR_KEYWORD,
    "template": COLOR_KEYWORD, "this": COLOR_KEYWORD, "thread_local": COLOR_KEYWORD,
    "throw": COLOR_KEYWORD, "true": COLOR_KEYWORD, "try": COLOR_KEYWORD,
    "typedef": COLOR_KEYWORD, "typeid": COLOR_KEYWORD, "typename": COLOR_KEYWORD,
    "union": COLOR_KEYWORD, "using": COLOR_KEYWORD, "virtual": COLOR_KEYWORD,
    "volatile": COLOR_KEYWORD, "while": COLOR_KEYWORD, "xor": COLOR_KEYWORD,
    "xor_eq": COLOR_KEYWORD,
}

# --- Java ---
JAVA_KEYWORDS = {
    # Types
    "boolean": COLOR_TYPE, "byte": COLOR_TYPE, "char": COLOR_TYPE,
    "double": COLOR_TYPE, "float": COLOR_TYPE, "int": COLOR_TYPE, "long": COLOR_TYPE,
    "short": COLOR_TYPE, "void": COLOR_TYPE,
    # Keywords
    "abstract": COLOR_KEYWORD, "assert": COLOR_KEYWORD, "break": COLOR_KEYWORD,
    "case": COLOR_KEYWORD, "catch": COLOR_KEYWORD, "class": COLOR_KEYWORD,
    "const": COLOR_KEYWORD, "continue": COLOR_KEYWORD, "default": COLOR_KEYWORD,
    "do": COLOR_KEYWORD, "else": COLOR_KEYWORD, "enum": COLOR_KEYWORD,
    "extends": COLOR_KEYWORD, "final": COLOR_KEYWORD, "finally": COLOR_KEYWORD,
    "for": COLOR_KEYWORD, "goto": COLOR_KEYWORD, "if": COLOR_KEYWORD,
    "implements": COLOR_KEYWORD, "import": COLOR_KEYWORD,
    "instanceof": COLOR_KEYWORD, "interface": COLOR_KEYWORD,
    "native": COLOR_KEYWORD, "new": COLOR_KEYWORD, "package": COLOR_KEYWORD,
    "private": COLOR_KEYWORD, "protected": COLOR_KEYWORD, "public": COLOR_KEYWORD,
    "return": COLOR_KEYWORD, "static": COLOR_KEYWORD, "strictfp": COLOR_KEYWORD,
    "super": COLOR_KEYWORD, "switch": COLOR_KEYWORD, "synchronized": COLOR_KEYWORD,
    "this": COLOR_KEYWORD, "throw": COLOR_KEYWORD, "throws": COLOR_KEYWORD,
    "transient": COLOR_KEYWORD, "try": COLOR_KEYWORD, "volatile": COLOR_KEYWORD,
    "while": COLOR_KEYWORD, "true": COLOR_KEYWORD, "false": COLOR_KEYWORD,
    "null": COLOR_KEYWORD,
}


# --- UI Mappings ---
CATEGORY_COLORS = {
    "keyword": COLOR_KEYWORD,
    "type": COLOR_TYPE,
    "identifier": COLOR_IDENTIFIER,
    "literal": COLOR_NUMBER,
    "operator": COLOR_OPERATOR,
    "delimiter": COLOR_DELIMITER,
    "comment": COLOR_COMMENT,
    "error": COLOR_ERROR,
}
