# R43 AIC8800 Linux driver source reconstruction

Both Linux radio modules have now been reconstructed from a pinned upstream
source archive plus the published QC5 patch, compiled with the matching
kernel profile, and verified **byte-identical to installed R43**:

| Module | SHA-256 |
| --- | --- |
| `aic8800_fdrv.ko` | `830962f68677db9cb87e836845d0c0fe90423c01c84ec5bbd1bf001ee31c19e7` |
| `aic_load_fw.ko` | `eb5d23e92b703c4d6452c851c06a53d4356abca8d9a39ef942aa7e5d63365da1` |

The earlier R4D9 snapshot was not equivalent. The actual source is the
R34 management/command-lifetime implementation, built with
`CONFIG_USE_FW_REQUEST=y`. The reconstruction verifies all 121 source,
header, Kconfig and Makefile hashes. It extracts no radio firmware files.

## Reconstruct source offline

Download the [pinned upstream archive](https://codeload.github.com/juerware/aic8800dc/tar.gz/a8bf40ea92082936d2e93b7657e3b9c1bd5697d5)
separately. Its SHA-256 must be
`15260a6d109a2bf178b3d443e514c9a12299ea82424b83072b5864fb27b524b8`.
Then run from the repository root, with Python 3.9+ and `patch`:

```sh
python3 tools/prepare_aic_source.py --archive /path/to/archive.tar.gz
python3 tools/build_native_drivers.py \
  --component radio --radio-source build/aic-source \
  --kernel-tree /path/to/linux-7.3.1 \
  --kernel-build /path/to/matching/kernel-build \
  --output build/radio-modules
```

Use fresh output directories. These tools verify source hashes and compile
only; they never connect to a camera, load modules, flash or reboot.
The compiler flags and resulting module hashes are in
[`R43-compile-verification.json`](R43-compile-verification.json).

## License and firmware boundary

The Linux modules declare GPL; the QC5 modifications are GPL-2.0-only.
Upstream copyright notices and per-file terms are retained, not replaced
with a new blanket license. The patch and source lock let maintainers
inspect and reproduce those exact sources instead of relying on a binary
module. The source reconstruction tools include no upstream firmware or manufacturer
dump. The R44 SD binary release separately includes the unchanged radio
firmware; that does not apply the Linux driver's GPL grant to those blobs.

This **does not provide the source for the binary firmware inside the
radio**. The driver still loads revision-specific FMAC patches, tables and
calibration payloads. A C array of register values is not proof that the
embedded firmware's source is available. The candidate SDK also contains
a calibration path that starts an uploaded program; do not classify every
file named `calib` as passive data. Runtime selection needs separate review.

See [open-source status](../../docs/open-source-status.md). Qwen reviews
049–052 assisted the comparisons sequentially; actual source reconstruction
and matching compiled hashes establish this result. No hardware change was
needed to restore the Linux driver's source provenance.

The subsequent [Wi-Fi reverse-engineering audit](../../docs/wifi-reverse-engineering.md)
documents the actual USB transport, executable calibration entry points and
an optional bounds/error-handling correction for the host patch-table loader.
Its separate candidate source lock and compile receipt do not replace the
byte-identical R43 baseline above. That source-audit receipt predates installation. The corrected driver is now
qualified and installed in R44; see [R44 validation](../R44-validation.json).
Its SHA-256 is `4b0a8516255f712636b1f251ba3d8e6b0f66195a9450d0779e4e70fbbccea7f8`.
The six radio firmware files remain unchanged.

For the R44 source profile, add `--audit-fixes` to `prepare_aic_source.py`
and `--radio-audit-fixes` to `build_native_drivers.py`. The original R43
byte-match receipts above remain historical and are not overwritten.
