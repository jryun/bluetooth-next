/* This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Authors:
 * Alexander Aring <aar@pengutronix.de>
 *
 * Based on: net/mac80211/cfg.c
 */

#include <net/rtnetlink.h>
#include <net/cfg802154.h>

#include "ieee802154_i.h"
#include "driver-ops.h"
#include "cfg.h"

int mac802154_set_header_security(struct ieee802154_sub_if_data *sdata, struct ieee802154_hdr *hdr, const struct ieee802154_mac_cb *cb);

static struct net_device *
ieee802154_add_iface_deprecated(struct wpan_phy *wpan_phy,
				const char *name,
				unsigned char name_assign_type, int type)
{
	struct ieee802154_local *local = wpan_phy_priv(wpan_phy);
	struct net_device *dev;

	rtnl_lock();
	dev = ieee802154_if_add(local, name, name_assign_type, type,
				cpu_to_le64(0x0000000000000000ULL));
	rtnl_unlock();

	return dev;
}

static void ieee802154_del_iface_deprecated(struct wpan_phy *wpan_phy,
					    struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);

	ieee802154_if_remove(sdata);
}

#ifdef CONFIG_PM
static int ieee802154_suspend(struct wpan_phy *wpan_phy)
{
	struct ieee802154_local *local = wpan_phy_priv(wpan_phy);

	if (!local->open_count)
		goto suspend;

	ieee802154_stop_queue(&local->hw);
	synchronize_net();

	/* stop hardware - this must stop RX */
	ieee802154_stop_device(local);

suspend:
	local->suspended = true;
	return 0;
}

static int ieee802154_resume(struct wpan_phy *wpan_phy)
{
	struct ieee802154_local *local = wpan_phy_priv(wpan_phy);
	int ret;

	/* nothing to do if HW shouldn't run */
	if (!local->open_count)
		goto wake_up;

	/* restart hardware */
	ret = drv_start(local);
	if (ret)
		return ret;

wake_up:
	ieee802154_wake_queue(&local->hw);
	local->suspended = false;
	return 0;
}
#else
#define ieee802154_suspend NULL
#define ieee802154_resume NULL
#endif

static int
ieee802154_add_iface(struct wpan_phy *phy, const char *name,
		     unsigned char name_assign_type,
		     enum nl802154_iftype type, __le64 extended_addr)
{
	struct ieee802154_local *local = wpan_phy_priv(phy);
	struct net_device *err;

	err = ieee802154_if_add(local, name, name_assign_type, type,
				extended_addr);
	return PTR_ERR_OR_ZERO(err);
}

static int
ieee802154_del_iface(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev)
{
	ieee802154_if_remove(IEEE802154_WPAN_DEV_TO_SUB_IF(wpan_dev));

	return 0;
}

static int
ieee802154_set_channel(struct wpan_phy *wpan_phy, u8 page, u8 channel)
{
	struct ieee802154_local *local = wpan_phy_priv(wpan_phy);
	int ret;

	ASSERT_RTNL();

	if (wpan_phy->current_page == page &&
	    wpan_phy->current_channel == channel)
		return 0;

	ret = drv_set_channel(local, page, channel);
	if (!ret) {
		wpan_phy->current_page = page;
		wpan_phy->current_channel = channel;
	}

	return ret;
}

static int
ieee802154_set_cca_mode(struct wpan_phy *wpan_phy,
			const struct wpan_phy_cca *cca)
{
	struct ieee802154_local *local = wpan_phy_priv(wpan_phy);
	int ret;

	ASSERT_RTNL();

	if (wpan_phy_cca_cmp(&wpan_phy->cca, cca))
		return 0;

	ret = drv_set_cca_mode(local, cca);
	if (!ret)
		wpan_phy->cca = *cca;

	return ret;
}

static int
ieee802154_set_cca_ed_level(struct wpan_phy *wpan_phy, s32 ed_level)
{
	struct ieee802154_local *local = wpan_phy_priv(wpan_phy);
	int ret;

	ASSERT_RTNL();

	if (wpan_phy->cca_ed_level == ed_level)
		return 0;

	ret = drv_set_cca_ed_level(local, ed_level);
	if (!ret)
		wpan_phy->cca_ed_level = ed_level;

	return ret;
}

