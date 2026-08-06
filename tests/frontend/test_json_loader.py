"""JSON artifact deserialization tests."""

from __future__ import annotations

import pytest

from app.infrastructure.json_loader import ArtifactError, parse_token_stream
from tests.conftest import make_token_stream_data


def test_parse_valid_stream():
    stream = parse_token_stream(make_token_stream_data())
    assert stream.token_count == 2
    assert stream.error_count == 0
    assert stream.tokens[0].token == "KEYWORD_INT"
    assert stream.tokens[0].offset_start == 0
    assert stream.tokens[0].offset_end == 3
    assert stream.tokens[1].lexeme == "main"


def test_missing_schema_raises():
    data = make_token_stream_data()
    del data["schema"]
    with pytest.raises(ArtifactError):
        parse_token_stream(data)


def test_token_missing_field_raises():
    data = make_token_stream_data()
    del data["tokens"][0]["id"]
    with pytest.raises(ArtifactError):
        parse_token_stream(data)


def test_offset_missing_raises():
    data = make_token_stream_data()
    del data["tokens"][0]["offset"]
    with pytest.raises(ArtifactError):
        parse_token_stream(data)


def test_errors_are_parsed():
    data = make_token_stream_data(errors=[
        {"line": 4, "column": 7, "lexeme": "@", "message": "Unknown character"},
    ])
    stream = parse_token_stream(data)
    assert stream.error_count == 1
    assert stream.errors[0].message == "Unknown character"
    assert stream.errors[0].line == 4
