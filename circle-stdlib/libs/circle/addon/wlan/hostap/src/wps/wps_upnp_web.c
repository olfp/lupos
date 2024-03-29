/*
 * UPnP WPS Device - Web connections
 * Copyright (c) 2000-2003 Intel Corporation
 * Copyright (c) 2006-2007 Sony Corporation
 * Copyright (c) 2008-2009 Atheros Communications
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * See wps_upnp.c for more details on licensing and code history.
 */

#include "includes.h"

#include "common.h"
#include "base64.h"
#include "uuid.h"
#include "httpread.h"
#include "http_server.h"
#include "wps_i.h"
#include "wps_upnp.h"
#include "wps_upnp_i.h"
#include "upnp_xml.h"

/***************************************************************************
 * Web connections (we serve pages of info about ourselves, handle
 * requests, etc. etc.).
 **************************************************************************/

#define WEB_CONNECTION_TIMEOUT_SEC 30   /* Drop web connection after t.o. */
#define WEB_CONNECTION_MAX_READ 8000    /* Max we'll read for TCP request */
#define MAX_WEB_CONNECTIONS 10          /* max simultaneous web connects */


static const char *urn_wfawlanconfig =
	"urn:schemas-wifialliance-org:service:WFAWLANConfig:1";
static const char *http_server_hdr =
	"Server: unspecified, UPnP/1.0, unspecified\r\n";
static const char *http_connection_close =
	"Connection: close\r\n";

/*
 * "Files" that we serve via HTTP. The format of these files is given by
 * WFA WPS specifications. Extra white space has been removed to save space.
 */

static const char wps_scpd_xml[] =
"<?xml version=\"1.0\"?>\n"
"<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\n"
"<specVersion><major>1</major><minor>0</minor></specVersion>\n"
"<actionList>\n"
"<action>\n"
"<name>GetDeviceInfo</name>\n"
"<argumentList>\n"
"<argument>\n"
"<name>NewDeviceInfo</name>\n"
"<direction>out</direction>\n"
"<relatedStateVariable>DeviceInfo</relatedStateVariable>\n"
"</argument>\n"
"</argumentList>\n"
"</action>\n"
"<action>\n"
"<name>PutMessage</name>\n"
"<argumentList>\n"
"<argument>\n"
"<name>NewInMessage</name>\n"
"<direction>in</direction>\n"
"<relatedStateVariable>InMessage</relatedStateVariable>\n"
"</argument>\n"
"<argument>\n"
"<name>NewOutMessage</name>\n"
"<direction>out</direction>\n"
"<relatedStateVariable>OutMessage</relatedStateVariable>\n"
"</argument>\n"
"</argumentList>\n"
"</action>\n"
"<action>\n"
"<name>PutWLANResponse</name>\n"
"<argumentList>\n"
"<argument>\n"
"<name>NewMessage</name>\n"
"<direction>in</direction>\n"
"<relatedStateVariable>Message</relatedStateVariable>\n"
"</argument>\n"
"<argument>\n"
"<name>NewWLANEventType</name>\n"
"<direction>in</direction>\n"
"<relatedStateVariable>WLANEventType</relatedStateVariable>\n"
"</argument>\n"
"<argument>\n"
"<name>NewWLANEventMAC</name>\n"
"<direction>in</direction>\n"
"<relatedStateVariable>WLANEventMAC</relatedStateVariable>\n"
"</argument>\n"
"</argumentList>\n"
"</action>\n"
"<action>\n"
"<name>SetSelectedRegistrar</name>\n"
"<argumentList>\n"
"<argument>\n"
"<name>NewMessage</name>\n"
"<direction>in</direction>\n"
"<relatedStateVariable>Message</relatedStateVariable>\n"
"</argument>\n"
"</argumentList>\n"
"</action>\n"
"</actionList>\n"
"<serviceStateTable>\n"
"<stateVariable sendEvents=\"no\">\n"
"<name>Message</name>\n"
"<dataType>bin.base64</dataType>\n"
"</stateVariable>\n"
"<stateVariable sendEvents=\"no\">\n"
"<name>InMessage</name>\n"
"<dataType>bin.base64</dataType>\n"
"</stateVariable>\n"
"<stateVariable sendEvents=\"no\">\n"
"<name>OutMessage</name>\n"
"<dataType>bin.base64</dataType>\n"
"</stateVariable>\n"
"<stateVariable sendEvents=\"no\">\n"
"<name>DeviceInfo</name>\n"
"<dataType>bin.base64</dataType>\n"
"</stateVariable>\n"
"<stateVariable sendEvents=\"yes\">\n"
"<name>APStatus</name>\n"
"<dataType>ui1</dataType>\n"
"</stateVariable>\n"
"<stateVariable sendEvents=\"yes\">\n"
"<name>STAStatus</name>\n"
"<dataType>ui1</dataType>\n"
"</stateVariable>\n"
"<stateVariable sendEvents=\"yes\">\n"
"<name>WLANEvent</name>\n"
"<dataType>bin.base64</dataType>\n"
"</stateVariable>\n"
"<stateVariable sendEvents=\"no\">\n"
"<name>WLANEventType</name>\n"
"<dataType>ui1</dataType>\n"
"</stateVariable>\n"
"<stateVariable sendEvents=\"no\">\n"
"<name>WLANEventMAC</name>\n"
"<dataType>string</dataType>\n"
"</stateVariable>\n"
"<stateVariable sendEvents=\"no\">\n"
"<name>WLANResponse</name>\n"
"<dataType>bin.base64</dataType>\n"
"</stateVariable>\n"
"</serviceStateTable>\n"
"</scpd>\n"
;


