"""Language-specific lexical classification (C / C++ / Java).

These run the real flex-based backend and pin the token type and role of
every category the task requires: keywords, identifiers, literals,
operators, delimiters, comments and — critically — preprocessor
directives, which must NOT be classified as comments.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from app.infrastructure.backend_runner import BackendRunner
from app.infrastructure.paths import PROJECT_ROOT
from app.infrastructure.tool_detector import detect_toolchain

pytestmark = pytest.mark.integration

JAVA_CLASS_MAIN = """public class Main {
    public static void main(String[] args) {
        int count = 42;
    }
}
"""


@pytest.fixture(scope="module")
def runner():
    toolchain = detect_toolchain(PROJECT_ROOT, force_redetect=True)
    r = BackendRunner(toolchain)
    if not r.available():
        pytest.skip("compileone backend not found, skipping integration tests.")
    return r


def _lex(runner: BackendRunner, tmp_path: Path, name: str, source: str, language: str) -> dict:
    src = tmp_path / name
    src.write_text(source, encoding="utf-8")
    out = tmp_path / f"{name}.json"
    return runner.run_phase("lex", src, out, language=language)


def _cls(artifact: dict, lexeme: str) -> tuple[str, str]:
    for t in artifact["tokens"]:
        if t["lexeme"] == lexeme:
            return t["token"], t["category"]
    raise AssertionError(
        f"lexeme {lexeme!r} not tokenized; got "
        f"{[t['lexeme'] for t in artifact['tokens']]}"
    )


# ---------------------------------------------------------------- preprocessor vs comments

def test_c_preprocessor_is_not_a_comment(runner, tmp_path):
    src = "#include <stdio.h>\n// a line comment\n/* a block comment */\n"
    art = _lex(runner, tmp_path, "pp.c", src, "c")
    assert _cls(art, "#include <stdio.h>") == ("PREPROC", "preprocessor")
    assert _cls(art, "// a line comment") == ("COMMENT", "comment")
    assert _cls(art, "/* a block comment */") == ("COMMENT", "comment")


def test_cpp_preprocessor_is_not_a_comment(runner, tmp_path):
    src = "#include <iostream>\nint main() { return 0; }\n"
    art = _lex(runner, tmp_path, "pp.cpp", src, "c++")
    assert _cls(art, "#include <iostream>") == ("PREPROC", "preprocessor")


def test_indented_preprocessor_is_still_a_directive(runner, tmp_path):
    src = "  #define X 1\nint main() { return X; }\n"
    art = _lex(runner, tmp_path, "pp2.c", src, "c")
    assert _cls(art, "  #define X 1") == ("PREPROC", "preprocessor")


# ---------------------------------------------------------------- C

def test_c_keywords_and_identifiers(runner, tmp_path):
    src = "int count = 42;\nint main() { return 0; }\n"
    art = _lex(runner, tmp_path, "kw.c", src, "c")
    assert _cls(art, "int") == ("KW_INT", "type")
    assert _cls(art, "count") == ("IDENTIFIER", "identifier")
    assert _cls(art, "main") == ("IDENTIFIER", "identifier")
    assert _cls(art, "return") == ("KW_RETURN", "keyword")


def test_c_native_only_keywords_are_identifiers(runner, tmp_path):
    """C has no class/public/namespace/String: they must be identifiers."""
    src = "int class = 1;\nint public = 2;\nString s = 0;\nint namespace = 3;\n"
    art = _lex(runner, tmp_path, "cident.c", src, "c")
    for word in ("class", "public", "String", "namespace"):
        assert _cls(art, word) == ("IDENTIFIER", "identifier"), word


def test_c_operators_and_literals(runner, tmp_path):
    src = "int a = 1 + 2 * 3;\nfloat b = 1.5;\nchar c = 'x';\nconst char *s = \"hi\";\n"
    art = _lex(runner, tmp_path, "clit.c", src, "c")
    assert _cls(art, "1") == ("INT_LITERAL", "literal")
    assert _cls(art, "1.5") == ("FLOAT_LITERAL", "literal")
    assert _cls(art, "'x'") == ("CHAR_LITERAL", "literal")
    assert _cls(art, '"hi"') == ("STRING_LITERAL", "literal")
    assert _cls(art, "+") == ("OP_ADD", "operator")
    assert _cls(art, "=") == ("OP_ASSIGN", "operator")


# ---------------------------------------------------------------- C++

def test_cpp_keywords_are_keywords(runner, tmp_path):
    src = "namespace ns { class Foo { public: int x; }; }\nint main() { return 0; }\n"
    art = _lex(runner, tmp_path, "cppkw.cpp", src, "c++")
    assert _cls(art, "namespace") == ("KW_NAMESPACE", "keyword")
    assert _cls(art, "class") == ("KW_CLASS", "keyword")
    assert _cls(art, "public") == ("KW_PUBLIC", "keyword")
    assert _cls(art, "ns") == ("IDENTIFIER", "identifier")


def test_cpp_java_string_is_identifier(runner, tmp_path):
    """'String' is a Java type keyword but an ordinary C++ identifier."""
    src = "String s = \"cpp\";\n"
    art = _lex(runner, tmp_path, "cppstr.cpp", src, "c++")
    assert _cls(art, "String") == ("IDENTIFIER", "identifier")


# ---------------------------------------------------------------- Java

def test_java_keywords_identifiers_and_string(runner, tmp_path):
    art = _lex(runner, tmp_path, "Main.java", JAVA_CLASS_MAIN, "java")
    assert _cls(art, "class") == ("KW_CLASS", "keyword")
    assert _cls(art, "Main") == ("IDENTIFIER", "identifier")
    assert _cls(art, "public") == ("KW_PUBLIC", "keyword")
    assert _cls(art, "static") == ("KW_STATIC", "keyword")
    assert _cls(art, "void") == ("KW_VOID", "type")
    assert _cls(art, "main") == ("IDENTIFIER", "identifier")
    assert _cls(art, "String") == ("KW_STRING", "type")
    assert _cls(art, "args") == ("IDENTIFIER", "identifier")
    assert _cls(art, "int") == ("KW_INT", "type")
    assert _cls(art, "count") == ("IDENTIFIER", "identifier")


def test_java_comments_and_literals(runner, tmp_path):
    src = ("// line\n/* block */\n"
           "int a = 42;\ndouble d = 1.5;\nchar c = 'z';\nString s = \"hi\";\n")
    art = _lex(runner, tmp_path, "javlit.java", src, "java")
    assert _cls(art, "// line") == ("COMMENT", "comment")
    assert _cls(art, "/* block */") == ("COMMENT", "comment")
    assert _cls(art, "42") == ("INT_LITERAL", "literal")
    assert _cls(art, "1.5") == ("FLOAT_LITERAL", "literal")
    assert _cls(art, "'z'") == ("CHAR_LITERAL", "literal")
    assert _cls(art, '"hi"') == ("STRING_LITERAL", "literal")


# ---------------------------------------------------------------- same word, different language

def test_same_word_differs_by_language(runner, tmp_path):
    """class/public are keywords in Java/C++ but identifiers in C."""
    java = _lex(runner, tmp_path, "same.java", JAVA_CLASS_MAIN, "java")
    assert _cls(java, "class")[0] == "KW_CLASS"
    cpp = _lex(runner, tmp_path, "same.cpp", "class Foo {};\n", "c++")
    assert _cls(cpp, "class")[0] == "KW_CLASS"
    c = _lex(runner, tmp_path, "same.c", "int class;\n", "c")
    assert _cls(c, "class")[0] == "IDENTIFIER"
