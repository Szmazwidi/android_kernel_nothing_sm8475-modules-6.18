/* SPDX-License-Identifier: GPL-2.0-only */
/* Local 6.18 API adaptation; original implementations remain untouched. */
static int pong456_change_station(struct wiphy *wiphy, struct wireless_dev *wdev, const u8 *mac, struct station_parameters *params)
{
	if (!wdev || !wdev->netdev)
		return -EOPNOTSUPP;
	return wlan_hdd_change_station(wiphy, wdev->netdev, mac, params);
}

static int pong456_add_station(struct wiphy *wiphy, struct wireless_dev *wdev, const u8 *mac, struct station_parameters *params)
{
	if (!wdev || !wdev->netdev)
		return -EOPNOTSUPP;
	return wlan_hdd_cfg80211_add_station(wiphy, wdev->netdev, mac, params);
}

static int pong456_del_station(struct wiphy *wiphy, struct wireless_dev *wdev, struct station_del_parameters *params)
{
	if (!wdev || !wdev->netdev)
		return -EOPNOTSUPP;
	return wlan_hdd_cfg80211_del_station(wiphy, wdev->netdev, params);
}

static int pong456_get_station(struct wiphy *wiphy, struct wireless_dev *wdev, const u8 *mac, struct station_info *info)
{
	if (!wdev || !wdev->netdev)
		return -EOPNOTSUPP;
	return wlan_hdd_cfg80211_get_station(wiphy, wdev->netdev, mac, info);
}

static int pong456_dump_station(struct wiphy *wiphy, struct wireless_dev *wdev, int index, u8 *mac, struct station_info *info)
{
	if (!wdev || !wdev->netdev)
		return -EOPNOTSUPP;
	return wlan_hdd_cfg80211_dump_station(wiphy, wdev->netdev, index, mac, info);
}

static int pong456_add_key(struct wiphy *wiphy, struct wireless_dev *wdev, int link_id, u8 index, bool pairwise, const u8 *mac, struct key_params *params)
{
	if (!wdev || !wdev->netdev)
		return -EOPNOTSUPP;
	return wlan_hdd_cfg80211_add_key(wiphy, wdev->netdev, link_id, index, pairwise, mac, params);
}

static int pong456_get_key(struct wiphy *wiphy, struct wireless_dev *wdev, int link_id, u8 index, bool pairwise, const u8 *mac, void *cookie, void (*callback)(void *, struct key_params *))
{
	if (!wdev || !wdev->netdev)
		return -EOPNOTSUPP;
	return wlan_hdd_cfg80211_get_key(wiphy, wdev->netdev, link_id, index, pairwise, mac, cookie, callback);
}

static int pong456_del_key(struct wiphy *wiphy, struct wireless_dev *wdev, int link_id, u8 index, bool pairwise, const u8 *mac)
{
	if (!wdev || !wdev->netdev)
		return -EOPNOTSUPP;
	return wlan_hdd_cfg80211_del_key(wiphy, wdev->netdev, link_id, index, pairwise, mac);
}

static int pong456_set_default_mgmt_key(struct wiphy *wiphy, struct wireless_dev *wdev, int link_id, u8 index)
{
	if (!wdev || !wdev->netdev)
		return -EOPNOTSUPP;
	return wlan_hdd_set_default_mgmt_key(wiphy, wdev->netdev, link_id, index);
}

static int pong456_get_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev, int radio_idx, unsigned int link_id, int *dbm)
{
	if (radio_idx > 0) return -EOPNOTSUPP;
	return wlan_hdd_cfg80211_get_txpower(wiphy, wdev, link_id, dbm);
}

static int pong456_set_antenna(struct wiphy *wiphy, int radio_idx, u32 tx, u32 rx)
{
	if (radio_idx > 0) return -EOPNOTSUPP;
	return wlan_hdd_cfg80211_set_chainmask(wiphy, tx, rx);
}

static int pong456_get_antenna(struct wiphy *wiphy, int radio_idx, u32 *tx, u32 *rx)
{
	if (radio_idx > 0) return -EOPNOTSUPP;
	return wlan_hdd_cfg80211_get_chainmask(wiphy, tx, rx);
}

static int pong456_remain_on_channel(struct wiphy *wiphy, struct wireless_dev *wdev, struct ieee80211_channel *chan, unsigned int duration, u64 *cookie, const u8 *rx_addr)
{
	if (rx_addr && (!wdev || !wdev->netdev || !ether_addr_equal(rx_addr, wdev->netdev->dev_addr))) return -EOPNOTSUPP;
	return wlan_hdd_cfg80211_remain_on_channel(wiphy, wdev, chan, duration, cookie);
}

static int pong456_set_wiphy_params(struct wiphy *wiphy, int radio_idx, u32 changed)
{
	if (radio_idx > 0) return -EOPNOTSUPP;
	return wlan_hdd_cfg80211_set_wiphy_params(wiphy, changed);
}

static int pong456_set_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev, int radio_idx, enum nl80211_tx_power_setting type, int mbm)
{
	if (radio_idx > 0) return -EOPNOTSUPP;
	return wlan_hdd_cfg80211_set_txpower(wiphy, wdev, type, mbm);
}

static int pong456_set_default_beacon_key(struct wiphy *wiphy, struct wireless_dev *wdev, int link_id, u8 index)
{
	if (!wdev || !wdev->netdev) return -EOPNOTSUPP;
	return wlan_hdd_cfg80211_set_default_beacon_key(wiphy, wdev->netdev, link_id, index);
}

