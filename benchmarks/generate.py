#!/usr/bin/env python3
import argparse
import zipfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output")
    parser.add_argument("--entries", type=int, default=100_000)
    args = parser.parse_args()
    with zipfile.ZipFile(args.output, "w", allowZip64=True) as archive:
        for i in range(args.entries):
            archive.writestr(f"entries/{i:09d}.txt", b"x",
                             compress_type=zipfile.ZIP_STORED)


if __name__ == "__main__":
    main()
