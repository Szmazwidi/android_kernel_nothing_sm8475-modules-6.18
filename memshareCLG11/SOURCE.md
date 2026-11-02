Port by Claude (stage CLG11, 2026-11-02) for ACK android17-6.18.
Nothing sm8475 5.10 drivers/soc/qcom/memshare. SSR via qcom_register_ssr_notifier("mpss") (old subsys_notif API gone),
hyp_assign_phys() emulated with the exported hyp_assign_table(), ramdump of loaned blocks not supported (debug only).
