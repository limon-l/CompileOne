"""Token grid model + filter proxy tests (headless Qt)."""

from __future__ import annotations

from PyQt5.QtCore import Qt

from app.ui.models.token_model import USER_ROLE, TokenTableModel
from app.ui.models.token_proxy import TokenFilterProxy
from tests.conftest import make_token


def _model_with_tokens():
    model = TokenTableModel()
    model.set_tokens([
        make_token(id=1, lexeme="int", token="KEYWORD_INT", category="keyword", description="the int keyword"),
        make_token(id=2, lexeme="main", token="IDENTIFIER", category="identifier", description="function name", column=5, length=4),
        make_token(id=3, lexeme="42", token="LITERAL_INT", category="literal", description="integer literal", column=10, length=2, color="#b5cea8"),
    ])
    return model


def test_model_row_and_column_counts():
    model = _model_with_tokens()
    assert model.rowCount() == 3
    assert model.columnCount() == 11


def test_model_user_role_returns_token():
    model = _model_with_tokens()
    index = model.index(1, 0)
    token = model.data(index, USER_ROLE)
    assert token.lexeme == "main"
    assert token.token == "IDENTIFIER"


def test_model_headers():
    model = _model_with_tokens()
    assert model.headerData(0, Qt.Horizontal) == "ID"
    assert model.headerData(4, Qt.Horizontal) == "Token"
    assert model.headerData(10, Qt.Horizontal) == "Description"


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
