# R48 stable — extract-to-SD factory installer

The common kit creates each unit's own two verified 8 MiB backups and paired
update/factory-return capsules before changing flash. The native R48 payload is
shared; no reference camera's PARAM, MAC, Wi-Fi profile or SSH key is copied.
Each backup retains its matching loader, manifest and local restore instructions.

Supported factory profile: QC5_YA_CODE_V82, T23DL, GC1084, AIC8800DC USB,
8 MiB NOR and its supported U-Boot/kernel recovery transport. ROOT/SYSTEM
software-version hashes do not determine hardware eligibility. Both original
software builds already backed up on this hardware are covered by regression
tests; no reference unit firmware partition is copied as a substitute backup.
The original PARAM MAC must pass the same decoder used by R45 RC2 before install.
Unknown boot/recovery transports, wrong geometry, occupied reserved-bank data
and cross-unit capsules are refused. Uniform zero-filled or FF-filled unused
padding is accepted only outside the original SquashFS extent. A uniformly
empty zero-filled or FF-filled ENV uses the pinned U-Boot compiled defaults;
populated ENV records still require a valid CRC. A similar case or T23 processor alone does not establish compatibility.

The Linux cold preparer is compiled with `QC5_PREPARE_ONLY` and opens NOR
read-only. The temporary, per-unit-bound old-kernel helper may erase only the
4 KiB ENV sector at `0x3c000`; payload and CRC are written/read back separately.
BOOT code, WWW and PARAM are never written. U-Boot owns the subsequent bounded
KERNEL/ROOT/SYSTEM transaction. Recovery still requires intact U-Boot.

## Operator workflow

Download the [R48 stable SD kit](https://github.com/citroale90/qc5-t23-firmware/releases/tag/r48-sd)
and extract its contents directly into the root of an already formatted
FAT32/MBR card (8–64 KiB clusters, at least 160 MiB free). The archive has
no enclosing folder and already includes `qc5-run`, the cold-entry trigger
and `qc5-installer/AZIONE.txt` set to `INSTALLA`. No Python or other PC script
is required for a factory camera. Root `config.txt` is deliberately absent
so extraction preserves existing settings; the camera creates a default
when needed. Optionally copy `qc5-installer/config.txt.example` to `config.txt`
and edit Wi-Fi settings, or use the local hotspot after installation.

Safely eject, insert in the powered-off original camera, power on holding
Reset, release after about three seconds, and keep power applied for at
least fifteen minutes. Backup and unit binding are performed on the camera.
See the [quick guide](AVVIO-RAPIDO.txt). `prepara_sd.py` remains an optional
helper for archiving root controls and reusing a card.

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

[Validation](VALIDATION.json) distinguishes 30 generic C fault/identity cases,
20 MIPS ENV-helper instruction cases, four shell flows, nine recovery-CLI cases,
13 exact PC-copy/reuse cases and seven whole U-Boot model cases. The shared R45 RC2
runtime and U-Boot installer are already qualified on the reference camera.
**The new common kit's first cold installation on an untouched twin remains
physically unqualified.** The source/host/model checks do not imply that test.

## Source and builds

Per-file license declarations and the [scoped grants](LICENSING.md) apply. `build_preparer.py` compiles the
static little-endian MIPS32 preparer and regenerates the ENV-module SHA/size/
prefix binding for the selected toolchain, without device or SD access:

```sh
python3 build_preparer.py --output /new/build/directory \
  --payload /path/to/verified/native.img \
  --zig /path/to/zig --mips-cc /path/to/mipsel-linux-gnu-gcc
```

The production build passes `QC5_PREPARE_ONLY`; the historical Linux write
entry is compiled out. The shipped kit uses hash-pinned, qualified binaries.
A different compiler/output requires requalification, not a blind substitution.
The shared U-Boot source and payload pin are in the parent directory. These
sources do not make the complete native-image assembler portable: the binary release includes unchanged radio firmware, while factory backups
and per-unit identities remain excluded. Per-file terms and outstanding
source/provenance gaps remain documented in NOTICE.md and the release notices.

## RC2: first cold-install diagnosis

The RC1 cold entry and both 8 MiB snapshots succeeded on an original twin.
It then returned to the stock application before any flash changes because
the preparer compared ROOT/SYSTEM against a single software-build fingerprint.
Testing the independently retained original backup also exposed zero-filled
unused tail padding and a zero-filled empty ENV; RC1 accepted only FF there.
RC2 removes the filesystem-build allowlist and accepts both empty encodings,
while retaining geometry, bounded filesystem extents, boot/kernel transport,
watchdog, paired snapshots, per-unit PARAM and recovery checks.

RC2 adds the preparer exit code to `qc5-installer/avvio.log` and the precise
refusal stage to `qc5-backups/unit-*/prepare.log`. Read these before repeating
Reset; a white LED alone is not evidence of a completed installation.
The common RC2 cold installation itself still requires its physical test.

Older native QC5 units use aggiorna_qc5.py and power on without Reset. Factory units use the direct-extraction ZIP and Reset entry; prepara_sd.py is optional. Per-unit backups and identities must never be exchanged.

## Release-chain correction in stable R47

The earlier RC1 common kit retained an R45 payload pin in its preparer and
an old loader digest in `run.sh`. Native R47 runtime installations were
verified separately and were unaffected, but a factory install would refuse
that inconsistent kit before changing firmware. The stable kit rebuilds
the read-only preparer for the actual payload and generates all SHA-256/MD5
script checks from the packaged binaries. A required `--payload` argument
prevents future preparer builds from silently reusing a historical pin.
`prepare --verify-payload native.img` checks the same production payload
validation routine without opening MTD, accessing the watchdog or requiring
a camera. `build_sd_package.py` assembles the direct-extract archive from a
verified base kit and compiled preparer receipt. Runtime payload and streamed
U-Boot installer remain unchanged from the qualified R47 RC1.

R47 stable and R47 RC1 had identical runtime payloads. R48 adds persistent
hostname support and is a new firmware update. Updating an older native
release still uses `aggiorna_qc5.py` and its own per-unit backup before a
normal boot without Reset; plain extraction is the factory-install workflow.

## R48 hostname

System → Camera name → Save name persists a validated DNS label in the flash
Wi-Fi banks, even without SD. Empty restores the MAC-based default. V1
profiles are accepted; V2 records retain a V1 credential mirror for downgrade.
Kernel, media and peripheral modules remain byte-identical to R47. The
preparer and all script checks are regenerated for the exact R48 image.
