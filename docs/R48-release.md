# R48 — persistent camera name

The existing System card adds a camera-name form without changing the media
player or other controls. The network owner is the only writer. HTTP queues a
validated literal request using the existing CSRF token and private mailbox.

Names are lowercase DNS labels, maximum 63 characters, with no leading/trailing
hyphen. Empty means the unique MAC-based default. The name and credentials share
the existing two 4 KiB flash banks; no new partition or driver is added.

A V2 record is verified in the inactive bank before refreshing the other bank's
lower-generation V1 credential mirror. Existing V1 records are accepted. A normal
downgrade can still reconnect through the V1 mirror. Interrupted writes preserve
either the previously verified record or the new verified record. Repeated saves
of the same profile avoid flash erases. Flash, including an explicitly empty
name, takes precedence over stale SD configuration. Optional SD-mirror failure
does not undo a verified flash save.

The current system hostname and the owned DHCP client's advertised name are
updated without restarting the Wi-Fi radio. A failed application is reported
separately from failed storage. Router DNS/lease behavior is outside the camera.

Kernel, media/audio, image tuning, PTZ homing, flash Wi-Fi driver and SSH driver
are byte-identical to R47. Only network owner, HTTP portal and index.html change.
[Validation](../native/R48-validation.json) records offline and hardware results.

## Installation and recovery

Download QC5-R48-SD.zip from the r48-sd release. Original supported twins use
extract-to-root, Reset at power-on for about three seconds, and uninterrupted
power for at least fifteen minutes. Already-native cameras use aggiorna_qc5.py
with their own original backup before a normal boot without Reset. Read the
included LEGGIMI.txt. Per-unit double backups and restoration remain mandatory;
never share identity/configuration blocks between units. Recovery needs intact
U-Boot and does not recover a damaged bootloader from the processor ROM.

The common package contains no personal keys, credentials or unit dumps.
The radio's internal firmware and the retained original bootloader remain the
binary components described in the existing licensing notices.
