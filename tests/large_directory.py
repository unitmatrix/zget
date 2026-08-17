#!/usr/bin/env python3
"""Generated large-directory test; the million-entry case is opt-in."""
import io
import os
import subprocess
import sys
import threading
import zipfile

from range_server import serve


def check(binary, count):
    """Generate a large archive and probe early, partial, and full scans.

    First, middle, last, and absent names cover early termination through a
    complete Central Directory scan without retaining an archive-wide index.
    """
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", allowZip64=True) as archive:
        for i in range(count):
            archive.writestr(f"entry/{i:09d}", str(i).encode(),
                             compress_type=zipfile.ZIP_STORED)
    server = serve(output.getvalue())
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    base = f"http://127.0.0.1:{server.server_port}/archive.zip"
    try:
        for i in (0, count // 2, count - 1):
            result = subprocess.run([binary, base, f"entry/{i:09d}"],
                                    check=True, stdout=subprocess.PIPE)
            assert result.stdout == str(i).encode()
        assert subprocess.run([binary, base, "missing"],
                              stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL).returncode != 0
    finally:
        server.shutdown()
        thread.join()


if __name__ == "__main__":
    check(sys.argv[1], 100_000)
    # Keep the expensive fixture opt-in for normal developer and CI runs.
    if os.environ.get("ZGET_MILLION_ENTRY_TEST") == "1":
        check(sys.argv[1], 1_000_000)
