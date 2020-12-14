# LUPOS

Warning: This is a work in progress

## Overview
LUPOS - short for Lua environment on the Raspbery Pi like an OS - is 
a Lua (http://www.lua.org) interpreter mated to the Circle 
bare metal environment (https://github.com/rsta2/circle) 
for the Raspberry Pi (https://www.raspberrypi.org) with the help of Circle-Stdlib (https://github.com/smuehlst/circle-stdlib).

It look like this:

![LUPOS Screenshot](https://olfp.github.io/LUPOS-Snap0.png)

A video of LUPOS booting is avaliable [here](https://youtu.be/QnL8gHgV4kQ).

A big, big thank you goes out to the people who wrote most of the code used:

* Rene Stange for [Circle](https://github.com/rsta2/circle).
* Stephan Mühlstrasser fo [Circe-Stdlib](https://github.com/smuehlst/circle-stdlib).
* Corinna Vinschen and Jeff Johnston for [Newlib](https://sourceware.org/newlib/).
* Roberto Ierusalimschy, Waldemar Celes and Luiz Henrique de Figueiredo for [Lua](http://www.lua.org)
* philanc for the [Pure Lua Editor](https://github.com/philanc/ple)

## Platform
LUPOS started on a Raspi 3b+ and is now developed on a Raspi 400. There is no reason it should not run on the other Raspis - as supported by Circle - but this is untested.

## Prerequisites
As the Circle README states:
This describes building on PC Linux. See the file [doc/windows-build.txt](doc/windows-build.txt) for information about building on Windows. If building for the Raspberry Pi 1 you need a [toolchain](http://elinux.org/Rpi_Software#ARM) for the ARM1176JZF core (with EABI support). For Raspberry Pi 2/3/4 you need a toolchain with Cortex-A7/-A53/-A72 support. A toolchain, which works for all of these, can be downloaded [here](https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/gnu-a/downloads). Circle has been tested with the version *9.2-2019.12* (gcc-arm-9.2-2019.12-x86_64-arm-none-eabi.tar.xz) from this website.

## Building
 
1. Clone the GitHub repository:
```bash
	git clone https://github.com/olfp/lupos.git
```
2. Configure your Pi version & environment
```bash
	./configure -r 4
```
The -r option gives the Pi verison 1, 2, 3 or 4 (4 for 400).
3. Build Circle & other libraries needed
```bash
	make libs
```
4. Build LUPOS
```bash
	make
```
5. Fetch Raspi firmware files (optional)
```bash
	cd circle-stdlib/libs/circle/boot && make
```
Follow the instuctions in the README. You can also use a working Rasbian image.
6. Copy needed boot files (if needed) and the kernel image (Pi 400: kernel7l.img) to an MicroSD card, plug in Pi, enjoy

## Status
Boot to a REPL which accepts Lua code. Filessystem commands for FAT on SD/USB as provided by Circle/FATFS are partly there: dir, go (cd), path (pwd) and del are present. The REPL is tweaked to call functions without arguments even if th etrailing parentheses are mising. This means you can just type "dir" to get a directory. 

A text editor "edit" is provided based on https://github.com/philanc/ple.
Storage is on the SD card "SD:" and USB sticks "USB:", "USB2", .., "USB4".

## License

This project is licensed under the GNU GENERAL PUBLIC LICENSE
Version 3 - see the [LICENSE](LICENSE) file for details
