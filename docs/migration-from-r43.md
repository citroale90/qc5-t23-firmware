# R43 to R44 driver and SSH update

R44 retains the working Linux 7.3.1 kernel, WebUI, media program and the six
firmware files executed inside the Wi-Fi chip. The update applies the audited
Linux Wi-Fi loader and SSH startup/key-publication corrections. Radio firmware
reverse engineering is outside this migration's scope.

The nine camera/radio-specific Linux drivers already had public matching R43
sources. R44 is a small corrective update, not a replacement of all nine binaries.
See the [complete 16-module inventory](../native/drivers/R44-driver-catalog.json).

## Data preserved

- BOOT/U-Boot, environment, WWW, PARAM and the original MAC.
- The entire **32 KiB** reserved NOR block `0x4f0000..0x4f8000`.
- Wi-Fi banks at `0x4f0000..0x4f2000` and SSH identity banks at
  `0x4f2000..0x4f4000`, with unchanged formats and per-camera MAC binding.
- SD configuration, recordings, NTP and remote-storage settings.
- Operation without SD for system startup, Wi-Fi and SSH.

The corrected SSH startup reads an existing valid flash identity first.
An I/O failure does not generate or import a different identity. Unbound SD
keys are not automatically imported; swapping SD cards cannot clone host keys.

## Backup and return paths

Keep both paths with the installation kit:

1. A current native R43 backup and its **matched R43 update capsule/loader**.
   A native return preserves the Wi-Fi/SSH reserved block.
2. The separate original full-flash backup and factory recovery capsule for
   the same camera. A factory return clears the native Wi-Fi/SSH block.

Do not rename the R43 backup to `QC5FULL.BIN`: that validator requires the
original kernel fingerprint. Do not mix another camera's PARAM, identities,
backup capsule or a loader pinned to another payload.

This remains a single 8 MiB NOR device, not A/B firmware. SD recovery requires
working U-Boot and cannot repair a damaged bootloader. R44 changes neither
U-Boot nor its environment/partition layout.

## Qualification and installation

The release procedure checks exact source/build hashes, capsule bounds,
readback and protected areas. Model cases cover installation, factory return,
normal native handoff and rejected corrupt RAM images. A full RAM boot must
then verify the new Wi-Fi driver, unchanged MAC/host key, Wi-Fi from flash,
WebUI and decoded RTSP video/audio before flash activation.

After installation, verify the runtime partition, update journal and unchanged
configuration banks, then perform a normal reboot and repeat Wi-Fi/SSH/WebUI
and video/audio checks. Existing unchanged PTZ, illumination and media source
rebuild proofs remain separately documented; a new physical observation is
not implied by an automated protocol result.

The private per-unit kit contains the update and original recovery pair,
checksums and a local SD preparation helper. It is not a generic installer for
factory cameras. A different unit requires its own verified original backup
and matching capsule. Private capsules and radio binaries are not distributed
through the public source repository.

After a factory return, remove the SD or disable its native loader before
another boot. After a native update, the consumed one-shot marker must be zero.
Follow the kit's instructions without depending on an AI session.
