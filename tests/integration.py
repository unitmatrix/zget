#!/usr/bin/env python3
"""Hermetic CLI tests over a deliberately adversarial loopback Range server.

Python's zipfile is the independent archive writer for ordinary cases. Small
hand-built records and byte mutations cover formats or failures that zipfile
cannot emit directly without allocating multi-gigabyte fixtures.
"""

import io
import os
import subprocess
import struct
import sys
import tempfile
import threading
import warnings
import zipfile
import zlib

from range_server import serve


def archive():
    """Build one compact archive that crosses the important streaming paths."""
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w") as z:
        z.writestr("stored.txt", b"stored payload", compress_type=zipfile.ZIP_STORED)
        z.writestr("nested/deflated.txt", b"deflated payload " * 100,
                   compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("empty", b"", compress_type=zipfile.ZIP_STORED)
        z.writestr("unicod\u00e9.txt", "hello".encode(), compress_type=zipfile.ZIP_DEFLATED)
        info = zipfile.ZipInfo("unknown-extra.txt")
        info.extra = b"\xfe\xca\x04\x00test"
        z.writestr(info, b"extra")
        z.writestr("long/" + "a" * 2000, b"long")
        for i in range(10):
            z.writestr(f"small/{i}", str(i).encode())
        # The maximum ZIP32 comment pushes EOCD discovery to its legal limit.
        z.comment = b"c" * 65535
    return out.getvalue()


def empty_archive():
    """Build a valid ZIP whose Central Directory contains no entries."""
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w"):
        pass
    return out.getvalue()


def zip64_archive(data):
    """Convert a small ZIP32 tail to ZIP64 without creating a huge archive."""
    raw = bytearray(data)
    pos = raw.rfind(b"PK\x05\x06")
    assert pos >= 0
    eocd = raw[pos:pos + 22]
    entries = int.from_bytes(eocd[10:12], "little")
    cd_size = int.from_bytes(eocd[12:16], "little")
    cd_offset = int.from_bytes(eocd[16:20], "little")
    record = (b"PK\x06\x06" + (44).to_bytes(8, "little") +
              (45).to_bytes(2, "little") * 2 + (0).to_bytes(4, "little") * 2 +
              entries.to_bytes(8, "little") * 2 + cd_size.to_bytes(8, "little") +
              cd_offset.to_bytes(8, "little"))
    locator = (b"PK\x06\x07" + (0).to_bytes(4, "little") +
               pos.to_bytes(8, "little") + (1).to_bytes(4, "little"))
    terminal = bytearray(eocd)
    # Saturated terminal fields make the preceding ZIP64 records authoritative.
    terminal[8:12] = b"\xff" * 4
    terminal[12:20] = b"\xff" * 8
    return bytes(raw[:pos] + record + locator + terminal + raw[pos + 22:])


def mutate(data, offset, replacement):
    """Return fixture bytes with one selected wire-format field replaced."""
    result = bytearray(data)
    result[offset:offset + len(replacement)] = replacement
    return bytes(result)


def central_entry(data, name):
    """Locate a fixture entry without using any parser under test as an oracle."""
    wanted = name.encode("utf-8")
    offset = 0
    while True:
        offset = data.find(b"PK\x01\x02", offset)
        assert offset >= 0
        name_length = int.from_bytes(data[offset + 28:offset + 30], "little")
        extra_length = int.from_bytes(data[offset + 30:offset + 32], "little")
        comment_length = int.from_bytes(data[offset + 32:offset + 34], "little")
        if data[offset + 46:offset + 46 + name_length] == wanted:
            return offset
        offset += 46 + name_length + extra_length + comment_length


def descriptor_archive():
    """Use an unseekable writer to force emission of a data descriptor."""
    class Unseekable(io.BytesIO):
        def seekable(self):
            """Tell zipfile that it cannot backpatch the Local Header."""
            return False

        def seek(self, *args):
            """Reject seeks so zipfile must append a data descriptor."""
            raise io.UnsupportedOperation

    out = Unseekable()
    with zipfile.ZipFile(out, "w") as z:
        z.writestr("descriptor.txt", b"descriptor " * 100,
                   compress_type=zipfile.ZIP_DEFLATED)
    return out.getvalue()


def zip64_entry_archive():
    """Force ZIP64 entry metadata while keeping the member payload one byte."""
    name = b"zip64.txt"
    payload = b"z"
    crc = zlib.crc32(payload)
    local = struct.pack("<IHHHHHIIIHH", 0x04034B50, 45, 0, 0, 0, 0,
                        crc, 1, 1, len(name), 0) + name + payload
    # Saturated Central Directory fields require size, compressed size, and
    # Local Header offset to be consumed from this ZIP64 extra in order.
    extra_data = struct.pack("<QQQ", 1, 1, 0)
    extra = struct.pack("<HH", 1, len(extra_data)) + extra_data
    central = struct.pack("<IHHHHHHIIIHHHHHII", 0x02014B50, 45, 45, 0, 0,
                          0, 0, crc, 0xFFFFFFFF, 0xFFFFFFFF, len(name),
                          len(extra), 0, 0, 0, 0, 0xFFFFFFFF) + name + extra
    eocd = struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, 1, 1,
                       len(central), len(local), 0)
    return local + central + eocd


