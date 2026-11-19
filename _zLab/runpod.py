"""Small RunPod v2 client for the separate lab application.

This module owns no pod lifecycle policy. In particular, callers must reconcile
an uncertain create by the unique job name before attempting another create.
It never retries mutations, stores credentials on disk, or follows redirects.
"""

from __future__ import annotations

import json
import http.client
import math
import urllib.error
import urllib.parse
import urllib.request


API_ROOT = "https://api.runpod.io/v2"
MAX_RESPONSE_BYTES = 8 * 1024 * 1024
MAX_POD_PAGES = 100


class RunPodError(Exception):
    """A safe-to-display failure; response bodies and secrets are omitted.

    ``uncertain`` means a mutation may have reached the service. In that case
    callers must reconcile the remote state before retrying or assuming cleanup.
    """

    def __init__(self, message: str, *, status: int | None = None,
                 uncertain: bool = False):
        super().__init__(message)
        self.status = status
        self.uncertain = uncertain


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def _http_transport(method, url, headers, body, timeout):
    """Return (HTTP status, bounded response bytes); no automatic redirect."""
    request = urllib.request.Request(url, data=body, headers=headers, method=method)
    opener = urllib.request.build_opener(_NoRedirect())
    try:
        with opener.open(request, timeout=timeout) as response:
            return response.status, response.read(MAX_RESPONSE_BYTES + 1)
    except urllib.error.HTTPError as error:
        # Do not read or relay an untrusted service body containing secrets.
        try:
            return error.code, b""
        finally:
            error.close()


class RunPodClient:
    """Synchronous API client. Run network calls away from a GUI thread.

    ``transport`` is injectable for offline tests and has signature
    (method, url, headers, body_or_none, timeout) -> (status, response_bytes).
    """

    def __init__(self, api_key: str, *, timeout: float = 30, transport=None):
        if (not isinstance(api_key, str) or not api_key or
                any(ord(c) < 33 or ord(c) > 126 for c in api_key)):
            raise ValueError("API key must be a nonempty printable token without spaces")
        if not isinstance(timeout, (int, float)) or not math.isfinite(timeout) or timeout <= 0:
            raise ValueError("timeout must be a positive finite number")
        self._api_key = api_key
        self._timeout = timeout
        self._transport = transport or _http_transport

    def _request(self, method, path, *, payload=None, query=None, return_status=False):
        url = API_ROOT + path
        if query:
            url += "?" + urllib.parse.urlencode(query)
        headers = {"Authorization": "Bearer " + self._api_key,
                   "Accept": "application/json"}
        body = None
        if payload is not None:
            body = json.dumps(payload, ensure_ascii=True, allow_nan=False,
                              separators=(",", ":")).encode("utf-8")
            headers["Content-Type"] = "application/json"
        mutation = method in ("POST", "DELETE")
        try:
            status, response = self._transport(method, url, headers, body, self._timeout)
        except (OSError, TimeoutError, urllib.error.URLError, http.client.HTTPException):
            raise RunPodError("RunPod connection failed or timed out", uncertain=mutation) from None
        if not 200 <= status < 300:
            raise RunPodError(f"RunPod request failed (HTTP {status})", status=status,
                              uncertain=mutation and (300 <= status < 400 or
                                                      status == 408 or status >= 500))
        if len(response) > MAX_RESPONSE_BYTES:
            raise RunPodError("RunPod response exceeded the size limit", uncertain=mutation)
        if not response and method == "DELETE":
            return None
        try:
            result = json.loads(response)
        except (ValueError, UnicodeDecodeError):
            raise RunPodError("RunPod returned invalid JSON", uncertain=mutation) from None
        if not isinstance(result, dict):
            raise RunPodError("RunPod returned an unexpected response", uncertain=mutation)
        return (status, result) if return_status else result

    @staticmethod
    def _pod_path(pod_id):
        if not isinstance(pod_id, str) or not pod_id or pod_id in (".", ".."):
            raise ValueError("invalid pod ID")
        return "/pods/" + urllib.parse.quote(pod_id, safe="")

    @staticmethod
    def _queued_path(queue_id):
        if not isinstance(queue_id, str) or not queue_id or queue_id in (".", ".."):
            raise ValueError("invalid queued request ID")
        return "/queued-pods/" + urllib.parse.quote(queue_id, safe="")

    def list_gpus(self):
        return self._catalog("gpus")

    def list_cpus(self):
        return self._catalog("cpus")

    def _catalog(self, kind):
        result = self._request("GET", "/catalog/" + kind)
        if not isinstance(result.get(kind), list):
            raise RunPodError("RunPod returned an invalid catalog")
        return result[kind]

    def list_pods(self):
        return self._list_paginated("/pods", "pods")

    def list_queued(self):
        """List queued requests, whose ids are distinct from resulting podIds."""
        return self._list_paginated("/queued-pods", "queuedPods")

    def _list_paginated(self, path, field):
        pods = []
        seen = set()
        cursor = None
        for _ in range(MAX_POD_PAGES):
            query = {"limit": 100}
            if cursor:
                query["cursor"] = cursor
            result = self._request("GET", path, query=query)
            page = result.get(field)
            pagination = result.get("pagination")
            if not isinstance(page, list) or not isinstance(pagination, dict):
                raise RunPodError("RunPod returned an invalid pod list")
            pods.extend(page)
            more = pagination.get("hasNextPage")
            if more is False:
                return pods
            cursor = pagination.get("nextCursor")
            if more is not True or not isinstance(cursor, str) or not cursor or cursor in seen:
                raise RunPodError("RunPod returned invalid pagination")
            seen.add(cursor)
        raise RunPodError("RunPod pod list exceeded the pagination limit")

    def get_pod(self, pod_id):
        return self._request("GET", self._pod_path(pod_id))

    def create_pod(self, payload):
        """Return (HTTP status, body): 201 is a pod; 202 is a queued request."""
        if not isinstance(payload, dict):
            raise ValueError("pod configuration must be an object")
        status, body = self._request("POST", "/pods", payload=payload, return_status=True)
        if status not in (201, 202):
            raise RunPodError("RunPod returned an unexpected creation status", status=status,
                              uncertain=True)
        return status, body

    def delete_pod(self, pod_id):
        self._request("DELETE", self._pod_path(pod_id))

    def get_queued(self, queue_id):
        return self._request("GET", self._queued_path(queue_id))

    def cancel_queued(self, queue_id):
        self._request("DELETE", self._queued_path(queue_id))
