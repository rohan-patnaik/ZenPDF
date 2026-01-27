"""Convex HTTP client helpers for the worker."""

import json
from dataclasses import dataclass
from typing import Any, Dict, Optional

import requests


@dataclass
class ConvexError(Exception):
    """Raised when Convex returns an error response."""

    message: str
    data: Optional[Dict[str, Any]] = None

    def __post_init__(self) -> None:
        """Initialize the base exception with the message."""
        super().__init__(self.message)


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

        response = self.session.post(
            f"{self.url}/api/{kind}",
            data=json.dumps(body),
            headers=headers,
            timeout=60,
        )
        if response.status_code not in (200, 560):
            raise RuntimeError(response.text)

        payload = response.json()
        if payload.get("status") == "success":
            return payload.get("value")
        raise ConvexError(payload.get("errorMessage", "Unknown error"), payload.get("errorData"))

    def query(self, path: str, args: Dict[str, Any]) -> Any:
        """Execute a Convex query."""
        return self._call("query", path, args)

    def mutation(self, path: str, args: Dict[str, Any]) -> Any:
        """Execute a Convex mutation."""
        return self._call("mutation", path, args)
