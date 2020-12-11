Change Log
==========

> If you read this file in an editor you should switch line wrapping on.

This file contains the release notes (the major changes) since Circle Step30 for quick access. For earlier releases please checkout the respective git tag and look into README.md. More info is attached to the release tags (git cat-file tag StepNN) and is available in the git commit log.

Release 43.1
------------

This intermediate release adds **USB plug-and-play** support to the classes `CConsole` and `CUGUI` and related samples (*sample/32-i2cshell* and *addon/ugui/sample*).

Furthermore two **important bug fixes** have been applied. The first one affects the handling of interrupts in the xHCI USB driver for the Raspberry Pi 4, where the interrupts and thus all data transfers might have been stalled after a random amount of time. The second one prevents the access to deleted USB device object, when an USB device is surprise-removed from the Raspberry Pi 1-3 or Zero.

Some effort have been spent to allow **reducing the boot time**, when using the USB driver and the network subsystem. This is shown in [sample/18-ntptime](sample/18-ntptime). Have a look at the [README file](sample/18-ntptime/README) for details.

The **make target "tftpboot"** has been added to *Rules.mk*. If you have installed the *sample/38-bootloader* on your Raspberry Pi with Ethernet interface and have configured the host name (e.g. "raspberrypi") or IP address of it as `TFTPHOST=` in the file *Config.mk*, you can build and start a test program in a row using `make tftpboot`.

The 43rd Step
-------------

2020-10-02

This release adds **USB plug-and-play** (USB PnP) support to Circle. It has been implemented for all USB device drivers, which can be subject of dynamic device attachments or removes, and for a number of sample programs. Existing applications have to be modified to support USB PnP, but this is not mandatory. An existing application can continue to work without USB PnP unmodified. Please see the file [doc/usb-plug-and-play.txt](doc/usb-plug-and-play.txt) for details on USB PnP support and [sample/README](sample/README) for information about which samples are USB PnP aware!

USB PnP requires the **system option USE_USB_SOF_INTR** to be enabled in [include/circle/sysconfig.h](include/circle/sysconfig.h) on the Raspberry Pi 1-3 and Zero. Because it has proved to be beneficial for most other applications too, it is enabled by default now. Rarely it may be possible, that your application has disadvantages from it. In this case you should disable this option and go back to the previous setting (e.g. if you need the maximum network performance).

An important issue has been fixed throughout Circle, which affected the **alignment of buffers used for DMA operations**. These buffers must be aligned to the size of a data-cache line (32 bytes on Raspberry 1 and Zero, 64 bytes otherwise) in base address and size. In some cases your application may need to be updated to meet this requirement. For example this applies to the samples *05-usbsimple*, *06-ethernet* and *25-spidma*. Please see the file [doc/dma-buffer-requirements.txt](doc/dma-buffer-requirements.txt) for details!

Another problem in the past was, that the output to screen or serial device affected the **IRQ timing of applications**. There is the system option `REALTIME`, which already improved this timing. Unfortunately it was not possible to use low- or full-speed USB devices (e.g. USB keyboard) on the Raspberry Pi 1-3 and Zero, when this option was enabled. Now this is supported, when the system option `USE_USB_SOF_INTR` is enabled together with `REALTIME`.

The new **class CWriteBufferDevice** can be used to buffer the screen or serial output in a way, that writing to these devices is still possible at *IRQ_LEVEL*, even when the option `REALTIME` is defined. Using the new **class CLatencyTester** it is demonstrated in the new [sample/40-irqlatency](sample/40-irqlatency), how this affects the IRQ latency of the system. Please read the [README file](sample/40-irqlatency/README) of this sample for details!

Release 42.1
------------

2020-08-22

This intermediate release adds support for the **DMA4 "large address" channels** of the Raspberry Pi 4, which is available using the already known `CDMAChannel` class with the channel ID `DMA_CHANNEL_EXTENDED`. Despite the possibility to access more address bits, which is currently not used in Circle, the DMA4 channels provide a higher performance (compared to the legacy DMA channels). All (normally three available) DMA4 channels are free for application usage.

Newer **Raspberry Pi 4 models (with 8 GB RAM)** do not have a dedicated EEPROM for the firmware of the xHCI USB controller. They need an additional property mailbox call for loading the xHCI firmware after PCIe reset. This call has been added.

