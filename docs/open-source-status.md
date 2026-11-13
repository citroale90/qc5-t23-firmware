# Open-source driver status — R44, 2026-11-13

The release objective is **open-source Linux drivers**. The binary firmware
executed inside the AIC8800DC Wi-Fi chip is a separate component and remains
unchanged. Its source is not a prerequisite for publishing or rebuilding the
Linux driver. The whole device firmware is not claimed to be entirely open source.

## Linux driver inventory

All **16 loaded Linux modules** have identified source:

- Nine camera/radio-specific modules: seven QC5 peripheral, illumination,
  storage and NOR/configuration modules, plus the two AIC Linux modules.
  The public source rebuilds match R43 byte for byte. R44 applies the
  separately published bounds/error-handling fix to `aic8800_fdrv.ko`.
- Seven standard Linux modules: MMC core/block, FAT/VFAT and three NLS
  modules. Their source is in the verified Linux 7.3.1 baseline with the
  published matching kernel configuration. No separate R44 rebuild of
  those unchanged seven modules is claimed.

See the [R44 inventory](../native/drivers/R44-driver-catalog.json),
[R43 matching rebuild proof](../native/drivers/R43-all-driver-verification.json),
[driver build instructions](../native/drivers/README.md),
[radio source reconstruction](../native/radio/README.md) and
[verified kernel baseline](../native/kernel/README.md).
The inventory distinguishes source availability, compiled equivalence and
hardware qualification; those are different claims.

## R44 changes

Only three runtime files change: the SSH key helper, SSH startup script and
`aic8800_fdrv.ko`. The kernel, media program, image quality and WebUI remain
identical to R43. The radio's six firmware files are byte-identical.
The [migration instructions](migration-from-r43.md) describe preserved
Wi-Fi/SSH data and distinct native/factory return paths.

The [source audit](source-audit-2026-11-13.md) also contains an Annex-B
replacement and restored GPL x264 source helpers. That media candidate is
**not deployed by R44**: the previously qualified media binary is retained.
The [radio audit](wifi-reverse-engineering.md) documents the Linux host
loader correction, its source lock, length/error tests and protocol comparison.

## Components outside the Linux driver claim

| Component | Status |
| --- | --- |
| WebUI | Project source and retained attribution are public |
| AIC8800DC internal firmware | Six unchanged binary dependencies, outside this driver migration |
| Original U-Boot | Retained; exact manufacturer corresponding source is not established |
| Media userspace/reference inputs | Source snapshot is public; four external descriptor/ROI files and generated calibration provenance remain separate release gaps |
| Complete native image build | Laboratory builds are pinned; a portable turnkey public assembler is not yet supplied |

The unchanged radio dependencies are:

- `fmacfw_calib_8800dc_h_u02.bin`
- `fmacfw_calib_8800dc_u02.bin`
- `fmacfw_patch_8800dc_h_u02.bin`
- `fmacfw_patch_8800dc_u02.bin`
- `fmacfw_patch_tbl_8800dc_h_u02.bin`
- `fmacfw_patch_tbl_8800dc_u02.bin`

They execute inside the radio, not as Linux drivers. Their presence does not
itself imply cloud communication. No radio binary, original flash dump,
password, private key or per-unit installation capsule is published here.

Qwen assists source comparisons sequentially. Its output is checked against
actual source, compiled hashes and hardware results; it is not evidence of
successful execution or an upstream license grant.
