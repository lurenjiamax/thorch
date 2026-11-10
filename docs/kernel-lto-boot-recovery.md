# Thor non-LTO boot recovery

Checked 2026-11-10 after a reported black-screen boot failure with
`7.1.2-thorch43`. Host-side package and media readback validation had passed;
those checks did not establish successful boot on the Thor.

## Recovery change

Return the maintained defconfig from `CONFIG_LTO_CLANG_THIN=y` to
`CONFIG_LTO_NONE=y`. Retain the remaining source changes and use a distinct
kernel/module release, `7.1.2-thorch44`, with matching native ARM64 headers and
rebuilt DKMS modules. The black-screen report motivates this controlled
comparison; it does not yet prove that LTO caused the failure.

## Upstream evidence

- ROCKNIX's current SM8550 configuration explicitly selects
  `CONFIG_LTO_NONE=y`. The latest change to that file when checked was
  `ee5afa287219b25450df4748b407c96c059a1d66` (2026-10-28):
  [pinned configuration](https://github.com/ROCKNIX/distribution/blob/ee5afa287219b25450df4748b407c96c059a1d66/projects/ROCKNIX/devices/SM8550/linux/linux.aarch64.conf).
  Thus disabling LTO matches the upstream device configuration; it is not an
  upstream announcement that all Thor ThinLTO builds fail.
- ARM64 advertises Clang full/ThinLTO support in
  [Linux Kconfig](https://github.com/torvalds/linux/blob/master/arch/arm64/Kconfig).
  Architecture support alone does not establish acceptance of this BSP,
  compiler version, firmware, and boot path together.
- [ClangBuiltLinux issue 1782](https://github.com/ClangBuiltLinux/linux/issues/1782)
  documents an ARM64 ThinLTO-only early boot exception on Ampere Altra. The
  diagnosed cause was an unaligned EFI TPM-table access using `READ_ONCE`,
  compiled to an alignment-requiring `LDAR` with LTO. The fix reached mainline
  in 2023 as `d3f450533bbcb6dd4d7d59cadc9b61b7321e4ac1`.
  This historical, fixed EFI/TPM issue is not evidence of the same cause on
  Thor's OpenABL path or the current kernel.
- Searches of ROCKNIX issues/PRs for `ThinLTO`, and for Thor black-screen
  reports, found no directly matching Thor/SM8550 kernel-LTO startup advisory.
  This is a bounded search result, not proof that no report exists elsewhere.

## Acceptance

Validate the embedded kernel configuration, package/kernel/module release,
DKMS module vermagic, boot files and post-remount checksums. Hardware acceptance
still requires booting the Thor, checking `uname -r`, display and input. Do not
mark a pending transaction boot-confirmed based only on offline checks.

## Follow-up: thorch44 also failed to boot

The user reported the same black screen with thorch44. Disabling LTO alone did
not recover boot, so LTO is not an established diagnosis.

Commit review found that distribution commit
`216743a35780091dac1ede53719fdf7acccc4990` changed the build invocation from GCC
to `LLVM=1` at the same time as enabling ThinLTO. The thorch44 recovery reverted
the LTO option but retained the Clang/LLD toolchain. Comparing only thorch43 and
thorch44 missed this wider difference from the known-working thorch40 baseline.

Evidence extracted from the retained Image files before the user-requested
artifact cleanup:

- thorch40: `aarch64-linux-gnu-gcc (GCC) 16.1.0`, GNU assembler/linker 2.47.
- thorch44: Clang/LLD 23.1.4, LLVM integrated assembler; no LTO.
- thorch44 additionally selected `CONFIG_ARM64_BTI_KERNEL=y` and compat VDSO
  options through toolchain-dependent Kconfig evaluation. The nftables feature
  changes also remain. These differences are investigation candidates, not
  proven causes.
- The retained working thorch40 DTB, pre-thorch43 DTB, and freshly built thorch44
  DTB were byte-identical: SHA-256
  `1be2559abda8ca409e3d87682cb18c77b46d7559bb4df6122c0268f7f7857c85`.
- Host-side file readback, module vermagic, and OpenABL format validation passed
  for thorch44, but those checks did not prove the binary booted on hardware.

Review also found pre-existing, uncommitted BSP/userland changes, including
`thorch-rebuild-abl-kernel`. They were not packaged by the kernel-only updates;
do not silently commit, discard, or treat them as installed. Preserve their
index/worktree state while auditing the boot path.

Next controlled build, if requested, should first restore the complete
known-working compiler/configuration baseline rather than changing another
isolated option. Preserve the distinction between source, build output,
packaged payload, installed bytes, and actual boot acceptance.