There has been no possibility for **application TCP flow control** before. An application sending much TCP data very fast was able to overrun the (low) heap, which caused a system halt. Now if `CSocket::Send()` is called with the flags parameter set to 0, the calling task will block until the TX queue is empty. This prevents the heap from overrun, but may slow down the transfer to some degree. If maximum transfer speed is wanted, the flags parameter can be set to `MSG_DONTWAIT`, but heap overrun must be prevented differently then (e.g. by sending less data).

Another fix has been applied to **network name resolution** in the class `CDNSClient`, which might have failed before, if the DNS server was sending uncompressed answer records.

The 42nd Step
-------------

2020-05-09

This release adds **Wireless LAN access** support in [addon/wlan](addon/wlan) to Circle. Please read the [README file](addon/wlan/sample/README) of the sample program for details! The WLAN support in Circle is still experimental.

To allow parallel access to WLAN and SD card, a new **SDHOST driver** for SD card access on Raspberry Pi 1-3 and Zero has been added. You can return to the previous EMMC interface in case of problems (e.g. if using QEMU) or for real-time applications by adding `DEFINE += -DNO_SDHOST` to *Config.mk*. WLAN access is not possible then. On Raspberry Pi 4 the **EMMC2 interface** is used for SD card access now.

Release 41.2
------------

2020-03-08

This intermediate release comes with some new features, improvements in detail and fixes. For the **Raspberry Pi 4** now the four **additional peripheral devices** each are supported for I2C master, SPI master and UART. The **SPI slave** is supported too on the latest Raspberry Pi model now.

Time has been spent to **improve the network library**. The **ARP** problem, which caused a delayed start of network transmissions, has been solved. ARP requests are retried now if necessary and the first packet sent in a new connection after reboot is not discarded any more. **ICMP** handling has been improved too. The **DHCP client** sends an configurable host name (default "raspberrypi") now in requests, so that the DHCP server can add a DNS entry for your Raspberry Pi, if it this enabled in the server.

There is a new **software profiling support** library in [addon/profile](addon/profile) for single core applications. The file *gmon.out*, which is generated by the software profiling library, is compatible with the *gprof* tool.

Previously a **CMemorySystem** object had to be instanced as first member of CKernel with `STDLIB_SUPPORT < 2` to be able to configure MMU usage by the application. This caused several problems. Newer compilers may generate code in the early stages, which does not work without MMU. Also constructors of static objects were called with MMU off before. Now that the CMemorySystem object is constructed early, these problems will be solved and all applications run with MMU on. For compatibility it is not a problem, if the application constructs a second instance of CMemorySystem in CKernel. It is used as an alias for the first instance.

