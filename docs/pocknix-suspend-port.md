# Pocknix sleep backport candidate

Source: shuuri-labs/pocknix-os snapshot
f09134848187911ec3a0a5b15cf2083f6de677e8 (2026-11-10).
Candidate: GCC16.2/GNU, no LTO, 7.1.2-thorch46. Kernel40 remains the accepted
boot/sensor recovery payload. No Clang caches are reused: candidate output uses
the fresh build/kernel46-pocknix directory and the accepted GCC40 configuration.

## Existing fixes (do not apply twice)

- 1001 UFS host-reset IRQ balance; 1007 hibern8-exit error propagation.
- 1008 auto-hibern8/software clock-gating exclusion; 1010 M-PHY retention while
  parked in hibern8; 1015 PHY RX LineCfg control.
- 1011 GENI system-sleep forced runtime suspend/resume.
- 1110 ICE resume clock unwind is already present and additionally handles our
  fallible HWKM initialization. Replacing it with Pocknix's simpler implementation
  would discard local key-handling safeguards.

## Selected missing fixes

1. Pocknix 1016 / upstream f07317a8d57f382ec505597816271dd72ffa20c7,
   Nitin Rawat: manage only the UFS lane clocks from lane clock APIs. The old
   devm_clk_bulk_get_all path re-votes non-lane clocks already managed by the UFS
   core, preventing balanced clock shutdown. This is the first direct UFS delta.
2. Pocknix 1053, Armada: restrict s2idle state-mem application to opted-in
   regulators, enabling it for RPMh. Avoid globally imposing deep-suspend state
   constraints on unrelated regulator drivers.

## Not bundled into this candidate

1017/1018 optional-query/timestamp capability handling is not the first suspend
variable. LPASS/SoundWire 1101-1109 and PCIe domain collapse form a dependency
stack and are not safe to cherry-pick as isolated clock-collapse changes.

Compilation, boot, mounted/unmounted userdata sleep cycles and sensor/audio
recovery are separate stages. No claim that userdata suspend is fixed is made
until those actual scenarios pass. Never use binfmt_misc or deliberate panic
to validate this candidate.

## Hardware result, 2026-11-10

- GCC16.2 kernel and complete module tree built successfully. Runtime config
  differs from accepted GCC40 only in LOCALVERSION. Thor DTB SHA256 is unchanged.
- Native ARM64 header helpers were prepared with the device's installed GCC
  16.1.1; matching cross-build Module.symvers was used. A real dump module build
  from the packaged headers passed after removing the inherited LLVM override.
- Both TME and dump DKMS builds passed during pacman installation. thorch46
  booted, boot payload validation passed, SSC returned both streams and dump
  remained enabled without requesting a panic.
- One corrected unmounted RTC-wake cycle passed. Four mounted read/write
  userdata cycles passed, with accelerometer/gyroscope samples after each.
  Three consecutive mounted cycles also compared an aligned 4096-byte O_DIRECT
  read before/after sleep; contents matched without exporting names/data/hash.
- RTC wake IRQ 167 was observed in these accepted cycles. The initial diagnostic
  incorrectly treated `systemctl suspend` enqueue as completion and cancelled
  the alarm early; that run is excluded. The corrected probe waits for the
  suspend.target lifecycle. An unrelated later user sleep is not counted.
- The test mount was stopped gracefully afterward. The device remains on
  thorch46 with SSC ready and dump enabled. GCC40 recovery materials remain.

Evidence: artifacts/kernel46-pocknix/hardware-verification.tar (explicitly
excludes private userdata path/hash state and the private mount log).
This is short-cycle acceptance, not proof of the original failure's cause,
long-duration sleep power, all power-key/lid transitions or production readiness.
