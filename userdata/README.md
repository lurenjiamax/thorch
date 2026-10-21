# Thor userdata components

See [collect/import/mount instructions](../docs/android-userdata-mount.md).
Android vendor runtime and device keys are deliberately excluded.

Source layout migration (one maintained copy per file):

- Workspace `src/storage/` KeyMint client, bundle importer, native converter,
  compatibility sources and MINK bridge moved to `userdata/src/`.
- Workspace `research/android-userdata/keymint-api` and
  `binder-platform-headers` moved to `userdata/third_party/keymint-v2` and
  `binder-headers`. These are AOSP frozen KeyMint V2/secureclock AIDL and
  Binder NDK platform headers, retaining their Apache-2.0 notices. Their
  original checkout revision was not recorded; this repository pins the
  imported file contents. Upstream projects are
  https://android.googlesource.com/platform/hardware/interfaces/ and
  https://android.googlesource.com/platform/frameworks/native/.
- The MINK, QCBOR and QCOMTEE source directories moved from workspace research
  into pinned submodules here. See `.gitmodules` and the gitlink revisions.

Old local workspace paths are compatibility symlinks. Generated AIDL sources,
object files and executables belong under `build/`, never in this source tree.
The native compatibility layer uses collected Android version properties;
it does not invent patch levels or persist ephemeral conversions.

The new collector/importer is build-validated only until a real Android export,
Linux mount and Android readback have completed with this exact bundle.
