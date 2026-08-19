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
        listing = subprocess.run([binary, "-l", base], check=True,
                                 stdout=subprocess.PIPE).stdout.splitlines()
        # Two header lines and two footer lines surround exactly one streamed
        # line per entry; checking the endpoints catches accidental buffering or
        # truncation without retaining another archive-sized model in zget.
        assert len(listing) == count + 4
        assert listing[2].endswith(b"entry/000000000")
        assert listing[-3].endswith(f"entry/{count - 1:09d}".encode())
        assert listing[-1].endswith(f"{count} files".encode())

        # Names-only listing must preserve the same streaming scale without
        # accidentally introducing table decoration or an entry-count buffer.
        names = subprocess.run([binary, "-1", base], check=True,
                               stdout=subprocess.PIPE).stdout.splitlines()
        assert len(names) == count
        assert names[0] == b"entry/000000000"
        assert names[-1] == f"entry/{count - 1:09d}".encode()
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
