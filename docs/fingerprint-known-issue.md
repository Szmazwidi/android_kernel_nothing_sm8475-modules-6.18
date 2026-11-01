# Fingerprint status on the development phone

The Goodix fingerprint path is not yet qualified. The development image keeps
its `goodix_fp` autoload disabled when the `fp_enable` guard is off. In that
state `/dev/goodix_fp` is absent. The currently installed original fingerprint
AIDL service can dereference a null device during session creation and crash;
the service can be restarted by Android and crash again on the next request.
An available device-source fix checks for an unavailable HAL and returns an
error instead, but that alone does not make fingerprint authentication work.

In the historical CL15 device test, loading the driver created the device
node and started the trusted application, but sensor identification returned
`GF_ERROR_CHIP_ID_NOT_CORRECT`. After repeated HAL restarts, the then-current
driver also hit an invalid regulator pointer and rebooted the phone. CL15
introduced resource-lifetime corrections; its implementation is in the current
source cohort. A normalized function-by-function comparison found 30 of 30
functions matching the previously shipped CLS38 module, but this is not proof
that the sensor, secure-world interactions or all configurations work.

The current 6.18.60 phone snapshot has `fp_enable=0` and `fp_pending=0`, no
`/dev/goodix_fp`, and repeated crashes of the original service. Preserve the
guard rather than automatically loading or repeatedly restarting the driver.
Useful next work is to compare the sensor regulator and reset sequence with a
working 5.10 baseline, validate the existing lifecycle fix separately, and
qualify the AIDL error handling and actual biometric authentication on device.
Do not infer hardware damage from the reported ID error alone.
