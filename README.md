# VeePai QC5 T23 firmware tools

Local firmware development for the **QC5_YA_CODE_V82** camera with Ingenic T23DL, GC1084, AIC8800DC USB Wi-Fi, and 8 MiB NOR flash. The current deployed native release is **QC5 Linux 7.3.1 R44 RC1** (2026-11-13).

The native sources and validation record are in [`native/`](native/README.md).
The top-level application builder and `src/`, `web/`, `firmware/` still
describe the earlier **R4g5b-i18n1 / Linux 3.10** line. Its installer is not
an installer for native R44. Native source export is not yet a standalone,
reproducible firmware distribution; required generated inputs and third-party
source/licensing gaps are documented explicitly.

The camera serves an Italian/English WebUI, a local H.264 player with audio and talk, RTSP, PTZ, IR and white-light controls, and SD recordings. The Wi-Fi setup page is available on the camera's fallback hotspot. The WebUI can generate Home Assistant controls using the address from which the page is opened. RTSP is configured separately in Generic Camera.

[Italiano](docs/README.it.md) · [Build and install](docs/build.md) · [Credits and licensing](NOTICE.md)

## R44 status

- The audited open-source Wi-Fi Linux loader and SSH startup/key helper are installed. The kernel, media, WebUI and six radio firmware files are unchanged.
- All 16 loaded Linux modules have identified source: nine camera/radio-specific modules and seven standard kernel modules. See the [driver inventory](native/drivers/R44-driver-catalog.json).

- Wi-Fi credentials persist in flash; startup does not require a QR or SD card.
- The original SSH host identity persists in two separate flash records. Normal
  boots avoid rewriting valid copies, and native upgrades preserve both the
  SSH and Wi-Fi areas. Root SSH remains available.
- A real normal reboot verified Wi-Fi, SSH identity, HTTP WebUI, and decoded
  1280×720 video/audio. A separate RAM boot with no SD drivers and no embedded
  private key verified loading the key from flash.
- The intercom retains the qualified half-duplex microphone gate. SD remains
  necessary for local recordings and other features that use files on it.
- The streamed SD installer/recovery sources are published. Device-specific
  firmware capsules, keys, credentials, dumps and recovery images are private
  and are not GitHub release assets.

See [native validation](native/R44-validation.json) and
[remaining open-source work](docs/open-source-status.md). The WebUI is open
source; the **complete device firmware is not yet entirely open source**.
In particular, the radio still loads binary firmware, and this update retains
the camera's original bootloader.

All nine specific Linux modules (seven native modules plus two AIC radio
modules) have now been rebuilt byte-identical to deployed R43, with
[driver build instructions](native/drivers/README.md) and
[pinned radio source reconstruction](native/radio/README.md).
The [R43-to-R44 migration](docs/migration-from-r43.md) preserves Wi-Fi, SSH
identity and settings. The per-unit install/recovery kit remains private;
source publication does not distribute another camera's backup or identity.

## Repository layout

- `src/` contains the gateway, media and control code, Wi-Fi helpers, recording tools, and application verifier.
- `web/`, `locale.js`, and `locales/` contain the complete WebUI and the language catalog.
- `firmware/` and `tools/` contain boot, SD preparation, update, and recovery sources.
- `tests/` contains host tests with a simulated camera.
- `native/` contains the R43 persistence, startup, installer, media source
  snapshot, actual native WebUI assets, and initial kernel reference patches.

The legacy source assembler uses local, owner-supplied firmware dumps. Its
application package updates SYSTEM and WWW and checks the starting firmware
fingerprints before writing. Native R43 instead uses its own KERNEL/ROOT/SYSTEM
layout and verified loader. Do not interchange these packages.

## Quick checks

With Python 3.9+ and a C compiler, run these commands from the repository root:

```sh
python3 build.py
python3 -m unittest discover -s tests
```

The first command renders the WebUI into `build/web`. Host tests exercise the gateway and streaming protocols over loopback. Assembling an SD package also requires the dependencies and firmware inputs in [the build guide](docs/build.md).

R4g5b-i18n1 was installed and checked on a real QC5 V82 by upgrading from R4g5b. The factory and R4e2 installation variants were verified in simulation.

The tests also exercise the native SSH/Wi-Fi flash-record logic against
synthetic interrupted writes. They do not connect to a camera or write flash.
