/*
 * WPA Supplicant / privileged helper program
 * Copyright (c) 2007-2009, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"
#ifdef __linux__
#include <fcntl.h>
#endif /* __linux__ */
#include <sys/un.h>
#include <sys/stat.h>

#include "common.h"
#include "eloop.h"
#include "version.h"
#include "drivers/driver.h"
#include "l2_packet/l2_packet.h"
#include "privsep_commands.h"
#include "ieee802_11_defs.h"

#ifndef ETH_P_EAPOL
#define ETH_P_EAPOL 0x888e
#endif

#ifndef ETH_P_RSN_PREAUTH
#define ETH_P_RSN_PREAUTH 0x88c7
#endif


struct wpa_priv_interface {
	struct wpa_priv_interface *next;
	char *driver_name;
	char *ifname;
	char *sock_name;
	int fd;

	struct wpa_driver_ops *driver;
	void *drv_priv;
	struct sockaddr_un drv_addr;
	int wpas_registered;

	/* TODO: add support for multiple l2 connections */
	struct l2_packet_data *l2;
	struct sockaddr_un l2_addr;
};


static void wpa_priv_cmd_register(struct wpa_priv_interface *iface,
				  struct sockaddr_un *from)
{
	if (iface->drv_priv) {
		wpa_printf(MSG_DEBUG, "Cleaning up forgotten driver instance");
		if (iface->driver->set_wpa)
			iface->driver->set_wpa(iface->drv_priv, 0);
		if (iface->driver->deinit)
			iface->driver->deinit(iface->drv_priv);
		iface->drv_priv = NULL;
		iface->wpas_registered = 0;
	}

	if (iface->l2) {
		wpa_printf(MSG_DEBUG, "Cleaning up forgotten l2_packet "
			   "instance");
		l2_packet_deinit(iface->l2);
		iface->l2 = NULL;
	}

	if (iface->driver->init == NULL)
		return;

	iface->drv_priv = iface->driver->init(iface, iface->ifname);
	if (iface->drv_priv == NULL) {
		wpa_printf(MSG_DEBUG, "Failed to initialize driver wrapper");
		return;
	}

	wpa_printf(MSG_DEBUG, "Driver wrapper '%s' initialized for interface "
		   "'%s'", iface->driver_name, iface->ifname);

	os_memcpy(&iface->drv_addr, from, sizeof(iface->drv_addr));
	iface->wpas_registered = 1;

	if (iface->driver->set_param &&
	    iface->driver->set_param(iface->drv_priv, NULL) < 0) {
		wpa_printf(MSG_ERROR, "Driver interface rejected param");
	}

	if (iface->driver->set_wpa)
		iface->driver->set_wpa(iface->drv_priv, 1);
}


static void wpa_priv_cmd_unregister(struct wpa_priv_interface *iface,
				    struct sockaddr_un *from)
{
	if (iface->drv_priv) {
		if (iface->driver->set_wpa)
			iface->driver->set_wpa(iface->drv_priv, 0);
		if (iface->driver->deinit)
			iface->driver->deinit(iface->drv_priv);
		iface->drv_priv = NULL;
		iface->wpas_registered = 0;
	}
}


static void wpa_priv_cmd_set_wpa(struct wpa_priv_interface *iface,
				 char *buf, size_t len)
{
	if (iface->drv_priv == NULL || len != sizeof(int))
		return;

	if (iface->driver->set_wpa)
		iface->driver->set_wpa(iface->drv_priv, *((int *) buf));
}


static void wpa_priv_cmd_scan(struct wpa_priv_interface *iface,
			      char *buf, size_t len)
{
	if (iface->drv_priv == NULL)
		return;

	if (iface->driver->scan)
		iface->driver->scan(iface->drv_priv, len ? (u8 *) buf : NULL,
				    len);
}


static void wpa_priv_get_scan_results2(struct wpa_priv_interface *iface,
				       struct sockaddr_un *from)
{
	struct wpa_scan_results *res;
	u8 *buf = NULL, *pos, *end;
	int val;
	size_t i;

	res = iface->driver->get_scan_results2(iface->drv_priv);
	if (res == NULL)
		goto fail;

	buf = os_malloc(60000);
	if (buf == NULL)
		goto fail;
	pos = buf;
	end = buf + 60000;
	val = res->num;
	os_memcpy(pos, &val, sizeof(int));
	pos += sizeof(int);

	for (i = 0; i < res->num; i++) {
		struct wpa_scan_res *r = res->res[i];
		val = sizeof(*r) + r->ie_len;
		if (end - pos < (int) sizeof(int) + val)
			break;
		os_memcpy(pos, &val, sizeof(int));
		pos += sizeof(int);
		os_memcpy(pos, r, val);
		pos += val;
	}