static int
ieee802154_set_tx_power(struct wpan_phy *wpan_phy, s32 power)
{
	struct ieee802154_local *local = wpan_phy_priv(wpan_phy);
	int ret;

	ASSERT_RTNL();

	if (wpan_phy->transmit_power == power)
		return 0;

	ret = drv_set_tx_power(local, power);
	if (!ret)
		wpan_phy->transmit_power = power;

	return ret;
}

static int
ieee802154_set_addr_mode(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev,
		      u8 mode)
{
	int ret;

	ASSERT_RTNL();

	if (wpan_dev->addr_mode == mode)
		return 0;

	ret = mac802154_wpan_update_llsec(wpan_dev->netdev);
	if (!ret)
		wpan_dev->addr_mode = mode;

	return ret;
}

static int
ieee802154_set_pan_id(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev,
		      __le16 pan_id)
{
	int ret;

	ASSERT_RTNL();

	if (wpan_dev->pan_id == pan_id)
		return 0;

	ret = mac802154_wpan_update_llsec(wpan_dev->netdev);
	if (!ret)
		wpan_dev->pan_id = pan_id;

	return ret;
}


static int
ieee802154_set_coord_extended_addr(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev,
		      __le64 extended_addr)
{
	int ret;

	ASSERT_RTNL();

	if (wpan_dev->coord_extended_addr == extended_addr)
		return 0;

	ret = mac802154_wpan_update_llsec(wpan_dev->netdev);
	if (!ret)
		wpan_dev->coord_extended_addr = extended_addr;

	return ret;
}

static int
ieee802154_set_coord_addr_mode(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev,
		      u8 mode)
{
	int ret;

	ASSERT_RTNL();

	if (wpan_dev->addr_mode == mode)
		return 0;

	ret = mac802154_wpan_update_llsec(wpan_dev->netdev);
	if (!ret)
		wpan_dev->addr_mode = mode;

	return ret;
}

static int
ieee802154_set_backoff_exponent(struct wpan_phy *wpan_phy,
				struct wpan_dev *wpan_dev,
				u8 min_be, u8 max_be)
{
	ASSERT_RTNL();

	if (wpan_dev->min_be == min_be &&
	    wpan_dev->max_be == max_be)
		return 0;

	wpan_dev->min_be = min_be;
	wpan_dev->max_be = max_be;
	return 0;
}

static int
ieee802154_set_short_addr(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev,
			  __le16 short_addr)
{
	ASSERT_RTNL();

	if (wpan_dev->short_addr == short_addr)
		return 0;

	wpan_dev->short_addr = short_addr;
	return 0;
}

static int
ieee802154_set_coord_short_addr(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev,
			  __le16 short_addr)
{
	ASSERT_RTNL();

	if (wpan_dev->coord_short_addr == short_addr)
		return 0;

	wpan_dev->coord_short_addr = short_addr;
	return 0;
}

static int
ieee802154_set_max_csma_backoffs(struct wpan_phy *wpan_phy,
				 struct wpan_dev *wpan_dev,
				 u8 max_csma_backoffs)
{
	ASSERT_RTNL();

	if (wpan_dev->csma_retries == max_csma_backoffs)
		return 0;

	wpan_dev->csma_retries = max_csma_backoffs;
	return 0;
}

static int
ieee802154_set_max_frame_retries(struct wpan_phy *wpan_phy,
				 struct wpan_dev *wpan_dev,
				 s8 max_frame_retries)
{
	ASSERT_RTNL();

	if (wpan_dev->frame_retries == max_frame_retries)
		return 0;

	wpan_dev->frame_retries = max_frame_retries;
	return 0;
}

static int
ieee802154_set_lbt_mode(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev,
			bool mode)
{
	ASSERT_RTNL();

	if (wpan_dev->lbt == mode)
		return 0;

	wpan_dev->lbt = mode;
	return 0;
}

static int
ieee802154_ed_scan(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev,
            u8 page, u32 channels, u8 *level, size_t nlevel, u8 duration)
{
	struct ieee802154_local *local = wpan_phy_priv(wpan_phy);
	int ret = 0;

	ASSERT_RTNL();

	ret = drv_ed_scan( local, page, channels, level, nlevel, duration );

	return ret;
}

