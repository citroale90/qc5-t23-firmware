# Native QC5 Linux 7.3.1 — R44 RC1

This tree retains the R43 source baseline and matching rebuild receipts.
R44 deploys the audited SSH key/startup fixes and the open-source AIC Linux
loader correction. Kernel, WebUI, media and the six internal-radio firmware
files remain unchanged. See [R44 hardware validation](R44-validation.json)
and the [complete Linux-module inventory](drivers/R44-driver-catalog.json).

The separate Annex-B/media audit candidate remains **undeployed**. Original
R43 source hashes and historical receipts are preserved; current per-file
hashes and deployment flags are in the source manifest.

## Contents

| Folder | Component |
| --- | --- |
| `ssh/` | MAC-bound Ed25519 host-key records, Linux helper, startup and interrupted-write tests |
| `wifi/` | Flash-backed Wi-Fi profiles, network-controller recovery, original MAC handling |
| `modules/` | Bounded SFC runtime, Wi-Fi and SSH flash views; original shared SFC mutex |
| `drivers/` | Standalone GPL PTZ/speaker, illumination, SD and clock sources, Kbuild helper and seven-module rebuild proof |
| `radio/` | Pinned upstream source + QC5 patch reconstructing the two AIC Linux modules, with matching rebuild proof |
| `common/` | Frozen headers and policy sources used by the native frontend |
| `boot/` | Production startup, read-only runtime binding, decompressor sources |
| `installer/` | Bounded SD update/recovery implementation, preserving native configuration sectors |
| `media/` | Source snapshot used for native video/audio, half-duplex talk and recording |
| `web/` | Actual rendered R43 WebUI assets, distinct from legacy `web/` |
| `kernel/` | Verified upstream baseline, actual kernel configuration, board DT and older reference patches |

[`SOURCE-MANIFEST.json`](SOURCE-MANIFEST.json) records origin-relative
paths, source/export hashes, license markers and omitted inputs. Absolute
development includes and the native network includes are made
relative in the exported tree. Synthetic tests use a locally administered
example MAC rather than the test camera's identity.

[`R43-validation.json`](R43-validation.json) is a sanitised hardware receipt.
It contains no device serial, MAC, address, SSH fingerprint or credentials.
The media program is the same qualified implementation as R42; the R43
change is SSH identity persistence.

## Flash identity layout

| Physical NOR range | Purpose |
| --- | --- |
| `0x4f0000..0x4f2000` | Two alternating 4 KiB Wi-Fi profile banks |
| `0x4f2000..0x4f4000` | Two 4 KiB SSH host-key banks |
| `0x4f0000..0x4f8000` | Entire block preserved/normalised by native upgrades |

SSH uses an 83-byte canonical Dropbear Ed25519 key in SHA-256 checked,
MAC-bound records. Existing flash identity takes precedence over removable
SD state. A valid single replica is repaired on startup; two valid replicas
are not erased or programmed again. Conflicting valid identities are refused
by the store. No universal or device-specific private key is embedded here.

Wi-Fi retains its separate atomic profile format. Root SSH remains enabled
without a password, as configured for the local deployment. The browser's
HTTPS/microphone permission is handled through a local reverse proxy, not
an on-camera TLS server.

## Offline tests

From the repository root, with Python 3.9+ and a compiler with ASan/UBSan:

```sh
python3 -m unittest discover -s tests -p test_native_persistence.py -v
```

These tests use synthetic memory arrays and temporary local files. They check 513 SSH partial
record-write boundaries, replica repair, unchanged identity over 1000 saves,
512 Wi-Fi partial-write boundaries, interrupted key publication, and exported-source hashes. They cannot
arm an update, contact the camera, or write flash.

On Linux, the userspace SSH helper can be compiled against Linux MTD headers:

```sh
cc -Os -I native/common native/ssh/key-helper.c -o qc5-ssh-key
```

The kernel modules require the exact ported kernel tree/configuration and
matching SFC driver layout. `MODULE_LICENSE` does not make them compatible
with an arbitrary upstream kernel. Do not load them on a different layout.

## Build and recovery limits

This is **not a turnkey native firmware build or generic factory installer**.
The kernel baseline and configuration are now identified and published;
complete portable flash-image build integration and some generated hardware
inputs are not yet included. Generated sensor
profiles were deliberately omitted; a public recipe generates a fresh private
producer nonce. Eleven explicitly GPL-covered x264 helpers are included;
four remaining external descriptor/ROI files require a license grant or
replacement. The manifest lists these
gaps instead of replacing them with invented data.

No per-unit update/recovery capsule or factory dump is published. The common
R44 image is available in the [SD prerelease](https://github.com/citroale90/qc5-t23-firmware/releases/tag/r44-sd-originali-rc1). The validated update/recovery pair
is specific to its owner's camera and depends on matching original backup
fingerprints. A package for another unit must be prepared from that unit's
own verified backups. Do not copy another unit's PARAM, keys or identities.

The SD recovery loader requires working U-Boot. It is not ROM recovery from
a damaged bootloader. A factory restore replaces KERNEL/ROOT/SYSTEM and
clears the native Wi-Fi/SSH areas; it does not preserve them like a native
upgrade. After factory restore, disable the SD loader or remove the card
before the next boot. No recovery operation was triggered for publication.

## Common first installation on factory twins

The separate [common SD installer](installer/common/README.md) now automates
the required per-camera backups and capsule binding on supported original
firmware. It includes a PC copier and recovery/reuse tool, and stores the
matching loader/manifest with every backup. No reference unit's factory
dump or SSH/Wi-Fi identity is included in the common kit. First cold
installation of this new kit on an untouched twin is still physically
unqualified; the runtime and shared U-Boot installer have separate proof.

## R46 RC1

The current native release updates PTZ initialization and preserves the R45
imaging/audio/network runtime. See [R46 release notes](../docs/R46-release.md)
and [flash/RAM validation](R46-validation.json). The common SD kit supports
first installation, native updates and unit-specific recovery.
