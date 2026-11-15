# R47 stable — automatic PTZ startup calibration

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

On 2026-11-15 the owner confirmed that the behavior is OK and requested the
stable designation. The runtime payload is byte-identical to the installed
R47 RC1. This is an owner acceptance report, not a new instrumented power-cut
measurement. The [validation receipt](../native/R47-validation.json) keeps
that provenance distinct from the automatic RAM/flash tests.

## Extract-to-SD factory installation

Extract the complete ZIP directly into the root of a FAT32/MBR card. No PC
script is required for an original supported camera. Insert with power off,
power on holding Reset and release after about three seconds; leave power
applied for at least fifteen minutes. `qc5-run` and `qc5-installer` must be
at the card root, without an enclosing folder. The installer action and entry
trigger are already present. Configuration and previous backup folders are
not included as overwrite targets. See the [quick guide](../native/installer/common/AVVIO-RAPIDO.txt).

The old common RC1 kit carried a stale preparer payload pin and loader hash.
It would refuse factory installation; native runtime updates followed a
separate qualified path and were unaffected. The stable kit regenerates its
preparer for the actual native payload and generates script checks from the
packaged binaries. The real MIPS preparer accepted the correct image and
refused six invalid/old/path cases; eight C capsule checks passed with two
independent original units and refused cross-unit data. The read-only
verification mode does not access a camera, MTD or watchdog.

Cameras already running R47 need no reflash. Older native QC5 versions use
`aggiorna_qc5.py` with their own backup, then boot normally without Reset;
plain extraction is the factory-install workflow. The new stable factory
kit has not been re-run physically on another untouched unit for publication.

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
