Port of the Qualcomm DCVS stack (memlat, bwmon, L3 EPSS, DDR/LLCC fast path, PMU lib, LLCC PMU)
from the Nothing Phone (2) 5.10 kernel source to ACK android17-6.18 (stage CLG3, Claude, 2026-11-02).
Origin: /mnt/games/derp17/kernel/nothing/sm8475 (drivers/soc/qcom/dcvs, drivers/perf/qcom_llcc_pmu.c, include/soc/qcom/*).
The device tree on the phone is the matching 5.10 DT (qcom,dcvs / qcom,memlat / qcom,bwmon4/5).
Not used: SCMI/CPUCP (rimps), bwprof, prime helper (CONFIG off in this package).
