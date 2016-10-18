# Makefile to build light_ws2812 library examples
# This is not a very good example of a makefile - the dependencies do not work, therefore everything is rebuilt every time.

# Change these parameters for your device

F_CPU = 16000000
DEVICE = atmega168

CFLAGS = -g2 -mmcu=$(DEVICE) -DF_CPU=$(F_CPU) -std=c99
CFLAGS+= -Os -ffunction-sections -fdata-sections -fpack-struct -fno-move-loop-invariants -fno-tree-scev-cprop -fno-inline-small-functions  
CFLAGS+= -Wall -Wno-pointer-to-int-cast
#CFLAGS+= -Wa,-ahls=$<.lst

LDFLAGS = -Wl,--relax,--section-start=.text=0,-Map=main.map

main.elf: main.c
	avr-gcc $(CFLAGS) -o $@ $^
	avr-size $@

.PHONY: program
program: main.elf
	avrdude -c arduino -P /dev/serial/by-id/usb-1a86_USB2.0-Serial-if00-port0 -b 19200 -p atmega168 -U flash:w:$<

.PHONY:	clean
clean:
	rm -f main.elf
