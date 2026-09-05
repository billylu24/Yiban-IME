import io
import json
import socket
import struct
import sys
import threading
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "translator"))
import daemon


class DaemonProtocolTest(unittest.TestCase):
    def test_framed_request_response(self):
        client, server = socket.socketpair()
        worker = threading.Thread(
            target=daemon.serve_client, args=(server, daemon.FakeTranslator())
        )
        worker.start()
        request = {
            "version": 1,
            "id": 4,
            "context": "ctx",
            "generation": 9,
            "text": "你好。",
            "final": True,
        }
        payload = json.dumps(request, ensure_ascii=False).encode()
        framed = struct.pack(">I", len(payload)) + payload
        client.sendall(framed[:3])
        client.sendall(framed[3:])
        response = daemon.receive_message(client)
        self.assertEqual(response["id"], 4)
        self.assertEqual(response["generation"], 9)
        self.assertEqual(response["translation"], "Translation: 你好。")
        client.close()
        worker.join(timeout=1)

    def test_oversized_frame_rejected(self):
        client, server = socket.socketpair()
        client.sendall(struct.pack(">I", daemon.MAX_FRAME_SIZE + 1))
        with self.assertRaises(ValueError):
            daemon.receive_message(server)
        client.close()
        server.close()

    @mock.patch("daemon.urlopen")
    def test_ollama_translation_request(self, mocked_urlopen):
        mocked_urlopen.return_value = io.BytesIO(
            json.dumps({"response": "I did not say he stole the money."}).encode()
        )
        translator = daemon.OllamaTranslator(
            "http://127.0.0.1:11434",
            "qwen3.5:0.8b",
            "5m",
            30,
            warmup=False,
        )

        translated = translator.translate("我没有说他偷了钱")

        self.assertEqual(translated, "I did not say he stole the money.")
        request = mocked_urlopen.call_args.args[0]
        payload = json.loads(request.data.decode())
        self.assertEqual(request.full_url, "http://127.0.0.1:11434/api/generate")
        self.assertEqual(payload["model"], "qwen3.5:0.8b")
        self.assertFalse(payload["think"])
        self.assertEqual(payload["keep_alive"], "5m")
        self.assertEqual(payload["options"]["temperature"], 0)
        self.assertIn("<source>我没有说他偷了钱</source>", payload["prompt"])

    @mock.patch("daemon.urlopen")
    def test_ollama_empty_translation_rejected(self, mocked_urlopen):
        mocked_urlopen.return_value = io.BytesIO(b'{"response":""}')
        translator = daemon.OllamaTranslator(
            "http://127.0.0.1:11434",
            "qwen3.5:0.8b",
            "5m",
            30,
            warmup=False,
        )

        with self.assertRaisesRegex(ValueError, "empty translation"):
            translator.translate("你好")


if __name__ == "__main__":
    unittest.main()