	sendto(iface->fd, buf, pos - buf, 0, (struct sockaddr *) from,
	       sizeof(*from));

	os_free(buf);
	os_free(res);
	return;

fail:
	os_free(buf);
	os_free(res);
	sendto(iface->fd, "", 0, 0, (struct sockaddr *) from, sizeof(*from));
}


static void wpa_priv_send_old_scan_results(struct wpa_priv_interface *iface,
					   struct sockaddr_un *from)
{
#define SCAN_AP_LIMIT 128
	int i, res, val;
	struct wpa_scan_result *results = NULL;
	u8 *buf = NULL, *pos, *end;
	struct wpa_scan_res nres;

	results = os_malloc(SCAN_AP_LIMIT * sizeof(*results));
	if (results == NULL)
		goto fail;

	res = iface->driver->get_scan_results(iface->drv_priv, results,
					      SCAN_AP_LIMIT);
	if (res < 0 || res > SCAN_AP_LIMIT)
		goto fail;

	buf = os_malloc(60000);
	if (buf == NULL)
		goto fail;
	pos = buf;
	end = buf + 60000;
	os_memcpy(pos, &res, sizeof(int));
	pos += sizeof(int);

	os_memset(&nres, 0, sizeof(nres));
	for (i = 0; i < res; i++) {
		struct wpa_scan_result *r = &results[i];
		size_t ie_len;

		ie_len = 2 + r->ssid_len + r->rsn_ie_len + r->wpa_ie_len;
		if (r->maxrate)
			ie_len += 3;
		if (r->mdie_present)
			ie_len += 5;

		val = sizeof(nres) + ie_len;
		if (end - pos < (int) sizeof(int) + val)
			break;
		os_memcpy(pos, &val, sizeof(int));
		pos += sizeof(int);

		os_memcpy(nres.bssid, r->bssid, ETH_ALEN);
		nres.freq = r->freq;
		nres.caps = r->caps;
		nres.qual = r->qual;
		nres.noise = r->noise;
		nres.level = r->level;
		nres.tsf = r->tsf;
		nres.ie_len = ie_len;

		os_memcpy(pos, &nres, sizeof(nres));
		pos += sizeof(nres);

		/* SSID IE */
		*pos++ = WLAN_EID_SSID;
		*pos++ = r->ssid_len;
		os_memcpy(pos, r->ssid, r->ssid_len);
		pos += r->ssid_len;

		if (r->maxrate) {
			/* Fake Supported Rate IE to include max rate */
			*pos++ = WLAN_EID_SUPP_RATES;
			*pos++ = 1;
			*pos++ = r->maxrate;
		}

		if (r->rsn_ie_len) {
			os_memcpy(pos, r->rsn_ie, r->rsn_ie_len);
			pos += r->rsn_ie_len;
		}

		if (r->mdie_present) {
			os_memcpy(pos, r->mdie, 5);
			pos += 5;
		}

		if (r->wpa_ie_len) {
			os_memcpy(pos, r->wpa_ie, r->wpa_ie_len);
			pos += r->wpa_ie_len;
		}
	}

	sendto(iface->fd, buf, pos - buf, 0, (struct sockaddr *) from,
	       sizeof(*from));

	os_free(buf);
	os_free(results);
	return;

fail:
	os_free(buf);
	os_free(results);
	sendto(iface->fd, "", 0, 0, (struct sockaddr *) from, sizeof(*from));
}


static void wpa_priv_cmd_get_scan_results(struct wpa_priv_interface *iface,
					  struct sockaddr_un *from)
{
	if (iface->drv_priv == NULL)
		return;

	if (iface->driver->get_scan_results2)
		wpa_priv_get_scan_results2(iface, from);
	else if (iface->driver->get_scan_results)
		wpa_priv_send_old_scan_results(iface, from);
	else
		sendto(iface->fd, "", 0, 0, (struct sockaddr *) from,
		       sizeof(*from));
}


