"""Offline tests for the lab's provider API boundary."""

import json
import http.client
import io
import subprocess
import sys
import time
import unittest
import urllib.error
from email.message import Message
from unittest.mock import patch

from runpod import MAX_RESPONSE_BYTES, RunPodClient, RunPodError, _container_lines, _log_probe, _NoRedirect


class FakeTransport:
    def __init__(self, *responses):
        self.responses = list(responses)
        self.calls = []

    def __call__(self, method, url, headers, body, timeout):
        self.calls.append((method, url, headers, body, timeout))
        response = self.responses.pop(0)
        if isinstance(response, Exception):
            raise response
        status, data = response
        return status, data if isinstance(data, bytes) else json.dumps(data).encode()


class RunPodTests(unittest.TestCase):
    def test_container_log_events_preserve_boundaries(self):
        stream = io.BytesIO(b': keepalive\r\n\r\nid: 1\r\ndata: {"source":"container",\r\n'
                            b'data: "line":"second"}\r\n\r\nid: 2\ndata: '
                            b'{"source":"container","line":"third"}\n\n')
        self.assertEqual(list(_container_lines(stream)), ["second", "third"])
        for data in (b'data: {"source":"system","line":"wrong source"}\n\n',
                     b'data: malformed\n\n', b'data: partial', b':' * 16385 + b'\n',
                     b'\n' * 5001, (b':' * 16382 + b'\n') * 65):
            with self.assertRaises(ValueError):
                list(_container_lines(io.BytesIO(data)))

    def test_stream_request_authenticates_exact_pod_and_reads_only_fingerprint(self):
        fingerprint = "SHA256:" + "A" * 43
        lines = ["Pod Started", "SSH host keys:", "Key: /etc/ssh/ssh_host_ed25519_key.pub",
                 f"256 {fingerprint} root@machine (ED25519)", "Exporting environment variables...",
                 "Start script(s) finished, Pod is ready to use."]
        response = io.BytesIO(b"".join(
            b"data: " + json.dumps({"source": "container", "line": line}).encode() + b"\n\n"
            for line in lines))
        response.status = 200
        response.headers = Message()
        response.headers["Content-Type"] = "text/event-stream; charset=utf-8"
        with patch("runpod.urllib.request.build_opener") as opener:
            opener.return_value.open.return_value = response
            self.assertEqual(_log_probe("pod/a", "test-token", 5), fingerprint)
            self.assertIsInstance(opener.call_args.args[0], _NoRedirect)
            req = opener.return_value.open.call_args.args[0]
            self.assertEqual(req.full_url, "https://api.runpod.io/v2/pods/pod%2Fa/logs?source=container&tail=1000")
            self.assertEqual(req.get_header("Authorization"), "Bearer test-token")
            self.assertEqual(req.get_header("Accept"), "text/event-stream")
            self.assertTrue(response.closed)

    def test_logs_redact_errors_and_do_not_follow_redirects(self):
        client = RunPodClient("private-token")
        for status in (302, 401, 403, 404, 429):
            error = urllib.error.HTTPError("https://example.invalid", status, "private reason",
                                           {}, io.BytesIO(b"private body"))
            with patch("runpod.urllib.request.build_opener") as opener:
                opener.return_value.open.side_effect = error
                result = _log_probe("private-id", "private-token", 2)
                self.assertEqual(result, str(status))
                self.assertTrue(error.closed)
            with patch("runpod.subprocess.run", return_value=subprocess.CompletedProcess(
                    [], 0, stdout=result.encode())) as runner:
                with self.assertRaises(RunPodError) as caught:
                    client.get_pod_ssh_fingerprint("private-id")
                self.assertNotIn("private", str(caught.exception))
                self.assertNotIn("private", str(runner.call_args.args))
                self.assertEqual(caught.exception.status, status)
                self.assertEqual(caught.exception.required_scope, "pods:read")
                if status == 403:
                    self.assertIn("pods:read", str(caught.exception))

    def test_log_reader_deadline_kills_stalled_child(self):
        run = subprocess.run
        def stall(_args, **kwargs):
            return run([sys.executable, "-c", "import time; time.sleep(30)"], **kwargs)
        started = time.monotonic()
        with patch("runpod.subprocess.run", side_effect=stall):
            with self.assertRaisesRegex(RunPodError, "timed out"):
                RunPodClient("private-token", timeout=0.1).get_pod_ssh_fingerprint("private-id")
        self.assertLess(time.monotonic() - started, 3)

    def test_catalog_and_authorization_header(self):
        fake = FakeTransport((200, {"gpus": [{"id": "RTX"}]}),
                             (200, {"cpus": [{"id": "cpu"}]}), (200, {"gpus": []}))
        client = RunPodClient("secret-key", timeout=7, transport=fake)
        self.assertEqual(client.list_gpus(), [{"id": "RTX"}])
        self.assertEqual(client.list_cpus(), [{"id": "cpu"}])
        self.assertEqual(fake.calls[0],
                         ("GET", "https://api.runpod.io/v2/catalog/gpus?include=AVAILABILITY&product=POD&count=1&cloud=SECURE",
                          {"Authorization": "Bearer secret-key", "Accept": "application/json"},
                          None, 7))
        self.assertTrue(fake.calls[1][1].endswith("/cpus?include=AVAILABILITY&product=POD&vcpuCount=32"))
        self.assertEqual(client.list_gpus("COMMUNITY", count=3), [])
        self.assertTrue(fake.calls[2][1].endswith("count=3&cloud=COMMUNITY"))
        with self.assertRaises(ValueError):
            client.list_gpus("invalid")
        for count in (0, -1, 2.0, True):
            with self.subTest(count=count), self.assertRaises(ValueError):
                client.list_gpus(count=count)
        self.assertNotIn("secret-key", fake.calls[0][1])
        self.assertNotIn("secret-key", repr(client))

    def test_pagination_encodes_opaque_cursor(self):
        fake = FakeTransport(
            (200, {"pods": [{"id": "first"}], "pagination":
                   {"hasNextPage": True, "nextCursor": "a& b/="}}),
            (200, {"pods": [{"id": "second"}], "pagination":
                   {"hasNextPage": False, "nextCursor": None}}))
        client = RunPodClient("key", transport=fake)
        self.assertEqual([p["id"] for p in client.list_pods()], ["first", "second"])
        self.assertTrue(fake.calls[1][1].endswith("?limit=100&cursor=a%26+b%2F%3D"))

    def test_repeated_cursor_is_error(self):
        page = {"pods": [], "pagination": {"hasNextPage": True, "nextCursor": "same"}}
        client = RunPodClient("key", transport=FakeTransport((200, page), (200, page)))
        with self.assertRaises(RunPodError):
            client.list_pods()

    def test_pod_actions_encode_data_and_path(self):
        fake = FakeTransport((201, {"id": "id"}), (200, {"id": "id"}), (204, b""))
        client = RunPodClient("key", transport=fake)
        config = {"name": "demo\nnot a shell command", "image": "image", "gpu": {"id": "a", "count": 1}}
        self.assertEqual(client.create_pod(config), (201, {"id": "id"}))
        self.assertEqual(json.loads(fake.calls[0][3]), config)
        self.assertEqual(fake.calls[0][2]["Content-Type"], "application/json")
        self.assertEqual(client.get_pod("id/../name?x"), {"id": "id"})
        self.assertTrue(fake.calls[1][1].endswith("/pods/id%2F..%2Fname%3Fx"))
        client.delete_pod("id")
        self.assertEqual(fake.calls[2][0], "DELETE")
        with self.assertRaises(ValueError):
            client.get_pod("..")

    def test_queued_request_is_distinct_from_pod(self):
        fake = FakeTransport(
            (202, {"id": "queue/1", "status": "QUEUED", "podId": None}),
            (200, {"queuedPods": [{"id": "queue/1", "podId": "pod/1"}],
                   "pagination": {"hasNextPage": False, "nextCursor": None}}),
            (200, {"id": "queue/1", "status": "DEPLOYED", "podId": "pod/1"}),
            (204, b""))
        client = RunPodClient("key", transport=fake)
        status, request = client.create_pod({"name": "job"})
        self.assertEqual(status, 202)
        self.assertIsNone(request["podId"])
        self.assertEqual(client.list_queued()[0]["podId"], "pod/1")
        self.assertEqual(client.get_queued(request["id"])["podId"], "pod/1")
        client.cancel_queued(request["id"])
        self.assertTrue(fake.calls[1][1].endswith("/queued-pods?limit=100"))
        self.assertTrue(fake.calls[2][1].endswith("/queued-pods/queue%2F1"))
        self.assertEqual(fake.calls[3][0], "DELETE")
        self.assertTrue(fake.calls[3][1].endswith("/queued-pods/queue%2F1"))

    def test_truncated_http_mutation_is_uncertain(self):
        fake = FakeTransport(http.client.IncompleteRead(b"token-must-not-leak"))
        client = RunPodClient("key", transport=fake)
        with self.assertRaises(RunPodError) as caught:
            client.create_pod({})
        self.assertTrue(caught.exception.uncertain)
        self.assertNotIn("token-must-not-leak", str(caught.exception))

    def test_create_rejection_explains_placement_or_field_without_echoing_details(self):
        cases = [(400, {"detail": "There are no longer any instances available with the requested specifications."},
                  "no available host"),
                 (422, {"detail": "Request validation failed.", "errors":
                  ["$.gpu.id: invalid value private-token", "$.image: rejected private-image",
                   "$.private-path: bad"]}, "gpu.id, image"),
                 (400, {"detail": "private-token"}, "Check the compute selection"),
                 (400, b"private-token" * 900, "Check the compute selection"),
                 (400, b"<html>private-token</html>", "Check the compute selection")]
        for status, data, expected in cases:
            with self.subTest(status=status, expected=expected):
                stream = io.BytesIO(data if isinstance(data, bytes) else json.dumps(data).encode())
                error = urllib.error.HTTPError("https://example.invalid", status, "private-reason", {}, stream)
                with patch("runpod.urllib.request.build_opener") as opener:
                    opener.return_value.open.side_effect = error
                    with self.assertRaises(RunPodError) as caught:
                        RunPodClient("private-token").create_pod({})
                    opener.return_value.open.assert_called_once()
                self.assertTrue(stream.closed)
                self.assertEqual(caught.exception.status, status)
                self.assertFalse(caught.exception.uncertain)
                self.assertIn(expected, str(caught.exception))
                self.assertNotIn("private", str(caught.exception))

    def test_failure_reading_create_problem_preserves_definite_rejection(self):
        class BrokenBody(io.BytesIO):
            def read(self, size):
                self.asserted_size = size
                raise http.client.IncompleteRead(b"private")
        stream = BrokenBody()
        error = urllib.error.HTTPError("https://example.invalid", 400, "private", {}, stream)
        with patch("runpod.urllib.request.build_opener") as opener:
            opener.return_value.open.side_effect = error
            with self.assertRaises(RunPodError) as caught:
                RunPodClient("fake").create_pod({})
        self.assertLessEqual(stream.asserted_size, 8193)
        self.assertTrue(stream.closed)
        self.assertEqual(caught.exception.status, 400)
        self.assertFalse(caught.exception.uncertain)

    def test_failures_preserve_uncertainty_and_hide_response(self):
        fake = FakeTransport((401, b"secret-key was included by server"),
                             (503, b"secret-key"),
                             TimeoutError("secret-key"),
                             (201, b"not json"),
                             (200, b"x" * (MAX_RESPONSE_BYTES + 1)))
        client = RunPodClient("secret-key", transport=fake)
        for operation, status, uncertain in [
            (client.list_gpus, 401, False),
            (lambda: client.create_pod({}), 503, True),
            (lambda: client.create_pod({}), None, True),
            (lambda: client.create_pod({}), None, True),
            (client.list_cpus, None, False),
        ]:
            with self.assertRaises(RunPodError) as caught:
                operation()
            self.assertEqual(caught.exception.status, status)
            self.assertEqual(caught.exception.uncertain, uncertain)
            self.assertNotIn("secret-key", str(caught.exception))

    def test_forbidden_reports_fixed_operation_and_scope_without_data(self):
        operations = [
            (lambda c: c.list_gpus(), "GET /v2/catalog/gpus", "catalog:read"),
            (lambda c: c.list_cpus(), "GET /v2/catalog/cpus", "catalog:read"),
            (lambda c: c.list_pods(), "GET /v2/pods", "pods:read"),
            (lambda c: c.list_queued(), "GET /v2/queued-pods", "pods:read"),
            (lambda c: c.get_pod("private-pod-id"), "GET /v2/pods/{id}", "pods:read"),
            (lambda c: c.get_queued("private-pod-id"), "GET /v2/queued-pods/{id}", None),
            (lambda c: c.create_pod({"name": "private-name"}), "POST /v2/pods", "pods:write"),
            (lambda c: c.delete_pod("private-pod-id"), "DELETE /v2/pods/{id}", "pods:write"),
            (lambda c: c.cancel_queued("private-pod-id"), "DELETE /v2/queued-pods/{id}", None),
        ]
        for action, operation, scope in operations:
            with self.subTest(operation=operation):
                client = RunPodClient("secret-token", transport=FakeTransport((403, b"secret-token private-name")))
                with self.assertRaises(RunPodError) as caught:
                    action(client)
                error = caught.exception
                self.assertEqual((error.status, error.operation, error.required_scope), (403, operation, scope))
                self.assertFalse(error.uncertain)
                self.assertIn(operation, str(error))
                if scope:
                    self.assertIn(scope, str(error))
                else:
                    self.assertIn("API key permissions", str(error))
                self.assertIn("account or team access", str(error))
                for secret in ("secret-token", "private-name", "private-pod-id"):
                    self.assertNotIn(secret, str(error))

    def test_auth_rate_and_network_failures_are_distinct_and_safe(self):
        responses = [(401, b"secret-token"), (403, b"secret-token"), (429, b"secret-token"),
                     TimeoutError("secret-token"), http.client.IncompleteRead(b"secret-token")]
        client = RunPodClient("secret-token", transport=FakeTransport(*responses))
        for expected, status in [("check the API key", 401), ("catalog:read", 403),
                                 ("rate limit", 429), ("connection failed", None)]:
            with self.assertRaises(RunPodError) as caught:
                client.list_gpus()
            self.assertIn(expected, str(caught.exception))
            self.assertEqual(caught.exception.status, status)
            self.assertFalse(caught.exception.uncertain)
            self.assertNotIn("secret-token", str(caught.exception))
        with self.assertRaises(RunPodError) as caught:
            client.delete_pod("private-pod-id")
        self.assertTrue(caught.exception.uncertain)
        self.assertIn("DELETE /v2/pods/{id}", str(caught.exception))
        self.assertNotIn("secret-token", str(caught.exception))
        self.assertNotIn("private-pod-id", str(caught.exception))

    def test_redirects_are_not_followed(self):
        self.assertIsNone(_NoRedirect().redirect_request(None, None, 302, "", {}, "https://elsewhere/"))

    def test_credentials_and_timeout_validation(self):
        for token in ("", "a b", "a\nb", "é"):
            with self.assertRaises(ValueError):
                RunPodClient(token)
        for timeout in (0, -1, float("nan"), float("inf")):
            with self.assertRaises(ValueError):
                RunPodClient("key", timeout=timeout)


if __name__ == "__main__":
    unittest.main()
