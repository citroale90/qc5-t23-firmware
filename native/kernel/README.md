# R43 kernel source baseline

The actual lab source tree used for the R43 kernel was compared file by
file against `linux-7.3.1.tar.xz`: **no source differences, new files,
omitted binary changes or changed symlinks were found**. The build
directory's source link points to this tree, and its `vmlinux` hash
matches the kernel pinned by the deployed R43 image build.

- [Official Linux 7.3.1 archive](https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.3.1.tar.xz)
- Archive SHA-256: `fa595aad93734f829c963c2a90ef7c3673224fd7fdae7b49406e397cde2de231`
- This agrees with [kernel.org's published checksums](https://cdn.kernel.org/pub/linux/kernel/v7.x/sha256sums.asc).
- [`qc5-7.3.1.config`](qc5-7.3.1.config) is the matching configuration.
- [`R43-source-verification.json`](R43-source-verification.json) records the comparison and pinned build hash.

This does not mean stock upstream Linux alone boots the complete camera.
The adaptation includes out-of-tree drivers, a board device tree, a
freestanding boot/decompression wrapper, inherited clock/PHY setup and the
media CPU guard. Those layers also matter for the complete flash image.
See `native/boot/` and `native/drivers/`; the overall native firmware
assembler is still not a portable, complete public build.

The older `reference-patches/` describe earlier bring-up and emulation.
**Do not apply them blindly to this verified upstream baseline** or treat
them as the patch stack that produced the validated R43 kernel.
