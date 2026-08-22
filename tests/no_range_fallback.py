#!/usr/bin/env python3
"""End-to-end coverage for servers that ignore HTTP Range requests.

This test deliberately returns 200 and the complete archive for every GET,
even when zget sends a Range header. A successful extraction proves the full
fallback path: rejected Range response -> ordinary full download -> anonymous
temporary file -> local source -> normal ZIP extraction.
"""

import http.server
import io
import subprocess
import sys
import threading
import zipfile


def make_archive():
    """Build the smallest archive needed to exercise fallback extraction."""
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w") as archive:
        archive.writestr("stored.txt", b"stored payload",
                         compress_type=zipfile.ZIP_STORED)
    return output.getvalue()


class IgnoreRangeHandler(http.server.BaseHTTPRequestHandler):
    """Serve the complete representation and intentionally ignore Range."""

    archive = b""

    def do_GET(self):
        if self.path != "/archive.zip":
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Length", str(len(self.archive)))
        self.end_headers()
        self.wfile.write(self.archive)

    def log_message(self, _format, *args):
        """Keep successful test output quiet."""
        del args


def run(binary):
    """Prove transparent fallback with one focused extraction scenario."""
    IgnoreRangeHandler.archive = make_archive()
    server = http.server.HTTPServer(("127.0.0.1", 0), IgnoreRangeHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        url = "http://127.0.0.1:{}/archive.zip".format(server.server_port)
        result = subprocess.run([binary, url, "stored.txt"], check=True,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE,
                                timeout=10)
        assert result.stdout == b"stored payload"
        assert result.stderr == b""
    finally:
        # Diagnostic teardown: close the listening socket, but deliberately do
        # not call shutdown() or join() here. The server thread is daemonized,
        # so process exit will end it. If macOS now reports the subprocess
        # timeout, zget is the blocker; if the test passes, HTTPServer teardown
        # was the source of the previous 30-second hang.
        server.server_close()


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: no_range_fallback.py ZGET")
    run(sys.argv[1])
