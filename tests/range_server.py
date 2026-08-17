"""Controllable HTTP Range fixture used by zget integration tests."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class RangeHandler(BaseHTTPRequestHandler):
    data = b""
    mode = "normal"
    etag = '"zget-test-v1"'
    requests = 0

    def log_message(self, format, *args):
        """Suppress expected request noise from the adversarial fixture."""
        pass

    def do_GET(self):
        """Serve one request according to the selected Range failure mode."""
        # Several modes change behavior only after the initial tail response,
        # matching the point where ETag and archive-size consistency matters.
        type(self).requests += 1
        request_number = type(self).requests
        if self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/archive.zip")
            self.end_headers()
            return
        if self.path == "/missing":
            self.send_error(404)
            return
        if self.path == "/redirect-loop":
            self.send_response(302)
            self.send_header("Location", "/redirect-loop")
            self.end_headers()
            return
        if type(self).mode == "403":
            self.send_error(403)
            return
        if type(self).mode == "404":
            self.send_error(404)
            return
        if type(self).mode == "416":
            self.send_response(416)
            self.end_headers()
            return
        if type(self).mode == "changed" and request_number > 1:
            if self.headers.get("If-Match"):
                self.send_response(412)
                self.end_headers()
                return
        if type(self).mode == "drop":
            self.connection.close()
            return
        body = type(self).data
        if type(self).mode == "size-change" and request_number > 1:
            body += b"x"
        range_header = self.headers.get("Range")
        if type(self).mode == "ignore" or not range_header:
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        try:
            # zget uses suffix syntax for the tail and explicit bounds for all
            # metadata and payload requests, so the fixture implements both.
            spec = range_header.removeprefix("bytes=")
            if spec.startswith("-"):
                count = min(int(spec[1:]), len(body))
                start, end = len(body) - count, len(body) - 1
            else:
                start_s, end_s = spec.split("-", 1)
                start, end = int(start_s), int(end_s)
            if start < 0 or end < start or start >= len(body):
                raise ValueError
            end = min(end, len(body) - 1)
        except ValueError:
            self.send_response(416)
            self.end_headers()
            return
        part = body[start:end + 1]
        self.send_response(206)
        # wrong-range preserves the body but lies in Content-Range; truncate
        # preserves the declared length but sends fewer bytes. Both must fail.
        shown_start = start + 1 if type(self).mode == "wrong-range" else start
        if type(self).mode == "signed-range":
            # scanf accepts a leading plus, but the HTTP field grammar does not.
            content_range = f"bytes +{start}-{end}/{len(body)}"
        elif type(self).mode == "overflow-range":
            content_range = f"bytes {10 ** 100}-{end}/{len(body)}"
        else:
            content_range = f"bytes {shown_start}-{end}/{len(body)}"
        self.send_header("Content-Range", content_range)
        self.send_header("Content-Length", str(len(part)))
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("ETag", type(self).etag)
        if type(self).mode == "duplicate-etag":
            self.send_header("ETag", '"zget-test-v2"')
        if type(self).mode == "encoding":
            self.send_header("Content-Encoding", "gzip")
        elif type(self).mode == "encoding-reset":
            # A later identity field must not erase an earlier transformation.
            self.send_header("Content-Encoding", "gzip")
            self.send_header("Content-Encoding", "identity")
        if type(self).mode == "duplicate-range":
            # Content-Range is a singleton; even identical duplicates are invalid.
            self.send_header("Content-Range", f"bytes {start}-{end}/{len(body)}")
        self.end_headers()
        if type(self).mode == "truncate":
            part = part[:-1]
        try:
            self.wfile.write(part)
        except (BrokenPipeError, ConnectionResetError):
            # Early Central Directory matches deliberately cancel the transfer.
            pass


def serve(data, mode="normal"):
    """Create an unstarted loopback server with isolated fixture state."""
    # A fresh subclass keeps counters and mode state isolated between servers.
    class Handler(RangeHandler):
        pass
    Handler.data = data
    Handler.mode = mode
    Handler.requests = 0
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    return server
