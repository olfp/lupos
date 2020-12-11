CC = arm-none-eabi-gcc
ARCH = -DAARCH=32 -mcpu=cortex-a72 -marm -mfpu=neon-fp-armv8 -mfloat-abi=hard
TOOLPREFIX = arm-none-eabi-
NEWLIB_BUILD_DIR = /home/olf/projects/circle/circle-stdlib/build/circle-newlib
NEWLIB_INSTALL_DIR = /home/olf/projects/circle/circle-stdlib/install
CFLAGS_FOR_TARGET = -DAARCH=32 -mcpu=cortex-a72 -marm -mfpu=neon-fp-armv8 -mfloat-abi=hard -Wno-parentheses
CPPFLAGS_FOR_TARGET = -I"/home/olf/projects/circle/circle-stdlib/libs/circle/include" -I"/home/olf/projects/circle/circle-stdlib/libs/circle/addon" -I"/home/olf/projects/circle/circle-stdlib/include"
CC_FOR_TARGET = arm-none-eabi-gcc
CXX_FOR_TARGET = arm-none-eabi-g++
GCC_FOR_TARGET = arm-none-eabi-gcc
AR_FOR_TARGET = arm-none-eabi-gcc-ar
AS_FOR_TARGET = arm-none-eabi-gcc-as
LD_FOR_TARGET = arm-none-eabi-gcc-ld
RANLIB_FOR_TARGET = arm-none-eabi-gcc-ranlib
OBJCOPY_FOR_TARGET = arm-none-eabi-gcc-objcopy
OBJDUMP_FOR_TARGET = arm-none-eabi-gcc-objdump
NEWLIB_ARCH = arm-none-circle
