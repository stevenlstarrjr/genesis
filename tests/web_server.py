"""Regression checks for the local browser bundle server (no external network)."""
import functools
import http.client
import importlib.util
from pathlib import Path
import tempfile
import threading
import unittest

spec = importlib.util.spec_from_file_location(
    'serve_web', Path(__file__).resolve().parents[1] / 'tools' / 'serve-web.py')
web = importlib.util.module_from_spec(spec)
spec.loader.exec_module(web)


class WebServerTests(unittest.TestCase):
    def test_bundle_only_and_wasm_mime(self):
        with tempfile.TemporaryDirectory() as folder:
            for name in web.BUNDLE_FILES:
                (Path(folder) / name).write_bytes(b'test bundle')
            (Path(folder) / 'private.txt').write_text('not served')
            handler = functools.partial(web.Handler, directory=folder)
            server = web.Server(('127.0.0.1', 0), handler)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                connection = http.client.HTTPConnection(*server.server_address)
                for method in ('GET', 'HEAD'):
                    for name in web.BUNDLE_FILES:
                        connection.request(method, '/' + name + '?version=test')
                        response = connection.getresponse()
                        self.assertEqual(response.status, 200)
                        self.assertEqual(response.getheader('Cache-Control'), 'no-store')
                        if name.endswith('.wasm'):
                            self.assertEqual(response.getheader('Content-Type'), 'application/wasm')
                        self.assertEqual(response.read(), b'test bundle' if method == 'GET' else b'')
                    for path in ('/private.txt', '/.tools/', '/%2e%2e/private.txt'):
                        connection.request(method, path)
                        response = connection.getresponse()
                        self.assertEqual(response.status, 404)
                        response.read()
                connection.request('GET', '/')
                response = connection.getresponse()
                self.assertEqual(response.status, 302)
                self.assertEqual(response.getheader('Location'), '/genesis.html')
                response.read()
                connection.close()
            finally:
                server.shutdown()
                server.server_close()
                thread.join()


if __name__ == '__main__':
    unittest.main()
