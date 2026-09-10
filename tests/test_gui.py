import base64
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "gui"))
from model import Controller, ReceiveCache


def packet(identifier, data):
    return {"id": identifier, "data_base64": base64.b64encode(data).decode(), "validated": True}


class GuiStateTests(unittest.TestCase):
    def test_replacement_eviction_and_explicit_save(self):
        cache = ReceiveCache(5)
        cache.add(packet("a", b"abc"))
        cache.add(packet("a", b"de"))
        self.assertEqual(cache.used, 2)
        cache.add(packet("b", b"1234"))
        self.assertNotIn("a", cache.items)
        with tempfile.TemporaryDirectory() as folder:
            output = pathlib.Path(folder) / "selected.txt"
            self.assertEqual(list(pathlib.Path(folder).iterdir()), [])
            cache.save("b", output)
            self.assertEqual(output.read_bytes(), b"1234")
            with self.assertRaises(FileExistsError):
                cache.save("b", output)

    def test_unvalidated_and_oversized_receive_rejected(self):
        cache = ReceiveCache(5)
        bad = packet("x", b"hi")
        bad["validated"] = False
        with self.assertRaises(ValueError):
            cache.add(bad)
        with self.assertRaises(ValueError):
            cache.add(packet("y", b"123456"))
        self.assertEqual(cache.used, 0)

    def test_diagnostics_not_retained_per_packet(self):
        cache = ReceiveCache(1024)
        incoming = packet("x", b"one")
        incoming["diagnostics"] = {"waveform": [0.0] * 2048}
        saved = cache.add(incoming)
        self.assertNotIn("diagnostics", saved)
        self.assertIn("diagnostics", incoming)

    def test_monotonic_cooldown(self):
        now = [100.0]
        controller = Controller("pump", clock=lambda: now[0])
        controller.transmitted()
        self.assertEqual(controller.remaining_delay(), 6)
        now[0] += 5
        self.assertEqual(controller.remaining_delay(), 1)
        now[0] += 7
        self.assertEqual(controller.remaining_delay(), 0)


if __name__ == "__main__":
    unittest.main()
