# Thor custom image

For AYN Thor only. Download the image and `SHA256SUMS` from the same Release.
Verify the checksum, decompress `.img.zst`, then write the `.img` to an SD card
with an image writer. Writing an image erases the selected card; check the target
carefully before proceeding. Boot Thor from that card.

The boot partition follows upstream OpenABL's native layout: raw ARM64 `KERNEL`,
separate `dtbs/`, and a `loader/entries/thorch.conf` entry referencing the Thorch
initramfs. No Android boot-image conversion is used.

The image includes the maintained kernel, matching headers, BSP, TME modules,
motion runtime and InputPlumber. Device-private Android materials are not included.
See [sensor setup](thor-sensors.md) and [vibration setup](thor-vibration.md).

Debug configuration is disabled by default. `thorch-dump-debug` is a separate,
optional package; installing it does not enable capture.

`SOURCES.json` records the resolved component main/master revisions. `PROVENANCE`
records the checksum-verified ROCKNIX boot template; `TME-PROVENANCE` records the
module build inputs. Use packages from the same Release when updating an existing
installation; follow the [kernel update guide](custom-kernel-install.md).
