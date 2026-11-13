# Credits and licensing

The QC5 WebUI keeps the original credits for Alexandre, Jalecom and
[ZS-GX1 Hacks](https://github.com/ant-thomas/zsgx1hacks) by ant-thomas.
[The BW4 camera project](https://github.com/neonil123/BW4-ONVIF-RTSP-CAMERAS)
is a reference for earlier work on related hardware.

`src/pppp_table.h` adapts the protocol lookup table from
[devbis/aiopppp 0.2.3](https://pypi.org/project/aiopppp/0.2.3/) into a C
array. That release uses the Apache License 2.0, included in
[`licenses/aiopppp-LICENSE.txt`](licenses/aiopppp-LICENSE.txt).

The upstream notice applies to the attributed component. This snapshot
does not declare a repository-wide software license.

## Native R43 source export

Per-file SPDX declarations are retained: the native export contains
GPL-2.0-only, GPL-2.0-or-later, GPL-3.0-only and LGPL-2.1-or-later
components. GPL version 2/version 3 and LGPL 2.1 texts are in `licenses/`. Their
presence does not relicense unmarked files or override upstream notices.
The Apache PPPP attribution above also applies to copies of that table in
the frozen native frontend snapshot.

The WebUI is part of the project's open-source work and retains its credits.
The repository does not claim a single new license for all historical UI
and third-party contributions. Public source visibility alone does not
settle a missing license grant for unrelated imported code.

Original bootloader images, vendor flash dumps, Wi-Fi secrets, SSH identities
and per-camera update/recovery capsules are excluded from public assets.
The common R44 SD binary kit includes unchanged AIC8800 radio firmware and
compiled media inputs. It is not represented as a wholly open-source firmware
or as a new license grant for unmarked third-party code. The source tree keeps
unmarked external reference headers omitted pending permission/provenance work.

The upstream OpenIMP notice is explicit that licensing is per file and that
unmarked files have no explicit grant. The native source manifest lists the
remaining media references and generated inputs. Their outstanding status is
not removed by publishing a binary kit. The release provides per-component
notices and existing license texts; these do not establish missing permissions
or imply complete corresponding-source availability for the whole image.

See [`docs/open-source-status.md`](docs/open-source-status.md) for the
component inventory and the distinction between driver source and the
firmware executed inside the radio.
