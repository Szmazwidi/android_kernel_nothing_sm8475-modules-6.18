Port by Claude (stage CLG11, 2026-11-02) for ACK android17-6.18.
ucsi_glink_qti.c: upstream 6.18 drivers/usb/typec/ucsi/ucsi_glink.c adapted to the vendor qti_pmic_glink client API
(the protocol with the ADSP is the same as the Nothing 5.10 ucsi_glink). ucsi.h: copy of the 6.18 internal UCSI header.
Not ported yet: altmode-glink and fsa4480 (DP alt mode / analog audio accessory switching).