static void wpa_priv_cmd_associate(struct wpa_priv_interface *iface,
				   void *buf, size_t len)
{
	struct wpa_driver_associate_params params;
	struct privsep_cmd_associate *assoc;
	u8 *bssid;
	int res;

	if (iface->drv_priv == NULL || iface->driver->associate == NULL)
		return;

	if (len < sizeof(*assoc)) {
		wpa_printf(MSG_DEBUG, "Invalid association request");
		return;
	}

	assoc = buf;
	if (sizeof(*assoc) + assoc->wpa_ie_len > len) {
		wpa_printf(MSG_DEBUG, "Association request overflow");
		return;
	}

	os_memset(&params, 0, sizeof(params));
	bssid = assoc->bssid;
	if (bssid[0] | bssid[1] | bssid[2] | bssid[3] | bssid[4] | bssid[5])
		params.bssid = bssid;
	params.ssid = assoc->ssid;
	if (assoc->ssid_len > 32)
		return;
	params.ssid_len = assoc->ssid_len;
	params.freq = assoc->freq;
	if (assoc->wpa_ie_len) {
		params.wpa_ie = (u8 *) (assoc + 1);
		params.wpa_ie_len = assoc->wpa_ie_len;
	}
	params.pairwise_suite = assoc->pairwise_suite;
	params.group_suite = assoc->group_suite;
	params.key_mgmt_suite = assoc->key_mgmt_suite;
	params.auth_alg = assoc->auth_alg;
	params.mode = assoc->mode;

	res = iface->driver->associate(iface->drv_priv, &params);
	wpa_printf(MSG_DEBUG, "drv->associate: res=%d", res);
}


static void wpa_priv_cmd_get_bssid(struct wpa_priv_interface *iface,
				   struct sockaddr_un *from)
{
	u8 bssid[ETH_ALEN];

	if (iface->drv_priv == NULL)
		goto fail;

	if (iface->driver->get_bssid == NULL ||
	    iface->driver->get_bssid(iface->drv_priv, bssid) < 0)
		goto fail;

	sendto(iface->fd, bssid, ETH_ALEN, 0, (struct sockaddr *) from,
	       sizeof(*from));
	return;

fail:
	sendto(iface->fd, "", 0, 0, (struct sockaddr *) from, sizeof(*from));
}


static void wpa_priv_cmd_get_ssid(struct wpa_priv_interface *iface,
				  struct sockaddr_un *from)
{
	u8 ssid[sizeof(int) + 32];
	int res;

	if (iface->drv_priv == NULL)
		goto fail;

	if (iface->driver->get_ssid == NULL)
		goto fail;

	res = iface->driver->get_ssid(iface->drv_priv, &ssid[sizeof(int)]);
	if (res < 0 || res > 32)
		goto fail;
	os_memcpy(ssid, &res, sizeof(int));

	sendto(iface->fd, ssid, sizeof(ssid), 0, (struct sockaddr *) from,
	       sizeof(*from));
	return;

fail:
	sendto(iface->fd, "", 0, 0, (struct sockaddr *) from, sizeof(*from));
}


static void wpa_priv_cmd_set_key(struct wpa_priv_interface *iface,
				 void *buf, size_t len)
{
	struct privsep_cmd_set_key *params;
	int res;

	if (iface->drv_priv == NULL || iface->driver->set_key == NULL)
		return;

	if (len != sizeof(*params)) {
		wpa_printf(MSG_DEBUG, "Invalid set_key request");
		return;
	}

	params = buf;

	res = iface->driver->set_key(iface->drv_priv, params->alg,
				     params->addr, params->key_idx,
				     params->set_tx,
				     params->seq_len ? params->seq : NULL,
				     params->seq_len,
				     params->key_len ? params->key : NULL,
				     params->key_len);
	wpa_printf(MSG_DEBUG, "drv->set_key: res=%d", res);
}


static void wpa_priv_cmd_get_capa(struct wpa_priv_interface *iface,
				  struct sockaddr_un *from)
{
	struct wpa_driver_capa capa;

	if (iface->drv_priv == NULL)
		goto fail;

	if (iface->driver->get_capa == NULL ||
	    iface->driver->get_capa(iface->drv_priv, &capa) < 0)
		goto fail;

	sendto(iface->fd, &capa, sizeof(capa), 0, (struct sockaddr *) from,
	       sizeof(*from));
	return;

fail:
	sendto(iface->fd, "", 0, 0, (struct sockaddr *) from, sizeof(*from));
}


static void wpa_priv_l2_rx(void *ctx, const u8 *src_addr, const u8 *buf,
			   size_t len)
{
	struct wpa_priv_interface *iface = ctx;
	struct msghdr msg;
	struct iovec io[2];

	io[0].iov_base = (u8 *) src_addr;
	io[0].iov_len = ETH_ALEN;
	io[1].iov_base = (u8 *) buf;
	io[1].iov_len = len;

	os_memset(&msg, 0, sizeof(msg));
	msg.msg_iov = io;
	msg.msg_iovlen = 2;
	msg.msg_name = &iface->l2_addr;
	msg.msg_namelen = sizeof(iface->l2_addr);

	if (sendmsg(iface->fd, &msg, 0) < 0) {
		perror("sendmsg(l2 rx)");
	}
}