static int
ieee802154_header_create( struct sk_buff *skb,
								struct wpan_dev *wpan_dev,
								unsigned short type,
								const void *daddr,
								const void *saddr,
								unsigned len,
								bool intra_pan)
{
	printk(KERN_INFO "Inside %s\n", __FUNCTION__);
	struct ieee802154_hdr hdr;
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(wpan_dev->netdev);
	struct ieee802154_mac_cb *cb = mac_cb(skb);
	int hlen;

	if (!daddr)
		return -EINVAL;

	memset(&hdr.fc, 0, sizeof(hdr.fc));
	hdr.fc.type = cb->type;
	hdr.fc.security_enabled = cb->secen;
	hdr.fc.ack_request = cb->ackreq;
	hdr.fc.intra_pan = intra_pan;

	printk(KERN_INFO "%x", wpan_dev);
	printk(KERN_INFO "%x", atomic_inc_return(&wpan_dev->dsn));

	hdr.seq = (atomic_inc_return(&wpan_dev->dsn)/2) & 0xFF;

	if (mac802154_set_header_security(sdata, &hdr, cb) < 0)
		return -EINVAL;

	if (!saddr) {
		if (wpan_dev->short_addr == cpu_to_le16(IEEE802154_ADDR_BROADCAST) ||
		    wpan_dev->short_addr == cpu_to_le16(IEEE802154_ADDR_UNDEF) ||
		    wpan_dev->pan_id == cpu_to_le16(IEEE802154_PANID_BROADCAST)) {
			hdr.source.mode = IEEE802154_ADDR_LONG;
			hdr.source.extended_addr = wpan_dev->extended_addr;
		} else {
			hdr.source.mode = IEEE802154_ADDR_SHORT;
			hdr.source.short_addr = wpan_dev->short_addr;
		}

		hdr.source.pan_id = wpan_dev->pan_id;
	} else {
		hdr.source = *(const struct ieee802154_addr *)saddr;
	}

	hdr.dest = *(const struct ieee802154_addr *)daddr;

	hlen = ieee802154_hdr_push(skb, &hdr);
	if (hlen < 0)
		return -EINVAL;

	skb_reset_mac_header(skb);
	skb->mac_len = hlen;

	if (len > ieee802154_max_payload(&hdr))
		return -EMSGSIZE;

	return hlen;
}

static void
ieee802154_assoc_ack(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev,
		u8 addr_mode, u16 coord_pan_id, u64 coord_addr, u64 src_addr ){

	int r = 0;
	struct sk_buff *skb;
	struct ieee802154_mac_cb *cb;
	int hlen, tlen, size;
	struct ieee802154_addr dst_addr, source_addr;
	unsigned char *data;

	printk(KERN_INFO "Inside %s\n", __FUNCTION__);

	struct ieee802154_local * local = wpan_phy_priv(wpan_phy);

	//Create beacon frame / payload
	hlen = LL_RESERVED_SPACE(wpan_dev->netdev);
	tlen = wpan_dev->netdev->needed_tailroom;
	size = 1; //Todo: Replace magic number. Comes from ieee std 802154 "Association Request Frame Format" with a define

	printk( KERN_INFO "The skb lengths used are hlen: %d, tlen %d, and size %d\n", hlen, tlen, size);
	printk( KERN_INFO "Address of the netdev device structure: %x\n", wpan_dev->netdev );
	printk( KERN_INFO "Address of ieee802154_local * local from wpan_phy_priv: %x\n", local );

	//Subvert and populate the ieee802154_local pointer in ieee802154_sub_if_data
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(wpan_dev->netdev);
	sdata->local = local;

	skb = alloc_skb( hlen + tlen + size, GFP_KERNEL );
	if (!skb){
		goto error;
	}

	skb_reserve(skb, hlen);

	skb_reset_network_header(skb);

	data = skb_put(skb, size);

	source_addr.mode = IEEE802154_ADDR_LONG;
	source_addr.pan_id = 0;
	source_addr.extended_addr = src_addr;

	dst_addr.mode = addr_mode;
	dst_addr.pan_id = coord_pan_id;

	if ( IEEE802154_ADDR_SHORT == addr_mode ){
		dst_addr.short_addr = (u16*)coord_addr;
	} else {
		dst_addr.extended_addr = coord_addr;
	}

	cb = mac_cb_init(skb);
	cb->type = IEEE802154_FC_TYPE_MAC_CMD;
	cb->ackreq = true;

	cb->secen = false;
	cb->secen_override = false;
	cb->seclevel = 0;

	cb->source = source_addr;
	cb->dest = dst_addr;

	printk( KERN_INFO "DSN value in wpan_dev: %x\n", &wpan_dev->dsn );

	printk( KERN_INFO "Dest addr: %x\n", dst_addr.short_addr );
	printk( KERN_INFO "Dest addr long: %x\n", dst_addr.extended_addr );
	printk( KERN_INFO "Src addr: %x\n", source_addr.short_addr );
	printk( KERN_INFO "Src addr long: %x\n", source_addr.extended_addr );

	//Since the existing subroutine for creating the mac header doesn't seem to work in this situation, will be rewriting it it with a correction here
	r = ieee802154_header_create( skb, wpan_dev, ETH_P_IEEE802154, &dst_addr, &source_addr, hlen + tlen + size, true);

	printk( KERN_INFO "Header is created");

	//Add the mac header to the data
	r = memcpy( data, cb, size );
	data[0] = IEEE802154_CMD_DATA_REQ;

	skb->dev = wpan_dev->netdev;
	skb->protocol = htons(ETH_P_IEEE802154);

	printk( KERN_INFO "Data bytes sent out %x",data[0]);

	r = ieee802154_subif_start_xmit( skb, wpan_dev->netdev );
	printk( KERN_INFO "r value is %x", r );
	if( 0 == r) {
		goto out;
	}

error:
	kfree_skb(skb);
out:
	return;
}

