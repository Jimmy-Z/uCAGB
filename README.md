open source Dumper and/or Flasher for GBA composed of three parts:
---
1. DFAGB, a multiboot rom runs on GBA, do the actual dump/flash work. needs [devkitARM](http://devkitpro.org/wiki/Getting_Started/devkitARM) to compile.
2. PC client, send multiboot rom and/or talk with DFAGB. Windows only, should be easy to port though. needs Visual Studio to compile.
3. uCSIO, a micro controller firmware runs on [Teensy](https://www.pjrc.com/teensy/)(2.0/++2.0) and/or [Arduino](https://www.arduino.cc/)(Leonardo/Micro), it uses GPIO to bit bang the GBA SIO port and talk to the PC client as a USB serial port. needs [AVR8 Toolchain](http://www.atmel.com/tools/ATMELAVRTOOLCHAINFORWINDOWS.aspx) to compile.

It can:
---
* send multiboot rom to GBA.
* Dump GBA cartridge.
* Program GBA flash cart. currently supports only flash carts using a single Intel 28F128J3F(or similar chips in the J3 family).
* SRAM save read/write.

uCSIO Pin connection(GBA - ATmega)
---
	SC - PB1/SCLK
	SI - PB2/MOSI
	SO - PB3/MISO
	and obviously GND
GBA is 3.3V so I use a Voltage Level Translator on SC and SI, no need to do this on SO since ATmega treats 3.3V as high even running on 5V.

Future plans:
---
* FLASH save read/write.
* EEPROM save read/write.
* Support more insteresting GBA flash carts, if I found any.

Trivia
---
* [the particular flash cart using Intel 28F128J3F](https://item.taobao.com/item.htm?id=520027241239) is the original motivation of this project, which is a charm since it uses FRAM for save, but relies on a flasher which was discontinued years ago.
* EEPROM save handling is another motivation of this project, I bid a Zelda ALTTP cart from eBay which turned out to be counterfeit, anyway I played on it and had a perfect save which I desperately wanted to get out of the cart, but long story short in the end the rom is patched and doesn't use EEPROM save, so I currently have no cart using EEPROM save to develope this feature, maybe eBay time again.

Credits
---
* [Daniel Tang](https://github.com/tangrs) for the [USB-GBA multiboot](https://github.com/tangrs/usb-gba-multiboot) project which I copied a lot.
* [GBATEK](http://problemkaputt.de/gbatek.htm)
* [devkitARM](http://devkitpro.org/)

License
---
GPL v3

