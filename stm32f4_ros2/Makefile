# Flag adapter for the upstream builder. Firmware itself uses CMake.
CFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -Os -ffunction-sections -fdata-sections
.PHONY: print_cflags
print_cflags:
	@echo $(CFLAGS)
