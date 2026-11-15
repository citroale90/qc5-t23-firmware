# R49 — Day/Night Auto after a manual override

R48 was reproduced on hardware: Day → manual Night → Auto remained Night
for 80 seconds, including its no-IR ambient probe. The probe reported 450
exposure lines, gain index6 and mean125. Its old day criterion required at
most 224 lines/gain2, while its 800-line dark criterion was unreachable with
the 450-line (20 ms) exposure ceiling already installed by R45.

The controller now initiates an ambient probe on explicit Auto entry after
the preceding acknowledged transition settles. It turns IR off, waits three
seconds for fresh ambient samples, and requires one second of consistent
classification. The bounded probe lasts at most eight seconds and restores
IR if it cannot establish usable ambient light. It does not force a day trial
on re-entry from manual Night in darkness or bypass image/filter/LED acknowledgements.

Explicit Auto entry prefers colour when ambient light is usable, so the
previous manual override does not determine the result inside the normal
hysteresis band. Subsequent automatic probes retain the stricter threshold.
The GC1084 gain table identifies index 8 as 4× and index 16 as 16×; the policy
uses this two-stop sensor-gain band with the 450-line exposure cap. These
are image-quality policy limits, not calibrated lux. Temporal confirmation
handles gain fluctuations without requiring 500 ms of exactly identical AE
settings. Stale or non-advancing frames cannot accumulate a decision.

Only daynight-controller.h and the resulting portal binary change in the
runtime. Kernel, media/AE/image tuning, Wi-Fi, hostname persistence, audio,
SSH and PTZ driver binaries retain their R48 bytes. Regression checks cover
the recorded trace, dark retention,450-line dark detection, AE jitter,
repeated Auto requests, manual cancellation, stale samples, periodic probes,
and consistent explicit entry from either manual mode. Validation records
separate model tests from hardware tests.

## Install and restore

Use QC5-R49-SD.zip and its included instructions. Original supported twins
use extract-to-root, Reset at power-on for about three seconds, then at least
15 minutes of uninterrupted power. Native QC5 s, R48 included, use the supplied
aggiorna_qc5.py with that unit's own SD and original backup, then a normal boot
without Reset. Per-unit original backup and recovery remain available; never
copy identity/configuration blocks between units. Recovery requires intact
U-Boot. The radio firmware and retained original bootloader remain the binary
components documented by the existing licensing notices.