static inline bool is_extended_address( u64 addr ) {
	static const u64 mask = ~((1 << 16) - 1);
	return mask & addr;
}

static inline bool is_short_address( u16 addr ) {
	return !( IEEE802154_ADDR_BROADCAST == addr || IEEE802154_ADDR_UNDEF == addr );
}

#ifndef PRIx64
#define PRIx64 "llx"
#endif

static int
ieee802154_assoc_req(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev,
		u8 addr_mode, u16 coord_pan_id, u64 coord_addr,
		u8 capability_information, u64 src_addr ){

	int r;

	struct sk_buff *skb;
	struct ieee802154_mac_cb *cb;
	int hlen, tlen, size;
	struct ieee802154_addr dst_addr, source_addr;
	unsigned char *data;

	struct ieee802154_sub_if_data *sdata;
	struct ieee802154_local * local;

	local = wpan_phy_priv(wpan_phy);

	memset( &source_addr, 0, sizeof( src_addr ) );
	memset( &dst_addr, 0, sizeof( dst_addr ) );

	//Create beacon frame / payload
	hlen = LL_RESERVED_SPACE(wpan_dev->netdev);
	tlen = wpan_dev->netdev->needed_tailroom;
	size = 2; //Todo: Replace magic number. Comes from ieee std 802154 "Association Request Frame Format" with a define

	dev_dbg( &wpan_dev->netdev->dev, "The skb lengths used are hlen: %d, tlen %d, and size %d\n", hlen, tlen, size);
	dev_dbg( &wpan_dev->netdev->dev, "Address of the netdev device structure: %p\n", wpan_dev->netdev );
	dev_dbg( &wpan_dev->netdev->dev, "Address of ieee802154_local * local from wpan_phy_priv: %p\n", local );

	//Subvert and populate the ieee802154_local pointer in ieee802154_sub_if_data
	sdata = IEEE802154_DEV_TO_SUB_IF(wpan_dev->netdev);

	sdata->local = local;

	skb = alloc_skb( hlen + tlen + size, GFP_KERNEL );
	if (!skb){
		r = -ENOMEM;
		goto error;
	}

	skb_reserve(skb, hlen);

	skb_reset_network_header(skb);

	data = skb_put(skb, size);

	source_addr.mode = IEEE802154_ADDR_LONG;
	source_addr.pan_id = IEEE802154_PANID_BROADCAST;
	source_addr.extended_addr = src_addr;

	dst_addr.mode = addr_mode;
	dst_addr.pan_id = coord_pan_id;

	if ( IEEE802154_ADDR_SHORT == addr_mode ){
		dst_addr.short_addr = (u16*)coord_addr;
	} else {
		dst_addr.extended_addr = coord_addr;
	}

	cb = mac_cb_init(skb);
	cb->type = IEEE802154_FC_TYPE_MAC_CMD;
	cb->ackreq = true;

	cb->secen = false;
	cb->secen_override = false;
	cb->seclevel = 0;

	cb->source = source_addr;
	cb->dest = dst_addr;

	dev_dbg( &wpan_dev->netdev->dev, "DSN value in wpan_dev: %p\n", &wpan_dev->dsn);

	dev_dbg( &wpan_dev->netdev->dev, "Dest addr: 0x%04x\n", dst_addr.short_addr );
	dev_dbg( &wpan_dev->netdev->dev, "Dest addr long: 0x%016" PRIx64 "\n", dst_addr.extended_addr );
	dev_dbg( &wpan_dev->netdev->dev, "Src addr: 0x%04x\n", source_addr.short_addr );
	dev_dbg( &wpan_dev->netdev->dev, "Src addr long: 0x%016" PRIx64 "\n", source_addr.extended_addr );

	//Since the existing subroutine for creating the mac header doesn't seem to work in this situation, will be rewriting it it with a correction here
	r = ieee802154_header_create( skb, wpan_dev, ETH_P_IEEE802154, &dst_addr, &source_addr, hlen + tlen + size, false);

	printk( KERN_INFO "Header is created");

	//Add the mac header to the data
	r = memcpy( data, cb, size );
	data[0] = IEEE802154_CMD_ASSOCIATION_REQ;
	data[1] = capability_information;

	skb->dev = wpan_dev->netdev;
	skb->protocol = htons(ETH_P_IEEE802154);

	dev_dbg( &wpan_dev->netdev->dev, "Data bytes sent out %x, %x",data[0], data[1]);

	r = ieee802154_subif_start_xmit( skb, wpan_dev->netdev );
	dev_dbg( &wpan_dev->netdev->dev, "r value is %x", r );

	if( 0 == r) {
		goto error;
	}

	r = 0;
	goto out;

error:
	kfree_skb(skb);
out:
	return r;
}

