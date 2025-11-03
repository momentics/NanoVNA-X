# NanoVNA-X Licensing Overview

## Project license

NanoVNA-X is a continuation of the NanoVNA firmware lineage and remains under the GNU General Public License, v3 or any later version, so that upstream authors retain their rights.【F:README.md†L20-L24】  The repository now ships with a dedicated `LICENSE` file that contains the full GPLv3 text and an explicit notice for NanoVNA-X contributors, making the licensing terms discoverable in source and binary releases.【F:LICENSE†L1-L18】

The GPLv3 is compatible with all third-party components bundled in this tree because they are distributed under GPLv3 itself, or under permissive licenses that the GPLv3 can absorb. As a consequence, the entire firmware image must be distributed under GPLv3 (or later), and any party conveying binaries must provide corresponding source code and preserve copyright notices as required by the license.

## Third-party components

| Component | Location | License | Compatibility notes |
|-----------|----------|---------|----------------------|
| ChibiOS RTOS core | `third_party/ChibiOS` | GNU GPLv3 (or later) | Matches the project license; derivative works must remain GPL-compliant.【F:third_party/ChibiOS/os/license/chlicense.h†L1-L17】 |
| ChibiOS startup files | `third_party/ChibiOS/os/common/startup/ARMCMx` | Apache License 2.0 | Apache 2.0 is GPLv3-compatible; notice obligations are satisfied by shipping the full license text in `LICENSES/Apache-2.0.txt`.【F:third_party/ChibiOS/os/common/startup/ARMCMx/devices/STM32F0xx/cmparams.h†L1-L15】【F:LICENSES/Apache-2.0.txt†L1-L114】 |
| ARM CMSIS core headers | `third_party/ChibiOS/os/common/ext/CMSIS/include` | BSD 3-Clause | BSD 3-Clause terms are compatible with GPLv3; attribution requirements are met by bundling the reproduced license text in `LICENSES/BSD-3-Clause-ARM_ST.txt`.【F:third_party/ChibiOS/os/common/ext/CMSIS/include/core_cm4.h†L1-L35】【F:LICENSES/BSD-3-Clause-ARM_ST.txt†L1-L24】 |
| STMicroelectronics device headers | `third_party/ChibiOS/os/common/ext/CMSIS/ST` | BSD 3-Clause | Shares the same attribution obligations as the CMSIS core and is covered by the same reproduced notice file.【F:third_party/ChibiOS/os/common/ext/CMSIS/ST/STM32F0xx/stm32f0xx.h†L20-L43】【F:LICENSES/BSD-3-Clause-ARM_ST.txt†L1-L24】 |
| FatFs filesystem | `third_party/FatFs` | FatFs permissive license | Single-clause permissive license compatible with GPLv3; the notice is reproduced in `LICENSES/FatFs.txt`.【F:third_party/FatFs/ff.c†L1-L17】【F:LICENSES/FatFs.txt†L1-L14】 |

## Compliance status and obligations

* **Source availability:** Maintaining this Git repository alongside release artifacts satisfies GPLv3 section 6 so long as binary releases link back to the source. Anyone redistributing binaries must either accompany them with source code or provide a written offer consistent with GPLv3.
* **License texts:** The repository now includes `LICENSE`, `LICENSES/Apache-2.0.txt`, `LICENSES/BSD-3-Clause-ARM_ST.txt`, and `LICENSES/FatFs.txt`, ensuring that all required license texts accompany binary or source distributions.【F:LICENSE†L1-L18】【F:LICENSES/Apache-2.0.txt†L1-L114】【F:LICENSES/BSD-3-Clause-ARM_ST.txt†L1-L24】【F:LICENSES/FatFs.txt†L1-L14】
* **Notices in documentation:** When preparing user or developer documentation for releases, reference the third-party licenses (for example by linking to this document or the `LICENSES/` folder) so downstream users receive attribution as required by Apache 2.0 and BSD 3-Clause.
* **Contribution guidance:** New source files should keep or add GPLv3 headers consistent with existing files. Third-party imports must include their original license notices and be recorded in the table above.

Following the checklist above keeps NanoVNA-X compliant with the licensing terms of its dependencies while preserving the GPLv3 protections chosen by the project.
