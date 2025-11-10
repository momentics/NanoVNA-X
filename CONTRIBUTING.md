# Contributing to NanoVNA-X

Thank you for your interest in contributing. This guide explains how to propose changes, report issues, and participate constructively.

## Quick start
- Fork the repository and create a topic branch from main.
- Build locally and run basic checks before opening a pull request.
- Open a draft pull request early for visibility; mark ready when complete.

## Code of Conduct
- Participation requires adherence to the NanoVNA-X Code of Conduct.
- Report concerns via momentics@gmail.com or private GitHub contact of a maintainer @momentics.

## Ways to contribute
- Bug reports and reproduction cases.
- Feature requests with clear motivation and scope.
- Code contributions: fixes, features, refactoring.
- Documentation: README, build guides, API comments.
- Tooling: CI, linters, scripts, labels.

## Issue reporting
- Search existing issues and discussions first.
- Provide steps to reproduce, expected vs actual behavior, logs, and environment.
- Attach minimal test cases or traces when possible.
- Use labels if you have permission; otherwise describe severity and area.

Template:
- Environment: OS, compiler/toolchain versions, target (F072 or F303).
- Steps: numbered steps to reproduce.
- Evidence: logs, screenshots, traces, or links.
- Impact: frequency, severity, workaround.

## Pull requests
- Create a focused branch per change.
- Keep diffs minimal; split unrelated changes.
- Reference related issues with keywords (Fixes #ID).
- Include rationale, design notes, and test coverage notes.
- Ensure builds pass and follow style guidelines.

Checklist:
- Code compiles for TARGET=F072 and TARGET=F303.
- No warnings added under default flags.
- Changelog entry or release notes draft if user-visible.
- Updated docs and help strings if applicable.

## Development setup
- Toolchain: arm-none-eabi, dfu-util installed and in PATH.
- Build: make -jN TARGET=F072 or TARGET=F303. Artifacts in build/*.elf, *.bin, *.hex.
- Flash: DFU or SWD; ensure proper permissions for dfu-util.
- When switching TARGET, run make clean.

## Coding standards
- Language: C with CMSIS and ChibiOS; prefer static functions where possible.
- Endianness: little-endian for on-wire formats.
- Types: freq_t is uint32 Hz; complex as float32 (Re, Im).
- Concurrency: respect IRQ-safe sections; set thread priorities explicitly.
- Keep modules small; document dependencies at file top.

## Protocol and data notes
- USB CDC ACM: VID 0x0483, PID 0x5740; endpoints 0x01 OUT 64B, 0x81 IN 64B, 0x82 IN 8B; CRLF line endings; echo and prompt conventions.
- Commands: sweep, scan, scanbin, data, frequencies, pause/resume, freq (CW), power, smooth, bandwidth, cal open/short/load/thru/isoln/done/on/off/reset, edelay, s21offset, save/recall, capture [rle], i2c/lcd/si/gain/stat/threads/time/usart*. 
- Scan binary format: u16 mask, u16 points, then u32 freq + selected complex values.
- Number formats: 0x, 0o, 0b and k/M/G/m/u/n suffixes.

## Documentation
- Keep README and build guides current.
- Add or update API comments for new interfaces.
- Link related docs in pull requests.

## Commit conventions
- Use imperative subject line, max 72 chars.
- Body explains what and why, not just how.
- Reference issues and include breaking-change notes if any.

## Tests and validation
- Provide unit or integration tests where practical.
- For hardware-affecting changes, include bench notes or scope captures when feasible.
- Describe manual test steps for reviewers.

## Review process
- Maintainers aim to respond within a reasonable time window.
- Address review comments with follow-up commits, not force-push, unless requested.
- Maintainers may adjust labels, titles, and scope for clarity.

## Release notes
- If the change is user-facing, propose a short entry for release notes.
- Include migration steps for incompatible changes.

## Licensing
- By contributing, you agree that your contributions are licensed under the project’s GPLv3+ license.
- Do not submit code you do not have the right to license.
