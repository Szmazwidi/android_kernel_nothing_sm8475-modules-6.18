# Pong 6.18.60 community source candidate

CLG43 selects 209 vendor modules and 84 GKI modules. Its source includes the
display power recovery and PowerStats record fix, GCC/GPUCC clock handoff,
the USB reset correction and the retained UFS domain workaround. The OTG and
FSA4480 experiments are kept on separate development branches.

The community profile uses normal Linux module signature admission with
`CONFIG_MODULE_SIG_FORCE=y` and SHA-256. It builds with
`CONFIG_PONG_LAB_GUARDS` disabled. The LAB profile retains the fixed-UFS write
guard, mount restrictions and immutable module payload admission for the
development handset. Select the community reference explicitly when building
artifacts for distribution. The default wrappers select LAB.

## Build

Use the matching pinned kernel manifest and run from its Kleaf workspace.
Install AArch64 system QEMU and the normal Android kernel host dependencies.

```sh
PONG_REFERENCE=devices/pong618/docs/build-reference-6.18.60-community.json \
PONG_QEMU=/absolute/path/to/qemu-system-aarch64 \
PONG_OUT=out/pong-community \
bash devices/pong618/tools/build-release.sh
```

The build selects all 293 outputs, verifies imported and exported ABI against
the actual selected modules, freezes the unsigned payloads and signs the full
bank with the kernel certificate. A diskless VM checks acceptance of the
trusted modules and rejection of unsigned, unknown-key and damaged-signature
modules. Community additionally accepts an otherwise valid module signed by
the trusted key after its payload changes. LAB rejects that altered payload.

`kernel-final`, `signed` and `frozen` are the input directories for the
Pong-kernel importer. Pass this same community reference and the CLG43 startup
lists when importing. No old module, private handset signing key or build
directory from the maintainer's computer is required as input.

Keep the generated module signing private key and any ROM release keys outside
Git. The generated source-build report records host verification. ROM boot,
data-preserving update, hardware variants and physical peripherals have
separate qualification requirements.

## Distribution requirements

Package matching device sources, firmware prerequisites and recovery
instructions with the final pinned commits and SHA-256. Use the maintainer's
release certificates for ROM updates and system packages. Existing user data
from a test-key installation needs separate signing-migration qualification.
Kernel-source and module-source histories and donor attribution are retained.

## Recorded native ROM checks (2026-11-07)

The freshly built community cohort completed a nine-image native Android update
on one EEA 12 GB / 256 GB Pong with personal data preserved, normal checkpoint
and Enforcing. An additional RAM boot of the unmodified source boot image
completed Android without Magisk, then returned to the persistent rooted image.
Front/rear video with audio, microphone, cellular validation, physical display,
touch and fingerprint checks passed. This does not qualify all camera modes,
a complete signed OTA, key migration, fresh installation or deep sleep.

See the [immutable qualification record](https://github.com/Szmazwidi/android_device_nothing_Pong/blob/6bc47b0c532e286aaeaf7e5a8d36e0f723eb83d1/manifests/community-clg43/qualification.json)
for the precise scope and remaining gates. The build-time manifests remain
unchanged; runtime evidence is recorded separately.
