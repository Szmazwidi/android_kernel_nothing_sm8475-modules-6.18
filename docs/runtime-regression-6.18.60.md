# 6.18.60 runtime qualification: heap source selection

The 6.18.60 candidate built from kernel 89684ef737b91893c9c982200f9bd3402a0e4ec2
and modules 3a5a4ec78c1635e2d7b2afa686e5d2cbbd6db076 passed the full 262-module
build, CRC/dependency closure, CMS signatures and module-admission VM test.
Those checks did not prove the runtime behavior of the selected driver sources.

Its first native trial did not complete Android boot. Gralloc requested
`qcom,system`, while the selected heap974 exporter registered `pong-system`.
A RAM-only alias allowed allocation and exposed a second failure: metadata
mmap returned EINVAL. The module lacked pong104_mmap and lazy DMA imports.
No SELinux rules were relaxed. The alias disappeared at reboot.

The wrong heap974 candidate had already been rejected during historical
stage982/983. The accepted heap982 source adds permission-pinned vmap to the
actual working exporter while preserving mmap, VMA ownership pins, the
qcom,system kernel name and CONFIG_QCOM_LAZY_MAPPING. It is copied unchanged
here; its SHA-256 is d40f2e640fb8b28c94e2caa87e058235b5d12c38577cd3679440ffafecb583c8.
Original source/license notices are retained; this is source selection and
build integration, not a claim of new upstream driver authorship.

The aggregate target, dependency provider and 6.18.60 reference now select
heap982/fixed:heap_fixed982. The verifier requires its mmap/VMA/vmap functions,
permission checks, lazy-map imports and exact NUL-terminated heap name.
The old diagnostic sources and historical evidence are preserved.

The phone was restored to all four exact CLS38 images, with native Android,
root and Enforcing verified. Partition hashes, data canary, normal checkpoint
and unchanged slot A/GPT layout were verified. No data reset was performed.
The experimental fingerprint bind overlay was also disabled after its
nosuid execution failure; the original HAL was restored.

Remaining qualification: rebuild the full bank and immutable payload policy,
repeat final ABI/signatures and admission VM tests, regenerate pinned helper
cohorts, then perform a bounded native test. A partial module swap is not
valid with the immutable module policy. The corrected recipe itself is not
hardware-qualified. This source-interface check does not prove all hardware
behavior or resolve every difference from CLS38 (camera flash and memory
hotplug require separate review).

## Additional preemptive recipe corrections

A comparison of all 178 vendor outputs found two more source-selection/config
regressions. The phone's working memory hot-add module contains the accepted
stage1149 AOP TX-status callback. The source recipe instead selected the older
CLS16 client, which does not propagate controller failure through tx_done.
The existing aop1149:mem1149 source is now selected beside aoss1149. Its source
is unchanged from the accepted historical adaptation; original notices remain.

Camera's working module imports qti_flash_led_prepare and qti_flash_led_set_param.
The camera DDK rule had the matching provider dependency but did not enable
CONFIG_LEDS_QTI_FLASH_MODULE, so those paths silently compiled out. That module
configuration is now explicit. The artifact verifier requires both imports.
These are source/config parity corrections, pending the full build and new
hardware test, rather than newly invented hardware implementations.

## CPU-access ownership parity with the working image

The corrected heap982 build booted Android with signatures forced, full RAM and
Enforcing, but camera/CDM command buffers still failed dma_buf_begin_cpu_access
with EPERM. The actual working CLS38 heap matches stage1063 byte-for-byte in its
entire .text and imported symbol CRCs. That later source pins VM permissions
before the begin/end CPU-access checks and releases the pin on both paths.
The release recipe now selects that actual heap1063 source, retaining mmap,
vmap, lazy DMA and ownership checks. Its historical source SHA-256 is
3803b7436bf9a9e23c9227e928f48233d068610553590a5e1f8c932bdf831494.
The existing 2304 callback ownership/failure test cases are retained as evidence.
Only the successful allocation message is changed to pr_info_once to avoid
per-buffer kernel-log flooding; permission and failure diagnostics remain.
The verifier now inspects AArch64 call relocations within both CPU callbacks,
requiring pin, permission check and unpin at the actual callback call sites.
Global imports alone could not detect the missing ownership pin.
