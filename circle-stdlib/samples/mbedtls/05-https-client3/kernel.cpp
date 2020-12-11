//
// kernel.cpp
//
// Copyright (C) 2018-2020  R. Stange <rsta2@o2online.de>
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
#include <circle-mbedtls/tlssimpleclientsocket.h>
#include <circle/net/in.h>
#include <circle/types.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <mbedtls/error.h>

#define NET_DEVICE_TYPE		NetDeviceTypeEthernet		// or: NetDeviceTypeWLAN

#define SERVER_NAME	"elinux.org"
#define SERVER_PORT	"443"
#define SERVER_PAGE	"/Main_Page"

using namespace CircleMbedTLS;

CKernel::CKernel (void)
        : CStdlibAppNetwork ("https-client3", CSTDLIBAPP_DEFAULT_PARTITION,
			     0, 0, 0, 0, NET_DEVICE_TYPE),
          m_TLSSupport (&mNet)
{
	mActLED.Blink (5);	// show we are alive
}

CStdlibApp::TShutdownMode CKernel::Run (void)
{
	//m_TLSSupport.SetDebugThreshold (1);

	int nResult = GetDocument ();

	printf ("GetDocument() returned %d\n", nResult);

	if (nResult != 0)
	{
		PrintError (nResult);
	}

	mScheduler.Sleep (10);		// allow network subsystem to close connections

	return ShutdownHalt;
}

int CKernel::GetDocument (void)
{
	CTLSSimpleClientSocket Socket (&m_TLSSupport, IPPROTO_TCP);

	int nResult = Socket.AddCertificatePath ("/");
	if (nResult <= 0)
	{
		printf ("Invalid or no certificate (%d)\n", nResult);

		return nResult == 0 ? -1 : nResult;
	}

	nResult = Socket.Setup (SERVER_NAME);
	if (nResult != 0)
	{
		printf ("SSL setup failed (%d)\n", nResult);

		return nResult;
	}

	nResult = Socket.Connect (SERVER_NAME, SERVER_PORT);
	if (nResult != 0)
	{
		printf ("Connect failed (%d)\n", nResult);

		return nResult;
	}

	char Buffer[1024];
	int nLength = sprintf (Buffer, "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n",
			       SERVER_PAGE, SERVER_NAME);

	nResult = Socket.Send (Buffer, nLength, 0);
	if (nResult < 0)
	{
		printf ("Send failed (%d)\n", nResult);

		return nResult;
	}

	int nBytesReceived = 0;
	while ((nResult = Socket.Receive (Buffer, sizeof Buffer-1, 0)) > 0)
	{
		assert (nResult < (int) sizeof Buffer);
		Buffer[nResult] = '\0';

		if (nBytesReceived < 200)
		{
			fputs (Buffer, stdout);
		}
		else
		{
			putchar ('.');
		}

		nBytesReceived += nResult;
	}

	putchar ('\n');

	return nResult != -1 ? nResult : 0;
}

void CKernel::PrintError (int nErrorCode)
{
	char Buffer[200];
	mbedtls_strerror (nErrorCode, Buffer, sizeof Buffer);

	printf ("%s\n", Buffer);
}