static void wpa_priv_cmd_l2_register(struct wpa_priv_interface *iface,
				     struct sockaddr_un *from,
				     void *buf, size_t len)
{
	int *reg_cmd = buf;
	u8 own_addr[ETH_ALEN];
	int res;
	u16 proto;

	if (len != 2 * sizeof(int)) {
		wpa_printf(MSG_DEBUG, "Invalid l2_register length %lu",
			   (unsigned long) len);
		return;
	}

	proto = reg_cmd[0];
	if (proto != ETH_P_EAPOL && proto != ETH_P_RSN_PREAUTH) {
		wpa_printf(MSG_DEBUG, "Refused l2_packet connection for "
			   "ethertype 0x%x", proto);
		return;
	}

	if (iface->l2) {
		wpa_printf(MSG_DEBUG, "Cleaning up forgotten l2_packet "
			   "instance");
		l2_packet_deinit(iface->l2);
		iface->l2 = NULL;
	}

	os_memcpy(&iface->l2_addr, from, sizeof(iface->l2_addr));

	iface->l2 = l2_packet_init(iface->ifname, NULL, proto,
				   wpa_priv_l2_rx, iface, reg_cmd[1]);
	if (iface->l2 == NULL) {
		wpa_printf(MSG_DEBUG, "Failed to initialize l2_packet "
			   "instance for protocol %d", proto);
		return;
	}

	if (l2_packet_get_own_addr(iface->l2, own_addr) < 0) {
		wpa_printf(MSG_DEBUG, "Failed to get own address from "
			   "l2_packet");
		l2_packet_deinit(iface->l2);
		iface->l2 = NULL;
		return;
	}

	res = sendto(iface->fd, own_addr, ETH_ALEN, 0,
		     (struct sockaddr *) from, sizeof(*from));
	wpa_printf(MSG_DEBUG, "L2 registration: res=%d", res);
}


static void wpa_priv_cmd_l2_unregister(struct wpa_priv_interface *iface,
				       struct sockaddr_un *from)
{
	if (iface->l2) {
		l2_packet_deinit(iface->l2);
		iface->l2 = NULL;
	}
}


static void wpa_priv_cmd_l2_notify_auth_start(struct wpa_priv_interface *iface,
					      struct sockaddr_un *from)
{
	if (iface->l2)
		l2_packet_notify_auth_start(iface->l2);
}


static void wpa_priv_cmd_l2_send(struct wpa_priv_interface *iface,
				 struct sockaddr_un *from,
				 void *buf, size_t len)
{
	u8 *dst_addr;
	u16 proto;
	int res;

	if (iface->l2 == NULL)
		return;

	if (len < ETH_ALEN + 2) {
		wpa_printf(MSG_DEBUG, "Too short L2 send packet (len=%lu)",
			   (unsigned long) len);
		return;
	}

	dst_addr = buf;
	os_memcpy(&proto, buf + ETH_ALEN, 2);

	if (proto != ETH_P_EAPOL && proto != ETH_P_RSN_PREAUTH) {
		wpa_printf(MSG_DEBUG, "Refused l2_packet send for ethertype "
			   "0x%x", proto);
		return;
	}

	res = l2_packet_send(iface->l2, dst_addr, proto, buf + ETH_ALEN + 2,
			     len - ETH_ALEN - 2);
	wpa_printf(MSG_DEBUG, "L2 send: res=%d", res);
}


static void wpa_priv_cmd_set_mode(struct wpa_priv_interface *iface,
				  void *buf, size_t len)
{
	if (iface->drv_priv == NULL || iface->driver->set_mode == NULL ||
	    len != sizeof(int))
		return;

	iface->driver->set_mode(iface->drv_priv, *((int *) buf));
}


static void wpa_priv_cmd_set_country(struct wpa_priv_interface *iface,
				     char *buf)
{
	if (iface->drv_priv == NULL || iface->driver->set_country == NULL ||
	    *buf == '\0')
		return;

	iface->driver->set_country(iface->drv_priv, buf);
}


