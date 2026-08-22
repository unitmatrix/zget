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
import time
import zipfile


SUBPROCESS_TIMEOUT = 5


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


def mark(start, message):
    """Expose the exact stage reached if a platform-specific CI timeout fires."""
    print("{:.3f}s {}".format(time.monotonic() - start, message),
          file=sys.stderr, flush=True)


def run_command(arguments, stage):
    """Bound each CLI invocation so a CI hang identifies the exact operation."""
    try:
        return subprocess.run(arguments, check=True, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE,
                              timeout=SUBPROCESS_TIMEOUT)
    except subprocess.TimeoutExpired as error:
        raise RuntimeError("{} timed out after {} seconds".format(
            stage, SUBPROCESS_TIMEOUT)) from error


def run(binary):
    """Exercise extraction and both listing modes through the fallback path."""
    start = time.monotonic()
    archive = make_archive()
    IgnoreRangeHandler.archive = archive
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0),
                                              IgnoreRangeHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    mark(start, "server started")
    try:
        url = "http://127.0.0.1:{}/archive.zip".format(server.server_port)

        result = run_command([binary, url, "stored.txt"], "stored extraction")
        assert result.stdout == b"stored payload"
        assert result.stderr == b""
        mark(start, "stored extraction complete")

        result = run_command([binary, url, "deflated.txt"],
                             "deflated extraction")
        assert result.stdout == b"deflated payload " * 64
        assert result.stderr == b""
        mark(start, "deflated extraction complete")

        result = run_command([binary, "-1", url], "short listing")
        assert result.stdout == b"stored.txt\ndeflated.txt\n"
        assert result.stderr == b""
        mark(start, "short listing complete")

        result = run_command([binary, "-l", url], "long listing")
        assert b"stored.txt" in result.stdout
        assert b"deflated.txt" in result.stdout
        assert result.stderr == b""
        mark(start, "long listing complete")
    finally:
        mark(start, "server shutdown begin")
        server.shutdown()
        mark(start, "server shutdown complete")
        server.server_close()
        mark(start, "server close complete")
        thread.join()
        mark(start, "server thread joined")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: no_range_fallback.py ZGET")
    run(sys.argv[1])
