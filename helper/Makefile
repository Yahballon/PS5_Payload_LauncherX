ifndef PS5_PAYLOAD_SDK
$(error PS5_PAYLOAD_SDK not configured)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
CFLAGS += -Wall -Wextra -O2
all: usbscan.elf
usbscan.elf: usbscan.c
	$(CC) $(CFLAGS) -o $@ $<
