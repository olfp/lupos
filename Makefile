#
# Makefile
#

CSTDLIB = circle-stdlib
include $(CSTDLIB)/Config.mk

CIRCLEHOME = $(CSTDLIB)/libs/circle
NEWLIBDIR = $(CSTDLIB)/install/$(NEWLIB_ARCH)
LUADIR = liblua

OBJS	= main.o kernel.o luaint.o lconsole.o lfilesystem.o

include $(CIRCLEHOME)/Rules.mk

CFLAGS += -I $(LUADIR) -I "$(NEWLIBDIR)/include" -I $(STDDEF_INCPATH) -I $(CSTDLIB)/include
LIBS := "$(NEWLIBDIR)/lib/libm.a" "$(NEWLIBDIR)/lib/libc.a" "$(NEWLIBDIR)/lib/libcirclenewlib.a" \
 	$(CIRCLEHOME)/addon/SDCard/libsdcard.a \
  	$(CIRCLEHOME)/lib/usb/libusb.a \
 	$(CIRCLEHOME)/lib/input/libinput.a \
 	$(CIRCLEHOME)/addon/fatfs/libfatfs.a \
 	$(CIRCLEHOME)/addon/wlan/libwlan.a \
 	$(CIRCLEHOME)/addon/wlan/hostap/wpa_supplicant/libwpa_supplicant.a \
 	$(CIRCLEHOME)/lib/fs/libfs.a \
  	$(CIRCLEHOME)/lib/net/libnet.a \
  	$(CIRCLEHOME)/lib/sched/libsched.a \
  	$(CIRCLEHOME)/lib/libcircle.a \
	$(LUADIR)/liblua.a

-include $(DEPS)
