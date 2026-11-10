# Pocknix kernel and suspend review

Checked 2026-11-10. The user's name "piconix" was interpreted in handheld
context as [Pocknix OS](https://github.com/shuuri-labs/pocknix-os), not the
unrelated Raspberry Pi Pico PicoNIX project. This was a read-only upstream
review; no Pocknix kernel, patch or package was installed.

## Source snapshot and kernel

Reviewed main commit `f09134848187911ec3a0a5b15cf2083f6de677e8`
(2026-11-10). For SM8550:

- `kernel/sm8550/kernel.conf` pins upstream kernel.org Linux **7.2.10**, package
  release **14**. This is the source recipe, not an inspection of a published
  package binary.
- `kernel/sm8550/PATCHES.md` records ROCKNIX next `6103571f67` as the synced
  baseline, plus device and common patches. Sleep stack 3 was rebased on Nov 6.
- `scripts/build-kernel.sh` defaults to native GCC or cross
  `aarch64-linux-gnu-gcc`, with GNU tools; it does not select `LLVM=1`.
- The SM8550 config selects `CONFIG_LTO_NONE=y`. Its compiler metadata records
  GCC 16.2.0 and BFD, but the actual compiler used by a release builder depends
  on that builder. Architecture capability symbols advertising Clang LTO support
  are not activation of LTO.
- The SM8550 `qcom-abl` deployment produces an Android boot image `/KERNEL`
  with appended device DTBs. Its build explicitly requires built-in Btrfs
  because this path has no initramfs. This differs from the current Thorch
  native OpenABL raw-Image + separate DTB/initramfs entry.

The latest GitHub image release is v0.4.0 (2026-09-12); it predates the current
main recipe and some later hardware reports. Package updates are continuous,
so image-release notes alone do not describe current installed systems.

## Suspend: evidence levels

Pocknix SM8550 selects real **s2idle**, not a claim that suspend-to-RAM/deep or
disk hibernation is hardware-validated. `CONFIG_HIBERNATION=y` in a config does
not establish a usable hibernation setup.

There is direct Thor hardware evidence in
[issue 68's Oct 21 report](https://github.com/shuuri-labs/pocknix-os/issues/68#issuecomment-6323234161):
Cress tested on Oct 17 with kernel 7.2.10-6. Both **Game mode: sleep + lid** and
**Plasma: sleep + lid** passed. Audio, controls, Wi-Fi/Bluetooth and dual-screen
desktop touch also passed. That report also identified charging, display and
brightness problems; it is not an all-features acceptance report.

The current stack contains substantial additional sleep work:

- GENI UART IRQ masking/forced PM, TSENS wake/threshold handling;
- regulator state-mem application during s2idle;
- gamepad MCU and LED supply release;
- ADSP AudioReach graph release, LPASS and SoundWire clock/runtime-PM work;
- PCIe D3cold and SM8550 PCIe GDSC collapse, intended to permit CX collapse and
  DDR self-refresh;
- UFS/ICE error-path clock cleanup, SMP2P state handling and haptic-work cleanup.

These patches have dependencies. In particular, the PCIe GDSC collapse work
depends on releasing LPASS clock votes; importing one patch is not equivalent
to importing the working stack and its DT supply policy.

Long-duration low-power acceptance is less complete:
[issue 65](https://github.com/shuuri-labs/pocknix-os/issues/65), original Odin 2
sleep drain, is still open. Its Oct 3 maintainer comment says a large sleep-power
fix shipped in 7.2.6-5 and requests repeat hardware measurements. This is not a
Thor failure report, but it prevents claiming all SM8550 sleep problems are
closed. No equivalent complete Thor overnight power measurement, nor acceptance
of Thorch's Android userdata/ICE/TME-mounted scenario, was established here.

## Features and limitations

The project ships an Arch Linux ARM system with Steam ARM gaming mode,
gamescope, Plasma Mobile, FEX/Proton integration, optional ES-DE/emulators,
performance control, and package updates. Kernel configuration enables
sched_ext/BTF for scx_lavd and cluster-aware scheduling; graphics/device work
includes Adreno support, SDR104 SD, gamepad/haptics, and Thor dual-screen policy.
Thor remains beta. Current known-issues documentation says gyro integration is
not working, and Thor issues 151/152 track display corruption and panel
brightness controls. Source configuration is not proof that every feature was
validated on every board.

## Additional build concern relevant to our investigation

[Issue 92](https://github.com/shuuri-labs/pocknix-os/issues/92) reports malformed
module BTF with default DWARF5, GCC 16.1.0 and pahole 1.32 on an Odin 3 tree;
the reporter found explicit DWARF4 fixed module loading. This is a reported
configuration-specific failure, not a demonstrated universal bug. The issue
remains open and the reviewed SM8550 build script still selects toolchain-default
DWARF, so do not say Pocknix universally ships the suggested DWARF4 fix.

Our failed thorch45 build also uses pahole 1.32 and toolchain-default DWARF.
As an artifact check, its `ath12k.ko` was parsed against that exact build's
`vmlinux` with `pahole -F btf --btf_base`; it returned zero and printed type
information, without reproducing the reported malformed-string error. This is
not a kernel module-load validation or an explanation of our boot failure.
Record: `artifacts/kernel45-clang-control/btf-ath12k-inspection.log`.

## Primary references

- [Pinned SM8550 recipe](https://github.com/shuuri-labs/pocknix-os/blob/f09134848187911ec3a0a5b15cf2083f6de677e8/kernel/sm8550/kernel.conf)
- [Pinned config](https://github.com/shuuri-labs/pocknix-os/blob/f09134848187911ec3a0a5b15cf2083f6de677e8/kernel/sm8550/config/linux.aarch64.conf)
- [Kernel builder](https://github.com/shuuri-labs/pocknix-os/blob/f09134848187911ec3a0a5b15cf2083f6de677e8/scripts/build-kernel.sh)
- [SM8550 patch ledger](https://github.com/shuuri-labs/pocknix-os/blob/f09134848187911ec3a0a5b15cf2083f6de677e8/kernel/sm8550/PATCHES.md)
- [Shared patch ledger](https://github.com/shuuri-labs/pocknix-os/blob/f09134848187911ec3a0a5b15cf2083f6de677e8/kernel/common/PATCHES.md)
- [Known issues](https://github.com/shuuri-labs/pocknix-os/blob/f09134848187911ec3a0a5b15cf2083f6de677e8/docs/known-issues.md)
