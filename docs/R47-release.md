# R47 RC1 — automatic PTZ startup calibration

At each owner startup the PTZ controller runs the full calibration sequence:
pan left, pan right, pan to center; tilt down, tilt up, tilt to center.
The sequence is cooperative and keeps networking and media event handling
available. It runs once; reloading the WebUI or reconnecting Wi-Fi does not
start it again. Stop cancels calibration and invalidates both position
references. A fault does not cause an automatic retry loop.

The existing board limits remain 8000 pan steps and 2500 tilt steps. Centers
are 4000 and 1252, using the OEM setter's upward four-step rounding. The
original `motor_center`, probe and ioctl paths were checked with Ghidra;
the OEM generic probe defaults are not substituted for the tested board
limits. Like the original driver this uses full-span motion against the
mechanical travel limits. The MCU counter reports emitted steps; it is not
encoder feedback or a mechanical-stall sensor.

Full-span homing has a distinct completion result and retains the measured
count. Centering, jogs and absolute moves still require exact counts. The
change does not add a four-step tolerance to ordinary moves. Both position
references become valid only after all six phases succeed.

The only runtime binaries changed from R46 are `qc5-peripheral.ko` and
`qc5-native-portal`. The video/audio runtime and image-quality settings are
unchanged. The new portal requires peripheral progress ABI version 3; update
them together through the complete firmware package.

## Validation and limitations

The new sequencer passed 1900 checks with host ASan/UBSan and actual MIPS
execution. These include cancellation and malformed/short completion in
every phase. Install, recovery, bad-input and normal-boot loader models passed.
The PC SD tools passed two independent per-unit backup/recovery fixtures and
refused corrupted backups before changing their activation marker.

An instrumented RAM boot on the reference camera completed all six phases,
centered the software position, and immediately completed all four 256-step
jogs exactly. SSH, HTTP WebUI, decoded RTSP video/audio and a silent intercom
transport cycle passed. Automatic return to R46 and Wi-Fi without QR were
verified with PARAM, Wi-Fi and SSH records unchanged.

R47 was subsequently installed and read back on both native cameras. Both
automatically completed the six phases at their first flash startup, then
completed four exact 256-step jogs, WebUI/SSH and media/intercom checks. The
original backup pairs and restore capsules were rehashed on both SD cards;
PARAM, Wi-Fi and SSH records stayed unchanged.

That trial was started by a software reboot. Physical endpoint observation
and the intermittent first-move behavior after removing power remain
unqualified. See [the current validation receipt](../native/R47-validation.json)
for flash-installation status. Do not interpret a successful software reboot
as proof of every cold-power cycle.

## Source integration

The matching kernel source is in `native/drivers/ptz`. The portal actuator
owner is under `native/media/native-integrated/native-features/native-actuators`;
its ABI and progress-policy includes refer to the same driver headers.
The existing portal feature dispatcher includes `ptz-http.h` before the
day/night adapter and calls `qfh_tick()` from its event loop independently
of HTTP requests. That tick now also starts the once-per-owner calibration;
the day/night adapter yields while it owns the motors.

Existing native-build limitations in `native/README.md` still apply; this
release does not claim a new turnkey build of all historical generated
hardware inputs. No unit identity, credentials or original flash dump is
included in the public source or common installer.
