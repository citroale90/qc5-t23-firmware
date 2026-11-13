# R44 common SD installer for factory QC5 cameras

The common kit creates each unit's own two verified 8 MiB backups and paired
update/factory-return capsules before changing flash. The native R44 payload is
shared; no reference camera's PARAM, MAC, Wi-Fi profile or SSH key is copied.
Each backup retains its matching loader, manifest and local restore instructions.

Supported factory profile: QC5_YA_CODE_V82, T23DL, GC1084, AIC8800DC USB,
8 MiB NOR and the exact known OEM boot/kernel/ROOT/SYSTEM fingerprints.
The original PARAM MAC must pass the same decoder used by R44 before install.
Unknown firmware, geometry, reserved-bank data and cross-unit capsules are
refused. A similar case or T23 processor alone does not establish compatibility.

The Linux cold preparer is compiled with `QC5_PREPARE_ONLY` and opens NOR
read-only. The temporary, per-unit-bound old-kernel helper may erase only the
4 KiB ENV sector at `0x3c000`; payload and CRC are written/read back separately.
BOOT code, WWW and PARAM are never written. U-Boot owns the subsequent bounded
KERNEL/ROOT/SYSTEM transaction. Recovery still requires intact U-Boot.

## Operator workflow

Download the [R44 SD kit](https://github.com/citroale90/qc5-t23-firmware/releases/tag/r44-sd-originali-rc1) and use its [quick guide](AVVIO-RAPIDO.txt): extract it,
run `python3 prepara_sd.py --sd /mounted/card`, optionally edit root `config.txt`,
then insert it into the powered-off original camera. Power on holding Reset
for about three seconds and leave power applied for at least fifteen minutes.
The card must already be FAT32/MBR with 8–64 KiB clusters and 160 MiB free.
The PC tool does not format, access a raw device, create Finder resource forks,
or delete recordings, configuration or previous backups.

The first native boot can use the local QC5 hotspot and HTTP setup without QR
or an app. Wi-Fi credentials and each camera's newly generated SSH host key
then persist in flash. Root SSH stays enabled as configured for the local LAN.

`ripristina_sd.py --elenca` lists the backups. `--prepara-ripristino` stages the
selected unit's own factory return and removes the cold install trigger first.
`--nuova-camera` archives the prior root control files and re-arms only the cold
factory entry, retaining backups. `--disattiva` removes both entry markers.
After a factory return, remove/disable the card before the next power-on.
See the complete [Italian instructions](LEGGIMI.txt) and [recovery guide](RIPRISTINO.txt).

## Verification boundaries

[Validation](VALIDATION.json) distinguishes 25 generic C fault/identity cases,
20 MIPS ENV-helper instruction cases, four shell flows, nine recovery-CLI cases,
13 exact PC-copy/reuse cases and seven whole U-Boot model cases. The shared R44
runtime and U-Boot installer are already qualified on the reference camera.
**The new common kit's first cold installation on an untouched twin remains
physically unqualified.** The source/host/model checks do not imply that test.

## Source and builds

Per-file license declarations and the [scoped grants](LICENSING.md) apply. `build_preparer.py` compiles the
static little-endian MIPS32 preparer and regenerates the ENV-module SHA/size/
prefix binding for the selected toolchain, without device or SD access:

```sh
python3 build_preparer.py --output /new/build/directory \
  --zig /path/to/zig --mips-cc /path/to/mipsel-linux-gnu-gcc
```

The production build passes `QC5_PREPARE_ONLY`; the historical Linux write
entry is compiled out. The shipped kit uses hash-pinned, qualified binaries.
A different compiler/output requires requalification, not a blind substitution.
The shared U-Boot source and payload pin are in the parent directory. These
sources do not make the complete native-image assembler portable: the binary release includes unchanged radio firmware, while factory backups
and per-unit identities remain excluded. Per-file terms and outstanding
source/provenance gaps remain documented in NOTICE.md and the release notices.
