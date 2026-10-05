HOST_DIR := build/host
FW_DIR   := build/fw
HOST_BIN := $(HOST_DIR)/hexmcu_host
TEST_BIN := $(HOST_DIR)/test_host

# --- SD card image ----------------------------------------------------
# Lives in host/, outside build/, so `make clean` leaves it alone.
CARD      := host/card.img
CARD_SIZE := 512M
# 8 sectors/cluster = 4 KiB. Match this to however the real card is
# formatted: cluster size decides how often a sequential read crosses a
# boundary and has to consult the FAT.
CARD_SPC  := 8
CARD_NAME := GAMECARD

# --- test_host picture -----------------------------------------------
# test_host wants a 640x360 RGB565 BMP. It is made from $(IMAGE) with
# ImageMagick, and redone only when $(IMAGE) changes. -alpha off matters:
# with an alpha channel ImageMagick ignores the RGB565 subtype and writes
# a 24-bit BMP instead.
IMAGE     := host/test.bmp
IMAGE565  := host/test565.bmp
MAGICK    := $(shell command -v magick 2>/dev/null || echo convert)

.PHONY: host run test fw flash clean card card-reset ls cat

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
	@./$(HOST_BIN)

# `make test`, or `make test ARGS=--errors=2` to watch it resync
test: host $(IMAGE565)
	@./$(TEST_BIN) $(ARGS) $(IMAGE565)

# $(wildcard): if only the converted picture exists, just use it
$(IMAGE565): $(wildcard $(IMAGE))
	@test -f "$(IMAGE)" || { echo "need $(IMAGE) (or a ready $(IMAGE565))"; exit 1; }
	@echo "converting $(IMAGE) -> $@ (640x360 RGB565)"
	@$(MAGICK) "$(IMAGE)" -alpha off -resize '640x360!' -define bmp:subtype=RGB565 $@

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