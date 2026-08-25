#!/usr/bin/env python3
"""End-to-end coverage for the strict selective HTTP Range contract."""

import http.server
import io
import subprocess
import sys
import threading
import zipfile

from range_server import NoReverseDNSHTTPServer


def make_archive():
    """Build the smallest archive needed to reach the initial tail request."""
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w") as archive:
        archive.writestr("stored.txt", b"stored payload",
                         compress_type=zipfile.ZIP_STORED)
    return output.getvalue()


class IgnoreRangeHandler(http.server.BaseHTTPRequestHandler):
    """Ignore Range while recording whether zget ever sends an ordinary GET."""

    archive = b""
    requests = 0
    range_requests = 0

    def do_GET(self):
        type(self).requests += 1
        if self.headers.get("Range") is not None:
            type(self).range_requests += 1
        if self.path != "/archive.zip":
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Length", str(len(self.archive)))
        self.end_headers()
        self.wfile.write(self.archive)

    def log_message(self, _format, *args):
        """Keep expected rejection output quiet."""
        del args


def assert_range_failure(binary, arguments):
    """Require a clear failure with no member or listing bytes emitted."""
    result = subprocess.run([binary, *arguments], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=10)
    assert result.returncode == 1, (arguments, result.stderr)
    assert result.stdout == b"", (arguments, result.stdout)
    assert b"HTTP Range unsupported" in result.stderr, result.stderr


def run(binary):
    """Prove extraction and listing remain selective when Range is ignored."""
    IgnoreRangeHandler.archive = make_archive()
    IgnoreRangeHandler.requests = 0
    IgnoreRangeHandler.range_requests = 0
    server = NoReverseDNSHTTPServer(("127.0.0.1", 0), IgnoreRangeHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        url = "http://127.0.0.1:{}/archive.zip".format(server.server_port)
        assert_range_failure(binary, [url, "stored.txt"])
        assert_range_failure(binary, ["-l", url])
        assert IgnoreRangeHandler.requests > 0
        assert (IgnoreRangeHandler.range_requests ==
                IgnoreRangeHandler.requests), "zget sent an ordinary GET"
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: strict_range.py ZGET")
    run(sys.argv[1])
