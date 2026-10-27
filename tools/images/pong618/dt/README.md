# Device tree of the development image

These are the device tree blobs the 6.18 kernel runs with on the development phone (image 734).
They are binaries: there is no DTS source tree for them in this repository.

| File | What |
|---|---|
| `vendor_boot.dtb` | The DTB section of `vendor_boot`: three concatenated DTBs, in this order: Cape LTE Only (`qcom,msm-id` 0x21c), Cape (0x212), CapeP (0x213). The bootloader picks the one matching the SoC. |
| `dtbo.img` | The `dtbo` image without its AVB footer: one overlay, `model = "Nothing Phone (2)"`, `qcom,board-id = <0x1000b 0x00>`. Its original AVB footer carried the fingerprint of an AOSPA `phone2` build (`CQ2A.260827.001`), i.e. the device tree of the 6.6 kernel for this phone. |
| `cape-lte.dts`, `cape.dts`, `capep.dts`, `dtbo-pong.dts` | The same blobs decompiled with `dtc -I dtb -O dts`, for reading and diffing only. |

## Where they come from

The DTBs started from the 6.6 device tree of the porting base and were adjusted in the lab for
the 6.18 drivers. Compared with that base, the three DTBs differ in exactly these properties:

| Node | Change |
|---|---|
| `/soc/clock-controller@ade0000` (camcc) | `clocks`/`clock-names` become `iface`, `bi_tcxo`, `bi_tcxo_ao`, `sleep_clk` |
| `/soc/qcom,gdsc@adf31dc` | adds `qcom,support-cfg-gdscr`; `reg` size 0x4 → 0xc |
| `/soc/pcie@1c00000` | adds the `conf` region `0x60100000 0x100000` |
| `/soc/interrupt-controller@17100000` | explicit `#address-cells = <1>`, `#size-cells = <1>` |
| `/soc/qcom,cam_smmu` | explicit `#address-cells = <1>`, `#size-cells = <1>` |
| `/soc/spf_core_platform` | explicit `#address-cells = <1>`, `#size-cells = <1>` |

They were made in the lab while bringing up, on 6.18, the camera clock controller and its GDSC,
the PCIe controller of the Wi-Fi chip, and the child nodes of the GIC ITS, the camera SMMU and
the SPF core platform.

The earlier history of these blobs is not reconstructed here.

SHA-256:

```
9d964c67d4aa8231da05973107fdfbb959a390ba21d0159f95cc646281d22189  vendor_boot.dtb
9c8843e6888aee45d45b10e8795133ed1293a1d64215db946bb0ffb2e34b4ed7  dtbo.img
```
