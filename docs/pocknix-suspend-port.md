# Pocknix sleep backport candidate

## Current resume regression

The SM8550 BSP's `sleep.d/{pre,post}/010-cpuidle-state1` hooks temporarily
enable CPU0 state1 and restore its previous value. Thorch's awake-time GMU
workaround disabled that state without the sleep exception. CPU0 then cannot
enter the tick-freeze path, leaving monotonic time advancing across s2idle.
The BSP now provides the equivalent `system-sleep/thorch-cpuidle` hook.

The touchscreen retain-power change (1050), GMU fixes (1076-1078), and UCSI
role correction (1096) are already in kernel47. The separate LPASS/SoundWire
and PCIe-collapse stack remains outside this repair. Pocknix's kernel base is
7.2.10; this repair retains Thorch's maintained 7.1.2 source.

## Earlier comparisons

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

## Re-review, 2026-11-15

### Sources and scope

- Distribution source cloned to `../pocknix-os`, upstream
  `https://github.com/shuuri-labs/pocknix-os`, HEAD
  `49870a955099f0e09f5d5342af94748542100f25`.
- Its SM8550 stack targets Linux **7.2.10**, pkgrel 17, on the pinned ROCKNIX
  `next` patch snapshot `6103571f67`; this is not a ready-made patch set for
  our 7.1.2 kernel. The clone contains the distribution, patches and DTS, not
  a separately cloned upstream Linux repository.
- Comparison target is the maintained Thor candidate source
  `ca566c2b060159ff6179be5c53a747d3db27e55f`. This does not establish which
  kernel is in the currently unresponsive device.
- Read-only forward/reverse applicability checks covered **146** common and
  SM8550 patches: 37 reverse-applicable, 49 forward-applicable, 60 requiring
  context/semantic reconciliation. Results:
  `artifacts/pocknix-review/patch-applicability.json`.
  Forward applicability proves neither correctness nor hardware suitability;
  failed reverse application does not mean a fix is absent.
- Since the November 10 snapshot, `kernel/common` and `scx-scheds` have no
  changes. The notable new SM8550 work is display colour/LUT handling and
  panel brightness. The sleep and scheduler candidates below were already
  carried then; they are not newly discovered upstream fixes to this incident.

### Highest-priority stability candidates

| Area | Pocknix change | Thor source finding and recommendation |
|---|---|---|
| USB/charging | common **1096** | Missing; forward-applicable. Restores `USB_ROLE_NONE` for a charger-only partner. Patch describes a 7.1-era hard hang when unplugging during sleep. First isolated candidate if charging/cable transitions occurred; not proof of this failure. |
| GPU recovery | common **1076–1078** | All forward-applicable. HFI handler must not clear response/OOB bits that another path polls; devcoredump completion waits gain a 2 s bound; HFI is masked before GMU startup. Strong display/runtime-resume candidates, but RCU stall alone does not identify GMU as the cause. |
| MCU/UART | SM8550 **1004, 1009, 1065** | Thor rsinput has no system-sleep callbacks. 1004/1009 apply directly; 1065 needs reconciliation. GENI IRQ masking does not substitute for quiescing/reinitializing the continuously reporting MCU. Review as a dependency-ordered input-recovery series. |
| Thermal IRQ | common **1023** | Thor skips uplow wake enabling but lacks this lower-threshold prepare mask. Pocknix masks LOWER after thermal zones are quiesced, retaining UPPER/critical protection. Investigate warm-device entry abort/IRQ storm separately from long-sleep wake failure. |
| Mailbox | common **1095**, removal of old **0502** | Thor still lacks `IRQF_NO_SUSPEND` globally. Pocknix restores it for s2idle and hard-masks only s2ram in late/early callbacks. Our OF table also differs, so adapt compatible matching, not just IRQ flags. Validate charging, ADSP/SSC and spurious wakes together. |
| Power key / lid | common **1080** and Thor DTS edits | Power-key patch forward-applicable: suppresses wake press/release in a 1 s resume window. Pocknix lid DT wakes only on opening and pulls up the open-drain hall input; Thor lacks the explicit `wakeup-event-action`. Can fix immediate re-suspend, not an established fix for RCU starvation. |

### Already present: do not transplant again

- UFS 1001/1007/1008/1010 and GENI 1011 reverse-check successfully.
  Lane-only management equivalent to 1016 is already in our candidate, using
  explicit lane clock handles rather than the newer bulk API patch layout.
- GENI 1006's non-console IRQ masking is present, with local error unwinding.
  1015 RX LineCfg handling is already part of the prior accepted audit.
- 1045's PCIe L23 poll avoidance and 1048's suspend-OPP mechanism are present
  in locally adapted code. They do not reverse-check as identical patches.
- RPMh SLEEP/WAKE requests, active-to-SLEEP mirroring, resume realignment and
  the regulator s2idle opt-in are present. The stale sleep-vote/runtime-idle
  hazard described by 1046 is therefore not simply a missing patch here.
- SMP2P 1099 is already present: `if (!restart) return false` before clearing
  inbound state. Do not misdiagnose its context difference as a missing fix.
- Touch 1050, lowest GPU OPP 1051, battery-unit fix 1082, A740 IFPC, SM8550
  interconnect QoS, GCC CX parent-domain and QCE runtime-PM series all
  reverse-check successfully where included in the matrix.
- ICE 1110 must retain our HWKM initialization/error unwinding.

### Power-collapse stack: keep dependency boundaries

- **1101–1106 → 1107 → 1108/1109**: LPASS macros move to PM-clock management,
  then force runtime suspend across system sleep; SoundWire must release its
  votes, with a special clock-stop exception for the haptics port.
