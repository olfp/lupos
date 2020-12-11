//
/// kernel.cpp
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
#include <circle/net/ntpdaemon.h>

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <circle/startup.h>

#include "luaint.h"

static const char NTPServer[]    = "pool.ntp.org";
static const int nTimeZone       = 1*60;                // minutes diff to UTC

CKernel *CKernel::s_pThis = 0;

CKernel::CKernel (void) 
  : CLuposApp("LUPOS")
{
	assert (s_pThis == 0);
	s_pThis = this;
	mActLED.Blink (5);// show we are alive
}

CLuposApp::TShutdownMode CKernel::Run (void)
{
  mLogger.Write (GetKernelName (), LogNotice, "Starting ...");
  
  mTimer.SetTimeZone (nTimeZone);
  if(mNet.IsRunning()) {
    new CNTPDaemon (NTPServer, &mNet);
    mScheduler.Sleep (4);
  } else {
    mLogger.Write (GetKernelName (), LogNotice, "No network, time not set.");
  }
  
  int res = lua_interface(CKernel::Get());
  mLogger.Write (GetKernelName (), LogNotice, "\nLUPOS quit %d", res);
  
  return ShutdownHalt;
}

CKernel *CKernel::Get (void)
{
  assert (s_pThis != 0);
  return s_pThis;
}