static const char *wps_device_xml_prefix =
	"<?xml version=\"1.0\"?>\n"
	"<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\n"
	"<specVersion>\n"
	"<major>1</major>\n"
	"<minor>0</minor>\n"
	"</specVersion>\n"
	"<device>\n"
	"<deviceType>urn:schemas-wifialliance-org:device:WFADevice:1"
	"</deviceType>\n";

static const char *wps_device_xml_postfix =
	"<serviceList>\n"
	"<service>\n"
	"<serviceType>urn:schemas-wifialliance-org:service:WFAWLANConfig:1"
	"</serviceType>\n"
	"<serviceId>urn:wifialliance-org:serviceId:WFAWLANConfig1</serviceId>"
	"\n"
	"<SCPDURL>" UPNP_WPS_SCPD_XML_FILE "</SCPDURL>\n"
	"<controlURL>" UPNP_WPS_DEVICE_CONTROL_FILE "</controlURL>\n"
	"<eventSubURL>" UPNP_WPS_DEVICE_EVENT_FILE "</eventSubURL>\n"
	"</service>\n"
	"</serviceList>\n"
	"</device>\n"
	"</root>\n";


/* format_wps_device_xml -- produce content of "file" wps_device.xml
 * (UPNP_WPS_DEVICE_XML_FILE)
 */
static void format_wps_device_xml(struct upnp_wps_device_sm *sm,
				  struct wpabuf *buf)
{
	const char *s;
	char uuid_string[80];

	wpabuf_put_str(buf, wps_device_xml_prefix);

	/*
	 * Add required fields with default values if not configured. Add
	 * optional and recommended fields only if configured.
	 */
	s = sm->wps->friendly_name;
	s = ((s && *s) ? s : "WPS Access Point");
	xml_add_tagged_data(buf, "friendlyName", s);

	s = sm->wps->dev.manufacturer;
	s = ((s && *s) ? s : "");
	xml_add_tagged_data(buf, "manufacturer", s);

	if (sm->wps->manufacturer_url)
		xml_add_tagged_data(buf, "manufacturerURL",
				    sm->wps->manufacturer_url);

	if (sm->wps->model_description)
		xml_add_tagged_data(buf, "modelDescription",
				    sm->wps->model_description);

	s = sm->wps->dev.model_name;
	s = ((s && *s) ? s : "");
	xml_add_tagged_data(buf, "modelName", s);

	if (sm->wps->dev.model_number)
		xml_add_tagged_data(buf, "modelNumber",
				    sm->wps->dev.model_number);

	if (sm->wps->model_url)
		xml_add_tagged_data(buf, "modelURL", sm->wps->model_url);

	if (sm->wps->dev.serial_number)
		xml_add_tagged_data(buf, "serialNumber",
				    sm->wps->dev.serial_number);

	uuid_bin2str(sm->wps->uuid, uuid_string, sizeof(uuid_string));
	s = uuid_string;
	/* Need "uuid:" prefix, thus we can't use xml_add_tagged_data()
	 * easily...
	 */
	wpabuf_put_str(buf, "<UDN>uuid:");
	xml_data_encode(buf, s, os_strlen(s));
	wpabuf_put_str(buf, "</UDN>\n");

	if (sm->wps->upc)
		xml_add_tagged_data(buf, "UPC", sm->wps->upc);

	wpabuf_put_str(buf, wps_device_xml_postfix);
}


static void http_put_reply_code(struct wpabuf *buf, enum http_reply_code code)
{
	wpabuf_put_str(buf, "HTTP/1.1 ");
	switch (code) {
	case HTTP_OK:
		wpabuf_put_str(buf, "200 OK\r\n");
		break;
	case HTTP_BAD_REQUEST:
		wpabuf_put_str(buf, "400 Bad request\r\n");
		break;
	case HTTP_PRECONDITION_FAILED:
		wpabuf_put_str(buf, "412 Precondition failed\r\n");
		break;
	case HTTP_UNIMPLEMENTED:
		wpabuf_put_str(buf, "501 Unimplemented\r\n");
		break;
	case HTTP_INTERNAL_SERVER_ERROR:
	default:
		wpabuf_put_str(buf, "500 Internal server error\r\n");
		break;
	}
}


static void http_put_date(struct wpabuf *buf)
{
	wpabuf_put_str(buf, "Date: ");
	format_date(buf);
	wpabuf_put_str(buf, "\r\n");
}


static void http_put_empty(struct wpabuf *buf, enum http_reply_code code)
{
	http_put_reply_code(buf, code);
	wpabuf_put_str(buf, http_server_hdr);
	wpabuf_put_str(buf, http_connection_close);
	wpabuf_put_str(buf, "Content-Length: 0\r\n"
		       "\r\n");
}