static int
ieee802154_disassoc_req(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev,
						u16 device_panid, u64 device_address,
						u8 disassociate_reason, u8 tx_indirect)
{
	int r;

	struct sk_buff *skb;
	struct ieee802154_mac_cb *cb;
	int hlen, tlen, size;
	struct ieee802154_addr dst_addr, src_addr;
	unsigned char *data;

	struct ieee802154_sub_if_data *sdata;
	struct ieee802154_local * local;

	local = wpan_phy_priv(wpan_phy);

	memset( &src_addr, 0, sizeof( src_addr ) );
	memset( &dst_addr, 0, sizeof( dst_addr ) );

	//Create beacon frame / payload
	hlen = LL_RESERVED_SPACE(wpan_dev->netdev);
	tlen = wpan_dev->netdev->needed_tailroom;
	size = 2; //Todo: Replace magic number. Comes from ieee std 802154 "Association Request Frame Format" with a define

	dev_dbg( &wpan_dev->netdev->dev, "The skb lengths used are hlen: %d, tlen %d, and size %d\n", hlen, tlen, size);
	dev_dbg( &wpan_dev->netdev->dev, "Address of the netdev device structure: %p\n", wpan_dev->netdev );
	dev_dbg( &wpan_dev->netdev->dev, "Address of ieee802154_local * local from wpan_phy_priv: %p\n", local );

	//Subvert and populate the ieee802154_local pointer in ieee802154_sub_if_data
	sdata = IEEE802154_DEV_TO_SUB_IF(wpan_dev->netdev);

	sdata->local = local;

	skb = alloc_skb( hlen + tlen + size, GFP_KERNEL );
	if (!skb){
		r = -ENOMEM;
		goto error;
	}

	skb_reserve(skb, hlen);

	skb_reset_network_header(skb);

	data = skb_put(skb, size);

	src_addr.mode = wpan_dev->addr_mode;
	src_addr.pan_id = wpan_dev->pan_id;
	if ( IEEE802154_ADDR_LONG == src_addr.mode ) {
		src_addr.short_addr = wpan_dev->short_addr;
	} else {
		src_addr.extended_addr = wpan_dev->extended_addr;
	}

	dst_addr.mode = wpan_dev->coord_addr_mode;
	dst_addr.pan_id = wpan_dev->pan_id;
	if ( IEEE802154_ADDR_SHORT == dst_addr.mode ){
		dst_addr.short_addr = wpan_dev->coord_short_addr;
	} else {
		dst_addr.extended_addr = wpan_dev->coord_extended_addr;
	}

	cb = mac_cb_init(skb);
	cb->type = IEEE802154_FC_TYPE_MAC_CMD;
	cb->ackreq = true;

	cb->secen = false;
	cb->secen_override = false;
	cb->seclevel = 0;

	cb->source = src_addr;
	cb->dest = dst_addr;

	dev_dbg( &wpan_dev->netdev->dev, "DSN value in wpan_dev: %p\n", &wpan_dev->dsn);

	dev_dbg( &wpan_dev->netdev->dev, "Dest addr: 0x%04x\n", dst_addr.short_addr );
	dev_dbg( &wpan_dev->netdev->dev, "Dest addr long: 0x%016" PRIx64 "\n", dst_addr.extended_addr );
	dev_dbg( &wpan_dev->netdev->dev, "Src addr: 0x%04x\n", src_addr.short_addr );
	dev_dbg( &wpan_dev->netdev->dev, "Src addr long: 0x%016" PRIx64 "\n", src_addr.extended_addr );

	//Since the existing subroutine for creating the mac header doesn't seem to work in this situation, will be rewriting it it with a correction here
	r = ieee802154_header_create( skb, wpan_dev, ETH_P_IEEE802154, &dst_addr, &src_addr, hlen + tlen + size, false);
	if ( 0 != r ) {
		dev_err( &wpan_dev->netdev->dev, "ieee802154_header_create failed (%d)\n", r );
		goto error;
	}

	dev_dbg( &wpan_dev->netdev->dev, "Header is created");

	//Add the mac header to the data
	memcpy( data, cb, size );
	data[0] = IEEE802154_CMD_DISASSOCIATION_NOTIFY;
	data[1] = disassociate_reason;

	skb->dev = wpan_dev->netdev;
	skb->protocol = htons(ETH_P_IEEE802154);

	dev_dbg( &wpan_dev->netdev->dev, "Data bytes sent out %x, %x",data[0], data[1]);

	r = ieee802154_subif_start_xmit( skb, wpan_dev->netdev );
	dev_dbg( &wpan_dev->netdev->dev, "r value is %x", r );
	if( 0 == r) {
		goto error;
	}

	r = 0;
	goto out;

error:
	kfree_skb(skb);
out:
	return r;
}