The documentation generated by **Doxygen** with `./makedoc` has been improved by integrating and linking available documentation files. Important class definitions from the *addon/* subdirectory are included in the generated documentation now.

The following **issues** have been **fixed**:

* The BCM54213PE Gigabit Ethernet driver for the Raspberry Pi 4 did not reset the device properly. This caused some heavy re-transmissions of frames after chain boot (e.g. with sample/38-bootloader).

* AArch64 FIQ support did not initialize or close properly on Raspberry Pi 4 in some cases.

* It turned out, that the PWM audio channel outputs are swapped on some Raspberry Pi models. This will be considered automatically now. If you have implemented your own PWM audio driver, you can request from the CMachineInfo class, if the channels are swapped.

* Multi-core applications might have freezed when calling CSocket::Connect().

* CNTPDaemon did only save a pointer to the host name of the NTP server before. This might have caused problems, if the string buffer disappeared after returning from the constructor.

Release 41.1
------------

2020-02-14

This is a hotfix release, which fixes the bootloader. It did not work with the recommended firmware on Raspberry Pi 1 and Zero any more.

The 41st Step
-------------

2020-02-04

With this release Circle supports nearly all features on the Raspberry Pi 4, which are known from earlier models. Only OpenGL ES / OpenVG / EGL and the I2C slave support are not available.

On Raspberry Pi 4 models with over 1 GB SDRAM Circle provides a separate *HEAP_HIGH* memory region now. You can use it to dynamically allocate memory blocks with `new` and `malloc()` as known from the low heap. Both heaps can be configured to form a larger unified heap using a system option. Please read the file *doc/new-operator.txt* for details about using and configuring the heaps.

The new *sample/39-umsdplugging* demonstrates how to use `CUSBHCIDevice::RescanDevices()` and `CDevice::RemoveDevice()` to be able to attach USB mass-storage devices (e.g. USB flash drives) and to remove them again on application request without rebooting the system.

Some support for the QEMU semihosting interface has been added, like the possibility to exit QEMU on `halt()`, optionally with a specific exit status (`set_qemu_exit_status()`). This may be used to automate tests. There is a new class `CQEMUHostFile`, which allows reading and writing files (including stdin / stdout) on the host system, where QEMU is running on. See the directory *addon/qemu/*, the new sample in *addon/qemu/hostlogdemo/* and the file file *doc/qemu.txt* for info on using QEMU with Circle with the semihosting API.

The Circle build system checks dependencies of source files with header files now and automatically rebuilds the required object files. You will not need to clean the whole project after editing a header file any more. You have to append the (last) line `-include $(DEPS)` to an existing Makefile to enable this feature in your project. The dependencies check may be globally disabled, by defining `CHECK_DEPS = 0` in Config.mk.

If you want to modify a system option in *include/circle/sysconfig.h*, explicitly changing this file is not required any more, which makes it easier to include Circle as a git submodule. All system options can be defined in *Config.mk* this way:


```
DEFINE += -DARM_ALLOW_MULTI_CORE
DEFINE += -DNO_CALIBRATE_DELAY
```

Finally a project file for the Geany IDE is provided in *tools/template/*. The recommended toolchain is based on GNU C 9.2.1 now. As announced the Bluetooth support has been removed for legal reasons.

Release 40.1
------------

2019-12-15

This intermediate release mostly adds FIQ support for AArch64 on the Raspberry Pi 4. This has been implemented using an additional ARM stub file *armstub8-rpi4.bin*, which is loaded by the firmware. The configuration file *config.txt*, provided in *boot/*, is required in any case for AArch64 operation now. Please read the file *boot/README* for detailed information on preparing a SD card with the firmware for using it with Circle applications.

*boot/Makefile* downloads a specific firmware revision now. With continuous Raspberry Pi firmware development there may occur compatibility problems between a Circle release and a new firmware revision, which may lead to confusion, if they are not detected and fixed immediately. To overcome this situation a specific tested firmware revision is downloaded by default now. This firmware revision reference will be updated with each new Circle release.

Further news in this release are:

* User timer (class CUserTimer) supported on Raspberry Pi 4
* LittlevGL support in *addon/littlevgl/* updated to v6.1.1
* FatFs support in *addon/fatfs/* updated to R0.14

Finally the system option *SCREEN_HEADLESS* has been added to *include/circle/sysconfig.h*. It can be defined to allow headless operation of sample programs especially on the Raspberry Pi 4, which would otherwise fail to start without notice, if there is not a display attached (see the end of next section for a description of the problem).

The 40th Step
-------------

2019-09-02

This Circle release adds support for the Raspberry Pi 4 Model B. All features supported by previous releases on Raspberry Pi 1-3 should work on Raspberry Pi 4 except:

* Accelerated graphics support
* FIQ on AArch64
* User timer (class CUserTimer)
* I2C slave (class CI2CSlave)
* USB RescanDevices() and RemoveDevice()

The Raspberry Pi 4 provides a number of new features. Not all of these are supported yet. Support for the following features is planned to be added later:

* USB 3.0 hubs
* High memory (over 1 GByte)
* additional peripherals (SPI, I2C, UART)

The Raspberry Pi 4 has a new xHCI USB host controller and a non-USB Gigabit Ethernet controller. This requires slight modifications to existing applications. The generic USB host controller class is named `CUSBHCIDevice` and has to be included from `<circle/usb/usbhcidevice.h>` now. The Ethernet controller driver is automatically loaded, if the TCP/IP network library is used, but has to be loaded manually otherwise (see *sample/06-ethernet*).

Please note that there is a different behavior regarding headless operation on the Raspberry Pi 4 compared to earlier models, where the frame buffer initialization succeeds, even if there is no display connected. On the Raspberry Pi 4 there must be a display connected, the initialization of the class CBcmFrameBuffer (and following of the class CScreenDevice) will fail otherwise. Most of the Circle samples are not aware of this and may fail to run without a connected display. You have to modify CKernel::Initialize() for headless operation so that `m_Screen.Initialize()` is not called. `m_Serial` (or maybe class `CNullDevice`) can be used as logging target in this case.

Release 39.2
------------

2019-07-08

This is another intermediate release, which collects the recent changes to the project as a basis for the planned Raspberry Pi 4 support in Circle 40.

News in this release are:

* LittlevGL embedded GUI library (by Gabor Kiss-Vamosi) supported (in addon/littlevgl/).

* The SD card access (EMMC) performance has been remarkable improved.

* USB mass-storage devices (e.g. flash drives) can be removed from the running system now. The application has to call `CDevice::RemoveDevice()` for the device object of the USB mass-storage device, which will be removed afterwards. The file system has to be unmounted before by the application. If FatFs (in addon/fatfs/) is used, the device removal is announced by calling `disk_ioctl (pdrv, CTRL_EJECT, 0)`, where pdrv is the physical drive number (e.g. 1 for the first USB mass-storage device).

* Network initialization can be done in background now to speed-up system initialization. If `CNetSubSystem::Initialize()` is called with the parameter FALSE, it returns quickly without waiting for the Ethernet link to come up and for DHCP to be bound. The network must not be accessed, before `CNetSubSystem::IsRunning()` returns TRUE. This has to be ensured by the application.

Please note that the rudimentary Bluetooth support is deprecated now. There are legal reasons, why it cannot be developed further and because it is currently of very limited use, it will probably be removed soon.

Release 39.1
------------

2019-03-20

This intermediate release comes with a new in-memory-update (chain boot) function and some improvements in detail. Furthermore it is the basis for AArch64 support in the [circle-stdlib](https://github.com/smuehlst/circle-stdlib) project.

The in-memory-update function allows starting a new (Circle-based) kernel image without writing it out to the SD card. This is used to implement a HTTP- and TFTP-based bootloader with Web front-end in *sample/38-bootloader*. Starting larger kernel images with it is much quicker, compared with the serial bootloader. See the *README* file in this directory for details.

The ARM Generic Timer is supported now on Raspberry Pi 2 and 3 as a replacement for the BCM2835 System Timer. This should improve performance and allows using QEMU with AArch64. The system option *USE_PHYSICAL_COUNTER* is enabled by default now.

The relatively rare resource of DMA channels is assigned dynamically now. Lite DMA channels are supported. This allows to use DMA for scrolling the screen much quicker.

There is a new make target "install". If you define `SDCARD = /path` with the full path of your SD card mount point in *Config.mk*, the built kernel image can be copied directly to the SD card. There is a second optional configuration file *Config2.mk* now. Because some Circle-based projects overwrite the file *Config.mk* for configuration, you can set additional non-volatile configuration variables using this new file.

The 39th Step
-------------

2019-02-28

Circle supports the following accelerated graphics APIs now:

* OpenGL ES 1.1 and 2.0
* OpenVG 1.1
* EGL 1.4
* Dispmanx

This has been realized by (partially) porting the Raspberry Pi [userland libraries](https://github.com/raspberrypi/userland), which use the VC4 GPU to render the graphics. Please see the *addon/vc4/interface/* directory and the *README* file in this directory for more details. This support is limited to AArch32 and cannot be built on Raspbian.

The accelerated graphics support requires support for <math.h> functions. To provide this, the *libm.a* standard library is linked now, in case `STDLIB_SUPPORT = 1` is set (default). You need an appropriate toolchain so that it works. See the *Building* section for a link. You may use the <math.h> functions in your own applications too now.

Circle does not support normal USB hot-plugging, but there is a new feature, which allows to detect newly attached USB devices on application request. You can call CDWHCIDevice::ReScanDevices() now, while the application is running, to accomplish this.

The 38th Step
-------------

2019-01-04

The entire Circle project has been ported to the AArch64 architecture. Please see the *AArch64* section below for details! The 32-bit support is still available and will be maintained and developed further.

Moreover a driver for the BMP180 digital pressure sensor has been added to *addon/sensor/*.

Circle creates a short beautified build log now.

The 37th Step
-------------

2018-12-01

In this step the USB gamepad drivers have been totally revised. The PS3, PS4, Xbox 360 Wired, Xbox One and Nintendo Switch Pro gamepads are supported now, including LEDs, rumble and gyroscope. All these drivers support a unique class interface and mapping for the button and axis controls. This should simplify the development of gamepad applications and is used in the new *sample/37-showgamepad*. See the *README* file in this directory for details.

The touchpad of the PS4 gamepad can be used as a mouse device to control normal mouse applications. To implement this, a new unique mouse interface class has been added, which is used by the USB mouse driver too. Therefore existing mouse applications have to be updated as follows:

* Include <circle/input/mouse.h> instead of <circle/usb/usbmouse.h>
* Class of the mouse device is *CMouseDevice* instead of *CUSBMouseDevice*
* Name of the first mouse device is "mouse1" instead of "umouse1"

Finally with this release Circle supports the new Raspberry Pi 3 Model A+.

Release 36.1
------------

2018-11-14

This hotfix release fixes race conditions in the FIQ synchronization, which affected using the FIQ together with EnterCritical() or the class CSpinLock.

The 36th Step
-------------

2018-10-21

In this step the class *CUserTimer* has been added to Circle, which implements a fine grained user programmable interrupt timer. It can generate up to 500000 interrupts per second (if used with FIQ).

*CUserTimer* is used in the new *sample/36-softpwm* to implement Software PWM (Pulse Width Modulation), which can be used to control the brightness of a LED or a servo. See the *README* file in this directory for details.

This release of Circle comes with an updated version of the FatFs module (in *addon/fatfs*). Furthermore there have been fixes to the touchscreen driver and the bootloader tools. Finally there are the new system options *SAVE_VFP_REGS_ON_IRQ* and *SAVE_VFP_REGS_ON_FIQ* in *include/circle/sysconfig.h*, which can be enabled if your application uses floating point registers in interrupt handlers.

You may be interested in the [Alpha GDB server](https://github.com/farjump/Alpha_Raspberry_Pi_Circle) by Farjump, which can be used to source-level debug single-core Circle applications with the GNU debugger (GDB).

Release 35.1
------------

2018-05-15

This intermediate release is done to allow a new release of the [circle-stdlib project](https://github.com/smuehlst/circle-stdlib), which provides C and C++ standard library support for Circle and which has been extended with SSL/TLS support by porting [mbed TLS](https://tls.mbed.org/).

Another new feature in this release is a driver for LCD dot-matrix displays with HD44780 controller in the *addon/display/* subdirectory.

The 35th Step
-------------

2018-04-26

In this step a client for the MQTT IoT protocol has been added to Circle. It is demonstrated in *sample/35-mqttclient*. See the *README* file in this directory for details.

Furthermore a serial bootloader is available now and can be started directly from the Circle build environment. The well-known bootloader by David Welch is used here. See the file *doc/eclipse-support.txt* for instructions on using the bootloader with or without the Eclipse IDE.

There is an incompatible change in behaviour of the CSocket API. Previously when a TCP connection received a FIN (disconnect) and the application called CSocket::Receive(), it did not get an error so far. Because of this, the disconnect has not been detectable. Now an error will be returned in this case. This may cause problems with existing user applications using TCP. You may have to check this.

Release 34.1
------------

2018-03-28

This intermediate release was necessary, because the new Raspberry Pi 3 Model B+ became available. Circle supports it now.

Beside this a driver for the Auxiliary SPI master (SPI1) has been added. The sample/23-spisimple has been updated to support this too. The class CDMAChannel supports the 2D mode now. Furthermore there are some minor improvements in the network subsystem.

The 34th Step
-------------

2018-01-16

In this step a VCHIQ audio service driver and a VCHIQ interface driver have been added to Circle in the *addon/vc4/* subdirectory. They have been ported from Linux and allow to output sound via the HDMI connector. You need a HDMI display which is capable of output sound, to use this.

There is a new Linux kernel driver emulation library in *addon/linux/* which allows to use the VCHIQ interface driver from Linux with only a few changes.

The new *sample/34-sounddevices* can be used to demonstrate the HDMI sound and integrates different sound devices (PWM, I2S, VCHIQ). See the *README* file in this directory for details.

There is a new system option *USE_PHYSICAL_COUNTER* in *include/circle/sysconfig.h* which enables the use of the CPU internal physical counter on the Raspberry Pi 2 and 3, which should improve the system performance, especially if the scheduler is used. It cannot be used with QEMU and that's why it is not enabled by default.

The 33rd Step
-------------

2017-11-18

The main expenditure in this step was spent to prepare Circle for external projects which allow to develop applications which are using C and C++ standard library features. Please see the file *doc/stdlib-support.txt* for details.

Furthermore a syslog client has been added, which allows to send log messages to a syslog server. This is demonstrated in *sample/33-syslog*. See the *README* file in this directory for details.

Circle applications will be linked using the standard library libgcc.a by default now. This library should come with your toolchain. Only if you have problems with that, you can fall back to the previous handling using the setting *STDLIB_SUPPORT=0* in the files *Config.mk* or *Rules.mk*.

If you are using an USB mouse with the mouse cursor in your application, you have to add a call to *CUSBMouseDevice::UpdateCursor()* in your application's main loop so that that cursor can be updated. Please see *sample/10-usbmouse* for an example.

In a few cases it may be important, that the Circle type *boolean* is now equivalent to the C++ type *bool*, which only takes one byte. *boolean* was previously four bytes long.

Release 32.1
------------

2017-10-29

This hotfix release fixes possible SPI DMA errors in the class CSPIMasterDMA.

The 32nd Step
-------------

2017-10-28

In this step a console class has been added to Circle, which allows the easy use of an USB keyboard and a screen together as one device. The console class provides a line editor mode and a raw mode and is demonstrated in *sample/32-i2cshell*, a command line tool for interactive communication with I2C devices. If you are often experimenting with I2C devices, this may be a tool for you. See the *README* file in this directory for details.

Furthermore the Circle USB HCI driver provides an improved compatibility for low-/full-speed USB devices (e.g. keyboards, some did not work properly). Because this update changes the overall system timing, it is not enabled by default to be compatible with existing applications. You should enable the system option *USE_USB_SOF_INTR*, if the improved compatibility is important for your application.

The system options can be found in the file *include/circle/sysconfig.h*. This file has been completely revised and each option is documented now.

Finally there are some improvements in the SPI0 master drivers (polling and DMA) included in this step.

The 31st Step
-------------

2017-08-26

In this step HTTP client support has been added to Circle and is demonstrated in *sample/31-webclient*. See the *README* file in this directory for details.

Furthermore the I2S sound device is supported now with up to 192 KHz sample rate (24-bit I2S audio, tested with PCM5102A DAC). *sample/29-miniorgan* has been updated to be able to use this feature.

The addon/ subdirectory contains a port of the FatFs file system driver (by ChaN), drivers for the HC-SR04 and MPU-6050 sensor devices and special samples to be used with QEMU now.

Finally there are some fixes in the TCP protocol handler included in this step.

Release 30.3
------------

2017-08-01

With the firmware from 2017-07-28 on the ActLED didn't worked on RPi 3 any more and the official touchscreen on RPi 2 and 3. This has been fixed.

Release 30.2
------------

2017-07-30

Multicore support didn't work with recent firmware versions. This has been fixed.

Release 30.1
------------

2017-05-03

This hotfix release fixes an invalid section alignment with -O0. This caused a crash while calling the constructors of static objects in sysinit().

The 30th Step
-------------

2017-04-11

In this step FIQ (fast interrupt) support has been added to Circle. This is used to implement the class CGPIOPinFIQ, which allows fast interrupt-driven event capture from a GPIO pin and is demonstrated in *sample/30-gpiofiq*. See the *README* file in this directory for details.

FIQ support is also used in the class CSerialDevice, which allows an interrupt-driven access to the UART0 device. *sample/29-miniorgan* has been updated to use the UART0 device (at option) as a serial MIDI interface.

Finally in this step QEMU support has been added. See the file *doc/qemu.txt* for details!