/* Given that we have received a header w/ GET, act upon it
 *
 * Format of GET (case-insensitive):
 *
 * First line must be:
 *      GET /<file> HTTP/1.1
 * Since we don't do anything fancy we just ignore other lines.
 *
 * Our response (if no error) which includes only required lines is:
 * HTTP/1.1 200 OK
 * Connection: close
 * Content-Type: text/xml
 * Date: <rfc1123-date>
 *
 * Header lines must end with \r\n
 * Per RFC 2616, content-length: is not required but connection:close
 * would appear to be required (given that we will be closing it!).
 */
static void web_connection_parse_get(struct upnp_wps_device_sm *sm,
				     struct http_request *hreq, char *filename)
{
	struct wpabuf *buf; /* output buffer, allocated */
	char *put_length_here;
	char *body_start;
	enum {
		GET_DEVICE_XML_FILE,
		GET_SCPD_XML_FILE
	} req;
	size_t extra_len = 0;
	int body_length;
	char len_buf[10];

	/*
	 * It is not required that filenames be case insensitive but it is
	 * allowed and cannot hurt here.
	 */
	if (filename == NULL)
		filename = "(null)"; /* just in case */
	if (os_strcasecmp(filename, UPNP_WPS_DEVICE_XML_FILE) == 0) {
		wpa_printf(MSG_DEBUG, "WPS UPnP: HTTP GET for device XML");
		req = GET_DEVICE_XML_FILE;
		extra_len = 3000;
		if (sm->wps->friendly_name)
			extra_len += os_strlen(sm->wps->friendly_name);
		if (sm->wps->manufacturer_url)
			extra_len += os_strlen(sm->wps->manufacturer_url);
		if (sm->wps->model_description)
			extra_len += os_strlen(sm->wps->model_description);
		if (sm->wps->model_url)
			extra_len += os_strlen(sm->wps->model_url);
		if (sm->wps->upc)
			extra_len += os_strlen(sm->wps->upc);
	} else if (!os_strcasecmp(filename, UPNP_WPS_SCPD_XML_FILE)) {
		wpa_printf(MSG_DEBUG, "WPS UPnP: HTTP GET for SCPD XML");
		req = GET_SCPD_XML_FILE;
		extra_len = os_strlen(wps_scpd_xml);
	} else {
		/* File not found */
		wpa_printf(MSG_DEBUG, "WPS UPnP: HTTP GET file not found: %s",
			   filename);
		buf = wpabuf_alloc(200);
		if (buf == NULL) {
			http_request_deinit(hreq);
			return;
		}
		wpabuf_put_str(buf,
			       "HTTP/1.1 404 Not Found\r\n"
			       "Connection: close\r\n");

		http_put_date(buf);

		/* terminating empty line */
		wpabuf_put_str(buf, "\r\n");

		goto send_buf;
	}

	buf = wpabuf_alloc(1000 + extra_len);
	if (buf == NULL) {
		http_request_deinit(hreq);
		return;
	}

	wpabuf_put_str(buf,
		       "HTTP/1.1 200 OK\r\n"
		       "Content-Type: text/xml; charset=\"utf-8\"\r\n");
	wpabuf_put_str(buf, "Server: Unspecified, UPnP/1.0, Unspecified\r\n");
	wpabuf_put_str(buf, "Connection: close\r\n");
	wpabuf_put_str(buf, "Content-Length: ");
	/*
	 * We will paste the length in later, leaving some extra whitespace.
	 * HTTP code is supposed to be tolerant of extra whitespace.
	 */
	put_length_here = wpabuf_put(buf, 0);
	wpabuf_put_str(buf, "        \r\n");

	http_put_date(buf);

	/* terminating empty line */
	wpabuf_put_str(buf, "\r\n");

	body_start = wpabuf_put(buf, 0);

	switch (req) {
	case GET_DEVICE_XML_FILE:
		format_wps_device_xml(sm, buf);
		break;
	case GET_SCPD_XML_FILE:
		wpabuf_put_str(buf, wps_scpd_xml);
		break;
	}

	/* Now patch in the content length at the end */
	body_length = (char *) wpabuf_put(buf, 0) - body_start;
	os_snprintf(len_buf, 10, "%d", body_length);
	os_memcpy(put_length_here, len_buf, os_strlen(len_buf));

send_buf:
	http_request_send_and_deinit(hreq, buf);
}


static enum http_reply_code
web_process_get_device_info(struct upnp_wps_device_sm *sm,
			    struct wpabuf **reply, const char **replyname)
{
	static const char *name = "NewDeviceInfo";

	wpa_printf(MSG_DEBUG, "WPS UPnP: GetDeviceInfo");
	if (sm->ctx->rx_req_get_device_info == NULL)
		return HTTP_INTERNAL_SERVER_ERROR;
	*reply = sm->ctx->rx_req_get_device_info(sm->priv, &sm->peer);
	if (*reply == NULL) {
		wpa_printf(MSG_INFO, "WPS UPnP: Failed to get DeviceInfo");
		return HTTP_INTERNAL_SERVER_ERROR;
	}
	*replyname = name;
	return HTTP_OK;
}


