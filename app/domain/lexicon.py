"""mini-c token lexicon — Python mirror of backend/src/util/token.c.

This is the source of truth for editor syntax-coloring colours and for
any UI that needs per-token styling. It is intentionally a static mirror
of the backend lexicon; the authoritative token data always comes from
the compileone backend JSON artifacts.
"""

from __future__ import annotations

# VS Code Dark palette
COLOR_TYPE = "#4ec9b0"        # int / float / bool / char
COLOR_KEYWORD = "#569cd6"     # if / else / while / return / print ...
COLOR_IDENTIFIER = "#9cdcfe"
COLOR_NUMBER = "#b5cea8"
COLOR_STRING = "#ce9178"
COLOR_COMMENT = "#6a9955"
COLOR_OPERATOR = "#d4d4d4"
COLOR_DELIMITER = "#d4d4d4"
COLOR_ERROR = "#f44747"

# keyword -> colour (both type-specifiers and control-flow keywords)
MINIC_KEYWORDS = {
    "int": COLOR_TYPE,
    "float": COLOR_TYPE,
    "bool": COLOR_TYPE,
    "char": COLOR_TYPE,
    "const": COLOR_KEYWORD,
    "if": COLOR_KEYWORD,
    "else": COLOR_KEYWORD,
    "while": COLOR_KEYWORD,
    "for": COLOR_KEYWORD,
    "return": COLOR_KEYWORD,
    "print": COLOR_KEYWORD,
    "true": COLOR_KEYWORD,
    "false": COLOR_KEYWORD,
}

# category -> colour, used for the token grid decoration column
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
