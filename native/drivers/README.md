# Native driver source — R43

The public source rebuild has produced **seven modules byte-identical to the
modules in the qualified R43**. This is source publication and build
verification; no module was installed or loaded on the camera for this work.

The two AIC Linux modules now match as well, bringing the total to **nine
specific modules**. See [radio source reconstruction](../radio/README.md)
and [combined verification](R43-all-driver-verification.json). Standard
MMC/FAT/NLS modules come from the verified upstream kernel source baseline.

| Component | Module | Source license |
| --- | --- | --- |
| PTZ and speaker gate | `qc5-peripheral.ko` | GPL-2.0-only; compatible GPL-2.0-or-later companion header |
| White/IR illumination | `qc5-illumination.ko` | GPL-2.0-only |
| SD host and recording I/O | `qc5-mmc-recording.ko` | GPL-2.0-only |
| Inherited SD clock | `qc5-msc-clock-ro.ko` | GPL-2.0-or-later |
| Read-only NOR runtime | `qc5-sfc-runtime.ko` | GPL-2.0-only; GPL-2.0-or-later clock helper |
| Wi-Fi profile flash view | `qc5-wifi-config.ko` | GPL-2.0-only |
| SSH identity flash view | `qc5-ssh-key.ko` | GPL-2.0-only |

The last three sources are in [`../modules/`](../modules). The SFC master
source is the deployed **read-only** implementation. It does not contain
the unused R41 prototype's write/erase callbacks; writes to configuration
sectors belong only to the separately bounded sidecars sharing its mutex.

[`SOURCE-MANIFEST.json`](SOURCE-MANIFEST.json) maps complete local include
closures to source hashes. Only the SD clock helper's include path was
flattened for standalone Kbuild; source and export hashes are both recorded.
[`R43-compile-verification.json`](R43-compile-verification.json) records the
actual rebuild and comparisons, not a model's prediction.

## Rebuild on Linux

Prepare the matching ported Linux tree and kernel build directory, using
[`../kernel/qc5-7.3.1.config`](../kernel/qc5-7.3.1.config). Then, from the
repository root:

```sh
python3 tools/build_native_drivers.py \
  --kernel-tree /path/to/ported/linux-7.3.1 \
  --kernel-build /path/to/matching/kernel-build
```

The helper verifies source hashes and runs Kbuild in a fresh output
directory, strips debug data and writes a JSON report. It never runs
`modules_install`, `insmod`, SSH, a flash writer, or a reboot. This does not
make a different kernel/SFC layout compatible: a successful build is not
permission to load these drivers on arbitrary hardware.

The source archive contains neither a private key nor radio firmware. The
AIC8800 Linux driver and its embedded firmware remain separate components.
The driver's exact source is now reconstructed and verified; the embedded
firmware source is still unresolved. These results do **not** claim the
entire device firmware is now open.

Qwen reviews 049 and 050 assisted the source/dependency classification,
sequentially. Compilation, matching hashes and header inspection provide
the verification. Unsupported model assumptions about ROM internals or
unseen header licensing are not treated as established facts.
