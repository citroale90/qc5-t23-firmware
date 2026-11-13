# Licenses for the common SD installer

The new standalone `env-sector.c` helper written for this project is licensed
under **GPL-2.0-only**, matching its use as a temporary Linux 3.10 kernel module.
This grant covers its source as published here; its binary is a build of that
source, not an imported manufacturer's module.

The project-authored shell templates `run.sh.in` and `qc5-run.in`, PC/build
Python tools, generated policy/binding headers, and accompanying operator
instructions in this directory are licensed under **GPL-3.0-only**.

Copied C core/support files retain their per-file SPDX declarations. This
scoped grant does not relicense other native media references, historical
WebUI contributions, upstream radio firmware or the original bootloader.

License texts: [GPL version 2](../../../licenses/GPL-2.0-only.txt) and
[GPL version 3](../../../licenses/GPL-3.0-only.txt).

The project-authored `factory-mac-check.h` copy additionally grants the choice
of GPL-2.0-only or GPL-3.0-only for this mixed kernel/userspace use. The
original native Wi-Fi header retains its existing GPL-2.0-only declaration.
