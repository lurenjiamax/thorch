# Custom Kernel Storage Performance

## Current conclusion: repeated G4/G5 validation

The five-round, same-file revalidation supersedes the earlier single-run
estimate as the main evidence for the Gear 5 read benefit. Sequential read
median increased from **1845.0 to 3429.6 MiB/s (+85.9%)**, with non-overlapping
five-run ranges. All thirty read jobs completed without errors. Random-read
results improved in median but include dynamic gear scaling and greater
variability. Write performance and the correct general upstream solution
remain unverified. See the controlled revalidation section below for conditions
and limitations; the older table is retained only as historical evidence.

## Scope and tested build

Tests were performed on an AYN Thor with Linux 7.1.2-thorch19 and Android 13
(vendor kernel 5.15.123). The Gear 5 candidate reused the thorch19 module ABI
for a manual experiment. This branch advances the package release to 20 so a
normal rebuild does not silently reuse that experimental release identity.
The packaged thorch20 build has not yet been boot-tested.

The underlying source is Linux stable v7.1.2 with ROCKNIX configuration,
patches and device trees, followed by Thorch patches 0231–0236. The UFS
correction is patch 0236; it is a downstream candidate, not an accepted
upstream fix.

## Historical single-run matched fio comparison

fio 3.42, 8 GiB file, libaio/O_DIRECT, one job, refill_buffers and end_fsync.
Sequential operations use 1 MiB/QD16 and cover the file once; random
operations use 4 KiB/QD1 or QD32, 15 seconds plus 3 seconds ramp time.
All bandwidth values below are **MiB/s**, not decimal MB/s.

| Test | Android | Linux before G5 | Linux after G5 |
| --- | ---: | ---: | ---: |
| Sequential read | 3668.6 | 1831.8 | 3200.0 |
| Sequential write | 2768.5 | 1259.5 | 1262.2 |
| Random read QD1 | 54.0 | 22.1 | 20.9 |
| Random write QD1 | 80.6 | 24.0 | 22.6 |
| Random read QD32 | 381.4 | 210.9 | 229.4 |
| Random write QD32 | 172.4 | 149.5 | 118.2 |

In this earlier single run, sequential reads improved **74.7%**, reaching
87.2% of the Android baseline. This estimate is superseded by the repeated
controlled read comparison below, which uses a different fixed CPU policy.
Sequential writes were essentially unchanged; random results were mixed.
Linux originally remained at G4/two lanes/300 MHz under load; the candidate
reaches G5/two lanes/300 MHz, matching Android's observed loaded state.

Linux tested unlocked media/0; Android tested encrypted /data/local/tmp,
avoiding /sdcard FUSE. Directories and keys differ. Each original configuration
has one run, with uncontrolled thermal, GC and device-cache state. These
results do not establish encryption overhead or sustained post-SLC write speed.
No raw-device write benchmark was used and durability was not weakened.

## Controlled G4/G5 read revalidation

Completed a separate 8 GiB same-file experiment with one reboot: five rounds
on G5, then five on G4. Each round measured sequential read (1 MiB/QD16),
random read (4 KiB/QD1) and random read (4 KiB/QD32), for 15 seconds plus
3 seconds ramp per workload. Thirty jobs reported error zero. Sequential
reads are time-based here, unlike the earlier one-pass comparison.

Both images have identical DTB, ramdisk, command line and second stage.
Conditions were one job, direct/libaio, CPU performance governors, UFS IRQ
CPU0, mq-deadline, 1024 KiB request limit, unchanged mount/durability options,
60 seconds settling and 10 seconds between jobs. UFS retained simple_ondemand:
forcing its performance governor in an earlier aborted trial raised clocks
while leaving the link at G1; that trial was excluded and preserved separately.

| Read workload | G4 median (min–max) | G5 median (min–max) | Median change |
| --- | ---: | ---: | ---: |
| Sequential | 1845.0 (1786.2–1847.8) | 3429.6 (3396.5–3480.3) | +85.9% |
| Random QD1 | 19.0 (18.4–20.5) | 21.0 (19.1–23.5) | +10.7% |
| Random QD32 | 202.3 (196.1–211.7) | 334.4 (246.5–449.8) | +65.3% |

