//
// mqttsubscriber.h
//
// Copyright (C) 2018  R. Stange <rsta2@o2online.de>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#ifndef _mqttsubscriber_h
#define _mqttsubscriber_h

#include <circle-mbedtls/mqttclient.h>
#include <circle-mbedtls/tlssimplesupport.h>
#include <circle/types.h>

namespace CircleMbedTLS
{

class CMQTTSubscriber : public CMQTTClient
{
public:
	CMQTTSubscriber (CTLSSimpleSupport *pTLSSupport);

	void OnConnect (boolean bSessionPresent);

	void OnDisconnect (TMQTTDisconnectReason Reason);

	void OnMessage (const char *pTopic,
			const u8 *pPayload, size_t nPayloadLength,
			boolean bRetain);

	void OnLoop (void);

private:
	boolean m_bTimerRunning;
	unsigned m_nConnectTime;
};

}

#endif
