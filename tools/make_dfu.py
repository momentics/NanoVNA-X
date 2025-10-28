#!/usr/bin/env python3
"""Minimal DfuSe image builder used in CI workflows.

/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * The software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

This script wraps a raw binary firmware image into a DfuSe (.dfu) container.
Only the fields required by STM32 DFU bootloaders are populated, including the
mandatory 16-byte suffix with the CRC checksum.
"""

from __future__ import annotations

import argparse
import binascii
import pathlib
import struct
from typing import Final

# DfuSe files always reserve 16 bytes for the suffix appended at the end.
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


def _build_suffix(
    *,
    vendor_id: int,
    product_id: int,
    device_id: int,
    dfu_version: int,
    payload_without_crc: bytes,
) -> bytes:
    """Return the 16-byte DfuSe suffix including the CRC field."""

    suffix_without_crc = struct.pack(
        "<HHHH3sB",
        device_id,
        product_id,
        vendor_id,
        dfu_version,
        b"UFD",
        _DFUSE_SUFFIX_LEN,
    )
    crc = binascii.crc32(payload_without_crc + suffix_without_crc) & 0xFFFFFFFF
    return suffix_without_crc + struct.pack("<I", crc)


def build_dfu(
    image: bytes,
    *,
    base_address: int,
    target_name: str,
    vendor_id: int = 0x0483,
    product_id: int = 0xDF11,
    device_id: int = 0xFFFF,
    dfu_version: int = 0x011A,
) -> bytes:
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
    payload_without_crc = prefix + targets
    suffix = _build_suffix(
        vendor_id=vendor_id,
        product_id=product_id,
        device_id=device_id,
        dfu_version=dfu_version,
        payload_without_crc=payload_without_crc,
    )
    return payload_without_crc + suffix


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

    parser.add_argument(
        "--vendor-id",
        type=_parse_address,
        default="0x0483",
        help="USB vendor identifier stored in the suffix (default: 0x0483)",
    )
    parser.add_argument(
        "--product-id",
        type=_parse_address,
        default="0xDF11",
        help="USB product identifier stored in the suffix (default: 0xDF11)",
    )
    parser.add_argument(
        "--device-id",
        type=_parse_address,
        default="0xFFFF",
        help="USB device release number stored in the suffix (default: 0xFFFF)",
    )
    parser.add_argument(
        "--dfu-version",
        type=_parse_address,
        default="0x011A",
        help="DFU specification version stored in the suffix (default: 0x011A)",
    )

    args = parser.parse_args()
    data = args.input_bin.read_bytes()
    args.output_dfu.parent.mkdir(parents=True, exist_ok=True)
    payload = build_dfu(
        data,
        base_address=args.address,
        target_name=args.target_name,
        vendor_id=args.vendor_id,
        product_id=args.product_id,
        device_id=args.device_id,
        dfu_version=args.dfu_version,
    )
    args.output_dfu.write_bytes(payload)


if __name__ == "__main__":  # pragma: no cover - CLI entry point
    main()
