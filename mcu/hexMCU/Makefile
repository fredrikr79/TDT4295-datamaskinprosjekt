HOST_DIR := build/host
FW_DIR   := build/fw
HOST_BIN := $(HOST_DIR)/hexmcu_host

# --- SD card image ----------------------------------------------------
# Lives in host/, outside build/, so `make clean` leaves it alone.
CARD      := host/card.img
CARD_SIZE := 512M
# 8 sectors/cluster = 4 KiB. Match this to however the real card is
# formatted: cluster size decides how often a sequential read crosses a
# boundary and has to consult the FAT.
CARD_SPC  := 8
CARD_NAME := GAMECARD

.PHONY: host run fw flash clean card card-reset ls cat

host:
	@cmake -S host -B $(HOST_DIR) -G Ninja || \
		{ echo "reconfiguring $(HOST_DIR) from scratch"; \
		  rm -rf $(HOST_DIR); cmake -S host -B $(HOST_DIR) -G Ninja; }
	@cmake --build $(HOST_DIR)

run: host $(CARD)
	@if command -v losetup >/dev/null && losetup -j $(CARD) | grep -q .; then \
		echo "$(CARD) is attached to a loop device -- unmount it first"; \
		exit 1; \
	fi
	@./$(HOST_BIN) --card=$(CARD) $(ARGS)

fw:
	@cmake -S . -B $(FW_DIR) -G Ninja \
		--toolchain cmake/gcc-arm-none-eabi.cmake
	@cmake --build $(FW_DIR)

flash: fw
	STM32_Programmer_CLI -c port=SWD -w $(FW_DIR)/hexMCU.elf -rst

# --- card image targets -----------------------------------------------

card: $(CARD)

$(CARD):
	@echo "creating $@ ($(CARD_SIZE), FAT32, $(CARD_SPC) sectors/cluster)"
	@truncate -s $(CARD_SIZE) $@
	@mkfs.vfat -F 32 -S 512 -s $(CARD_SPC) -n $(CARD_NAME) $@ >/dev/null

card-reset:
	@rm -f $(CARD)
	@$(MAKE) --no-print-directory card

# Inspect without mounting. `make cat F=HELLO.TXT`
ls: $(CARD)
	@mdir -i $(CARD) -/ ::

cat: $(CARD)
	@test -n "$(F)" || { echo "usage: make cat F=HELLO.TXT"; exit 1; }
	@mtype -i $(CARD) ::/$(F)

clean:
	rm -rf build