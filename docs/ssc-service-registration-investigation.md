# SSC registration investigation, 2026-11-10

Scope: running GCC16.2 thorch40; no kernel, ABL or firmware replacement.

History evidence from `research/motion-20261012/SSC-COLD-BOOT-FINDINGS.md`:
fresh calibration-only registry seeding changed service discovery on the same
kernel; reference ADSP/DTB 1-13.4, patched HexagonRPC, SensorsPD then RootPD,
and firmware-owned registry creation subsequently produced both sensor streams.
Inherited/generated registry and process/PDR liveness alone did not prove SSC.
The successful path did not require an ABL SSC-specific change.

Current observations:

- ADSP running; sensor_pd PDR up; both FastRPC daemon processes alive.
- QRTR enumeration works for other services; service 400/0x190 absent.
- 64 ADSP/descriptor files extracted from the current initramfs match rootfs.
- Device calibration and cached registry files are present.
- A bounded 22-second syscall observation, after stopping the service, shows
  INIT_ATTACH_SNS and INIT_ATTACH both returning zero and several successful
  INVOKEs. There are no runtime registry/config opens in this particular run;
  both daemon threads end blocked in an INVOKE until timeout stops observation.
  ERESTARTSYS at timeout is observer cancellation, not an earlier driver error.
- The regular motion service was restarted after this observation.

This localizes current observation to DSP initialization/callback progress
before SSC announcement, not libssc sampling or ABL service discovery. It does
not establish why the firmware has not progressed. In particular, a later
reattach need not replay initial callbacks, so the absence of callbacks in this
observation is not proof that the first attach of this boot had none.

On-device evidence: `/var/cache/thorch-build/ssc-current-inspection/`.
Avoid hot-restarting shared ADSP while audio is active. Capture first-attach
callback progress in a fresh boot before judging registry bootstrap or changing
configuration.

## Authorized fresh-boot verification

After explicit reboot authorization, a fresh boot with only temporary syscall
tracing added returned both accelerometer and gyroscope samples. The same kernel,
ABL, ADSP firmware and existing calibration were retained. Temporary tracing was
then removed, the regular service restarted successfully, and a second fresh
boot without tracing again reached `SSC ready: both sensor streams returned
samples` with zero restarts. The second boot's systemd startup time was 326 ms;
DSP readiness proceeded asynchronously. Missing oemconfig/testsig messages also
occurred in these successful boots and are not sufficient causes by themselves.

This rules out a requirement to replace the current kernel/ABL to obtain SSC.
It supports a state/initialization-lifetime problem but does not isolate suspend
as the cause. Compare ready-before-sleep and first-start-after-sleep separately;
neither suspend case has yet been accepted.
