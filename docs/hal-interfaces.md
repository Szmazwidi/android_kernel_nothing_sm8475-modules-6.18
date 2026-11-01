# HAL interfaces in the Android 17 / CLS38 build

This records the **assembled 2026-11-01 ROM**, not a live inventory of every
phone running this kernel. Kernel drivers do not themselves choose HIDL or AIDL.
The product package selection, service implementation and VINTF declaration do.

| Function | Selected interface in this build |
| --- | --- |
| Fingerprint | AIDL `IFingerprint/default` (`fingerprint/`) |
| Goodix vendor extension | HIDL 2.1, separate from the standard fingerprint interface |
| Speaker/microphone audio and effects | HIDL 7.0 |
| Bluetooth controller | HIDL 1.0 |
| Bluetooth audio provider | AIDL 6 |
| Camera provider | HIDL 2.7 |
| Sensors | HIDL 2.1, Nothing Multi-HAL |
| NFC | AIDL |
| Battery/health | AIDL 5 |
| Power | AIDL 7 |

The source also retains `hidl/fingerprint/`, implementing HIDL 2.3. It is **not
selected by `device.mk` on `pong-6.18`**. The selected package is
`android.hardware.biometrics.fingerprint-service.nothing`, with
`libudfps_extension.nothing`. Its source links the fingerprint V4 NDK library;
its source VINTF fragment declares version 1. A library dependency and a VINTF
version are different evidence and should not be silently equated.

Older device README text describes a successful HIDL enrollment experiment and
an AIDL enrollment failure. That is historical evidence, not a description of
the current package selection or proof of current AIDL enrollment success.
Keep both source paths and their history; do not install two competing standard
fingerprint services or change the Goodix TA/persist calibration to resolve a
documentation discrepancy.

Reproduce the build audit from any source checkout:

```sh
python3 devices/pong618/tools/audit-hal-interfaces.py \
  --device-tree /path/to/android/device/nothing/Pong \
  --product /path/to/android/out/target/product/Pong \
  --expect-fingerprint aidl --report /path/to/hal-build-report.json
```

The audit checks the selected service against assembled VINTF, init files and
the installed binary. The reference build contains 40 AIDL, 94 HIDL and one
native **declarations**; these counts are not a count of working features or a
security/performance score. Declaration presence does not prove a service is
running, fingerprint enrollment works, or a sensor produces events.

## Modernization policy

Prefer stable AIDL for supported new implementations. AOSP deprecates HIDL and
recommends AIDL for HALs where supported; the implementation must preserve the
hardware-specific behavior and VINTF compatibility. See the official
[AIDL HAL guide](https://source.android.com/docs/core/architecture/aidl/aidl-hals)
and [HIDL documentation](https://source.android.com/docs/core/architecture/hidl).
This is not a reason to relabel a proprietary HIDL implementation as AIDL.

The next source-backed candidate is the AOSP sensors AIDL Multi-HAL bridge. It
can retain legacy sub-HALs while exposing AIDL to the framework. Before replacing
the Nothing service, preserve its pickup-gesture normalization and event filter,
verify both `sensors.ssc.so` and `sensors.nothing.so`, and qualify event delivery,
batching/flush, direct channels, wake locks and suspend/resume. Building the
upstream bridge alone is not a completed device migration.

Audio and camera migration need a compatible hardware-backed implementation,
including routing/effects/offload and camera stream/metadata semantics. The AOSP
example service is not a substitute for the working QTI hardware stack. Keep
the tested HIDL path until the replacement passes device tests with Enforcing.
