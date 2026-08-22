"""Convex HTTP client helpers for the worker."""

import json
from typing import Any, Dict, Optional

import requests


class ConvexError(Exception):
    """Raised with a stable code when Convex returns an application error."""

    def __init__(self) -> None:
        """Initialize without retaining any backend-controlled fields."""
        self.code = "BACKEND_APPLICATION_ERROR"
        super().__init__(self.code)


class ConvexClient:
    """Minimal HTTP client for Convex query/mutation calls."""

    def __init__(self, url: str, auth_token: Optional[str] = None) -> None:
        """Initialize the client with a deployment URL and optional JWT."""
        self.url = url.rstrip("/")
        self.auth_token = auth_token
        self.session = requests.Session()

    def _call(self, kind: str, path: str, args: Dict[str, Any]) -> Any:
        """Call a Convex query or mutation endpoint."""
        body = {
            "path": path,
            "format": "convex_encoded_json",
            "args": [args],
        }
        headers = {
            "Content-Type": "application/json",
            "Convex-Client": "zenpdf-worker",
        }
        if self.auth_token:
            headers["Authorization"] = f"Bearer {self.auth_token}"

        try:
            response = self.session.post(
                f"{self.url}/api/{kind}",
                data=json.dumps(body),
                headers=headers,
                timeout=60,
            )
        except requests.Timeout as error:
            raise RuntimeError("BACKEND_TIMEOUT") from error
        except Exception as error:
            raise RuntimeError("BACKEND_REQUEST_FAILED") from error
        if response.status_code not in (200, 560):
            status = response.status_code
            stable_status = status if isinstance(status, int) and 100 <= status <= 599 else 0
            raise RuntimeError(f"BACKEND_HTTP_{stable_status}")

        try:
            payload = response.json()
        except Exception as error:
            raise RuntimeError("BACKEND_INVALID_RESPONSE") from error
        if not isinstance(payload, dict):
            raise RuntimeError("BACKEND_INVALID_RESPONSE")
        if payload.get("status") == "success":
            return payload.get("value")
        raise ConvexError()

    def query(self, path: str, args: Dict[str, Any]) -> Any:
        """Execute a Convex query."""
        return self._call("query", path, args)

    def mutation(self, path: str, args: Dict[str, Any]) -> Any:
        """Execute a Convex mutation."""
        return self._call("mutation", path, args)
