#!/usr/bin/env python3
"""Minimal DfuSe image builder used in CI workflows.

This script wraps a raw binary firmware image into a DfuSe (.dfu) container.
Only the fields required by STM32 DFU bootloaders are populated, including the
mandatory 16-byte suffix with the CRC checksum.
"""

# Copyright (c) 2024, @momentics <momentics@gmail.com>
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with
# this program. If not, see <https://www.gnu.org/licenses/>.


from __future__ import annotations

import argparse
import binascii
import pathlib
import struct
from dataclasses import dataclass
from typing import Final, Iterable, Tuple

# DfuSe files always reserve 16 bytes for the suffix appended at the end.
_DFUSE_SUFFIX_LEN: Final[int] = 16
_DFUSE_PREFIX_STRUCT = struct.Struct("<5sBIB")
_DFUSE_PREFIX_LEN: Final[int] = _DFUSE_PREFIX_STRUCT.size

@dataclass(frozen=True)
class _TargetPreset:
    label: str
    base_address: int
    segments: Tuple[Tuple[int, int], ...]

    def descriptor(self) -> str:
        return _format_target_descriptor(
            self.label,
            self.base_address,
            self.segments,
        )


_PRESET_TARGETS: Final[dict[str, _TargetPreset]] = {
    "stm32f072xb": _TargetPreset(
        label="STM32F072xB Flash",
        base_address=0x0800_0000,
        segments=((64, 0x800),),
    ),
    "stm32f303xc": _TargetPreset(
        label="STM32F303xC Flash",
        base_address=0x0800_0000,
        segments=((128, 0x800),),
    ),
}


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


def _round_up(value: int, multiple: int) -> int:
    if multiple <= 0:
        raise ValueError("multiple must be positive")
    return ((value + multiple - 1) // multiple) * multiple


def _format_target_descriptor(
    name: str,
    base_address: int,
    segments: Iterable[Tuple[int, int]],
) -> str:
    formatted_segments: list[str] = []
    for count, size_bytes in segments:
        if count <= 0:
            raise ValueError("segment count must be positive")
        if size_bytes <= 0:
            raise ValueError("segment size must be positive")
        if size_bytes % 1024 != 0:
            raise ValueError("segment size must be a multiple of 1024 bytes")
        size_kib = size_bytes // 1024
        formatted_segments.append(f"{count:02d}*{size_kib:03d}Kg")
    if not formatted_segments:
        raise ValueError("at least one memory segment is required")
    return f"@{name}  /0x{base_address:08X}/" + ",".join(formatted_segments)


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
    crc ^= 0xFFFFFFFF
    return suffix_without_crc + struct.pack("<I", crc)


def build_dfu(
    image: bytes,
    *,
    base_address: int,
    target_name: str,
    target_named: bool = True,
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
        1 if target_named else 0,
        _encode_name(target_name if target_named else ""),
        len(element),
        1,  # one element
    )
    targets = target_prefix + element
    prefix = _DFUSE_PREFIX_STRUCT.pack(
        b"DfuSe",
        0x01,
        _DFUSE_PREFIX_LEN + len(targets) + _DFUSE_SUFFIX_LEN,
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
        help="string stored in the DfuSe target prefix",
    )

    parser.add_argument(
        "--preset-target",
        choices=sorted(_PRESET_TARGETS.keys()),
        help="use a predefined DfuSe descriptor for a known STM32 target",
    )
    parser.add_argument(
        "--flash-size",
        type=_parse_address,
        help="total size in bytes of the memory range described in the target string",
    )
    parser.add_argument(
        "--page-size",
        type=_parse_address,
        default="0x800",
        help=(
            "erase page size in bytes when generating the target descriptor "
            "(default: 0x800)"
        ),
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
    target_named = True
    target_name = args.target_name
    base_address = args.address

    try:
        if args.preset_target:
            preset = _PRESET_TARGETS[args.preset_target]
            base_address = preset.base_address
            target_name = preset.descriptor()
        else:
            if target_name is None:
                target_name = "Application"

            if target_name.startswith("@"):
                target_name = target_name
            else:
                page_size = args.page_size
                flash_size = args.flash_size
                if page_size % 1024 != 0:
                    raise ValueError("page size must be a multiple of 1024 bytes")
                if flash_size is None:
                    flash_size = _round_up(len(data), page_size)
                if flash_size % page_size != 0:
                    raise ValueError("flash size must be a multiple of the page size")
                segment = (flash_size // page_size, page_size)
                target_name = _format_target_descriptor(target_name, base_address, (segment,))

        if not target_name.startswith("@"):
            target_named = False
    except ValueError as exc:  # pragma: no cover - CLI validation
        parser.error(str(exc))

    payload = build_dfu(
        data,
        base_address=base_address,
        target_name=target_name,
        target_named=target_named,
        vendor_id=args.vendor_id,
        product_id=args.product_id,
        device_id=args.device_id,
        dfu_version=args.dfu_version,
    )
    args.output_dfu.write_bytes(payload)


if __name__ == "__main__":  # pragma: no cover - CLI entry point
    main()