static void wpa_priv_receive(int sock, void *eloop_ctx, void *sock_ctx)
{
	struct wpa_priv_interface *iface = eloop_ctx;
	char buf[2000], *pos;
	void *cmd_buf;
	size_t cmd_len;
	int res, cmd;
	struct sockaddr_un from;
	socklen_t fromlen = sizeof(from);

	res = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *) &from,
		       &fromlen);
	if (res < 0) {
		perror("recvfrom");
		return;
	}

	if (res < (int) sizeof(int)) {
		wpa_printf(MSG_DEBUG, "Too short command (len=%d)", res);
		return;
	}

	os_memcpy(&cmd, buf, sizeof(int));
	wpa_printf(MSG_DEBUG, "Command %d for interface %s",
		   cmd, iface->ifname);
	cmd_buf = &buf[sizeof(int)];
	cmd_len = res - sizeof(int);

	switch (cmd) {
	case PRIVSEP_CMD_REGISTER:
		wpa_priv_cmd_register(iface, &from);
		break;
	case PRIVSEP_CMD_UNREGISTER:
		wpa_priv_cmd_unregister(iface, &from);
		break;
	case PRIVSEP_CMD_SET_WPA:
		wpa_priv_cmd_set_wpa(iface, cmd_buf, cmd_len);
		break;
	case PRIVSEP_CMD_SCAN:
		wpa_priv_cmd_scan(iface, cmd_buf, cmd_len);
		break;
	case PRIVSEP_CMD_GET_SCAN_RESULTS:
		wpa_priv_cmd_get_scan_results(iface, &from);
		break;
	case PRIVSEP_CMD_ASSOCIATE:
		wpa_priv_cmd_associate(iface, cmd_buf, cmd_len);
		break;
	case PRIVSEP_CMD_GET_BSSID:
		wpa_priv_cmd_get_bssid(iface, &from);
		break;
	case PRIVSEP_CMD_GET_SSID:
		wpa_priv_cmd_get_ssid(iface, &from);
		break;
	case PRIVSEP_CMD_SET_KEY:
		wpa_priv_cmd_set_key(iface, cmd_buf, cmd_len);
		break;
	case PRIVSEP_CMD_GET_CAPA:
		wpa_priv_cmd_get_capa(iface, &from);
		break;
	case PRIVSEP_CMD_L2_REGISTER:
		wpa_priv_cmd_l2_register(iface, &from, cmd_buf, cmd_len);
		break;
	case PRIVSEP_CMD_L2_UNREGISTER:
		wpa_priv_cmd_l2_unregister(iface, &from);
		break;
	case PRIVSEP_CMD_L2_NOTIFY_AUTH_START:
		wpa_priv_cmd_l2_notify_auth_start(iface, &from);
		break;
	case PRIVSEP_CMD_L2_SEND:
		wpa_priv_cmd_l2_send(iface, &from, cmd_buf, cmd_len);
		break;
	case PRIVSEP_CMD_SET_MODE:
		wpa_priv_cmd_set_mode(iface, cmd_buf, cmd_len);
		break;
	case PRIVSEP_CMD_SET_COUNTRY:
		pos = cmd_buf;
		if (pos + cmd_len >= buf + sizeof(buf))
			break;
		pos[cmd_len] = '\0';
		wpa_priv_cmd_set_country(iface, pos);
		break;
	}
}


static void wpa_priv_interface_deinit(struct wpa_priv_interface *iface)
{
	if (iface->drv_priv && iface->driver->deinit)
		iface->driver->deinit(iface->drv_priv);

	if (iface->fd >= 0) {
		eloop_unregister_read_sock(iface->fd);
		close(iface->fd);
		unlink(iface->sock_name);
	}

	if (iface->l2)
		l2_packet_deinit(iface->l2);

	os_free(iface->ifname);
	os_free(iface->driver_name);
	os_free(iface->sock_name);
	os_free(iface);
}


extern struct wpa_driver_ops *wpa_drivers[];

