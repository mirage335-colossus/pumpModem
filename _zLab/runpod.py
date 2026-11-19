"""Small RunPod v2 client for the separate lab application.

This module owns no pod lifecycle policy. In particular, callers must reconcile
an uncertain create by the unique job name before attempting another create.
It never retries mutations, stores credentials on disk, or follows redirects.
"""

from __future__ import annotations

import json
import http.client
import math
from pathlib import Path
import re
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request


API_ROOT = "https://api.runpod.io/v2"
MAX_RESPONSE_BYTES = 8 * 1024 * 1024
MAX_POD_PAGES = 100
_PROBLEM_BYTES = 8192
_LOG_OPERATION = "GET /v2/pods/{id}/logs"
_LOG_BYTES = 1024 * 1024
_LOG_LINE_BYTES = 16 * 1024
_LOG_LINES = 5000


class RunPodError(Exception):
    """A safe-to-display failure; response bodies and secrets are omitted.

    ``uncertain`` means a mutation may have reached the service. In that case
    callers must reconcile the remote state before retrying or assuming cleanup.
    """

    def __init__(self, message: str, *, status: int | None = None,
                 uncertain: bool = False, operation: str | None = None,
                 required_scope: str | None = None):
        super().__init__(message)
        self.status = status
        self.uncertain = uncertain
        self.operation = operation
        self.required_scope = required_scope


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
        try:
            # RunPod describes placement and input rejections with a JSON
            # problem document. Keep the status even if its optional body
            # is truncated or stalls after the server rejected the request.
            if method == "POST" and url == API_ROOT + "/pods" and error.code in (400, 422):
                try:
                    return error.code, error.read(_PROBLEM_BYTES + 1)
                except (OSError, http.client.HTTPException):
                    pass
            return error.code, b""
        finally:
            error.close()


def _create_rejection(response, status):
    """Translate known problems into local messages; never display provider strings."""
    fallback = ("RunPod rejected the pod configuration. Check the compute selection "
                "and container image in the RunPod console.")
    if not response or len(response) > _PROBLEM_BYTES:
        return fallback
    try:
        problem = json.loads(response)
    except (ValueError, UnicodeDecodeError, RecursionError):
        return fallback
    if not isinstance(problem, dict) or problem.get("status", status) != status:
        return fallback

    detail = problem.get("detail")
    reason = detail.casefold() if isinstance(detail, str) else ""
    if any(text in reason for text in ("no longer any instances available", "no instances available",
                                       "no available instances", "no capacity", "out of stock")):
        return ("RunPod has no available host matching this request. Reload the catalog "
                "to check capacity before trying again.")
    if any(text in reason for text in ("insufficient funds", "insufficient balance", "insufficient credits")):
        return "RunPod reports insufficient credit. Check Billing in the RunPod console."

    # API validation errors start with a JSON field path. Report only names
    # from our own request schema, excluding any echoed invalid value/message.
    errors = problem.get("errors")
    if isinstance(errors, list):
        known = {"gpu", "gpu.id", "gpu.count", "cpu", "cpu.id", "cpu.vcpuCount",
                 "image", "name", "disk", "ports", "startSsh", "cloud", "dataCenterIds",
                 "queue", "queue.expiresAt"}
        fields = set()
        for error in errors:
            if isinstance(error, str):
                path = error.partition(":")[0].strip().removeprefix("$.")
                if path in known:
                    fields.add(path)
        if fields:
            return "RunPod rejected these request fields: " + ", ".join(sorted(fields)) + "."
    return fallback


def _container_lines(stream):
    """Yield container lines from RunPod's id/data SSE records."""
    size = 0
    data = []
    for _ in range(_LOG_LINES):
        raw = stream.readline(_LOG_LINE_BYTES + 1)
        if not raw:
            return
        size += len(raw)
        if len(raw) > _LOG_LINE_BYTES or size > _LOG_BYTES:
            raise ValueError("pod log limit exceeded")
        if not raw.endswith(b"\n"):
            raise ValueError("incomplete pod log line")
        line = raw[:-1].removesuffix(b"\r")
        if line.startswith(b"data:"):
            data.append(line[5:].removeprefix(b" "))
        elif not line and data:
            event = json.loads(b"\n".join(data).decode("utf-8"))
            data.clear()
            if (not isinstance(event, dict) or event.get("source") != "container" or
                    not isinstance(event.get("line"), str)):
                raise ValueError("invalid container log event")
            yield event["line"]
    raise ValueError("pod log limit exceeded")


