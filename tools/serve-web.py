"""Serve the compiled browser bundle on loopback, never the SDK or build cache."""
import argparse
import http.server
from pathlib import Path
import urllib.parse

ROOT = Path(__file__).resolve().parent.parent
BUNDLE_FILES = ('genesis.html', 'genesis.js', 'genesis.wasm', 'genesis.data')
PUBLIC_PATHS = frozenset('/' + name for name in BUNDLE_FILES)


class Handler(http.server.SimpleHTTPRequestHandler):
    def do_HEAD(self):
        path = urllib.parse.unquote(urllib.parse.urlsplit(self.path).path)
        if path not in PUBLIC_PATHS:
            self.send_error(404)
            return
        super().do_HEAD()

    def do_GET(self):
        path = urllib.parse.unquote(urllib.parse.urlsplit(self.path).path)
        # Only the four build products are public; no directory/SDK listings.
        if path == '/':
            self.send_response(302)
            self.send_header('Location', '/genesis.html')
            self.end_headers()
            return
        if path not in PUBLIC_PATHS:
            self.send_error(404)
            return
        super().do_GET()

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()


class Server(http.server.ThreadingHTTPServer):
    # On Windows, SO_REUSEADDR permits two live servers to bind the same port.
    # Fail clearly instead of intermittently serving a stale build process.
    allow_reuse_address = False


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8080)
    parser.add_argument('--configuration', choices=['release', 'debug'], default='release')
    args = parser.parse_args()
    folder = ROOT / 'build' / args.configuration / 'web'
    missing = [name for name in BUNDLE_FILES if not (folder / name).is_file()]
    if missing:
        parser.error('Incomplete web build; missing ' + ', '.join(missing) +
                     '. Run tools/build-web.ps1 before starting the server.')
    handler = lambda *a, **kw: Handler(*a, directory=str(folder), **kw)
    server = Server(('127.0.0.1', args.port), handler)
    print(f'Genesis: http://127.0.0.1:{args.port}/genesis.html', flush=True)
    server.serve_forever()
