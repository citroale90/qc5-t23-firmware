# Planned migration from the installed R43

This is the migration contract for a future fully open-source driver
release. It is not an installer, and that release is not ready while the
radio firmware and corresponding-source gaps remain unresolved. The
currently working camera stays on R43 during this source work.

The nine specific Linux modules now reconstructed from public source are
already the modules installed in R43. Publishing their source does not
require reinstalling identical binaries. Migration becomes necessary when
the remaining components are replaced or executable/data formats change.

## Preserve the current installation's data

- Preserve BOOT/U-Boot, its environment, WWW, and PARAM/original MAC.
- Preserve the entire reserved NOR block `0x4f0000..0x4f8000`. The existing
  Wi-Fi format occupies `0x4f0000..0x4f2000`; the current SSH identity
  occupies `0x4f2000..0x4f4000`.
- Keep the SSH host key and Wi-Fi record formats compatible; read them
  before generating defaults. Never distribute a shared SSH identity.
- Retain `config.txt`, NTP/router settings and other supported SD settings,
  recordings and remote-storage configuration. Credentials stay private.
- Preserve operation without an SD card for boot, Wi-Fi and SSH. Local
  recordings and SD-based restore still require a card.

## Keep two distinct return paths

1. A per-camera backup and matched native **R43** forward capsule/loader
   pair for returning to the currently working native installation.
2. The separate, unchanged **factory** recovery capsule and original
   full-flash backup for that camera.

Do not rename a native R43 backup to `QC5FULL.BIN`. The stock recovery
validator checks the original kernel fingerprint. Native R43 return uses
its own matched loader and update payload. Mixing loaders, payloads or
backups from different releases/units must fail before erasing anything.

The camera has a single 8 MiB NOR image, not an A/B layout. Existing SD
recovery depends on intact U-Boot; it cannot repair a damaged bootloader.
No bootloader/environment/partition change is needed for this migration
contract. A later layout change requires a separately verified migration.

## Gates before an installation package is offered

1. Export the complete corresponding source, generated hardware input
   provenance, per-file licenses and reproducible build instructions.
   Firmware-in-radio blobs must be reported separately from Linux drivers.
2. Rebuild and test the new driver implementation; do not substitute a
   license label for source completeness or hardware equivalence.
3. Validate backup, update/return pair, bounds, readback and reserved-sector
   preservation offline, including interruption handling.
4. Boot the complete candidate in RAM before a flash update. Verify Wi-Fi
   without QR, unchanged MAC/SSH identity, WebUI, RTSP video/audio, Parla,
   PTZ in four directions, white/IR lights and automatic day/night.
5. Verify hotspot association, automatic HTTP setup, return to saved Wi-Fi,
   resumed video, reconnection after a lost network, motion stop and SD/
   remote recording behavior. Check memory during simultaneous operation.
6. Verify normal reboot and power-cycle persistence, plus return to R43
   with the correct SD package. Then assemble an unarmed installation kit
   from the target camera's own backups.

A factory restore clears the native Wi-Fi/SSH areas and is different from
a native upgrade preserving them. After factory restore, remove the SD or
disable its native loader before the next boot. Preserve these instructions
with the recovery package; do not rely on an AI session for recovery.
