# R45 RC2: image quality and SSH provisioning

R45 RC2 keeps the Linux 7.3.1 board profile, WebUI, network, intercom,
PTZ, IR and recording services from R44. It changes the native media binary
and SSH key helper. The experimental hostname UI is not included.

## Image policy

The accepted low-light profile caps integration at 450 sensor lines (20 ms,
1/50 s at the verified 25 fps timing). Temporal reference curves and Y/chroma
upper weights are capped at 192. Spatial coefficients, enable bits, thresholds,
buffer addresses, 1280x720 geometry, QP and GOP remain unchanged. The user
confirmed reduced trails with acceptable quality; more noise is possible.

The build-time tool `tools/tune_native_profile.py` transforms a locally
supplied 66,364-byte native MDNS profile, retaining duplicate trigger order
and checking minimum/maximum consistency and the checksum. Example:

    python3 tools/tune_native_profile.py local-profile.bin gc1084-mdns.private.h --format c --ceiling 192

The media build defines `QC5_AE_MAX_EXPOSURE_LINES=450`. The generated profile
matched all 16,591 words of the hardware-tested profile. No camera buffers or
runtime allocations are added. Factory calibration and recorded scenes remain
outside the public source tree.

## Boot archive and SSH

The local R45 RC1 production image was withdrawn before installation: its
initramfs was compressed twice. RC2 has one XZ layer decoding directly to
newc CPIO with the exact executable `/init`. Tests reject nested XZ, truncated
archives, duplicate/unsafe paths and an invalid or missing init. The published
R44 GitHub archives are unaffected.

The SSH helper recognizes uniformly zero-filled empty records, as well as
uniformly erased records. Populated invalid/conflicting records still fail
closed. Existing valid keys are preserved; each unprovisioned unit generates
its own MAC-bound identity and verifies two flash copies.

## Installation and recovery

Download the common SD ZIP from the [R45 RC2 release](https://github.com/citroale90/qc5-t23-firmware/releases/tag/r45-sd-rc2).
An existing QC5 installation uses `aggiorna_qc5.py` and power-on without Reset.
A compatible factory camera uses `prepara_sd.py` and the Reset entry described
in the ZIP. Keep power on for at least 15 minutes.

Each unit retains its own verified full original backup, recovery capsule,
loader and manifest. Native updates preserve BOOT/ENV, WWW/PARAM and the
Wi-Fi/SSH sector. Recovery requires intact U-Boot and restores that unit's
original KERNEL/ROOT/SYSTEM. It is not ROM recovery or A/B flash. Never exchange
per-unit backups, capsules or SSH identities. Copy private backups off the SD.
Normal operation does not require the SD; local recording and SD recovery do.

## Qualification and remaining limits

RC2 is installed from flash on two existing native QC5 cameras. Both rejoined
Wi-Fi without QR and provide root SSH and WebUI. RTSP video/audio decode at
1280x720. The second unit completed 320 seconds of WebUI video: 8,000 decoded
frames, no decoder errors or reconnects, same boot. Intercom transport and
bounded PTZ commands were exercised. `native/R45-validation.json` separates
automatic checks from human observations.

The first uncalibrated upward jog can report a counter mismatch (260 versus
256 requested steps); subsequent up/down checks completed at exactly 256.
The PTZ driver is unchanged from R44. Calibration remains required for known
positions and presets. A protocol acknowledgement does not establish physical
sound or movement.

Packaging has 30 generic installer cases, 41 core checks, seven U-Boot models
and 20 PC preparation/reuse cases. A new first R45 installation on an untouched
factory unit is not physically qualified. This is a prerelease. The source
scope remains as documented for R44: radio firmware blobs and original U-Boot
are retained.
