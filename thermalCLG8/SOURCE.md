Qualcomm thermal/BCL drivers ported by Claude CLG8 from Nothing sm8475 5.10
commit 673fc11462a0cdb264230c136a964583cceec798 to android17-6.18.
Original copyright and GPL notices retained. thermal_pause_hp.c is Claude's
local hotplug-based replacement for WALT pause and is not equivalent to WALT.

Codex1411 adaptation of qmi_sensors.c: align the disabled low-temperature
sentinel (-INT_MAX) with thermal_core and redducc's 6.6 driver at
db9180ccc252da74c82b876e3cbed2e080dc4f7c/drivers/thermal/qcom/qmi_sensors.c.
Additional local fixes encode zero without undefined shifts and use an unsigned
sign-bit shift. No new claim of original Qualcomm driver authorship.
Host IEEE754/UBSAN tests and hardware qualification are separate evidence.
