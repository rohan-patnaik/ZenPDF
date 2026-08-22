from __future__ import annotations

import pytest

from zenpdf_worker.client import ConvexClient, ConvexError


class _Response:
    def __init__(self, status_code: int, payload: object, text: str) -> None:
        self.status_code = status_code
        self.payload = payload
        self.text = text

    def json(self) -> object:
        return self.payload


class _Session:
    def __init__(self, response: _Response) -> None:
        self.response = response

    def post(self, *_args: object, **_kwargs: object) -> _Response:
        return self.response


class _HostileSession:
    def post(self, *_args: object, **_kwargs: object) -> _Response:
        raise RuntimeError(
            "signed-url token password /private/path filename.pdf content-marker"
        )


@pytest.mark.parametrize("status", [401, 502])
def test_non_success_body_is_never_retained_or_exposed(status: int) -> None:
    hostile = (
        "https://signed.invalid/file?token=url-secret worker-secret password-secret "
        "/private/path customer.pdf content-marker"
    )
    client = ConvexClient("https://convex.invalid")
    client.session = _Session(_Response(status, {}, hostile))  # type: ignore[assignment]

    with pytest.raises(RuntimeError) as captured:
        client.mutation("jobs:claimNextJob", {})

    assert str(captured.value) == f"BACKEND_HTTP_{status}"
    assert hostile not in str(captured.value)


def test_application_error_message_is_replaced_with_stable_code() -> None:
    hostile = "signed-url token password /path filename.pdf content-marker"
    client = ConvexClient("https://convex.invalid")
    client.session = _Session(  # type: ignore[assignment]
        _Response(200, {"status": "error", "errorMessage": hostile}, hostile)
    )

    with pytest.raises(ConvexError) as captured:
        client.query("files:getDownloadUrl", {})

    assert str(captured.value) == "BACKEND_APPLICATION_ERROR"
    assert hostile not in str(captured.value)


def test_request_exception_text_is_replaced_with_stable_code() -> None:
    client = ConvexClient("https://convex.invalid")
    client.session = _HostileSession()  # type: ignore[assignment]

    with pytest.raises(RuntimeError) as captured:
        client.query("files:getDownloadUrl", {})

    assert str(captured.value) == "BACKEND_REQUEST_FAILED"
    assert "content-marker" not in str(captured.value)