def run_server(data, mode, test):
    """Run one isolated server mode and return its observed request count."""
    server = serve(data, mode)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        test(f"http://127.0.0.1:{server.server_port}")
    finally:
        server.shutdown()
        thread.join()
    return server.RequestHandlerClass.requests


def main(binary):
    """Run the hermetic end-to-end CLI behavior and rejection cases."""
    data = archive()

    # URL syntax and scheme policy are resolved before any network operation.
    for invalid_url in ("not-a-url", "http://", "ftp://127.0.0.1/archive.zip"):
        result = subprocess.run([binary, invalid_url, "stored.txt"],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert result.returncode != 0, invalid_url

    def empty_member(base):
        """Verify invalid input fails before the archive-tail Range request."""
        result = subprocess.run([binary, base + "/archive.zip", ""],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert result.returncode == 2
        assert b"member path must not be empty" in result.stderr
    assert run_server(data, "normal", empty_member) == 0

    def normal(base):
        """Cover codecs, redirects, varied names, and safe file publication."""
        got = subprocess.run([binary, base + "/archive.zip", "stored.txt"],
                             check=True, stdout=subprocess.PIPE).stdout
        assert got == b"stored payload"
        got = subprocess.run([binary, base + "/redirect", "nested/deflated.txt"],
                             check=True, stdout=subprocess.PIPE).stdout
        assert got == b"deflated payload " * 100
        got = subprocess.run([binary, base + "/archive.zip", "empty"],
                             check=True, stdout=subprocess.PIPE).stdout
        assert got == b""
        got = subprocess.run([binary, base + "/archive.zip", "unknown-extra.txt"],
                             check=True, stdout=subprocess.PIPE).stdout
        assert got == b"extra"
        got = subprocess.run([binary, base + "/archive.zip", "long/" + "a" * 2000],
                             check=True, stdout=subprocess.PIPE).stdout
        assert got == b"long"
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "out")
            subprocess.run([binary, "-o", path, base + "/archive.zip", "stored.txt"],
                           check=True)
            assert open(path, "rb").read() == b"stored payload"
            again = subprocess.run(
                [binary, "-o", path, base + "/archive.zip", "stored.txt"])
            assert again.returncode != 0
    run_server(data, "normal", normal)

    # A syntactically successful HTTP exchange is still rejected unless its
    # status, Content-Range, and actual body length describe the requested bytes.
    for mode in ("ignore", "wrong-range", "truncate"):
        def rejected(base, mode=mode):
            """Require failure for the selected malformed Range response."""
            result = subprocess.run([binary, base + "/archive.zip", "stored.txt"],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            assert result.returncode != 0, mode
        run_server(data, mode, rejected)

    def expect_failure(payload, member="stored.txt", path="/archive.zip",
                       mode="normal", no_output=False):
        """Run one archive or HTTP mutation and require CLI failure."""
        def test(base):
            """Invoke the configured failure case against its fixture server."""
            result = subprocess.run([binary, base + path, member],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            assert result.returncode != 0, (mode, result.stderr)
            if no_output:
                # Invalid response metadata must fail before consumer callbacks.
                assert result.stdout == b"", (mode, result.stdout)
        run_server(payload, mode, test)

    for mode in ("encoding", "encoding-reset", "duplicate-range",
                 "signed-range", "overflow-range", "duplicate-etag"):
        expect_failure(data, mode=mode, no_output=True)
    # Later ranges must remain tied to the object size and strong ETag learned
    # from the first response; mixing versions could combine unrelated bytes.
    expect_failure(data, mode="changed")
    expect_failure(data, mode="size-change")
    for mode in ("403", "404", "416", "drop"):
        expect_failure(data, mode=mode)
    expect_failure(data, path="/redirect-loop")

    cd = central_entry(data, "stored.txt")
    local = data.find(b"PK\x03\x04")
    assert cd >= 0 and local >= 0
    # Central Directory metadata is authoritative. Mutate it directly for CRC,
    # and mutate both header copies where a local/central mismatch would mask
    # the intended unsupported-method or encryption check.
    bad_crc = mutate(data, cd + 16, b"\x00\x00\x00\x00")
    expect_failure(bad_crc)
    unsupported = mutate(mutate(data, cd + 10, (99).to_bytes(2, "little")),
                         local + 8, (99).to_bytes(2, "little"))
    expect_failure(unsupported)
    encrypted = mutate(mutate(data, cd + 8, (1).to_bytes(2, "little")),
                       local + 6, (1).to_bytes(2, "little"))
    expect_failure(encrypted)
    # Corrupt each structural layer independently, then exercise rejected
    # multi-volume metadata and an untrusted Local Header offset.
    eocd = data.rfind(b"PK\x05\x06")
    expect_failure(mutate(data, eocd, b"bad!"))
    expect_failure(mutate(data, cd, b"bad!"))
    expect_failure(mutate(data, local, b"bad!"))
    multi_disk = mutate(data, eocd + 4, (1).to_bytes(2, "little"))
    expect_failure(multi_disk)
    bad_offset = mutate(data, cd + 42, b"\xff" * 4)
    expect_failure(bad_offset)

    def descriptor(base):
        """Verify Central Directory sizes work with a data descriptor."""
        result = subprocess.run([binary, base + "/archive.zip", "descriptor.txt"],
                                check=True, stdout=subprocess.PIPE)
        assert result.stdout == b"descriptor " * 100
    run_server(descriptor_archive(), "normal", descriptor)

    def zip64_entry(base):
        """Verify ZIP64 extra fields resolve a small member's metadata."""
        result = subprocess.run([binary, base + "/archive.zip", "zip64.txt"],
                                check=True, stdout=subprocess.PIPE)
        assert result.stdout == b"z"
    run_server(zip64_entry_archive(), "normal", zip64_entry)

    duplicates = io.BytesIO()
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", UserWarning)
        with zipfile.ZipFile(duplicates, "w") as z:
            z.writestr("same", b"first")
            z.writestr("same", b"second")
    def duplicate(base):
        """Verify first-match semantics permit early directory termination."""
        result = subprocess.run([binary, base + "/archive.zip", "same"],
                                check=True, stdout=subprocess.PIPE)
        assert result.stdout == b"first"
    run_server(duplicates.getvalue(), "normal", duplicate)

    unicode_cd = central_entry(data, "unicod\u00e9.txt")
    unicode_name = "unicod\u00e9.txt".encode()
    # The UTF-8 flag turns malformed name bytes into an archive error even when
    # that entry is not the requested one.
    invalid_name = mutate(data, unicode_cd + 46 + len(unicode_name) - 1, b"\xff")
    expect_failure(invalid_name, member="missing")

    def missing(base):
        """Verify lookup failure after scanning a non-empty directory."""
        result = subprocess.run([binary, base + "/archive.zip", "missing"],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert result.returncode != 0
    run_server(data, "normal", missing)

    def not_found(base):
        """Verify lookup failure for an archive with no entries."""
        result = subprocess.run([binary, base + "/archive.zip", "anything"],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert result.returncode != 0
    run_server(empty_archive(), "normal", not_found)

    def zip64(base):
        """Verify archive-level ZIP64 EOCD and locator handling."""
        result = subprocess.run([binary, base + "/archive.zip", "stored.txt"],
                                check=True, stdout=subprocess.PIPE)
        assert result.stdout == b"stored payload"
    run_server(zip64_archive(data), "normal", zip64)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1]))
