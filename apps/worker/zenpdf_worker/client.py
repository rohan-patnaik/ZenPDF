import json
from dataclasses import dataclass
from typing import Any, Dict, Optional

import requests


@dataclass
class ConvexError(Exception):
    message: str
    data: Optional[Dict[str, Any]] = None


class ConvexClient:
    def __init__(self, url: str, auth_token: Optional[str] = None) -> None:
        self.url = url.rstrip("/")
        self.auth_token = auth_token
        self.session = requests.Session()

    def _call(self, kind: str, path: str, args: Dict[str, Any]) -> Any:
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
        return self._call("query", path, args)

    def mutation(self, path: str, args: Dict[str, Any]) -> Any:
        return self._call("mutation", path, args)