static enum http_reply_code
web_process_put_message(struct upnp_wps_device_sm *sm, char *data,
			struct wpabuf **reply, const char **replyname)
{
	struct wpabuf *msg;
	static const char *name = "NewOutMessage";
	enum http_reply_code ret;

	/*
	 * PutMessage is used by external UPnP-based Registrar to perform WPS
	 * operation with the access point itself; as compared with
	 * PutWLANResponse which is for proxying.
	 */
	wpa_printf(MSG_DEBUG, "WPS UPnP: PutMessage");
	if (sm->ctx->rx_req_put_message == NULL)
		return HTTP_INTERNAL_SERVER_ERROR;
	msg = xml_get_base64_item(data, "NewInMessage", &ret);
	if (msg == NULL)
		return ret;
	*reply = sm->ctx->rx_req_put_message(sm->priv, &sm->peer, msg);
	wpabuf_free(msg);
	if (*reply == NULL)
		return HTTP_INTERNAL_SERVER_ERROR;
	*replyname = name;
	return HTTP_OK;
}


static enum http_reply_code
web_process_put_wlan_response(struct upnp_wps_device_sm *sm, char *data,
			      struct wpabuf **reply, const char **replyname)
{
	struct wpabuf *msg;
	enum http_reply_code ret;
	u8 macaddr[ETH_ALEN];
	int ev_type;
	int type;
	char *val;

	/*
	 * External UPnP-based Registrar is passing us a message to be proxied
	 * over to a Wi-Fi -based client of ours.
	 */

	wpa_printf(MSG_DEBUG, "WPS UPnP: PutWLANResponse");
	msg = xml_get_base64_item(data, "NewMessage", &ret);
	if (msg == NULL)
		return ret;
	val = xml_get_first_item(data, "NewWLANEventType");
	if (val == NULL) {
		wpabuf_free(msg);
		return UPNP_ARG_VALUE_INVALID;
	}
	ev_type = atol(val);
	os_free(val);
	val = xml_get_first_item(data, "NewWLANEventMAC");
	if (val == NULL || hwaddr_aton(val, macaddr)) {
		wpabuf_free(msg);
		os_free(val);
		return UPNP_ARG_VALUE_INVALID;
	}
	os_free(val);
	if (ev_type == UPNP_WPS_WLANEVENT_TYPE_EAP) {
		struct wps_parse_attr attr;
		if (wps_parse_msg(msg, &attr) < 0 ||
		    attr.msg_type == NULL)
			type = -1;
		else
			type = *attr.msg_type;
		wpa_printf(MSG_DEBUG, "WPS UPnP: Message Type %d", type);
	} else
		type = -1;
	if (!sm->ctx->rx_req_put_wlan_response ||
	    sm->ctx->rx_req_put_wlan_response(sm->priv, ev_type, macaddr, msg,
					      type)) {
		wpa_printf(MSG_INFO, "WPS UPnP: Fail: sm->ctx->"
			   "rx_req_put_wlan_response");
		wpabuf_free(msg);
		return HTTP_INTERNAL_SERVER_ERROR;
	}
	wpabuf_free(msg);
	*replyname = NULL;
	*reply = NULL;
	return HTTP_OK;
}


static enum http_reply_code
web_process_set_selected_registrar(struct upnp_wps_device_sm *sm, char *data,
				   struct wpabuf **reply,
				   const char **replyname)
{
	struct wpabuf *msg;
	enum http_reply_code ret;

	wpa_printf(MSG_DEBUG, "WPS UPnP: SetSelectedRegistrar");
	msg = xml_get_base64_item(data, "NewMessage", &ret);
	if (msg == NULL)
		return ret;
	if (!sm->ctx->rx_req_set_selected_registrar ||
	    sm->ctx->rx_req_set_selected_registrar(sm->priv, msg)) {
		wpabuf_free(msg);
		return HTTP_INTERNAL_SERVER_ERROR;
	}
	wpabuf_free(msg);
	*replyname = NULL;
	*reply = NULL;
	return HTTP_OK;
}


static const char *soap_prefix =
	"<?xml version=\"1.0\"?>\n"
	"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
	"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\n"
	"<s:Body>\n";
static const char *soap_postfix =
	"</s:Body>\n</s:Envelope>\n";

static const char *soap_error_prefix =
	"<s:Fault>\n"
	"<faultcode>s:Client</faultcode>\n"
	"<faultstring>UPnPError</faultstring>\n"
	"<detail>\n"
	"<UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\">\n";
static const char *soap_error_postfix =
	"<errorDescription>Error</errorDescription>\n"
	"</UPnPError>\n"
	"</detail>\n"
	"</s:Fault>\n";

