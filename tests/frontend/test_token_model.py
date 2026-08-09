"""Token grid model + filter proxy tests (headless Qt)."""

from __future__ import annotations

from PyQt5.QtCore import Qt

from app.ui.models.token_model import USER_ROLE, TokenTableModel
from app.ui.models.token_proxy import TokenFilterProxy
from tests.conftest import make_token


def _model_with_tokens():
    model = TokenTableModel()
    model.set_tokens([
        make_token(id=1, lexeme="int", token="KEYWORD_INT", category="keyword"),
        make_token(id=2, lexeme="main", token="IDENTIFIER", category="identifier", column=5),
        make_token(id=3, lexeme="42", token="LITERAL_INT", category="literal", column=10),
    ])
    return model


def test_model_row_and_column_counts():
    model = _model_with_tokens()
    assert model.rowCount() == 3
    assert model.columnCount() == 4


def test_model_user_role_returns_token():
    model = _model_with_tokens()
    index = model.index(1, 0)
    token = model.data(index, USER_ROLE)
    assert token.lexeme == "main"
    assert token.token == "IDENTIFIER"


def test_model_headers():
    model = _model_with_tokens()
    assert model.headerData(0, Qt.Horizontal) == "Line"
    assert model.headerData(1, Qt.Horizontal) == "Lexeme"
    assert model.headerData(2, Qt.Horizontal) == "Token Type"
    assert model.headerData(3, Qt.Horizontal) == "Role"


def test_model_display_role():
    model = _model_with_tokens()
    assert model.data(model.index(0, 1)) == "int"
    assert model.data(model.index(0, 2)) == "KEYWORD_INT"
    assert model.data(model.index(0, 3)) == "keyword"


def test_model_clear():
    model = _model_with_tokens()
    model.clear()
    assert model.rowCount() == 0


def test_proxy_text_filter():
    proxy = TokenFilterProxy()
    proxy.setSourceModel(_model_with_tokens())
    proxy.set_filter_text("main")
    assert proxy.rowCount() == 1
    index = proxy.mapToSource(proxy.index(0, 0))
    assert proxy.sourceModel().data(index, USER_ROLE).lexeme == "main"


def test_proxy_category_filter():
    proxy = TokenFilterProxy()
    proxy.setSourceModel(_model_with_tokens())
    proxy.set_category_filter("identifier")
    assert proxy.rowCount() == 1
    index = proxy.mapToSource(proxy.index(0, 0))
    assert proxy.sourceModel().data(index, USER_ROLE).token == "IDENTIFIER"


def test_proxy_clear_filters():
    proxy = TokenFilterProxy()
    proxy.setSourceModel(_model_with_tokens())
    proxy.set_filter_text("int")
    proxy.clear_filters()
    assert proxy.rowCount() == 3