static struct wpa_priv_interface *
wpa_priv_interface_init(const char *dir, const char *params)
{
	struct wpa_priv_interface *iface;
	char *pos;
	size_t len;
	struct sockaddr_un addr;
	int i;

	pos = os_strchr(params, ':');
	if (pos == NULL)
		return NULL;

	iface = os_zalloc(sizeof(*iface));
	if (iface == NULL)
		return NULL;
	iface->fd = -1;

	len = pos - params;
	iface->driver_name = os_malloc(len + 1);
	if (iface->driver_name == NULL) {
		wpa_priv_interface_deinit(iface);
		return NULL;
	}
	os_memcpy(iface->driver_name, params, len);
	iface->driver_name[len] = '\0';

	for (i = 0; wpa_drivers[i]; i++) {
		if (os_strcmp(iface->driver_name,
			      wpa_drivers[i]->name) == 0) {
			iface->driver = wpa_drivers[i];
			break;
		}
	}
	if (iface->driver == NULL) {
		wpa_printf(MSG_ERROR, "Unsupported driver '%s'",
			   iface->driver_name);
		wpa_priv_interface_deinit(iface);
		return NULL;
	}

	pos++;
	iface->ifname = os_strdup(pos);
	if (iface->ifname == NULL) {
		wpa_priv_interface_deinit(iface);
		return NULL;
	}

	len = os_strlen(dir) + 1 + os_strlen(iface->ifname);
	iface->sock_name = os_malloc(len + 1);
	if (iface->sock_name == NULL) {
		wpa_priv_interface_deinit(iface);
		return NULL;
	}

	os_snprintf(iface->sock_name, len + 1, "%s/%s", dir, iface->ifname);
	if (os_strlen(iface->sock_name) >= sizeof(addr.sun_path)) {
		wpa_priv_interface_deinit(iface);
		return NULL;
	}

	iface->fd = socket(PF_UNIX, SOCK_DGRAM, 0);
	if (iface->fd < 0) {
		perror("socket(PF_UNIX)");
		wpa_priv_interface_deinit(iface);
		return NULL;
	}

	os_memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	os_strlcpy(addr.sun_path, iface->sock_name, sizeof(addr.sun_path));

	if (bind(iface->fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		wpa_printf(MSG_DEBUG, "bind(PF_UNIX) failed: %s",
			   strerror(errno));
		if (connect(iface->fd, (struct sockaddr *) &addr,
			    sizeof(addr)) < 0) {
			wpa_printf(MSG_DEBUG, "Socket exists, but does not "
				   "allow connections - assuming it was "
				   "leftover from forced program termination");
			if (unlink(iface->sock_name) < 0) {
				perror("unlink[ctrl_iface]");
				wpa_printf(MSG_ERROR, "Could not unlink "
					   "existing ctrl_iface socket '%s'",
					   iface->sock_name);
				goto fail;
			}
			if (bind(iface->fd, (struct sockaddr *) &addr,
				 sizeof(addr)) < 0) {
				perror("bind(PF_UNIX)");
				goto fail;
			}
			wpa_printf(MSG_DEBUG, "Successfully replaced leftover "
				   "socket '%s'", iface->sock_name);
		} else {
			wpa_printf(MSG_INFO, "Socket exists and seems to be "
				   "in use - cannot override it");
			wpa_printf(MSG_INFO, "Delete '%s' manually if it is "
				   "not used anymore", iface->sock_name);
			goto fail;
		}
	}

	if (chmod(iface->sock_name, S_IRWXU | S_IRWXG | S_IRWXO) < 0) {
		perror("chmod");
		goto fail;
	}

	eloop_register_read_sock(iface->fd, wpa_priv_receive, iface, NULL);

	return iface;

fail:
	wpa_priv_interface_deinit(iface);
	return NULL;
}


static int wpa_priv_send_event(struct wpa_priv_interface *iface, int event,
			       const void *data, size_t data_len)
{
	struct msghdr msg;
	struct iovec io[2];

	io[0].iov_base = &event;
	io[0].iov_len = sizeof(event);
	io[1].iov_base = (u8 *) data;
	io[1].iov_len = data_len;

	os_memset(&msg, 0, sizeof(msg));
	msg.msg_iov = io;
	msg.msg_iovlen = data ? 2 : 1;
	msg.msg_name = &iface->drv_addr;
	msg.msg_namelen = sizeof(iface->drv_addr);

	if (sendmsg(iface->fd, &msg, 0) < 0) {
		perror("sendmsg(wpas_socket)");
		return -1;
	}

	return 0;
}


static void wpa_priv_send_assoc(struct wpa_priv_interface *iface, int event,
				union wpa_event_data *data)
{
	size_t buflen = 3 * sizeof(int);
	u8 *buf, *pos;
	int len;

	if (data) {
		buflen += data->assoc_info.req_ies_len +
			data->assoc_info.resp_ies_len +
			data->assoc_info.beacon_ies_len;
	}

	buf = os_malloc(buflen);
	if (buf == NULL)
		return;

	pos = buf;

	if (data && data->assoc_info.req_ies) {
		len = data->assoc_info.req_ies_len;
		os_memcpy(pos, &len, sizeof(int));
		pos += sizeof(int);
		os_memcpy(pos, data->assoc_info.req_ies, len);
		pos += len;
	} else {
		len = 0;
		os_memcpy(pos, &len, sizeof(int));
		pos += sizeof(int);
	}

	if (data && data->assoc_info.resp_ies) {
		len = data->assoc_info.resp_ies_len;
		os_memcpy(pos, &len, sizeof(int));
		pos += sizeof(int);
		os_memcpy(pos, data->assoc_info.resp_ies, len);
		pos += len;
	} else {
		len = 0;
		os_memcpy(pos, &len, sizeof(int));
		pos += sizeof(int);
	}

	if (data && data->assoc_info.beacon_ies) {
		len = data->assoc_info.beacon_ies_len;
		os_memcpy(pos, &len, sizeof(int));
		pos += sizeof(int);
		os_memcpy(pos, data->assoc_info.beacon_ies, len);
		pos += len;
	} else {
		len = 0;
		os_memcpy(pos, &len, sizeof(int));
		pos += sizeof(int);
	}

	wpa_priv_send_event(iface, event, buf, buflen);

	os_free(buf);
}


static void wpa_priv_send_interface_status(struct wpa_priv_interface *iface,
					   union wpa_event_data *data)
{
	int ievent;
	size_t len, maxlen;
	u8 *buf;
	char *ifname;

	if (data == NULL)
		return;

	ievent = data->interface_status.ievent;
	maxlen = sizeof(data->interface_status.ifname);
	ifname = data->interface_status.ifname;
	for (len = 0; len < maxlen && ifname[len]; len++)
		;

	buf = os_malloc(sizeof(int) + len);
	if (buf == NULL)
		return;

	os_memcpy(buf, &ievent, sizeof(int));
	os_memcpy(buf + sizeof(int), ifname, len);

	wpa_priv_send_event(iface, PRIVSEP_EVENT_INTERFACE_STATUS,
			    buf, sizeof(int) + len);

	os_free(buf);

}


static void wpa_priv_send_ft_response(struct wpa_priv_interface *iface,
				      union wpa_event_data *data)
{
	size_t len;
	u8 *buf, *pos;

	if (data == NULL || data->ft_ies.ies == NULL)
		return;

	len = sizeof(int) + ETH_ALEN + data->ft_ies.ies_len;
	buf = os_malloc(len);
	if (buf == NULL)
		return;

	pos = buf;
	os_memcpy(pos, &data->ft_ies.ft_action, sizeof(int));
	pos += sizeof(int);
	os_memcpy(pos, data->ft_ies.target_ap, ETH_ALEN);
	pos += ETH_ALEN;
	os_memcpy(pos, data->ft_ies.ies, data->ft_ies.ies_len);

	wpa_priv_send_event(iface, PRIVSEP_EVENT_FT_RESPONSE, buf, len);

	os_free(buf);

}


void wpa_supplicant_event(void *ctx, wpa_event_type event,
			  union wpa_event_data *data)
{
	struct wpa_priv_interface *iface = ctx;

	wpa_printf(MSG_DEBUG, "%s - event=%d", __func__, event);

	if (!iface->wpas_registered) {
		wpa_printf(MSG_DEBUG, "Driver event received, but "
			   "wpa_supplicant not registered");
		return;
	}

	switch (event) {
	case EVENT_ASSOC:
		wpa_priv_send_assoc(iface, PRIVSEP_EVENT_ASSOC, data);
		break;
	case EVENT_DISASSOC:
		wpa_priv_send_event(iface, PRIVSEP_EVENT_DISASSOC, NULL, 0);
		break;
	case EVENT_ASSOCINFO:
		if (data == NULL)
			return;
		wpa_priv_send_assoc(iface, PRIVSEP_EVENT_ASSOCINFO, data);
		break;
	case EVENT_MICHAEL_MIC_FAILURE:
		if (data == NULL)
			return;
		wpa_priv_send_event(iface, PRIVSEP_EVENT_MICHAEL_MIC_FAILURE,
				    &data->michael_mic_failure.unicast,
				    sizeof(int));
		break;
	case EVENT_SCAN_RESULTS:
		wpa_priv_send_event(iface, PRIVSEP_EVENT_SCAN_RESULTS, NULL,
				    0);
		break;
	case EVENT_INTERFACE_STATUS:
		wpa_priv_send_interface_status(iface, data);
		break;
	case EVENT_PMKID_CANDIDATE:
		if (data == NULL)
			return;
		wpa_priv_send_event(iface, PRIVSEP_EVENT_PMKID_CANDIDATE,
				    &data->pmkid_candidate,
				    sizeof(struct pmkid_candidate));
		break;
	case EVENT_STKSTART:
		if (data == NULL)
			return;
		wpa_priv_send_event(iface, PRIVSEP_EVENT_STKSTART,
				    &data->stkstart.peer, ETH_ALEN);
		break;
	case EVENT_FT_RESPONSE:
		wpa_priv_send_ft_response(iface, data);
		break;
	default:
		wpa_printf(MSG_DEBUG, "Unsupported driver event %d - TODO",
			   event);
		break;
	}
}


void wpa_supplicant_rx_eapol(void *ctx, const u8 *src_addr,
			     const u8 *buf, size_t len)
{
	struct wpa_priv_interface *iface = ctx;
	struct msghdr msg;
	struct iovec io[3];
	int event = PRIVSEP_EVENT_RX_EAPOL;

	wpa_printf(MSG_DEBUG, "RX EAPOL from driver");
	io[0].iov_base = &event;
	io[0].iov_len = sizeof(event);
	io[1].iov_base = (u8 *) src_addr;
	io[1].iov_len = ETH_ALEN;
	io[2].iov_base = (u8 *) buf;
	io[2].iov_len = len;

	os_memset(&msg, 0, sizeof(msg));
	msg.msg_iov = io;
	msg.msg_iovlen = 3;
	msg.msg_name = &iface->drv_addr;
	msg.msg_namelen = sizeof(iface->drv_addr);

	if (sendmsg(iface->fd, &msg, 0) < 0)
		perror("sendmsg(wpas_socket)");
}


#ifdef CONFIG_CLIENT_MLME
void wpa_supplicant_sta_rx(void *ctx, const u8 *buf, size_t len,
			   struct ieee80211_rx_status *rx_status)
{
	struct wpa_priv_interface *iface = ctx;
	struct msghdr msg;
	struct iovec io[3];
	int event = PRIVSEP_EVENT_STA_RX;

	wpa_printf(MSG_DEBUG, "STA RX from driver");
	io[0].iov_base = &event;
	io[0].iov_len = sizeof(event);
	io[1].iov_base = (u8 *) rx_status;
	io[1].iov_len = sizeof(*rx_status);
	io[2].iov_base = (u8 *) buf;
	io[2].iov_len = len;

	os_memset(&msg, 0, sizeof(msg));
	msg.msg_iov = io;
	msg.msg_iovlen = 3;
	msg.msg_name = &iface->drv_addr;
	msg.msg_namelen = sizeof(iface->drv_addr);

	if (sendmsg(iface->fd, &msg, 0) < 0)
		perror("sendmsg(wpas_socket)");
}
#endif /* CONFIG_CLIENT_MLME */


static void wpa_priv_terminate(int sig, void *eloop_ctx, void *signal_ctx)
{
	wpa_printf(MSG_DEBUG, "wpa_priv termination requested");
	eloop_terminate();
}


static void wpa_priv_fd_workaround(void)
{
#ifdef __linux__
	int s, i;
	/* When started from pcmcia-cs scripts, wpa_supplicant might start with
	 * fd 0, 1, and 2 closed. This will cause some issues because many
	 * places in wpa_supplicant are still printing out to stdout. As a
	 * workaround, make sure that fd's 0, 1, and 2 are not used for other
	 * sockets. */
	for (i = 0; i < 3; i++) {
		s = open("/dev/null", O_RDWR);
		if (s > 2) {
			close(s);
			break;
		}
	}
#endif /* __linux__ */
}


static void usage(void)
{
	printf("wpa_priv v" VERSION_STR "\n"
	       "Copyright (c) 2007-2009, Jouni Malinen <j@w1.fi> and "
	       "contributors\n"
	       "\n"
	       "usage:\n"
	       "  wpa_priv [-Bdd] [-P<pid file>] <driver:ifname> "
	       "[driver:ifname ...]\n");
}


extern int wpa_debug_level;

int main(int argc, char *argv[])
{
	int c, i;
	int ret = -1;
	char *pid_file = NULL;
	int daemonize = 0;
	char *ctrl_dir = "/var/run/wpa_priv";
	struct wpa_priv_interface *interfaces = NULL, *iface;

	if (os_program_init())
		return -1;

	wpa_priv_fd_workaround();

	for (;;) {
		c = getopt(argc, argv, "Bc:dP:");
		if (c < 0)
			break;
		switch (c) {
		case 'B':
			daemonize++;
			break;
		case 'c':
			ctrl_dir = optarg;
			break;
		case 'd':
			wpa_debug_level--;
			break;
		case 'P':
			pid_file = os_rel2abs_path(optarg);
			break;
		default:
			usage();
			goto out;
		}
	}

	if (optind >= argc) {
		usage();
		goto out;
	}

	wpa_printf(MSG_DEBUG, "wpa_priv control directory: '%s'", ctrl_dir);

	if (eloop_init(NULL)) {
		wpa_printf(MSG_ERROR, "Failed to initialize event loop");
		goto out;
	}

	for (i = optind; i < argc; i++) {
		wpa_printf(MSG_DEBUG, "Adding driver:interface %s", argv[i]);
		iface = wpa_priv_interface_init(ctrl_dir, argv[i]);
		if (iface == NULL)
			goto out;
		iface->next = interfaces;
		interfaces = iface;
	}

	if (daemonize && os_daemonize(pid_file))
		goto out;

	eloop_register_signal_terminate(wpa_priv_terminate, NULL);
	eloop_run();

	ret = 0;

out:
	iface = interfaces;
	while (iface) {
		struct wpa_priv_interface *prev = iface;
		iface = iface->next;
		wpa_priv_interface_deinit(prev);
	}

	eloop_destroy();

	os_daemonize_terminate(pid_file);
	os_free(pid_file);
	os_program_deinit();

	return ret;
}