static void web_connection_send_reply(struct http_request *req,
				      enum http_reply_code ret,
				      const char *action, int action_len,
				      const struct wpabuf *reply,
				      const char *replyname)
{
	struct wpabuf *buf;
	char *replydata;
	char *put_length_here = NULL;
	char *body_start = NULL;

	if (reply) {
		size_t len;
		replydata = (char *) base64_encode(wpabuf_head(reply),
						   wpabuf_len(reply), &len);
	} else
		replydata = NULL;

	/* Parameters of the response:
	 *      action(action_len) -- action we are responding to
	 *      replyname -- a name we need for the reply
	 *      replydata -- NULL or null-terminated string
	 */
	buf = wpabuf_alloc(1000 + (replydata ? os_strlen(replydata) : 0U) +
			   (action_len > 0 ? action_len * 2 : 0));
	if (buf == NULL) {
		wpa_printf(MSG_INFO, "WPS UPnP: Cannot allocate reply to "
			   "POST");
		os_free(replydata);
		http_request_deinit(req);
		return;
	}

	/*
	 * Assuming we will be successful, put in the output header first.
	 * Note: we do not keep connections alive (and httpread does
	 * not support it)... therefore we must have Connection: close.
	 */
	if (ret == HTTP_OK) {
		wpabuf_put_str(buf,
			       "HTTP/1.1 200 OK\r\n"
			       "Content-Type: text/xml; "
			       "charset=\"utf-8\"\r\n");
	} else {
		wpabuf_printf(buf, "HTTP/1.1 %d Error\r\n", ret);
	}
	wpabuf_put_str(buf, http_connection_close);

	wpabuf_put_str(buf, "Content-Length: ");
	/*
	 * We will paste the length in later, leaving some extra whitespace.
	 * HTTP code is supposed to be tolerant of extra whitespace.
	 */
	put_length_here = wpabuf_put(buf, 0);
	wpabuf_put_str(buf, "        \r\n");

	http_put_date(buf);

	/* terminating empty line */
	wpabuf_put_str(buf, "\r\n");

	body_start = wpabuf_put(buf, 0);

	if (ret == HTTP_OK) {
		wpabuf_put_str(buf, soap_prefix);
		wpabuf_put_str(buf, "<u:");
		wpabuf_put_data(buf, action, action_len);
		wpabuf_put_str(buf, "Response xmlns:u=\"");
		wpabuf_put_str(buf, urn_wfawlanconfig);
		wpabuf_put_str(buf, "\">\n");
		if (replydata && replyname) {
			/* TODO: might possibly need to escape part of reply
			 * data? ...
			 * probably not, unlikely to have ampersand(&) or left
			 * angle bracket (<) in it...
			 */
			wpabuf_printf(buf, "<%s>", replyname);
			wpabuf_put_str(buf, replydata);
			wpabuf_printf(buf, "</%s>\n", replyname);
		}
		wpabuf_put_str(buf, "</u:");
		wpabuf_put_data(buf, action, action_len);
		wpabuf_put_str(buf, "Response>\n");
		wpabuf_put_str(buf, soap_postfix);
	} else {
		/* Error case */
		wpabuf_put_str(buf, soap_prefix);
		wpabuf_put_str(buf, soap_error_prefix);
		wpabuf_printf(buf, "<errorCode>%d</errorCode>\n", ret);
		wpabuf_put_str(buf, soap_error_postfix);
		wpabuf_put_str(buf, soap_postfix);
	}
	os_free(replydata);

	/* Now patch in the content length at the end */
	if (body_start && put_length_here) {
		int body_length = (char *) wpabuf_put(buf, 0) - body_start;
		char len_buf[10];
		os_snprintf(len_buf, sizeof(len_buf), "%d", body_length);
		os_memcpy(put_length_here, len_buf, os_strlen(len_buf));
	}

	http_request_send_and_deinit(req, buf);
}


static const char * web_get_action(struct http_request *req,
				   const char *filename, size_t *action_len)
{
	const char *match;
	int match_len;
	char *b;
	char *action;

	*action_len = 0;
	if (os_strcasecmp(filename, UPNP_WPS_DEVICE_CONTROL_FILE)) {
		wpa_printf(MSG_INFO, "WPS UPnP: Invalid POST filename %s",
			   filename);
		return NULL;
	}
	/* The SOAPAction line of the header tells us what we want to do */
	b = http_request_get_hdr_line(req, "SOAPAction:");
	if (b == NULL)
		return NULL;
	if (*b == '"')
		b++;
	else
		return NULL;
	match = urn_wfawlanconfig;
	match_len = os_strlen(urn_wfawlanconfig) - 1;
	if (os_strncasecmp(b, match, match_len))
		return NULL;
	b += match_len;
	/* skip over version */
	while (isgraph(*b) && *b != '#')
		b++;
	if (*b != '#')
		return NULL;
	b++;
	/* Following the sharp(#) should be the action and a double quote */
	action = b;
	while (isgraph(*b) && *b != '"')
		b++;
	if (*b != '"')
		return NULL;
	*action_len = b - action;
	return action;
}


