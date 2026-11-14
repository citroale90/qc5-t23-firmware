# R46 RC1: PTZ initialization and shared SD update

R46 RC1 changes only `qc5-peripheral.ko` in the R45 RC2 runtime. It preserves
the R45 low-light profile, 1280x720 encoder, audio/intercom, network,
recording services and flash-backed Wi-Fi/SSH identities. The Linux board
profile and all other runtime files are unchanged.

## PTZ correction

The original ID 0x53 driver groups paired setup writes and the four counter
reset bytes. The previous port inserted 50 microseconds after every byte.
R46 restores the original register/delay schedule, retaining the explicit
IR bridge-off safety operation before enabling motor power. It reads back
both counters before marking initialization complete and records the
pre-move observation separately from completion.

Short moves and overruns remain errors. There is no four-step tolerance,
artificial zeroing of measured progress or automatic acceptance of a 260-step
result for a 256-step request. The original decoder, direction mapping,
manual-move and stop ramps are unchanged. Calibration is still required for
known absolute positions and presets.

## Download and use

Download [QC5-R46-SD-RC1.zip](https://github.com/citroale90/qc5-t23-firmware/releases/tag/r46-sd-rc1)
and extract it to a folder on the computer. The same kit supports:

- **Already running native QC5:** keep that camera's own SD and original
  backup; run `python3 aggiorna_qc5.py --sd "/path/to/SD"`. Eject normally,
  insert in the same powered-off camera and power on **without Reset**.
- **Compatible factory QC5:** run `python3 prepara_sd.py --sd "/path/to/SD"`.
  Eject normally, insert in the powered-off camera, hold Reset while applying
  power and release after about three seconds. No initial companion app is
  required. Enter Wi-Fi in `config.txt` or use the QC5 HTTP setup hotspot.

Keep power applied for at least 15 minutes. Use FAT32/MBR and read the kit's
`AVVIO-RAPIDO.txt` and `LEGGIMI.txt` before installation. Compatibility is the
QC5_YA_CODE_V82 / T23DL / GC1084 / AIC8800DC USB / 8 MiB NOR profile; a T23
processor alone does not establish compatibility.

First installation creates and verifies two full original backups and a
unit-bound recovery capsule before installing. Native updates preserve and
verify the existing original pair. Never exchange backups or identities
between cameras. Normal operation does not require an SD; SD recording and
SD recovery do.

## Recovery and downgrade

On the computer, with the camera off and its own SD inserted:

```sh
python3 ripristina_sd.py --sd "/path/to/SD" --elenca
python3 ripristina_sd.py --sd "/path/to/SD" --prepara-ripristino
```

Eject normally, insert into the camera belonging to that backup and power on
without Reset. Keep power applied for at least 15 minutes. This restores the
unit's original KERNEL/ROOT/SYSTEM; it requires intact U-Boot and is not ROM
recovery or an A/B system. The kit's hardware/identity checks refuse a
mismatched backup before arming. To return to R45, use the R45 kit's
`aggiorna_qc5.py` with the same original backup.

## Qualification

The R46 RAM trial on the reference camera verified zero counters after
initialization, all four 256-step jogs including the first upward movement,
unchanged Wi-Fi/SSH records and automatic return to installed R45. Tests
cover the original register/delay trace, 23 injected transfer failures,
strict short/overrun detection, MIPS module compilation, boot archive
integrity, installer/recovery models and 12 PC preparation/update/recovery
cases using two independently bound backups.

R46 RC1 is installed from flash on two existing native QC5 cameras. Both
rejoined Wi-Fi without QR and passed root SSH, WebUI, the first upward jog
and all four 256-step jogs, intercom transport with silence, and 1280x720
video/audio decoding after intercom release. Both kept the same boot through
the checks, with no OOM kills and unchanged PARAM/Wi-Fi/SSH records.

The flash-installation and post-boot record is in
[`native/R46-validation.json`](../native/R46-validation.json). A successful
warm boot does not qualify every cold-power start: the original first-jog
symptom was intermittent and its resolution after complete power removal
remains unconfirmed. First installation on an untouched factory unit has
not been physically qualified for this release. R46 RC1 remains a prerelease.

The source/licensing scope is unchanged: Linux driver sources are published;
the device still retains its original bootloader and radio firmware blobs.
Private dumps, calibration, Wi-Fi credentials, host keys and per-unit
recovery capsules are excluded from the public ZIP and repository.
