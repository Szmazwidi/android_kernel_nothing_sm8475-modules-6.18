# Build evidence

`cls38-source-build.json` records a complete kernel/vendor source build:
178 vendor + 84 selected GKI modules, all checked for AArch64 identity, vermagic,
declared module dependencies and actual imported/exported ELF CRC closure.
Only built-in vmlinux exports and the selected module files satisfy imports.
Unselected or stale DDK symvers files cannot satisfy a missing provider.

The independent source workspace was bootstrapped without build outputs in
stage 1188. Later source revisions reuse Bazel's action cache; the current
revision check is not advertised as a second empty-cache build. No old prebuilt
module bank is an input to this source build. Random local signing keys and BTF
layout mean byte identity with the laboratory image is not claimed.

`negative-checks.json` records deliberate CRC corruption, malformed signature
trailers, conflicting payload filenames and competing fingerprint declarations.
`rom-patch-tests.json` covers clean-base application, idempotency and refusal to
overwrite unknown source contents. Raw host/device logs and private keys are
excluded from these records.

The generic upstream sensors AIDL intermediate build is preparatory evidence,
not a completed Nothing HAL migration. The Nothing-specific AIDL candidate
must retain device gestures and pass VINTF, events and power-management tests
before replacing the tested HIDL service. The source/device README states its
selection status.

These files contain host evidence. They do not assert that fresh locally keyed
artifacts were flashed, that the full reconstructed ROM was hardware-tested,
or that payment/enrollment/interoperability/overnight stability tests passed.