/* Given that we have received a header w/ POST, act upon it
 *
 * Format of POST (case-insensitive):
 *
 * First line must be:
 *      POST /<file> HTTP/1.1
 * Since we don't do anything fancy we just ignore other lines.
 *
 * Our response (if no error) which includes only required lines is:
 * HTTP/1.1 200 OK
 * Connection: close
 * Content-Type: text/xml
 * Date: <rfc1123-date>
 *
 * Header lines must end with \r\n
 * Per RFC 2616, content-length: is not required but connection:close
 * would appear to be required (given that we will be closing it!).
 */
static void web_connection_parse_post(struct upnp_wps_device_sm *sm,
				      struct http_request *req,
				      const char *filename)
{
	enum http_reply_code ret;
	char *data = http_request_get_data(req); /* body of http msg */
	const char *action;
	size_t action_len;
	const char *replyname = NULL; /* argument name for the reply */
	struct wpabuf *reply = NULL; /* data for the reply */

	ret = UPNP_INVALID_ACTION;
	action = web_get_action(req, filename, &action_len);
	if (action == NULL)
		goto bad;

	/*
	 * There are quite a few possible actions. Although we appear to
	 * support them all here, not all of them are necessarily supported by
	 * callbacks at higher levels.
	 */
	if (!os_strncasecmp("GetDeviceInfo", action, action_len))
		ret = web_process_get_device_info(sm, &reply, &replyname);
	else if (!os_strncasecmp("PutMessage", action, action_len))
		ret = web_process_put_message(sm, data, &reply, &replyname);
	else if (!os_strncasecmp("PutWLANResponse", action, action_len))
		ret = web_process_put_wlan_response(sm, data, &reply,
						    &replyname);
	else if (!os_strncasecmp("SetSelectedRegistrar", action, action_len))
		ret = web_process_set_selected_registrar(sm, data, &reply,
							 &replyname);
	else
		wpa_printf(MSG_INFO, "WPS UPnP: Unknown POST type");

bad:
	if (ret != HTTP_OK)
		wpa_printf(MSG_INFO, "WPS UPnP: POST failure ret=%d", ret);
	web_connection_send_reply(req, ret, action, action_len, reply,
				  replyname);
	wpabuf_free(reply);
}


/* Given that we have received a header w/ SUBSCRIBE, act upon it
 *
 * Format of SUBSCRIBE (case-insensitive):
 *
 * First line must be:
 *      SUBSCRIBE /wps_event HTTP/1.1
 *
 * Our response (if no error) which includes only required lines is:
 * HTTP/1.1 200 OK
 * Server: xx, UPnP/1.0, xx
 * SID: uuid:xxxxxxxxx
 * Timeout: Second-<n>
 * Content-Length: 0
 * Date: xxxx
 *
 * Header lines must end with \r\n
 * Per RFC 2616, content-length: is not required but connection:close
 * would appear to be required (given that we will be closing it!).
 */
