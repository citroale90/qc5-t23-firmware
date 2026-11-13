# Post-R43 source audit — 2026-11-13

This review changes candidate source, not the deployed R43 camera. No reboot,
module load, firmware installation or flash write was performed. The R43
validation/module rebuild receipts still describe their original inputs.
The source manifest retains original R43 hashes separately from current hashes.

## Reproduced defects and corrections

| Defect | Consequence | Correction and evidence |
| --- | --- | --- |
| SSH startup treated every failed flash load as an uninitialised key | Corrupt or unavailable storage could silently generate a different host identity | Only verified-empty status 2 permits provisioning; actual startup script exercised with injected command results |
| Startup imported an unbound legacy SD key | Moving the card could clone another camera's SSH identity | No automatic removable-card identity import; first provisioning creates a device-local key |
| Initial flash save failure allowed an ephemeral key to start SSH | The key could change at the next boot despite a supposedly permanent identity | Refuse initial server startup, remove the unpersisted key, retain verified existing identity on replica-repair failure |
| A killed key helper left `.flash-next` behind | Later loads failed on `O_EXCL`; previously this also triggered the wrong first-boot path | Under the existing exclusive lock/private RAM directory, remove the stale name, create with `O_NOFOLLOW`, handle short/interrupted writes, atomically rename |

`tests/test_ssh_startup.py` executes the real shell control flow with synthetic
83-byte keys: 4 of 8 scenarios failed against R43 and all 8 pass after correction.
The C publication test also checks a stale symlink without touching its target,
partial writes, a failed rename, cleanup, mode 0600 and preservation of the
previous complete key. Host tests use address/undefined-behaviour sanitizers;
the new C tests also run under `qemu-mipsel`. The userspace helper cross-compiles
for `mipsel-linux-musl`. No key-bank format or flash partition was changed.

An R42 SD-only identity migration must be explicit and tied to the correct unit;
this change never silently imports a key from an arbitrary card. Installed R43
already has flash-backed identity and does not need that migration.

## Source and dependency cleanup

- Restored 11 byte-pinned x264-derived OpenIMP files. Upstream `NOTICE` explicitly
  covers `src/t30/h264enc/` under GPL-2.0-or-later. Original notices are retained.
- Replaced the unlicensed external Annex-B helper dependency with
  `native/media/video-gc1084/annexb-native.h`. The independently written framing
  code handles chunk boundaries, emulation prevention and bounded output.
  832 encoder configurations produce identical descriptor, syntax and Annex-B
  outputs to the baseline; 625 exhaustive small vectors and 3125 chunk seams
  pass separate round-trip tests. This does not qualify timing/performance on
  hardware. See `native/media/ANNEXB-audit-verification.json`.
- Added a public nonce generator. One fresh private header must be shared by
  producer/child builds; a build nonce is not a substitute for an SSH key or
  device authentication. No existing deployment's nonce is published.
- Removed the unused `native/boot/bind-readonly-runtime.c` copy. `flash-init.c`
  uses the canonical `bind-native.c`; no caller referenced the removed copy.
- Retained 15 groups of identical snapshot/component headers. These preserve
  fixed historical build inputs or allow isolated builds. They do not represent
  extra RAM allocations on the camera. The measured inventory is in
  [source-duplicates.json](audit-data/source-duplicates.json).

## Model review and rejected findings

Qwen was used sequentially for persistence and media review. The stale-temp
failure was verified, not accepted solely from model output. Two proposed
media failures were rejected: `qat_complete` deliberately ignores the failed
clock result to retire a stopped DMA lease, and the supplied recording path
does not reach `waitpid(0)` from a failed spawn. Clearing an in-flight DMA lease
without confirming stop would introduce a fault. No such change was made.

## Scope and remaining work

This audit covers persistence failure paths, selected media ownership/queue
paths, exported source provenance and exact duplication. It is not a proof
that the entire codebase is bug-free or a new hardware qualification.

The manifest's omitted-input list fell from 31 to 19: 14 sensor/ISP calibration
headers, one privately generated nonce with a public recipe, and four external
descriptor/ROI files without an explicit license grant. Those four need a
licensed grant or an independently validated replacement. The radio still loads
six binary firmware files and the original U-Boot remains in use. The complete
firmware is therefore **not yet entirely open source**, and the portable native
image assembler is still incomplete. Do not publish a binary as a fully open
release by merely translating those binaries/tables into C arrays.

Future migration retains the separate flash Wi-Fi/SSH areas as described in
[migration-from-r43.md](migration-from-r43.md); no migration was run here.

## Subsequent R44 deployment

The historical review above was source-only. The SSH corrections were later
qualified and installed in R44, with separate [hardware evidence](../native/R44-validation.json).
The independent media/Annex-B candidate remains undeployed. Radio-internal
firmware replacement is outside the driver-only migration scope.
