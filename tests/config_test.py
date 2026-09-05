import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'translator'))
import daemon


class ConfigTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.config = Path(self.tmp.name) / 'settings.conf'

    def parse(self, text, *args):
        self.config.write_text(text, encoding='utf-8')
        with mock.patch.dict(os.environ, {}, clear=True):
            return daemon.parse_args(['--config', str(self.config), *args])

    def test_shared_fcitx_config(self):
        args = self.parse('Enabled=False\nSocketPath=/tmp/yiban-test/translator.sock\n'
                          'OllamaUrl=http://localhost:12345\nOllamaModel=test:model\n'
                          'TimeoutSeconds=12\nWarmup=False\n[SentenceBoundaries]\n0=space\n1=。\n')
        self.assertFalse(args.enabled)
        self.assertEqual(args.socket, '/tmp/yiban-test/translator.sock')
        self.assertEqual(args.ollama_url, 'http://localhost:12345')
        self.assertEqual(args.ollama_model, 'test:model')
        self.assertEqual(args.ollama_timeout, 12)
        self.assertTrue(args.no_warmup)
        with mock.patch.object(daemon, 'OllamaTranslator') as backend, mock.patch.object(daemon.socket, 'socket') as socket:
            self.assertEqual(daemon.run(args), 0)
            backend.assert_not_called()
            socket.assert_not_called()

    def test_cli_overrides_env_overrides_file(self):
        self.config.write_text('OllamaModel=file-model\n')
        with mock.patch.dict(os.environ, {'BILINGUAL_TRANSLATOR_OLLAMA_MODEL': 'env-model'}):
            args = daemon.parse_args(['--config', str(self.config)])
            self.assertEqual(args.ollama_model, 'env-model')
            args = daemon.parse_args(['--config', str(self.config), '--ollama-model', 'cli-model'])
            self.assertEqual(args.ollama_model, 'cli-model')

    def test_defaults_without_personal_paths(self):
        args = self.parse('')
        self.assertEqual(args.socket, f'/tmp/bilingual-ime-{os.getuid()}/translator.sock')
        self.assertEqual(args.model, '')
        self.assertTrue(args.enabled)

    def test_invalid_settings(self):
        for value in ['Enabled=perhaps', 'TimeoutSeconds=0', 'TimeoutSeconds=301',
                      'Backend=unknown', 'SocketPath=relative.sock', 'DebounceMs=6000',
                      'OllamaUrl=file:///tmp/server', 'OllamaModel=', 'Backend=argos']:
            with self.subTest(value=value), self.assertRaises(SystemExit):
                self.parse(value)

    def test_example_config(self):
        example = Path(__file__).resolve().parents[1] / 'config/bilingualcontext.conf'
        with mock.patch.dict(os.environ, {}, clear=True):
            args = daemon.parse_args(['--config', str(example), '--check-config'])
        self.assertTrue(args.enabled)
        self.assertEqual(args.ollama_model, 'qwen3.5:0.8b')

    @mock.patch('daemon.urlopen')
    def test_model_pull_uses_configured_endpoint(self, urlopen):
        import io
        import json
        urlopen.return_value = io.BytesIO(b'{"status":"success"}')
        args = self.parse('OllamaUrl=http://localhost:23456\nOllamaModel=custom:model')
        daemon.prepare_model(args)
        request = urlopen.call_args.args[0]
        self.assertEqual(request.full_url, 'http://localhost:23456/api/pull')
        self.assertEqual(json.loads(request.data)['model'], 'custom:model')


if __name__ == '__main__':
    unittest.main()
