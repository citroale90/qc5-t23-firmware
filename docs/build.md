# Local build and SD installation

**This guide builds the legacy Linux 3.10 application release. It does not
build or install native Linux 7.3.1 R43.** See [`native/README.md`](../native/README.md)
for the native source export, its tests, required private inputs and current
reproducibility limits. Never use a legacy application capsule as a native
kernel/root update.

## Requirements

Use Python 3.9 or later and an XZ-enabled `mksquashfs`. The validated image
build used squashfs-tools 4.7.5 with XZ preset 9. Other builds may produce
different compressed bytes. The builder checks the actual partition limits
and verifies the resulting filesystems.

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements-build.txt
```

Keep camera dumps outside Git, for example under the ignored `local/`
directory. The assembler accepts three 8 MiB dump inputs:

| Input | Firmware in the dump | Purpose |
| --- | --- | --- |
| `--base-flash` | Verified installed R4g5b | Runtime and base files |
| `--factory-flash` | Supported QC5 V82 factory image | Stock SYSTEM/WWW and recovery |
| `--r4e2-flash` | Installed R4e2 with NTP | Optional upgrade from R4e2 |

The assembler checks the boot code, kernel, ROOT and application
fingerprints. It carries forward the tested application binaries from R4g5b
and rebuilds the two application filesystems with the localized WebUI.

```sh
python build.py \
  --base-flash local/r4g5b.bin \
  --factory-flash local/factory.bin \
  --r4e2-flash local/r4e2.bin
```

The optional `--mksquashfs` argument selects a specific tool binary.
Output goes to `build/firmware`. Use a fresh `--output` directory for a
subsequent build. Each variant contains a verified pair of update and stock
recovery packages with its matching `prepara_sd.py` helper.

## SD installation

Choose the variant matching the current camera:

| Folder | Starting firmware |
| --- | --- |
| `prima-installazione` | Supported factory firmware |
| `aggiornamento-da-r4g5b` | Tested R4g5b release |
| `aggiornamento-da-r4e2` | R4e2 with NTP, when supplied |

Use a mounted FAT32 SD card with at least 32 MiB free. From the chosen
variant folder, run the check first and then prepare the card:

```sh
python3 prepara_sd.py --sd "/Volumes/UNTITLED"
python3 prepara_sd.py --sd "/Volumes/UNTITLED" --installa
```

Substitute the actual mount path. On Linux use the mounted volume path.
Eject the SD card, insert it with the camera powered off, and hold Reset
while powering it on. Release Reset after about three seconds. Keep
the camera powered for at least five minutes. The installer stores and
verifies an 8 MiB backup before erasing any application blocks. Keep a
private copy of the backup for that specific unit.

To prepare stock recovery, use the same variant with `--ripristina`
instead of `--installa`, then follow the same power-on sequence. Stock
recovery uses intact original BOOT, kernel and ROOT partitions.

## First connection

Use the address assigned by your router. The local WebUI uses port 80 and
RTSP uses port 8554 with path `/live`. For a camera waiting for Wi-Fi,
join the `QC5-…` hotspot and open `http://192.168.168.1/setup`.

SSH is enabled as root without a password by default. Keep it on a trusted
management network, or set `SSH_ENABLE=0` in `config.txt` if you do not
need it. Browser talk uses HTTPS through a local reverse proxy.
