# VeePai QC5 T23 firmware tools

Local firmware modifications for the **QC5_YA_CODE_V82** camera with Ingenic T23, GC1084, AIC8800DC USB Wi-Fi, and 8 MiB NOR flash. The current snapshot corresponds to **R4g5b-i18n1**.

The camera serves an Italian/English WebUI, a local H.264 player with audio and talk, RTSP, PTZ, IR and white-light controls, and SD recordings. The Wi-Fi setup page is available on the camera's fallback hotspot. The WebUI can generate Home Assistant controls using the address from which the page is opened. RTSP is configured separately in Generic Camera.

[Italiano](docs/README.it.md) · [Build and install](docs/build.md) · [Credits and licensing](NOTICE.md)

## Repository layout

- `src/` contains the gateway, media and control code, Wi-Fi helpers, recording tools, and application verifier.
- `web/`, `locale.js`, and `locales/` contain the complete WebUI and the language catalog.
- `firmware/` and `tools/` contain boot, SD preparation, update, and recovery sources.
- `tests/` contains host tests with a simulated camera.

This source repository uses local, owner-supplied firmware dumps for assembly. See the build instructions for the exact inputs. The supported application package updates SYSTEM and WWW and checks the starting firmware fingerprints before writing.

## Quick checks

With Python 3.9+ and a C compiler, run these commands from the repository root:

```sh
python3 build.py
python3 -m unittest discover -s tests
```

The first command renders the WebUI into `build/web`. Host tests exercise the gateway and streaming protocols over loopback. Assembling an SD package also requires the dependencies and firmware inputs in [the build guide](docs/build.md).

R4g5b-i18n1 was installed and checked on a real QC5 V82 by upgrading from R4g5b. The factory and R4e2 installation variants were verified in simulation.
