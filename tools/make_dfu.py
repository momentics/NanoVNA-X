#!/usr/bin/env python3
"""Minimal DfuSe image builder used in CI workflows.

This script wraps a raw binary firmware image into a DfuSe (.dfu) container.
Only the fields required by STM32 DFU bootloaders are populated. The final
suffix (including the CRC) is appended by ``dfu-suffix``; this utility only
emits the prefix, target header and the firmware payload.
"""

from __future__ import annotations

import argparse
import pathlib
import struct
from typing import Final

# DfuSe files always reserve 16 bytes for the suffix that ``dfu-suffix`` adds.
_DFUSE_SUFFIX_LEN: Final[int] = 16


def _parse_address(value: str) -> int:
    try:
        return int(value, 0)
    except ValueError as exc:  # pragma: no cover - defensive programming
        raise argparse.ArgumentTypeError(f"invalid address '{value}'") from exc


def _encode_name(name: str) -> bytes:
    encoded = name.encode("utf-8", errors="strict")
    if len(encoded) > 255:
        return encoded[:255]
    return encoded.ljust(255, b"\x00")


def build_dfu(image: bytes, *, base_address: int, target_name: str) -> bytes:
    """Wrap *image* into a DfuSe container and return the binary payload."""

    element = struct.pack("<II", base_address, len(image)) + image
    target_prefix = struct.pack(
        "<6sBB255sII",
        b"Target",
        0,  # alt setting
        1,  # named flag
        _encode_name(target_name),
        len(element),
        1,  # one element
    )
    targets = target_prefix + element
    prefix = struct.pack(
        "<5sBIB",
        b"DfuSe",
        0x01,
        len(targets) + _DFUSE_SUFFIX_LEN,
        1,  # number of targets
    )
    return prefix + targets


def main() -> None:
    parser = argparse.ArgumentParser(description="Create a DfuSe image")
    parser.add_argument("input_bin", type=pathlib.Path, help="source .bin file")
    parser.add_argument("output_dfu", type=pathlib.Path, help="target .dfu path")
    parser.add_argument(
        "--address",
        type=_parse_address,
        default="0x08000000",
        help="load address for the image (default: 0x08000000)",
    )
    parser.add_argument(
        "--target-name",
        default="NanoVNA-X",
        help="string stored in the DfuSe target prefix",
    )

    args = parser.parse_args()
    data = args.input_bin.read_bytes()
    args.output_dfu.parent.mkdir(parents=True, exist_ok=True)
    payload = build_dfu(data, base_address=args.address, target_name=args.target_name)
    args.output_dfu.write_bytes(payload)


if __name__ == "__main__":  # pragma: no cover - CLI entry point
    main()