static int
ieee802154_register_assoc_req_listener(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev,
		void (*callback)( struct sk_buff *, void *), void *arg )
{
	int ret = 0;

	printk(KERN_INFO "Inside %s\n",__FUNCTION__);

	struct ieee802154_local *local = wpan_phy_priv(wpan_phy);
	local->callback = callback;
	local->listen_flag = 1;
	local->delayed_work = arg;
	ret = drv_start( local );

	return ret;
}

const struct cfg802154_ops mac802154_config_ops = {
	.add_virtual_intf_deprecated = ieee802154_add_iface_deprecated,
	.del_virtual_intf_deprecated = ieee802154_del_iface_deprecated,
	.suspend = ieee802154_suspend,
	.resume = ieee802154_resume,
	.add_virtual_intf = ieee802154_add_iface,
	.del_virtual_intf = ieee802154_del_iface,
	.set_channel = ieee802154_set_channel,
	.set_cca_mode = ieee802154_set_cca_mode,
	.set_cca_ed_level = ieee802154_set_cca_ed_level,
	.set_tx_power = ieee802154_set_tx_power,
	.set_coord_addr_mode = ieee802154_set_coord_addr_mode,
	.set_coord_extended_addr = ieee802154_set_coord_extended_addr,
	.set_coord_short_addr = ieee802154_set_coord_short_addr,
	.set_addr_mode = ieee802154_set_addr_mode,
	.set_pan_id = ieee802154_set_pan_id,
	.set_short_addr = ieee802154_set_short_addr,
	.set_backoff_exponent = ieee802154_set_backoff_exponent,
	.set_max_csma_backoffs = ieee802154_set_max_csma_backoffs,
	.set_max_frame_retries = ieee802154_set_max_frame_retries,
	.set_lbt_mode = ieee802154_set_lbt_mode,
	.ed_scan = ieee802154_ed_scan,
	.assoc_req = ieee802154_assoc_req,
	.assoc_ack = ieee802154_assoc_ack,
	.register_assoc_req_listener = ieee802154_register_assoc_req_listener,
	.disassoc_req = ieee802154_disassoc_req,
};
