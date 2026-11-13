# AIC8800DC reverse engineering — first verified boundary

This analysis follows the source audit and uses Qwen sequentially (reviews
055 and 056), Capstone Thumb disassembly and Ghidra decompilation of exact
local R43 firmware copies. It does not execute uploaded ARM code, access a
radio, reboot a camera or install a module. Raw firmware/decompilation are
not redistributed as purported open-source implementations.

## Actual Linux/radio boundary

The Linux driver is already GPL source with a byte-matching R43 rebuild.
The camera build selects **USB**, not SDIO (`CONFIG_USB_SUPPORT=y`,
`CONFIG_SDIO_SUPPORT=n`). Do not infer the transport from a generic AIC guide.
It also enables `CONFIG_DPD` and `CONFIG_FORCE_DPD_CALIB`; the latter excludes
the cached-DPD-file path. `CONFIG_USE_FW_REQUEST=y` is supplied at build time.

| Input | Driver load address | Observed local files |
| --- | --- | --- |
| FMAC patch | `0x00180000` | 31,596 bytes normal U02; 30,664 bytes H U02 |
| Calibration executable | `0x00130000` | 40,284 bytes normal U02; 33,428 bytes H U02 |
| Patch table | Metadata destination from its first word, then explicit address/value pairs | 128-byte header plus 110 normal / 91 H pairs |

`chip_sub_id=1` selects normal U02 and `chip_sub_id=2` selects H U02. The
driver also contains a sub-ID-0 branch for different files; it is not the
same pair of U02 inputs. The six actual hashes, sizes and table metadata are
in [the inventory](../native/radio/hardening/R43-firmware-inventory.json).
They are general firmware files, not camera credentials or identities.

The DPD loader starts the calibration with `HOST_START_APP_FNCALL` at
`0x00130009`. Clearing the Thumb bit gives instruction address `0x00130008`.
The first code changes bits 17/18 at `0x40506008` and polls `0x40506030`,
then calls a patch helper: `0x00181dd4` normal / `0x001819cc` H. Ghidra confirms
the normal helper performs MMIO setup, including clearing **bit 14** at
`0x40344028`, and calls additional code outside the uploaded regions.
Register roles inferred from context are not hardware specifications.

Important unobserved dependencies include calls to `0x00012eb0`,
`0x00012ea0`, `0x0003652c` and indirect calls through a table at `0x001103b8`.
Their implementations are not in the mapped calibration or patch files.
They may be ROM/resident firmware services; this analysis has not read the
chip's ROM and does not prove their implementations or ABI. An address such
as `0x00012eb0` must not be confused with `0x0012eb00`.

Qwen's initial Thumb entry, address-region classification and one bit-number
interpretation were corrected against actual addresses and instruction masks.
Only the verified facts are retained here. Decompiler signatures and inferred
register names are not accepted as an equivalent source implementation.

## Verified Linux loader defects

`aicwf_patch_table_load` in the R43 source had:

1. A null dereference after setting `dst=NULL` on a zero-length input.
2. Missing validation of the 128-byte metadata and 8-byte pair alignment,
   permitting reads beyond truncated buffers.
3. An unnecessary unchecked allocation for a non-NUL-terminated description.
4. A loop that could replace a failed write's error with a later success;
   its diagnostic also indexed past the loop after a final write failure.

The optional [hardening overlay](../native/radio/hardening/patch-table-load.inc)
uses a signed load result, verifies length before any upload, uses bounded
logging without allocating a description, stops on the first transfer error
and releases the loaded buffer on every exit. This changes only the host
loader, **not the executable radio firmware**. It does not claim to fix every
error-propagation path in the outer vendor initialization routines, some of
which log errors through void-returning interfaces.

Evidence:

- Five malformed-size scenarios fail against the actual baseline function.
- 514 synthetic file lengths and load/header/pair failures pass with
  address/undefined-behaviour sanitizers; the test also executes under MIPS
  user-mode emulation.
- The complete 121-file candidate reconstructs from the pinned upstream
  archive. Only `aicwf_compat_8800dc.c` changes; 120 files stay identical.
- Both actual R43 table files produce identical ordered transport traces
  through baseline and candidate loaders.
- Both Linux radio modules compile with the matching kernel configuration.
  The changed `aic8800_fdrv.ko` hash is
  `4b0a8516255f712636b1f251ba3d8e6b0f66195a9450d0779e4e70fbbccea7f8`;
  `aic_load_fw.ko` remains byte-identical to R43.

Receipts: [offline checks](../native/radio/hardening/offline-verification.json)
and [protocol equivalence](../native/radio/hardening/protocol-equivalence.json).
These are not hardware qualification or an instruction to replace a working
module. The installed camera remains R43.

## Reproduce the candidate without enabling it

Use a fresh output directory and the same archive/kernel/toolchain described
in [the R43 radio instructions](../native/radio/README.md):

```sh
python3 tools/prepare_aic_source.py --archive /path/to/archive.tar.gz \
  --output build/aic-audit-source --audit-fixes
python3 tools/build_native_drivers.py --component radio --radio-audit-fixes \
  --radio-source build/aic-audit-source \
  --kernel-tree /path/to/linux-7.3.1 \
  --kernel-build /path/to/matching/kernel-build \
  --output build/aic-audit-modules
python3 -m unittest discover -s tests -p 'test_radio*.py' -v
```

Without the explicit audit flags, both tools retain their original pinned
R43 behavior. To inventory your own extracted local firmware without loading
it, run `python3 tools/inspect_aic_firmware.py /path/to/lib/firmware`.

## Remaining replacement work

A related [public AIC8800M SDK](https://github.com/vsfteam/AIC8800M_SDK_vsf/tree/cec536008ab6a99f205777a6a452439ced529ecb)
was checked at revision `cec536008ab6a99f205777a6a452439ced529ecb`.
Its tree contains firmware-facing headers and `wifi/lib/armgcc_4_8/libwifi.a`;
the existence of this SDK is not corresponding source for these exact
AIC8800DC U02/H payloads. It is a potential ABI reference, not an accepted
licensed or hardware-equivalent replacement in this release.

The six uploaded files are not yet replaced by open-source firmware. A real
replacement needs the normal/H boot and IPC contracts, callable resident
services, RF calibration results/ownership, and independently verified
equivalence. Replacing the loader or storing the existing ARM payload as C
arrays does not close this gap. Do not disable DPD, change RF power, or skip
calibration based only on a decompiler guess. The host-loader correction is
useful independently of that longer firmware implementation effort.

## Subsequent R44 deployment

The source-only audit above is historical. The audited Linux driver was later
qualified and installed in R44; see [R44 validation](../native/R44-validation.json).
The six internal-radio firmware files remain unchanged. Their replacement is
outside the driver-only update scope.
