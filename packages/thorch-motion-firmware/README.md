# Thor sensor firmware seeds

This package contains proprietary SensorsPD runtime files and generic vendor
sensor configuration under `/usr/share/thorch-motion/vendor-runtime`.
The companion `thorch-firmware-rocknix>=1-15` owns the ADSP firmware loaded by
the kernel. There are no overlapping package-owned paths.

These read-only files are provisioning inputs, not an initialized runtime.
Installation does not enable or start any service. Device calibration and
required per-device runtime metadata must be provisioned separately. The current
importer requires calibration; operation with neutral/default calibration has
not been validated and is not claimed by this release.

No factory calibration, serial numbers, keys, inherited registry caches or dumps
are included. Read `/usr/share/licenses/thorch-motion-firmware/NOTICE.txt` for
copyright and provenance; attribution does not grant redistribution permission.