- **1057–1059**: release/reopen AudioReach graphs and propagate rejected graph
  opens; includes an interaction with ROCKNIX 0612. Not an isolated clock fix.
- **1054 + 1116 + DTS**: endpoint-only D3cold eligibility and PCIe controller/
  PHY GDSC OFF_ON permit CX collapse/DDR self-refresh once LPASS votes are gone.
  Thor still has the older eligibility logic and RET_ON GDSCs. Pocknix itself
  warns that a wake hang after removing the older suspend-OPP floor points at
  this transition. Do not blindly combine it with our local floor/vote policy.
- **1004/1009 → 1055/1056 + shared-rail DTS**: quiesce/init the MCU first,
  then drop the MCU and LED regulator votes. Thor currently retains the MCU
  supply; adding rail-off alone would break wake initialization.
- **1117**: cancel haptics set-gain work during suspend. Thor cancels it in
  removal but not in its sleep callback; adapt to our different haptics tree.
- **1100**: optional charging-in-s2idle fan operation is a thermal/UX feature,
  not a sleep-hang repair. Measure its energy and wake/IRQ impact separately.

### Performance and other driver work

- **scx-scheds 1.1.3, patches 0001–0005**: cached-CPU affinity validation
  (upstream scx #3750 runnable-task stall), non-SMT neighbor migration,
  autopilot binding, online-CPU-sized compete window and futex-hook logging.
  Pocknix runs LAVD with `--pinned-slice-us 500 --lb-local-dsq-util-pct 0`;
  Thor's maintained launcher passes only its selected mode. Thor has no local
  scx-scheds recipe in this checkout: installed package patch provenance must
  be checked before claiming these fixes absent. LAVD's observed watchdog
  exit does not prove scx #3750 or explain every RCU stall. Isolate EEVDF vs
  LAVD when hardware experiments are authorized.
- Effective Pocknix config is not just the synced `.config`: build-kernel.sh
  enables sched_ext/BTF/tracing, SCHED_CLUSTER, PM_DEBUG/PM_SLEEP_DEBUG, and
  changes the default CPU frequency governor to **schedutil**. The synced
  config's performance-governor setting alone is misleading.
- No explicit cpuidle/PSCI/arch-timer/RCU repair series was found in the
  carried common/SM8550 patches. Changes in the upstream 7.2.10 base require
  separate comparison; do not claim that upgrading fixes the photographed stall.
- DPU **20-version 0011–0014** improve commit completion ordering, compressed/
  scaled fetch QoS, inline rotation and fetch width; all forward-applicable.
  common **1066** fixes command-mode vblank timestamps. Review for graphics
  correctness/frame pacing independently of the sleep candidate.
- New **1115, 1121–1124** add panel brightness curves and DSPP LUTDMA colour
  management/topology retention. Needs matching DPU DT register resources and
  compositor support; not a general black-screen workaround. Pocknix warns
  about untested Thor + DP dock DSPP allocation/modeset behavior.
- Gamescope has elapsed-time frame limiting, identity-LUT bypass, direct
  scanout DPU colour support and synthetic EDID/panel profiles. Mesa is now
  26.3.0 with no carried patches. These affect scanout/performance/colour,
  not proof of a kernel sleep fix.
- Iris **1029–1038** cover decode-state/tier/control correctness, clock voting
  and low-latency default clocks; top-clock decoding can increase power.
  **1087–1089** are gen1/SM8250-specific, not SM8550 candidates. PCIe IOMMU
  **1060/1061** target a 7.2 five-cell map binding transition; forward-check
  success does not justify applying them to our 7.1 binding configuration.
- UFS **1017/1018** avoid retries for unsupported optional queries/timestamps;
  useful for resume noise/latency, not a demonstrated primary hang cause.
  **1083** exposes charge-now; input 1063/1064 improve report rate/calibration;
  1062/1067 address FEX unaligned atomics. Keep outside first sleep-stability A/B.

### Userspace policy that must not be copied wholesale

- `pocknix-sleep` runs ordered pre/post hooks. USB-role hooks temporarily clear
  roles and restore them after resume, including a bounded reassert against
  Type-C renegotiation. This is a workaround to understand alongside 1096,
  not a substitute for fixing role ownership in the driver.
- Lid hooks disable power-key wake while the lid is closed and restore it on
  resume. That is a UX policy: a user expecting power-key wake while closed
  would perceive it as failure. Preserve Thor's intended behavior explicitly.
- `60-pocknix-latency.conf` disables watchdog/nmi_watchdog/soft_watchdog and
  removes RT throttling. These settings conflict with this incident's dump
  diagnostics and weaken protection from runaway RT work; do not import them.
- Full-RAM zstd zram, swappiness 180, dirty-page limits and reclaim settings
  target gaming responsiveness. They are memory/latency tradeoffs, not idle-
  power measurements or a sleep repair.
- Pocknix PowerDevil carries a brightness-floor patch, not our bounded
  resume-DPMS intent fix. It does not replace the separately verified fix.

### Recommended next implementation boundary

Start with independently attributable driver stability changes: USB 1096,
GMU 1076–1078, MCU/UART initialization, and the IPCC/thermal/wake-path review.
Retain diagnostic watchdogs and capture the exact failing boot before changing
power-collapse policy. Evaluate the LPASS/AudioReach/PCIe/DTS stack as a separate
dependent series with charging, SSC, audio, Wi-Fi, userdata I/O and long-sleep
acceptance. Do not bundle shader/colour/decoder performance work into that A/B.

This re-review cloned and inspected source and ran read-only applicability
checks. No patches were applied, no kernel was built or deployed, no unit tests
were run, and no device sleep/panic/reboot was triggered.