Units are MiB/s; each median uses five within-session observations. All
post-ramp sequential samples confirmed the intended G4/G5, two lanes and
300 MHz. G4 random workloads sometimes downscaled, so random differences
include linked scaling behavior and are not pure fixed-gear comparisons.
G5 QD32 variability is substantial. Temperatures were recorded, not held
constant; session order was not counterbalanced and each kernel had only one
boot session. No independent-boot statistical significance claim is made.

The non-overlapping sequential ranges strongly support the practical read
benefit of the G5 candidate under these conditions. This experiment does not
validate writes or establish that the candidate is the correct upstream fix.
Settings were restored, scratch removed and the original dedicated file
verified. The device was intentionally left running the G4 image after the
single allowed switch; restoring G5 requires another reboot.

## CPU7 interrupt placement experiments

Linux's UFS interrupt initially allowed CPUs 0–2 and effectively used CPU0.
Android's recorded UFS interrupt counts predominantly used CPU7. CPU0 is a
little core (maximum approximately 2.02 GHz); CPU7 is a faster core (2.96 GHz).
Completion handling, wakeups and subsequent submission can dominate small
random I/O latency, especially at QD1. Placement changes do not make the
flash itself faster or bypass encryption.

An 8 GiB IRQ-only run produced sequential read/write 3340.9/610.9 MiB/s,
random QD1 read/write 33.7/63.3 and QD32 read/write 327.7/82.8. Sequential
write fluctuated across all experiments, motivating a same-file comparison.

### Repeated same-file reads

One prepared 1 GiB file; configurations tested forward and then in reverse.
Each read workload runs 8 seconds plus 2 seconds ramp. Entries show both
observations, not confidence intervals. These are internal Linux comparisons,
**not** the original 8 GiB Android benchmark.

| Configuration | Sequential read | Random read QD1 | Random read QD32 |
| --- | ---: | ---: | ---: |
| Default schedutil/IRQ | 2914 / 2773 | 20.6 / 22.7 | 215 / 461 |
| CPU performance only | 3615 / 3552 | 26.3 / 22.6 | 469 / 406 |
| CPU7 IRQ only | 3677 / 3691 | 40.2 / 38.6 | 511 / 508 |
| Performance + CPU7 IRQ | 3687 / 3694 | 25.7 / 33.4 | 234 / 510 |

IRQ-only benefits were the most repeatable. Combined settings did not improve
all workloads consistently. The approximately 3684 MiB/s IRQ-only read rate
is **3.60 GiB/s**, not 3.68 GiB/s.

### Repeated same-file writes

One 1 GiB file, default/IRQ7/IRQ7/default order. Sequential writes overwrite
the file; random writes run 8 seconds plus 2 seconds ramp. end_fsync retained.

| Configuration | Sequential write | Random write QD1 | Random write QD32 |
| --- | ---: | ---: | ---: |
| Default, two observations | 2560 / 1125 | 22.9 / 34.5 | 180 / 191 |
| CPU7 IRQ, two observations | 1177 / 2129 | 73.9 / 70.8 | 444 / 383 |

Random write benefits repeated; sequential variability prevents a firm
regression/gain claim. All tasks reported error zero. Original settings were
restored and scratch files removed. CPU7 affinity is not deployed persistently;
power, suspend/resume and original-parameter repeated validation remain needed.

BFQ and a 512 KiB request limit were also tested and reverted without a
consistent overall benefit. Defaults remain mq-deadline, schedutil and a
1024 KiB request limit. No fsync/barrier changes were made.

## Upstream status

At inspection on 2026-10-09, Linux master still maps UniPro 300 MHz to G4
and 403 MHz to G5. SM8550's relevant clock/OPP tables top out at 300 MHz.
That explains the observed cap, but does not prove the local compatible
special case is the right general solution for every SM8550 device.

A [related upstream discussion](https://lkml.iu.edu/2609.0/04937.html),
dated 2026-09-02, reviews an OPP opp-level based gear-selection proposal.
It explicitly notes that frequency and gear need not have a one-to-one
relationship. It is not evidence that our patch has been accepted or that
this exact device issue has already been fixed. Hardware documentation,
other-device testing and maintainer review are required before upstreaming.

Original JSON and sensitive device artifacts are retained outside this
repository. No keys, ciphertext backups or vendor binaries are published.
