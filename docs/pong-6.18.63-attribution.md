# Pong 6.18.63 source attribution

Szmazwidi maintains and co-develops the Pong port, provides its integration
baseline, and runs and evaluates hardware trials. OpenAI Codex implemented and
documented the local adaptations listed here with Szmazwidi. Claude's existing
contributions are recorded in the relevant history. The original authors and
trailers of imported code remain in the corresponding commits.

## Kernel

| Pong change | Source and scope |
| --- | --- |
| [IdPool adaptation](https://github.com/Szmazwidi/android_kernel_nothing_sm8475-6.18/commit/a4d22d3ed7d2964fbcf37486252d0760848d7fc6) | Restores Alice Ryhl's [ACK IdPool and UnusedId API](https://github.com/Szmazwidi/android_kernel_nothing_sm8475-6.18/commit/a8e2b67f197a8997856a14cec519c1efdd4d0e8c), carried from upstream commit `f523d110a63b5b38ab5d54df1d06f1e0988c9b74`. |
| [BitmapVec adaptation](https://github.com/Szmazwidi/android_kernel_nothing_sm8475-6.18/commit/dde7c4260973a936576da9818593350f6381ac10) | Restores Alice Ryhl's [ACK inline constructor](https://github.com/Szmazwidi/android_kernel_nothing_sm8475-6.18/commit/ce6cda6e770032c5233e6f1ea5602c6c39be0672), carried from upstream commit `d0cf6512bbcf77afb6102f886fcd7fd48b7ae043`. |
| [xHCI structure adaptation](https://github.com/Szmazwidi/android_kernel_nothing_sm8475-6.18/commit/51ae1a8ab1c55442402cf7bae7b5d1961edfb042) | Retains Mathias Nyman's [stable link-error change](https://github.com/Szmazwidi/android_kernel_nothing_sm8475-6.18/commit/89e76d0fc8d25a47c391b56d5c34367759612c8e), with the bitfield placement adjusted for the Android KMI. |
| [io_uring reconciliation](https://github.com/Szmazwidi/android_kernel_nothing_sm8475-6.18/commit/3c60af37782fe18839a25715b76d4e0592c5714a) | The integration bot restored the Android task-work API from the [Pong ACK baseline](https://github.com/Szmazwidi/android_kernel_nothing_sm8475-6.18/blob/58b289d57b3d1f4b6fa6bb51db13920f459ae153/io_uring/io_uring.c), blob `0837ec81223546948ec302c26d59e369f5d1ff38`. |
| [6.18.63 stable merge](https://github.com/Szmazwidi/android_kernel_nothing_sm8475-6.18/commit/d444348c03f7605f97eaa99423483e3bc91420bc) | The integration bot merged Greg Kroah-Hartman's exact [stable tip](https://github.com/Szmazwidi/android_kernel_nothing_sm8475-6.18/commit/1cdda838e8475edf89b73ff0cc952005dd0b1fa7). The 1,876 incoming stable commits retain their original IDs and metadata. |

The later netfilter, xHCI and IR reconciliation commits link their respective
stable sources in their commit messages. The module sources retain the
[redducc 6.6 import history](https://github.com/Szmazwidi/android_kernel_nothing_sm8475-modules-6.18/commit/17ec63a564efb6ae22a23abc737d5dadfc04789d).

## WLAN and FastRPC

The [WLAN suspend follow-up](https://github.com/Szmazwidi/android_kernel_nothing_sm8475-modules-6.18/commit/cf41edd10596d97db2d073cf3a7b02c381aeaa68)
extends the closed-WLAN suspend exception from
[CLG20](https://github.com/Szmazwidi/android_kernel_nothing_sm8475-modules-6.18/commit/34f0d36dca66bb5b18276e156e788b78031126f8)
by Szmazwidi and Claude. The follow-up applies that intent at the outer
cfg80211 validation gate and adds logs. The related FastRPC changes adapt
the existing vendor driver to the Pong split between native CDSP and
legacy ADSP/SLPI. See the [provenance inventory guide](provenance-guide.md)
for donor mappings and licensing scope.