static void web_connection_parse_subscribe(struct upnp_wps_device_sm *sm,
					   struct http_request *req,
					   const char *filename)
{
	struct wpabuf *buf;
	char *b;
	char *hdr = http_request_get_hdr(req);
	char *h;
	char *match;
	int match_len;
	char *end;
	int len;
	int got_nt = 0;
	u8 uuid[UUID_LEN];
	int got_uuid = 0;
	char *callback_urls = NULL;
	struct subscription *s = NULL;
	enum http_reply_code ret = HTTP_INTERNAL_SERVER_ERROR;

	buf = wpabuf_alloc(1000);
	if (buf == NULL) {
		http_request_deinit(req);
		return;
	}

	/* Parse/validate headers */
	h = hdr;
	/* First line: SUBSCRIBE /wps_event HTTP/1.1
	 * has already been parsed.
	 */
	if (os_strcasecmp(filename, UPNP_WPS_DEVICE_EVENT_FILE) != 0) {
		ret = HTTP_PRECONDITION_FAILED;
		goto error;
	}
	wpa_printf(MSG_DEBUG, "WPS UPnP: HTTP SUBSCRIBE for event");
	end = os_strchr(h, '\n');

	for (; end != NULL; h = end + 1) {
		/* Option line by option line */
		h = end + 1;
		end = os_strchr(h, '\n');
		if (end == NULL)
			break; /* no unterminated lines allowed */

		/* NT assures that it is our type of subscription;
		 * not used for a renewl.
		 **/
		match = "NT:";
		match_len = os_strlen(match);
		if (os_strncasecmp(h, match, match_len) == 0) {
			h += match_len;
			while (*h == ' ' || *h == '\t')
				h++;
			match = "upnp:event";
			match_len = os_strlen(match);
			if (os_strncasecmp(h, match, match_len) != 0) {
				ret = HTTP_BAD_REQUEST;
				goto error;
			}
			got_nt = 1;
			continue;
		}
		/* HOST should refer to us */
#if 0
		match = "HOST:";
		match_len = os_strlen(match);
		if (os_strncasecmp(h, match, match_len) == 0) {
			h += match_len;
			while (*h == ' ' || *h == '\t')
				h++;
			.....
		}
#endif
		/* CALLBACK gives one or more URLs for NOTIFYs
		 * to be sent as a result of the subscription.
		 * Each URL is enclosed in angle brackets.
		 */
		match = "CALLBACK:";
		match_len = os_strlen(match);
		if (os_strncasecmp(h, match, match_len) == 0) {
			h += match_len;
			while (*h == ' ' || *h == '\t')
				h++;
			len = end - h;
			os_free(callback_urls);
			callback_urls = os_malloc(len + 1);
			if (callback_urls == NULL) {
				ret = HTTP_INTERNAL_SERVER_ERROR;
				goto error;
			}
			os_memcpy(callback_urls, h, len);
			callback_urls[len] = 0;
			continue;
		}
		/* SID is only for renewal */
		match = "SID:";
		match_len = os_strlen(match);
		if (os_strncasecmp(h, match, match_len) == 0) {
			h += match_len;
			while (*h == ' ' || *h == '\t')
				h++;
			match = "uuid:";
			match_len = os_strlen(match);
			if (os_strncasecmp(h, match, match_len) != 0) {
				ret = HTTP_BAD_REQUEST;
				goto error;
			}
			h += match_len;
			while (*h == ' ' || *h == '\t')
				h++;
			if (uuid_str2bin(h, uuid)) {
				ret = HTTP_BAD_REQUEST;
				goto error;
			}
			got_uuid = 1;
			continue;
		}
		/* TIMEOUT is requested timeout, but apparently we can
		 * just ignore this.
		 */
	}

	if (got_uuid) {
		/* renewal */
		if (callback_urls) {
			ret = HTTP_BAD_REQUEST;
			goto error;
		}
		s = subscription_renew(sm, uuid);
		if (s == NULL) {
			ret = HTTP_PRECONDITION_FAILED;
			goto error;
		}
	} else if (callback_urls) {
		if (!got_nt) {
			ret = HTTP_PRECONDITION_FAILED;
			goto error;
		}
		s = subscription_start(sm, callback_urls);
		if (s == NULL) {
			ret = HTTP_INTERNAL_SERVER_ERROR;
			goto error;
		}
	} else {
		ret = HTTP_PRECONDITION_FAILED;
		goto error;
	}

	/* success */
	http_put_reply_code(buf, HTTP_OK);
	wpabuf_put_str(buf, http_server_hdr);
	wpabuf_put_str(buf, http_connection_close);
	wpabuf_put_str(buf, "Content-Length: 0\r\n");
	wpabuf_put_str(buf, "SID: uuid:");
	/* subscription id */
	b = wpabuf_put(buf, 0);
	uuid_bin2str(s->uuid, b, 80);
	wpabuf_put(buf, os_strlen(b));
	wpabuf_put_str(buf, "\r\n");
	wpabuf_printf(buf, "Timeout: Second-%d\r\n", UPNP_SUBSCRIBE_SEC);
	http_put_date(buf);
	/* And empty line to terminate header: */
	wpabuf_put_str(buf, "\r\n");

	os_free(callback_urls);
	http_request_send_and_deinit(req, buf);
	return;

error:
	/* Per UPnP spec:
	* Errors
	* Incompatible headers
	*   400 Bad Request. If SID header and one of NT or CALLBACK headers
	*     are present, the publisher must respond with HTTP error
	*     400 Bad Request.
	* Missing or invalid CALLBACK
	*   412 Precondition Failed. If CALLBACK header is missing or does not
	*     contain a valid HTTP URL, the publisher must respond with HTTP
	*     error 412 Precondition Failed.
	* Invalid NT
	*   412 Precondition Failed. If NT header does not equal upnp:event,
	*     the publisher must respond with HTTP error 412 Precondition
	*     Failed.
	* [For resubscription, use 412 if unknown uuid].
	* Unable to accept subscription
	*   5xx. If a publisher is not able to accept a subscription (such as
	*     due to insufficient resources), it must respond with a
	*     HTTP 500-series error code.
	*   599 Too many subscriptions (not a standard HTTP error)
	*/
	http_put_empty(buf, ret);
	http_request_send_and_deinit(req, buf);
	os_free(callback_urls);
}


/* Given that we have received a header w/ UNSUBSCRIBE, act upon it
 *
 * Format of UNSUBSCRIBE (case-insensitive):
 *
 * First line must be:
 *      UNSUBSCRIBE /wps_event HTTP/1.1
 *
 * Our response (if no error) which includes only required lines is:
 * HTTP/1.1 200 OK
 * Content-Length: 0
 *
 * Header lines must end with \r\n
 * Per RFC 2616, content-length: is not required but connection:close
 * would appear to be required (given that we will be closing it!).
 */
