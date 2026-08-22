#!/usr/bin/env python3
"""End-to-end coverage for servers that ignore HTTP Range requests.

The fixture deliberately returns 200 and the complete archive for every GET,
even when zget sends a Range header. The first request must therefore be
rejected by the range source, after which libzget should transparently perform
one ordinary full download and continue through the local-file source.
"""

import http.server
import io
import subprocess
import sys
import threading
import zipfile


def make_archive():
    """Build a small archive containing both stored and deflated members."""
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w") as archive:
        archive.writestr("stored.txt", b"stored payload",
                         compress_type=zipfile.ZIP_STORED)
        archive.writestr("deflated.txt", b"deflated payload " * 64,
                         compress_type=zipfile.ZIP_DEFLATED)
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
    """Exercise extraction and both listing modes through the fallback path."""
    archive = make_archive()
    IgnoreRangeHandler.archive = archive
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0),
                                              IgnoreRangeHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        url = "http://127.0.0.1:{}/archive.zip".format(server.server_port)

        result = subprocess.run([binary, url, "stored.txt"], check=True,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE)
        assert result.stdout == b"stored payload"
        assert result.stderr == b""

        result = subprocess.run([binary, url, "deflated.txt"], check=True,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE)
        assert result.stdout == b"deflated payload " * 64
        assert result.stderr == b""

        result = subprocess.run([binary, "-1", url], check=True,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE)
        assert result.stdout == b"stored.txt\ndeflated.txt\n"
        assert result.stderr == b""

        result = subprocess.run([binary, "-l", url], check=True,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE)
        assert b"stored.txt" in result.stdout
        assert b"deflated.txt" in result.stdout
        assert result.stderr == b""
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: no_range_fallback.py ZGET")
    run(sys.argv[1])
