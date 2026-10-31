/* SPDX-License-Identifier: GPL-2.0-only */
/* Actual single-source definitions, not exported kernel interfaces. */
const char *GetSourceSubsystemString(uint32_t source_subsystem);
char* GetUwbPrimaryCrashReason(enum UwbPrimaryReasonCode reason);
char* GetUwbSecondaryCrashReason(enum UwbSecondaryReasonCode reason);
int STREAM_TO_UINT32 (struct sk_buff *skb);
int bt_aop_send_msg(struct platform_pwr_data *plat_priv, char *mbox_msg);
int bt_kernel_panic(char *arg);
int btpower_access_ctrl(enum plt_pwr_state request);
int btpower_handle_client_request(unsigned int cmd, int arg);
int btpower_off(enum plt_pwr_state client);
int btpower_on(enum plt_pwr_state client);
int btpower_process_access_req(unsigned int cmd, int req);
void btpower_register_client(int client, int cmd);
int btpower_retenion(enum plt_pwr_state client);
void fmd_reboot_on_usb_detection(unsigned char arg);
void fmd_set_sdam_bit(unsigned char arg);
void fmd_write_stop_counter(unsigned char arg);
void log_power_src_val(void);
int perform_fmd_operation(void);
int perisec_cnss_bt_hw_disable_check(struct platform_pwr_data *plat_priv);
int power_disable (enum SubSystem SubSystemType);
int power_enable (enum SubSystem SubSystemType);
int schedule_client_voting(enum plt_pwr_state request);
void send_signal_to_subsystem(int SubSystemType, int state);