static void web_connection_parse_unsubscribe(struct upnp_wps_device_sm *sm,
					     struct http_request *req,
					     const char *filename)
{
	struct wpabuf *buf;
	char *hdr = http_request_get_hdr(req);
	char *h;
	char *match;
	int match_len;
	char *end;
	u8 uuid[UUID_LEN];
	int got_uuid = 0;
	struct subscription *s = NULL;
	enum http_reply_code ret = HTTP_INTERNAL_SERVER_ERROR;

	/* Parse/validate headers */
	h = hdr;
	/* First line: UNSUBSCRIBE /wps_event HTTP/1.1
	 * has already been parsed.
	 */
	if (os_strcasecmp(filename, UPNP_WPS_DEVICE_EVENT_FILE) != 0) {
		ret = HTTP_PRECONDITION_FAILED;
		goto send_msg;
	}
	wpa_printf(MSG_DEBUG, "WPS UPnP: HTTP UNSUBSCRIBE for event");
	end = os_strchr(h, '\n');

	for (; end != NULL; h = end + 1) {
		/* Option line by option line */
		h = end + 1;
		end = os_strchr(h, '\n');
		if (end == NULL)
			break; /* no unterminated lines allowed */

		/* HOST should refer to us */
#if 0
		match = "HOST:";
		match_len = os_strlen(match);
		if (os_strncasecmp(h, match, match_len) == 0) {
			h += match_len;
			while (*h == ' ' || *h == '\t')
				h++;
			.....
		}
#endif
		/* SID is only for renewal */
		match = "SID:";
		match_len = os_strlen(match);
		if (os_strncasecmp(h, match, match_len) == 0) {
			h += match_len;
			while (*h == ' ' || *h == '\t')
				h++;
			match = "uuid:";
			match_len = os_strlen(match);
			if (os_strncasecmp(h, match, match_len) != 0) {
				ret = HTTP_BAD_REQUEST;
				goto send_msg;
			}
			h += match_len;
			while (*h == ' ' || *h == '\t')
				h++;
			if (uuid_str2bin(h, uuid)) {
				ret = HTTP_BAD_REQUEST;
				goto send_msg;
			}
			got_uuid = 1;
			continue;
		}
	}

	if (got_uuid) {
		s = subscription_find(sm, uuid);
		if (s) {
			wpa_printf(MSG_DEBUG, "WPS UPnP: Unsubscribing %p %s",
				   s,
				   (s && s->addr_list &&
				    s->addr_list->domain_and_port) ?
				   s->addr_list->domain_and_port : "-null-");
			subscription_unlink(s);
			subscription_destroy(s);
		}
	} else {
		wpa_printf(MSG_INFO, "WPS UPnP: Unsubscribe fails (not "
			   "found)");
		ret = HTTP_PRECONDITION_FAILED;
		goto send_msg;
	}

	ret = HTTP_OK;

send_msg:
	buf = wpabuf_alloc(200);
	if (buf == NULL) {
		http_request_deinit(req);
		return;
	}
	http_put_empty(buf, ret);
	http_request_send_and_deinit(req, buf);
}


/* Send error in response to unknown requests */
static void web_connection_unimplemented(struct http_request *req)
{
	struct wpabuf *buf;
	buf = wpabuf_alloc(200);
	if (buf == NULL) {
		http_request_deinit(req);
		return;
	}
	http_put_empty(buf, HTTP_UNIMPLEMENTED);
	http_request_send_and_deinit(req, buf);
}



/* Called when we have gotten an apparently valid http request.
 */
static void web_connection_check_data(void *ctx, struct http_request *req)
{
	struct upnp_wps_device_sm *sm = ctx;
	enum httpread_hdr_type htype = http_request_get_type(req);
	char *filename = http_request_get_uri(req);
	struct sockaddr_in *cli = http_request_get_cli_addr(req);

	if (!filename) {
		wpa_printf(MSG_INFO, "WPS UPnP: Could not get HTTP URI");
		http_request_deinit(req);
		return;
	}
	/* Trim leading slashes from filename */
	while (*filename == '/')
		filename++;

	wpa_printf(MSG_DEBUG, "WPS UPnP: Got HTTP request type %d from %s:%d",
		   htype, inet_ntoa(cli->sin_addr), htons(cli->sin_port));

	switch (htype) {
	case HTTPREAD_HDR_TYPE_GET:
		web_connection_parse_get(sm, req, filename);
		break;
	case HTTPREAD_HDR_TYPE_POST:
		web_connection_parse_post(sm, req, filename);
		break;
	case HTTPREAD_HDR_TYPE_SUBSCRIBE:
		web_connection_parse_subscribe(sm, req, filename);
		break;
	case HTTPREAD_HDR_TYPE_UNSUBSCRIBE:
		web_connection_parse_unsubscribe(sm, req, filename);
		break;

		/* We are not required to support M-POST; just plain
		 * POST is supposed to work, so we only support that.
		 * If for some reason we need to support M-POST, it is
		 * mostly the same as POST, with small differences.
		 */
	default:
		/* Send 501 for anything else */
		web_connection_unimplemented(req);
		break;
	}
}


/*
 * Listening for web connections
 * We have a single TCP listening port, and hand off connections as we get
 * them.
 */

void web_listener_stop(struct upnp_wps_device_sm *sm)
{
	http_server_deinit(sm->web_srv);
	sm->web_srv = NULL;
}


int web_listener_start(struct upnp_wps_device_sm *sm)
{
	struct in_addr addr;
	addr.s_addr = sm->ip_addr;
	sm->web_srv = http_server_init(&addr, -1, web_connection_check_data,
				       sm);
	if (sm->web_srv == NULL) {
		web_listener_stop(sm);
		return -1;
	}
	sm->web_port = http_server_get_port(sm->web_srv);

	return 0;
}
