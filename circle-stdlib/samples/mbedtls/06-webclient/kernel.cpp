//
// kernel.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2015-2020  R. Stange <rsta2@o2online.de>
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
#include "kernel.h"
#include "htmlscanner.h"
#include <circle/net/dnsclient.h>
#include <circle-mbedtls/httpclient.h>
#include <circle/net/ipaddress.h>
#include <circle/machineinfo.h>
#include <circle/string.h>
#include <circle/util.h>
#include <assert.h>

using namespace CircleMbedTLS;

// Network configuration
#define NET_DEVICE_TYPE		NetDeviceTypeEthernet		// or: NetDeviceTypeWLAN

#define USE_DHCP

#ifndef USE_DHCP
static const u8 IPAddress[]      = {192, 168, 0, 250};
static const u8 NetMask[]        = {255, 255, 255, 0};
static const u8 DefaultGateway[] = {192, 168, 0, 1};
static const u8 DNSServer[]      = {192, 168, 0, 1};
#endif

// Server configuration
static const boolean bUseSSL      = TRUE;
static const char Server[]        = "www.raspberrypi.org";
static const char Document[]      = "/documentation/hardware/raspberrypi/revision-codes/README.md";
static const unsigned nDocMaxSize = 200*1024;

CKernel::CKernel (void)
        : CStdlibAppNetwork ("webclient", CSTDLIBAPP_DEFAULT_PARTITION
#ifndef USE_DHCP
	, IPAddress, NetMask, DefaultGateway, DNSServer, NET_DEVICE_TYPE
#else
	, 0, 0, 0, 0, NET_DEVICE_TYPE
#endif
	),
	m_TLSSupport (&mNet)
{
	mActLED.Blink (5);	// show we are alive
}

CStdlibApp::TShutdownMode CKernel::Run (void)
{
	char *pBuffer = new char[nDocMaxSize+1];	// +1 for 0-termination
	if (pBuffer == 0)
	{
		CLogger::Get ()->Write (GetKernelName (), LogPanic, "Cannot allocate document buffer");
	}

	if (GetDocument (pBuffer))
	{
		ParseDocument (pBuffer);
	}

	delete [] pBuffer;
	pBuffer = 0;

	// should not halt here immediately, because TCP disconnect may still be in process
	mScheduler.Sleep (10);

	return ShutdownHalt;
}

boolean CKernel::GetDocument (char *pBuffer)
{
	CIPAddress ForeignIP;
	CDNSClient DNSClient (m_TLSSupport.GetNet ());
	if (!DNSClient.Resolve (Server, &ForeignIP))
	{
		CLogger::Get ()->Write (GetKernelName (), LogError, "Cannot resolve: %s", Server);

		return FALSE;
	}

	CString IPString;
	ForeignIP.Format (&IPString);
	mLogger.Write (GetKernelName (), LogDebug, "Outgoing connection to %s", (const char *) IPString);

	unsigned nLength = nDocMaxSize;

	assert (pBuffer != 0);
	CHTTPClient Client (&m_TLSSupport, ForeignIP,
			    bUseSSL ? HTTPS_PORT : HTTP_PORT, Server, bUseSSL);
	THTTPStatus Status = Client.Get (Document, (u8 *) pBuffer, &nLength);
	if (Status != HTTPOK)
	{
		mLogger.Write (GetKernelName (), LogError, "HTTP request failed (status %u)", Status);

		return FALSE;
	}

	mLogger.Write (GetKernelName (), LogDebug, "%u bytes successfully received", nLength);

	assert (nLength <= nDocMaxSize);
	pBuffer[nLength] = '\0';

	return TRUE;
}

boolean CKernel::ParseDocument (const char *pDocument)
{
	CMachineInfo MachineInfo;
	u32 nBoardRevision = MachineInfo.GetRevisionRaw () & 0xFFFFFF;		// mask out warranty bits
	mLogger.Write (GetKernelName (), LogNotice, "Revision of this board is %04x", nBoardRevision);

	assert (pDocument != 0);
	CHtmlScanner Scanner (pDocument);

	unsigned nState = 0;

	CString ItemText;
	THtmlItem Item = Scanner.GetFirstItem (ItemText);
	while (Item < HtmlItemEOF)
	{
		switch (nState)
		{
		case 0:		// find header above table
			if (   Item == HtmlItemText
			    && (      !(nBoardRevision & (1 << 23))
			           && ItemText.Compare ("Old-style revision codes") == 0
			        ||    (nBoardRevision & (1 << 23))
			           && ItemText.Compare ("New-style revision codes in use:") == 0))
			{
				nState = 1;
			}
			break;

		case 1:		// find a HTML table
			static const char Pattern[] = "<table";
			if (   Item == HtmlItemTag
			    && strncmp (ItemText, Pattern, sizeof Pattern-1) == 0)
			{
				nState = 2;
			}
			break;

		case 2:		// is the first column header == "Code"?
			if (Item == HtmlItemText)
			{
				if (ItemText.Compare ("Code") == 0)
				{
					// yes, start displaying the list
					static const char Msg[] = "\n";
					mScreen.Write (Msg, sizeof Msg-1);
					nState = 3;
				}
				else
				{
					// no, find next table
					nState = 0;
					break;
				}
			}
			else
			{
				// ignore other items
				break;
			}
			// fall through

		case 3:		// first column
		case 4:		// other column
			if (Item == HtmlItemTag)
			{
				if (ItemText.Compare ("</table>") == 0)
				{
					// table is finished, leave
					return TRUE;
				}
				else if (ItemText.Compare ("</tr>") == 0)
				{
					// reset highlighting and output newline
					static const char Msg[] = "\x1b[0m\n";
					mScreen.Write (Msg, sizeof Msg-1);
					nState = 3;
				}
			}
			else if (Item == HtmlItemText)
			{
				if (nState == 3)
				{
					// check for valid revision number and compare it with ours
					char *pEnd;
					unsigned long ulRevision = strtoul (ItemText, &pEnd, 16);
					if (   (    pEnd == 0
						|| *pEnd == '\0')
					    && ulRevision == nBoardRevision)
					{
						// highlight this line, because it is ours
						static const char Msg[] = "\x1b[1m";
						mScreen.Write (Msg, sizeof Msg-1);
					}
				}
				else
				{
					static const char Msg[] = ", ";
					mScreen.Write (Msg, sizeof Msg-1);
				}

				ItemText.Replace ("&#160;", " ");

				mScreen.Write (ItemText, ItemText.GetLength ());

				nState = 4;
			}
			break;

		default:
			assert (0);
			break;
		}

		Item = Scanner.GetNextItem (ItemText);
	}

	if (Item != HtmlItemEOF)
	{
		mLogger.Write (GetKernelName (), LogError, "HTML scanner error");

		return FALSE;
	}

	mLogger.Write (GetKernelName (), LogError, "Revision table not found");

	return FALSE;
}
