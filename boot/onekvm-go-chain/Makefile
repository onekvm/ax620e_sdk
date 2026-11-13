# SPDX-License-Identifier: BSD-3-Clause
CROSS_COMPILE ?= aarch64-linux-gnu-
CC ?= $(CROSS_COMPILE)gcc
LD ?= $(CROSS_COMPILE)ld
OBJCOPY ?= $(CROSS_COMPILE)objcopy

all: go_chain.bin

go_chain.o: go_chain.S
	$(CC) -c -o $@ $<

go_chain.elf: go_chain.o
	$(LD) -nostdlib -static -Ttext=0 -e _start --build-id=none -o $@ $<

go_chain.bin: go_chain.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f go_chain.o go_chain.elf go_chain.bin

.PHONY: all clean
