# InputPlumber artifact package

PKGBUILD is a fixed local artifact recipe. Cross-build the sole InputPlumber
checkout, assemble its upstream rootfs plus tracked Thorch payload, and archive
that package-root. Include SOURCE_COMMIT and SOURCE_CHANGES.patch in the license
directory so the executable's build inputs are recorded.

Provide inputplumber-thorch-0.82.0.tar.gz and its SHA256SUMS alongside this
unchanged PKGBUILD. prepare() verifies the manifest before extracting it.
Local source entries use SKIP because their digest is supplied in the manifest;
this is integrity checking of a locally produced artifact, not a signed remote
release or an independently authenticated download. No service file or package
recipe is rewritten by the producer. A release pipeline should publish signed
packages and independently authenticated artifact digests.

SSC is optional. No Requires/After dependency on thorch-motion is added to
InputPlumber. Sensor discovery and reconnection run in a bounded SSC worker;
physical input and virtual target lifetime remain independent.
