#!/usr/bin/env python3
"""Hermetic CLI tests over a deliberately adversarial loopback Range server.

Python's zipfile is the independent archive writer for ordinary cases. Small
hand-built records and byte mutations cover formats or failures that zipfile
cannot emit directly without allocating multi-gigabyte fixtures.
"""

import io
import os
import signal
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
        # A large decoded stream reliably reaches a closed pipe before it can
        # fit in the kernel buffer, while compressing to a tiny test fixture.
        z.writestr("pipe.bin", b"x" * (2 * 1024 * 1024),
                   compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("empty", b"", compress_type=zipfile.ZIP_STORED)
        z.writestr("unicod\u00e9.txt", "hello".encode(), compress_type=zipfile.ZIP_DEFLATED)
        # Listing must keep one physical output line per hostile member name.
        z.writestr("line\nbreak.txt", b"newline")
        z.writestr("tab\tname.txt", b"tab")
        z.writestr("back\\slash.txt", b"backslash")
        info = zipfile.ZipInfo("unknown-extra.txt")
        info.extra = b"\xfe\xca\x04\x00test"
        z.writestr(info, b"extra")
        z.writestr("long/" + "a" * 2000, b"long")
        for i in range(10):
            z.writestr(f"small/{i}", str(i).encode())
        # The maximum ZIP32 comment pushes EOCD discovery to its legal limit.
        z.comment = b"c" * 65535
    return out.getvalue()


def many_entry_archive(count=10_000):
    """Build enough directory output to exceed any ordinary pipe buffer."""
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w") as z:
        for i in range(count):
            z.writestr(f"listing/{i:05d}", b"")
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


def semantic_archive(include_unicode=True, unicode_crc_valid=True,
                     include_extended=True, include_ntfs=True,
                     extended_value=1_000_000_000):
    """Build one stored entry with independently controlled semantic metadata."""
    raw_name = b"legacy\x82.txt"
    resolved_name = "preferred.txt".encode()
    payload = b"semantic payload"
    crc = zlib.crc32(payload)
    extra = b""
    if include_unicode:
        name_crc = zlib.crc32(raw_name)
        if not unicode_crc_valid:
            name_crc ^= 1
        unicode_data = b"\x01" + struct.pack("<I", name_crc) + resolved_name
        extra += struct.pack("<HH", 0x7075, len(unicode_data)) + unicode_data
    if include_extended:
        timestamp_data = b"\x01" + struct.pack("<i", extended_value)
        extra += struct.pack("<HH", 0x5455, len(timestamp_data)) + timestamp_data
    if include_ntfs:
        # 2030-01-01 00:00:00 UTC as Windows FILETIME. The other two times are
        # present but deliberately zero because only mtime is public.
        ticks = (1_893_456_000 + 11_644_473_600) * 10_000_000
        ntfs_data = b"\0" * 4 + struct.pack("<HHQQQ", 1, 24, ticks, 0, 0)
        extra += struct.pack("<HH", 0x000A, len(ntfs_data)) + ntfs_data
    local = struct.pack("<IHHHHHIIIHH", 0x04034B50, 20, 0, 0, 0, 0,
                        crc, len(payload), len(payload), len(raw_name), 0)
    local += raw_name + payload
    # DOS fallback is 2026-04-08 13:40 UTC.
    dos_time = (13 << 11) | (40 << 5)
    dos_date = ((2026 - 1980) << 9) | (4 << 5) | 8
    central = struct.pack("<IHHHHHHIIIHHHHHII", 0x02014B50, 20, 20, 0, 0,
                          dos_time, dos_date, crc, len(payload), len(payload),
                          len(raw_name), len(extra), 0, 0, 0, 0, 0)
    central += raw_name + extra
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

    # Syntax failures must remain local and use the conventional usage status.
    for arguments in ([], ["only-a-url"], ["-o"], ["-o", ""], ["-l"], ["-1"],
                      ["-l", "url", "member"], ["-1", "url", "member"],
                      ["-l", "url", "member", "extra"],
                      ["-1", "url", "member", "extra"]):
        result = subprocess.run([binary, *arguments], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE)
        assert result.returncode == 2, (arguments, result.stderr)
    help_result = subprocess.run([binary, "--help"], stdout=subprocess.PIPE)
    assert help_result.returncode == 0 and b"[-o FILE]" in help_result.stdout
    version_result = subprocess.run([binary, "--version"], stdout=subprocess.PIPE)
    assert version_result.returncode == 0 and version_result.stdout.startswith(b"zget ")

    # URL syntax and scheme policy are resolved before any network operation.
    for invalid_url in ("not-a-url", "http://", "ftp://127.0.0.1/archive.zip"):
        result = subprocess.run([binary, invalid_url, "stored.txt"],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert result.returncode != 0, invalid_url

    def empty_member(base):
        """Verify invalid input fails before the archive-tail Range request."""
        url = base + "/archive.zip"
        result = subprocess.run([binary, url, ""], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE)
        assert result.returncode == 2
        assert b"member path must not be empty" in result.stderr
        result = subprocess.run([binary, url], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE)
        assert result.returncode == 2
        result = subprocess.run([binary, "-l", url, ""],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert result.returncode == 2
        result = subprocess.run([binary, "-1", url, ""],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert result.returncode == 2
    assert run_server(data, "normal", empty_member) == 0

    def listing(base):
        """List every member in one metadata request with safe line escaping."""
        result = subprocess.run([binary, "-l", base + "/archive.zip"],
                                check=True, stdout=subprocess.PIPE)
        lines = result.stdout.splitlines()
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            infos = z.infolist()
            total = sum(info.file_size for info in infos)
            stored = z.getinfo("stored.txt")
        year, month, day, hour, minute, _ = stored.date_time
        stored_line = (f"{14:9d}  {month:02d}-{day:02d}-{year:04d} "
                       f"{hour:02d}:{minute:02d}   stored.txt").encode()
        assert lines[:2] == [b"  Length      Date    Time    Name",
                             b"---------  ---------- -----   ----"]
        assert stored_line in lines
        assert "unicod\u00e9.txt".encode() in result.stdout
        assert b"line\\nbreak.txt" in result.stdout
        assert b"tab\\tname.txt" in result.stdout
        assert b"back\\\\slash.txt" in result.stdout
        assert len(lines) == len(infos) + 4
        assert lines[-2] == b"---------                     -------"
        assert lines[-1] == f"{total:9d}                     {len(infos)} files".encode()

        # The short form contains exactly the safely escaped member names: no
        # archive header, table headings, or summary are useful to its scripts.
        short = subprocess.run([binary, "-1", base + "/archive.zip"],
                               check=True, stdout=subprocess.PIPE)
        short_lines = short.stdout.splitlines()
        assert len(short_lines) == len(infos)
        assert short_lines[0] == b"stored.txt"
        assert "unicod\u00e9.txt".encode() in short_lines
        assert b"line\\nbreak.txt" in short_lines
        assert b"tab\\tname.txt" in short_lines
        assert b"back\\\\slash.txt" in short_lines
    # Each CLI process reads the tail, then streams the Central Directory once.
    assert run_server(data, "normal", listing) == 4

    def suffix_fallback(base):
        """Retry a rejected suffix request using explicit byte intervals."""
        result = subprocess.run([binary, "-1", base + "/archive.zip"],
                                check=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE)
        assert result.stdout.splitlines()[0] == b"stored.txt"
        assert result.stderr == b""
    # One rejected suffix, one size probe, one exact tail, and one CD request.
    assert run_server(data, "suffix-unsupported", suffix_fallback) == 4

    stored_cd = central_entry(data, "stored.txt")
    invalid_date = mutate(data, stored_cd + 14, b"\x00\x00")

    def invalid_timestamp_listing(base):
        """Reject an entry when no valid modification-time source exists."""
        result = subprocess.run([binary, "-l", base + "/archive.zip"],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert result.returncode != 0
    run_server(invalid_date, "normal", invalid_timestamp_listing)

    def empty_listing(base):
        """An empty archive needs no zero-length Central Directory request."""
        result = subprocess.run([binary, "-l", base + "/archive.zip"],
                                check=True, stdout=subprocess.PIPE)
        assert result.stdout.endswith(b"        0                     0 files\n")
        short = subprocess.run([binary, "-1", base + "/archive.zip"],
                               check=True, stdout=subprocess.PIPE)
        assert short.stdout == b""
    assert run_server(empty_archive(), "normal", empty_listing) == 2

    def normal(base):
        """Cover codecs, redirects, varied names, and file output semantics."""
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
            with open(path, "wb") as stream:
                stream.write(b"old content that must be truncated")
            subprocess.run([binary, "-o", path, base + "/archive.zip", "stored.txt"],
                           check=True)
            with open(path, "rb") as stream:
                assert stream.read() == b"stored payload"

            dash = subprocess.run(
                [binary, "-o", "-", base + "/archive.zip", "stored.txt"],
                cwd=d, check=True, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE)
            assert dash.stdout == b"stored payload"
            assert not os.path.exists(os.path.join(d, "-"))

            target = os.path.join(d, "target")
            link = os.path.join(d, "link")
            with open(target, "wb") as stream:
                stream.write(b"old symlink target")
            os.symlink(target, link)
            subprocess.run(
                [binary, "-o", link, base + "/archive.zip", "stored.txt"],
                check=True)
            assert os.path.islink(link)
            with open(target, "rb") as stream:
                assert stream.read() == b"stored payload"

            fifo = os.path.join(d, "fifo")
            os.mkfifo(fifo)
            fifo_fd = os.open(fifo, os.O_RDWR)
            try:
                process = subprocess.Popen(
                    [binary, "-o", fifo, base + "/archive.zip", "stored.txt"],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                stdout, stderr = process.communicate(timeout=10)
                assert process.returncode == 0, stderr
                assert stdout == b""
                assert os.read(fifo_fd, 1024) == b"stored payload"
            finally:
                os.close(fifo_fd)

            rejected = subprocess.run(
                [binary, "-o", d, base + "/archive.zip", "stored.txt"],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            assert rejected.returncode != 0
            assert b"cannot open output" in rejected.stderr
    run_server(data, "normal", normal)

    def broken_pipe(base):
        """A closed stdout reader should trigger quiet, conventional SIGPIPE."""
        process = subprocess.Popen(
            [binary, base + "/archive.zip", "pipe.bin"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert process.stdout.read(1) == b"x"
        process.stdout.close()
        stderr = process.stderr.read()
        assert process.wait() == -signal.SIGPIPE
        assert stderr == b""
    run_server(data, "normal", broken_pipe)

    def broken_listing_pipe(base):
        """Closing a large listing cancels its metadata transfer via SIGPIPE."""
        process = subprocess.Popen([binary, "-l", base + "/archive.zip"],
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
        assert process.stdout.read(1) != b""
        process.stdout.close()
        stderr = process.stderr.read()
        assert process.wait() == -signal.SIGPIPE
        assert stderr == b""
    run_server(many_entry_archive(), "normal", broken_listing_pipe)

    # A syntactically successful HTTP exchange is still rejected unless its
    # status, Content-Range, and actual body length describe the requested bytes.
    for mode in ("wrong-range", "truncate"):
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
    # A strong ETag must be repeated exactly; a server that later drops or
    # downgrades it must not be trusted just because the size still matches.
    expect_failure(data, mode="missing-etag")
    expect_failure(data, mode="weak-etag")
    # The first requested entry appears at the start of the Central Directory.
    # Its successful parser sentinel must not bypass response identity checks:
    # failure on request two proves zget did not continue to the Local Header.
    def early_match_identity_failure(base):
        """Reject a missing validator before accepting an early CD match."""
        result = subprocess.run([binary, base + "/archive.zip", "stored.txt"],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert result.returncode != 0
        assert result.stdout == b""
    assert run_server(data, "missing-etag", early_match_identity_failure) == 2

    # Identity headers are available before curl delivers payload bytes. Even
    # stdout, which cannot roll back a late CRC error, must remain untouched
    # when those headers show that the remote archive changed.
    expect_failure(data, mode="payload-etag-change", no_output=True)
    expect_failure(data, mode="payload-size-change", no_output=True)
    for mode in ("403", "404", "416"):
        def explicit_http_status(base, mode=mode):
            """Map explicit HTTP failures to the stable public error."""
            result = subprocess.run([binary, base + "/archive.zip", "stored.txt"],
                                    stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE)
            assert result.returncode != 0
            assert b"HTTP error" in result.stderr, (mode, result.stderr)
        run_server(data, mode, explicit_http_status)
    expect_failure(data, mode="drop")
    expect_failure(data, path="/redirect-loop")

    cd = central_entry(data, "stored.txt")
    local = data.find(b"PK\x03\x04")
    assert cd >= 0 and local >= 0
    # Central Directory metadata is authoritative. Mutate it directly for CRC,
    # and mutate both header copies where a local/central mismatch would mask
    # the intended unsupported-method or encryption check.
    bad_crc = mutate(data, cd + 16, b"\x00\x00\x00\x00")
    expect_failure(bad_crc)

    def late_named_output_failure(base):
        """Retain bytes written to named output before a late CRC failure."""
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "out")
            with open(path, "wb") as stream:
                stream.write(b"old content")
            result = subprocess.run(
                [binary, "-o", path, base + "/archive.zip", "stored.txt"],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            assert result.returncode != 0
            assert result.stdout == b""
            with open(path, "rb") as stream:
                assert stream.read() == b"stored payload"
    run_server(bad_crc, "normal", late_named_output_failure)

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

    def invalid_listing(base):
        """Reject a malformed name whose Central Directory claims UTF-8."""
        result = subprocess.run([binary, "-l", base + "/archive.zip"],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert result.returncode != 0
    run_server(invalid_name, "normal", invalid_listing)

    # Legacy names resolve through CP437 and can be used for exact UTF-8 lookup.
    legacy_name = bytearray(data)
    legacy_flags = int.from_bytes(legacy_name[unicode_cd + 8:unicode_cd + 10],
                                  "little") & ~(1 << 11)
    legacy_name[unicode_cd + 8:unicode_cd + 10] = legacy_flags.to_bytes(2, "little")
    high = unicode_cd + 46 + len("unicod")
    legacy_name[high:high + 2] = b"\x82\xa0"

    def legacy_listing(base):
        """Emit and match the UTF-8 form of a legacy CP437 member name."""
        result = subprocess.run([binary, "-l", base + "/archive.zip"],
                                check=True, stdout=subprocess.PIPE)
        assert "unicod\u00e9\u00e1.txt".encode() in result.stdout
        extracted = subprocess.run(
            [binary, base + "/archive.zip", "unicod\u00e9\u00e1.txt"],
            check=True, stdout=subprocess.PIPE)
        assert extracted.stdout == b"hello"
    run_server(bytes(legacy_name), "normal", legacy_listing)

    # A non-UTC process timezone ensures localtime() could not accidentally
    # satisfy the UTC listing assertions below.
    timestamp_environment = dict(os.environ, TZ="UTC-2")

    def semantic_metadata(base):
        """Prefer valid Unicode Path and NTFS mtime over all fallbacks."""
        url = base + "/archive.zip"
        listed = subprocess.run([binary, "-l", url], check=True,
                                stdout=subprocess.PIPE,
                                env=timestamp_environment).stdout
        assert b"01-01-2030 00:00   preferred.txt" in listed
        extracted = subprocess.run([binary, url, "preferred.txt"], check=True,
                                   stdout=subprocess.PIPE).stdout
        assert extracted == b"semantic payload"
    run_server(semantic_archive(), "normal", semantic_metadata)

    def extended_timestamp(base):
        """Use Extended Timestamp when NTFS mtime is absent."""
        listed = subprocess.run([binary, "-l", base + "/archive.zip"],
                                check=True, stdout=subprocess.PIPE,
                                env=timestamp_environment).stdout
        assert b"09-09-2001 01:46   preferred.txt" in listed
    run_server(semantic_archive(include_ntfs=False), "normal",
               extended_timestamp)

    def negative_timestamp(base):
        """Preserve signed Extended Timestamp values before the Unix epoch."""
        listed = subprocess.run([binary, "-l", base + "/archive.zip"],
                                check=True, stdout=subprocess.PIPE,
                                env=timestamp_environment).stdout
        assert b"12-31-1969 23:59   preferred.txt" in listed
    run_server(semantic_archive(include_ntfs=False, extended_value=-1),
               "normal", negative_timestamp)

    def bad_unicode_crc(base):
        """Ignore a Unicode Path field whose raw-name CRC does not match."""
        url = base + "/archive.zip"
        listed = subprocess.run([binary, "-1", url], check=True,
                                stdout=subprocess.PIPE).stdout
        assert listed == "legacy\u00e9.txt\n".encode()
        extracted = subprocess.run([binary, url, "legacy\u00e9.txt"], check=True,
                                   stdout=subprocess.PIPE).stdout
        assert extracted == b"semantic payload"
    run_server(semantic_archive(unicode_crc_valid=False), "normal",
               bad_unicode_crc)

    embedded_nul = mutate(data, stored_cd + 46 + 3, b"\0")
    expect_failure(embedded_nul, member="missing")

    unknown_cd = central_entry(data, "unknown-extra.txt")
    unknown_name_length = len("unknown-extra.txt")
    malformed_extra = mutate(data, unknown_cd + 46 + unknown_name_length + 2,
                             b"\xff\xff")
    expect_failure(malformed_extra, member="missing")

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
