# LUPOS

Warning: This is a work in progress

## Overview
LUPOS - short for Lua environment on the Raspbery Pi like an OS - is 
a Lua (http://www.lua.org) interpreter mated to the Circle 
bare metal environment (https://github.com/rsta2/circle) 
for the Raspberry Pi (https://www.raspberrypi.org) with the help of Circle-Stdlib (https://github.com/smuehlst/circle-stdlib).

A big, big thank you goes out to the people who wrote most of the code used:

* Rene Stange for [Circle](https://github.com/rsta2/circle).
* Stephan Mühlstrasser fo [Circe-Stdlib](https://github.com/smuehlst/circle-stdlib).
* Corinna Vinschen and Jeff Johnston for [Newlib](https://sourceware.org/newlib/).
* Roberto Ierusalimschy, Waldemar Celes and Luiz Henrique de Figueiredo for [Lua](http://www.lua.org)
* philanc for the [Pure Lua Editor](https://github.com/philanc/ple)

## Platform
LUPOS started on a Raspi 3b+ and is now developed on a Raspi 400. There is no reason it should not run on the other Raspis - as supported by Circle - but this is untested.

## Building
I will supply build instructions shortly

## Status
Boot to a REPL which accepts Lua code. Filessystem commands for FAT on SD/USB as provided by Circle/FATFS are partly there: dir, go (cd), path (pwd) and del are present. The REPL is tweaked to call functions without arguments even if th etrailing parentheses are mising. This means you can just type "dir" to get a directory. 

A text editor "edit" is provided based on https://github.com/philanc/ple.
Storage is on the SD card "SD:" and USB sticks "USB:", "USB2", .., "USB4".

## License

This project is licensed under the GNU GENERAL PUBLIC LICENSE
Version 3 - see the [LICENSE](LICENSE) file for details
