# Open-source status — R43, 2026-11-13

The WebUI is already open source. The complete device firmware cannot yet
be described as entirely open source or reproducibly buildable from this
repository alone. A public source archive, an open-source license grant,
complete corresponding source and the absence of binary firmware are
different properties.

| Component | Evidence and remaining work |
| --- | --- |
| WebUI and local controls | Public project source, retained credits; native rendered assets exported alongside the legacy UI |
| SSH/Wi-Fi flash persistence | Source and synthetic interrupted-write tests published; per-file GPL declarations retained |
| Native startup and SD installer | Sources published; per-camera capsules and original backups remain private |
| Peripheral, audio, sensor, ISP and encoder work | Native source snapshot exported; generated hardware inputs and complete driver/build provenance still need review |
| Linux kernel 7.3.1 baseline | Actual source verified identical to the official archive; matching configuration and kernel hash published. Boot wrappers and complete firmware build integration are separate layers |
| AIC8800 Linux drivers | Kernel-side driver source and radio firmware must be audited separately; initial API port patches are included |
| Firmware inside AIC8800DC | Binary dependency remains; source/licensing and a replacement strategy must be investigated |
| Original U-Boot | Original bootloader retained; upstream U-Boot being open source does not provide the exact camera build's corresponding source |
| External reference code without a license declaration | Omitted from this export pending a per-file grant/provenance review |

## Verified driver-source milestone

Seven GPL modules have now been rebuilt from the exported public tree with
the matching MIPS kernel configuration. Every resulting stripped module
matches its deployed R43 binary exactly: PTZ/speaker, white/IR lights,
SD host, SD clock, read-only NOR runtime, Wi-Fi flash view and SSH flash view.
See [driver sources and build instructions](../native/drivers/README.md).
The camera was not modified to perform this verification.

Generated sensor register tables are hardware/build inputs; a `.private.h`
filename alone is not evidence of a secret or proprietary algorithm. Their
origin and licensing still need tracing before they can close a source
release. Actual passwords, keys and producer nonces remain private.

The future [migration from installed R43](migration-from-r43.md) preserves
existing Wi-Fi/SSH records and SD settings, and keeps distinct R43 and stock
return paths. It is not currently an instruction to flash a new candidate.

## Binary radio dependencies

The deployed read-only runtime contains these radio firmware images:

- `fmacfw_calib_8800dc_h_u02.bin`
- `fmacfw_calib_8800dc_u02.bin`
- `fmacfw_patch_8800dc_h_u02.bin`
- `fmacfw_patch_8800dc_u02.bin`
- `fmacfw_patch_tbl_8800dc_h_u02.bin`
- `fmacfw_patch_tbl_8800dc_u02.bin`

They execute in the radio, not as the Linux kernel driver. Recompiling the
driver or removing the vendor cloud application does not remove these
dependencies. These files and the stock flash image are not distributed in
this repository. Their use does not itself imply a cloud connection.

## Next driver work

1. Map every loaded module and hardware interface to its actual source,
   build configuration and license. Distinguish missing source exports from
   components that still require a new implementation.
2. Export the exact matching source for the already native drivers and
   document generated hardware tables without device secrets.
3. Review remaining third-party dependencies and obtain or replace missing
   license grants. Preserve all existing attribution and per-file licenses.
4. Audit the radio firmware boundary. Search for published corresponding
   firmware source before considering a replacement; do not claim a closed
   binary is open source because its loader is GPL.
5. Make the native build reproducible before publishing general binary
   releases. Continue to keep original backups and unique identities private.

Qwen can assist with source comparison and reverse-engineering evidence,
one request at a time. Its conclusions require source/compile/hardware
verification; a model-generated implementation is not proof of hardware
equivalence or a substitute for the upstream license.
