#!/usr/bin/env bash
# Install the same verified compiler and runtime libraries for CI and releases.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/arm-toolchain.env"
install_dir="${1:?Usage: install-arm-toolchain.sh INSTALL_DIRECTORY}"
archive="arm-gnu-toolchain-${ARM_TOOLCHAIN_RELEASE}-x86_64-arm-none-eabi.tar.xz"
url="https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_TOOLCHAIN_RELEASE}/binrel/${archive}"
mkdir -p "$install_dir"
if [[ ! -f "$install_dir/$archive" ]]; then
  curl --fail --location --retry 3 --output "$install_dir/$archive" "$url"
fi
printf '%s  %s\n' "$ARM_TOOLCHAIN_SHA256" "$install_dir/$archive" | sha256sum --check
tar -xJf "$install_dir/$archive" --strip-components=1 -C "$install_dir"
actual_version="$("$install_dir/bin/arm-none-eabi-gcc" -dumpfullversion)"
if [[ "$actual_version" != "$ARM_TOOLCHAIN_GCC_VERSION" ]]; then
  echo "Unexpected ARM compiler version: $actual_version" >&2
  exit 1
fi
"$install_dir/bin/arm-none-eabi-gcc" --version | head -n 1
