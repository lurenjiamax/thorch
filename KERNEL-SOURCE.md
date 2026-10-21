# Kernel source layout

The sole maintained kernel source is `kernel-src/`, an independent Git repository.
It was moved (not copied) from `build/kernel-src/` on 2026-10-20. The recovered
baseline predates the deleted kernel25 experiment and must not be described as
an exact recovery of that source. Subsequent reconstruction is recorded in Git.

## Maintained trees and outputs

| Path | Role |
| --- | --- |
| `kernel-src/` | Independent Git repository: maintained kernel C, headers, DTS and config |
| `tme-src/` | Independent Git repository: maintained TME modules and overlay DTS |
| `build/kernel-build/` | Generated kernel compiler output |
| `build/tme-modules/` | Generated external module output (Kbuild MO) |
| `artifacts/` | Packaged products and deployment evidence |

The parent repository ignores the two independent source checkouts. A parent
checkout alone does not provide their contents. Public repositories are
`https://github.com/lurenjiamax/linux-thorch` and
`https://github.com/lurenjiamax/thorch-tme`. The custom-kernel workflow checks
out the full commits pinned in `source-checkouts.conf`. For a local build,
clone each repository into the paths above and check out those pins explicitly.
The builders fail if these checkouts are missing; they never regenerate them.

## Build without modifying source

Commit edits directly in the appropriate source repository first. Keep provenance
for upstream inputs and adaptation commits. Source preparation (including any
reviewed patch application) is separate from compiling a release.

```sh
bash scripts/build-thorch-kernel.sh --prepare-only --reuse-build-dir
bash scripts/build-thorch-kernel.sh --reuse-build-dir --template /path/to/KERNEL
CROSS_COMPILE=aarch64-linux-gnu- python3 scripts/build-tme-modules.py
```

The kernel builder uses the committed recovered config, resolves it in the build
directory and sets the release from the fixed PKGBUILD. It does not fetch/reset
the checkout, apply patch directories or copy DTS overlays into source. Legacy
source-acquisition options are rejected; `--no-fetch` and
`--skip-kernel-patches` are harmless compatibility options.

The TME builder compiles existing source using `M=tme-src MO=build/tme-modules`.
It has no download, text substitution or source-generation stage. Original input
hashes in `tme-src/PROVENANCE` describe vendor inputs; output provenance appends
actual source commits and dirty status. Its compiled overlay comes from the
maintained `tme-src/tme-overlay.dts`. Identical historical source copies under
packaging were removed after comparison; their history remains in parent Git.

`scripts/package-recovered-kernel.sh` remains a kernel28-specific artifact
packager retaining the known 13 DTBs. Its preserved DTB inputs are distinct from
the general source-DTS build above; do not assume those two paths produce
identical boot images. No device installation is performed by either builder.

Full suspend/reset and plugged-in charging stability remain unresolved. A rebuilt
candidate is intended for hardware acceptance, not a claim those faults are fixed.