def _log_probe(pod_id, key, timeout):
    """Run inside the deadline-limited subprocess. Emit only a fixed result."""
    from host_identity import startup_fingerprint

    path = RunPodClient._pod_path(pod_id) + "/logs?source=container&tail=1000"
    request = urllib.request.Request(API_ROOT + path, headers={
        "Authorization": "Bearer " + key, "Accept": "text/event-stream"})
    try:
        with urllib.request.build_opener(_NoRedirect()).open(request, timeout=timeout) as response:
            if response.status != 200 or response.headers.get_content_type() != "text/event-stream":
                return "invalid"
            return startup_fingerprint(_container_lines(response))
    except urllib.error.HTTPError as error:
        error.close()
        return str(error.code)
    except (OSError, TimeoutError, urllib.error.URLError, http.client.HTTPException):
        return "connection"
    except (ValueError, RecursionError):
        return "invalid"


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

    def _request(self, method, path, *, operation, scope, payload=None, query=None,
                 return_status=False):
        # These labels are fixed by the call sites, never supplied by the server
        # or derived from opaque IDs, cursors, user input or credentials.
        def failure(detail, *, status=None, uncertain=False):
            return RunPodError(f"{operation}: {detail}", status=status,
                               uncertain=uncertain, operation=operation,
                               required_scope=scope)

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
            raise failure("connection failed or timed out", uncertain=mutation) from None
        if not 200 <= status < 300:
            detail = f"request failed (HTTP {status})"
            if operation == "POST /v2/pods" and status in (400, 422):
                detail += ". " + _create_rejection(response, status)
            elif operation == "POST /v2/pods" and status == 402:
                detail += ". Check the billing balance and limits in the RunPod console."
            elif status == 401:
                detail += "; check the API key and its account"
            elif status == 403:
                detail += (f"; check the {scope} scope and account or team access" if scope
                           else "; check the API key permissions and account or team access")
            elif status == 429:
                detail += "; rate limit reached, wait before retrying"
            raise failure(detail, status=status,
                          uncertain=mutation and (300 <= status < 400 or
                                                  status == 408 or status >= 500))
        if len(response) > MAX_RESPONSE_BYTES:
            raise failure("response exceeded the size limit", uncertain=mutation)
        if not response and method == "DELETE":
            return None
        try:
            result = json.loads(response)
        except (ValueError, UnicodeDecodeError):
            raise failure("response contained invalid JSON", uncertain=mutation) from None
        if not isinstance(result, dict):
            raise failure("response was not a JSON object", uncertain=mutation)
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

    def list_gpus(self, cloud="SECURE"):
        if cloud not in ("SECURE", "COMMUNITY"):
            raise ValueError("Choose Secure or Community Cloud")
        return self._catalog("gpus", {"include": "AVAILABILITY", "product": "POD",
                                      "count": 1, "cloud": cloud})

    def list_cpus(self):
        return self._catalog("cpus", {"include": "AVAILABILITY", "product": "POD",
                                      "vcpuCount": 32})

    def _catalog(self, kind, query):
        operation = {"gpus": "GET /v2/catalog/gpus", "cpus": "GET /v2/catalog/cpus"}[kind]
        result = self._request("GET", "/catalog/" + kind, query=query, operation=operation, scope="catalog:read")
        if not isinstance(result.get(kind), list):
            raise RunPodError(f"{operation}: response contained an invalid catalog",
                              operation=operation, required_scope="catalog:read")
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
        operation = {"/pods": "GET /v2/pods", "/queued-pods": "GET /v2/queued-pods"}[path]
        for _ in range(MAX_POD_PAGES):
            query = {"limit": 100}
            if cursor:
                query["cursor"] = cursor
            result = self._request("GET", path, query=query, operation=operation, scope="pods:read")
            page = result.get(field)
            pagination = result.get("pagination")
            if not isinstance(page, list) or not isinstance(pagination, dict):
                raise RunPodError(f"{operation}: response contained an invalid pod list",
                                  operation=operation, required_scope="pods:read")
            pods.extend(page)
            more = pagination.get("hasNextPage")
            if more is False:
                return pods
            cursor = pagination.get("nextCursor")
            if more is not True or not isinstance(cursor, str) or not cursor or cursor in seen:
                raise RunPodError(f"{operation}: response contained invalid pagination",
                                  operation=operation, required_scope="pods:read")
            seen.add(cursor)
        raise RunPodError(f"{operation}: pod list exceeded the pagination limit",
                          operation=operation, required_scope="pods:read")

    def get_pod(self, pod_id):
        return self._request("GET", self._pod_path(pod_id), operation="GET /v2/pods/{id}", scope="pods:read")

    def get_pod_ssh_fingerprint(self, pod_id):
        """Read an owned pod's public Ed25519 fingerprint from its startup logs."""
        self._pod_path(pod_id)
        timeout = min(self._timeout, 60)
        # An active SSE stream can stall inside DNS, a chunk header, or a partial
        # line despite urllib's per-read timeout. Kill and reap the whole reader
        # at the deadline. The key travels only over its anonymous stdin pipe.
        request = json.dumps([pod_id, self._api_key, timeout]).encode("ascii")
        try:
            child = subprocess.run(
                [sys.executable, "-E", "-s", str(Path(__file__).resolve()), "--read-ssh-fingerprint"],
                input=request, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=timeout)
            result = child.stdout.strip() if child.returncode == 0 else b"invalid"
        except (OSError, subprocess.TimeoutExpired):
            result = b"connection"
        if re.fullmatch(rb"SHA256:[a-zA-Z0-9+/]{43}", result):
            return result.decode("ascii")
        status = int(result) if re.fullmatch(rb"[1-5][0-9]{2}", result) else None
        if status == 403:
            detail = "access denied (HTTP 403); check pods:read and account or team access"
        elif status is not None:
            detail = f"request failed (HTTP {status})"
        elif result == b"connection":
            detail = "connection failed or timed out before the startup fingerprint was verified"
        else:
            detail = "startup logs did not contain a verifiable Ed25519 host fingerprint"
        raise RunPodError(f"{_LOG_OPERATION}: {detail}", operation=_LOG_OPERATION,
                          required_scope="pods:read", status=status)

    def create_pod(self, payload):
        """Return (HTTP status, body): 201 is a pod; 202 is a queued request."""
        if not isinstance(payload, dict):
            raise ValueError("pod configuration must be an object")
        status, body = self._request("POST", "/pods", payload=payload, return_status=True,
                                     operation="POST /v2/pods", scope="pods:write")
        if status not in (201, 202):
            raise RunPodError("RunPod returned an unexpected creation status", status=status,
                              uncertain=True, operation="POST /v2/pods", required_scope="pods:write")
        return status, body

    def delete_pod(self, pod_id):
        self._request("DELETE", self._pod_path(pod_id), operation="DELETE /v2/pods/{id}", scope="pods:write")

    def get_queued(self, queue_id):
        # RunPod's current per-queued-request endpoint docs do not specify a
        # scope, so do not assert that the list endpoint's scope also applies.
        return self._request("GET", self._queued_path(queue_id), operation="GET /v2/queued-pods/{id}", scope=None)

    def cancel_queued(self, queue_id):
        self._request("DELETE", self._queued_path(queue_id), operation="DELETE /v2/queued-pods/{id}", scope=None)


if __name__ == "__main__" and sys.argv[1:] == ["--read-ssh-fingerprint"]:
    print(_log_probe(*json.load(sys.stdin)))
