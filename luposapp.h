//
// luposapp.h
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
#ifndef _luposapp_h
#define _luposapp_h

#include <circle/actled.h>
#include <circle/string.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/nulldevice.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/screen.h>
#include <circle/serial.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/usb/usbhcidevice.h>
#include <SDCard/emmc.h>
#include <circle/input/console.h>
#include <circle/sched/scheduler.h>
#include <circle/net/netsubsystem.h>
#include <wlan/bcm4343.h>
#include <wlan/hostap/wpa_supplicant/wpasupplicant.h>

#include <circle_glue.h>
#include <string.h>

#define CSTDLIBAPP_LEGACY_DEFAULT_PARTITION "emmc1-1"
#define CSTDLIBAPP_DEFAULT_PARTITION "SD:"
#define usbCSTDLIBAPP_DEFAULT_PARTITION "USB:"

#define CSTDLIBAPP_WLAN_FIRMWARE_PATH   CSTDLIBAPP_DEFAULT_PARTITION "/firmware/"
#define CSTDLIBAPP_WLAN_CONFIG_FILE     CSTDLIBAPP_DEFAULT_PARTITION "/wpa_supplicant.conf"

class CLuposApp
{
 public:
  enum TShutdownMode
  {
    ShutdownNone,
    ShutdownHalt,
    ShutdownReboot
  };

  CLuposApp (const char *kernel,
	   const char *pPartitionName = CSTDLIBAPP_DEFAULT_PARTITION,
	   const u8 *pIPAddress      = 0,       // use DHCP if pIPAddress == 0
	   const u8 *pNetMask        = 0,
	   const u8 *pDefaultGateway = 0,
	   const u8 *pDNSServer      = 0,
	   TNetDeviceType DeviceType = NetDeviceTypeEthernet) 
    : FromKernel (kernel),
    mScreen (mOptions.GetWidth (), mOptions.GetHeight (),
	     mOptions.GetColor(), mOptions.GetBackColor()),
    mTimer (&mInterrupt),
    mLogger (mOptions.GetLogLevel (), &mTimer),
    mpPartitionName (pPartitionName),
    mUSBHCI (&mInterrupt, &mTimer, TRUE),
    mEMMC (&mInterrupt, &mTimer, &mActLED),
#if !defined(__aarch64__) || !defined(LEAVE_QEMU_ON_HALT)
    mConsole (0, TRUE),
#else
    mConsole (&mScreen),
#endif
    mDeviceType (DeviceType),
    mWLAN (CSTDLIBAPP_WLAN_FIRMWARE_PATH),
    mNet(pIPAddress, pNetMask, pDefaultGateway, pDNSServer, DEFAULT_HOSTNAME, DeviceType),
    mWPASupplicant (CSTDLIBAPP_WLAN_CONFIG_FILE),
    mShutdownMode (ShutdownNone)
    {
    }

  virtual ~CLuposApp (void)
    {
    }

  virtual bool Initialize (void)
  {
    if (!mInterrupt.Initialize ())
      {
	return false;
      }
    if (!mScreen.Initialize ())
      {
	return false;
      }
    mScreen.Write(FromKernel, strlen(FromKernel));
    mScreen.Write("\r", 1);
	  
    if (!mSerial.Initialize (115200))
      {
	return false;
      }
	  
    CDevice *pTarget =
      mDeviceNameService.GetDevice (mOptions.GetLogDevice (), false);
    if (pTarget == 0)
      {
	pTarget = &mScreen;
      }
	  
    if (!mLogger.Initialize (pTarget))
      {
	return false;
      }
	  
    if (!mTimer.Initialize ())
      {
	return false;
      }
        
    if (!mEMMC.Initialize ())
      {
        mLogger.Write (GetKernelName (), LogNotice,
          "EMMS/SD init failed");
	//return false;
      }

#if !defined(__aarch64__) || !defined(LEAVE_QEMU_ON_HALT)
    // The USB driver is not supported under 64-bit QEMU, so
    // the initialization must be skipped in this case, or an
    // exit happens here under 64-bit QEMU.
    if (!mUSBHCI.Initialize ())
      {
	return false;
      }
#endif
	  
    //char const *partitionName = mpPartitionName;
    char const *partitionName = mOptions.GetSysDevice ();
    mLogger.Write (GetKernelName (), LogNotice, 
	"System device/partition: %s", partitionName);
 
/*
    // Recognize the old default partion name
    if (strcmp(partitionName, CSTDLIBAPP_LEGACY_DEFAULT_PARTITION) == 0)
      {
	partitionName = CSTDLIBAPP_DEFAULT_PARTITION;
      }
*/	  
    if (f_mount (&mFileSystem, partitionName, 1) != FR_OK)
      {
	mLogger.Write (GetKernelName (), LogError,
		       "Cannot mount partition: %s", partitionName);
	      
	return false;
      }
    if (f_chdrive(partitionName) != FR_OK)
      {
        mLogger.Write (GetKernelName (), LogError,
                       "Cannot switch to drive: %s", partitionName);

        return false;
      }
    mpPartitionName = partitionName;

	  
    if (!mConsole.Initialize ())
      {
	return false;
      }
	  
    // Initialize newlib stdio with a reference to Circle's file system and console
    CGlueStdioInit (mFileSystem, mConsole);
	  
    mLogger.Write (GetKernelName (), LogNotice, "Compile time: " __DATE__ " " __TIME__);
    mLogger.Write (GetKernelName (), LogNotice, "%dbpp framebuffer configured.", DEPTH);

    mNetDevice = mOptions.GetNetDevice();

    if(strcmp(mNetDevice, "none")) {
      if(!strncmp(mNetDevice, "wlan", 5)) {
	mDeviceType = NetDeviceTypeWLAN;
      }
	  
      if (mDeviceType == NetDeviceTypeWLAN)
	{
	  if (!mWLAN.Initialize ())
	    {
	      return false;
	    }
	}
      
      if (!mNet.Initialize (false))
	{
	  return false;
	}
      
      if (mDeviceType == NetDeviceTypeWLAN)
	{
	  if (!mWPASupplicant.Initialize ())
	    {
	      return false;
	    }
	}
      
      // wait up to 10 sec for network init
      unsigned nTime = mTimer.GetTime();	
      while (!mNet.IsRunning () &&
	     ((mTimer.GetTime() - nTime) < 30))
	{
	  mScheduler.Yield ();
	}
    }
    return true;
  }
	
  virtual void Cleanup (void)
  {
    f_mount(0, "", 0);
  }

  TShutdownMode Run (void);

  const char *GetKernelName(void) const
  {
    return FromKernel;
  }

 private:
  char const *FromKernel;

 public:
  CActLED            mActLED;
  CKernelOptions     mOptions;
  CDeviceNameService mDeviceNameService;
  CNullDevice        mNullDevice;
  CExceptionHandler  mExceptionHandler;
  CInterruptSystem   mInterrupt;
  CScreenDevice      mScreen;
  CSerialDevice      mSerial;
  CTimer             mTimer;
  CLogger            mLogger;
  char const         *mpPartitionName;
  CUSBHCIDevice      mUSBHCI;
  CEMMCDevice        mEMMC;
  FATFS              mFileSystem;
  CConsole           mConsole;
  CScheduler         mScheduler;
  char const         *mNetDevice;
  TNetDeviceType     mDeviceType;
  CBcm4343Device     mWLAN;
  CNetSubSystem      mNet;
  CWPASupplicant     mWPASupplicant;
  volatile TShutdownMode mShutdownMode;
};

#endif

