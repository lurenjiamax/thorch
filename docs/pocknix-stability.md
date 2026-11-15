# THOR-1 stability candidate

This candidate adds only Pocknix common 1096 (UCSI), 1076–1078 (GMU),
1095 (IPCC), LAVD 0001–0005 and performance watchdog defaults.
Reference distribution: `shuuri-labs/pocknix-os` at
`49870a955099f0e09f5d5342af94748542100f25`.

## Kernel

Kernel 7.1.2-thorch47 retains GCC16.2 and no LTO. UCSI clears the USB role
for charger-only partners. GMU preserves polled HFI/OOB bits, bounds the
fault-coredump wait to two seconds and masks HFI before startup. IPCC keeps
`IRQF_NO_SUSPEND` for s2idle and uses an unlazy summary IRQ mask in late
suspend / early resume only for SM8550/SM8750 suspend-to-RAM. Thor's existing
SM8550 compatible already identifies this policy; the generic fallback match
table does not need replacement.

## Watchdogs

`thorch-bsp` ships `usr/lib/sysctl.d/60-thorch-performance.conf`: watchdog,
soft watchdog and NMI watchdog default off. This is a boot default, not a
periodic service or a LAVD-mode hook: changing scheduler modes must not
silently disarm explicitly requested diagnostics. The PM diagnostic service
runs after systemd-sysctl and may explicitly enable the detectors. Existing
boot logging `nowatchdog` remains a default rather than a permanent lockout.
Administrators' later sysctl overrides retain their normal precedence.

The dump-debug service separately opts into panic capture. Installation of
the performance default must not stop an already active diagnostic capture.
Never use a deliberate panic or reboot as validation without permission.

## Scheduler

`sources/scx` is the maintained v1.1.3 source plus 0001–0005 applied once in
Git, not package-build-time patches. `scx-scheds` 1.1.3-3 is built before the
BSP in the package manifest and required by the BSP. The package retains the
upstream workspace binaries and Thor's existing launch arguments. Its native
ARM64 build uses a checksum-pinned mbx wrapper with automatic GC disabled.
The installed Arch Linux ARM 1.1.3-2 metadata was inspected, but cannot by
itself prove its complete patch set; rebuilding from the known upstream tag
avoids both binary patching and duplicate application.
The native build's bundled libbpf pre-includes system Linux types to support
the `__u128` required by current ARM64 signal/ptrace headers, without editing
system headers or Cargo registry sources.

BSP packaging also routes its existing standalone Rust input daemon through
a dependency-free Cargo manifest and the same pinned mbx wrapper. Edition
2021 and optimization level 2 are unchanged; its Rust source is untouched.
Build output stays in makepkg's source directory, outside maintained source.

## Verification status

Source applicability and whitespace checks passed. GCC16.2/no-LTO kernel47
and native BSP 1-50 builds succeeded. Kernel Image SHA256:
`e5acafec6e0883dd3116d7eb73e344bee4aeb9ac499ece14cea0d1bb68b3230a`.
BSP package SHA256:
`695b0195b2e184dd1399e1b92aa8ac2931cf2834a94853465131bbfcf4a32d35`.
The BSP archive contains all three defaults, diagnostic ordering and the
scx-scheds minimum version. LAVD and matching native headers are still being
prepared; no new kernel has been installed or booted. Hardware acceptance,
including lid/power-key, USB charging changes, GPU, SSC, audio, Wi-Fi and
userdata I/O, remains outstanding. Previous thorch46 short sleep results do
not establish acceptance of thorch47 or resolve the long-sleep RCU hang.

No runtime sysctl override or automatic detector restoration is added. Pacman's
normal sysctl hook applies the performance defaults on package installation;
diagnostic detection remains an explicit operator choice afterwards.
