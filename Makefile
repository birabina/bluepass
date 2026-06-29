PREFIX   = arm-none-eabi-
CC       = $(PREFIX)gcc
AS       = $(PREFIX)gcc -x assembler-with-cpp
OBJCOPY  = $(PREFIX)objcopy
OBJDUMP  = $(PREFIX)objdump
SIZE     = $(PREFIX)size

TARGET   = rfid_rgb_stm32
BUILD_DIR = build

C_SOURCES = \
    main.c \
    rc522.c \
    rgb.c

ASM_SOURCES = \
    startup_stm32f103c8tx.s

CMSIS_DEVICE_INC = Drivers/CMSIS/Device/ST/STM32F1xx/Include
CMSIS_CORE_INC   = Drivers/CMSIS/Include

C_INCLUDES = \
    -I. \
    -I$(CMSIS_DEVICE_INC) \
    -I$(CMSIS_CORE_INC)

C_DEFS = \
    -DSTM32F103xB

CPU      = -mcpu=cortex-m3
FPU      =
FLOAT_ABI=
MCU      = $(CPU) -mthumb $(FPU) $(FLOAT_ABI)

CFLAGS   = $(MCU) $(C_DEFS) $(C_INCLUDES)
CFLAGS  += -Wall -Wextra -Wpedantic
CFLAGS  += -fdata-sections -ffunction-sections
CFLAGS  += -std=c11
CFLAGS  += -O2

ASFLAGS  = $(MCU) $(C_DEFS) $(C_INCLUDES) -Wall -fdata-sections -ffunction-sections

LDSCRIPT = STM32F103C8TX_FLASH.ld
LIBS     = -lc -lm -lnosys
LIBDIR   =
LDFLAGS  = $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBDIR) $(LIBS)
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref
LDFLAGS += -Wl,--gc-sections

OBJECTS  = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))

VPATH    = $(sort $(dir $(C_SOURCES)) $(dir $(ASM_SOURCES)))

.PHONY: all flash clean size

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).bin
	$(SIZE) $(BUILD_DIR)/$(TARGET).elf

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.s | $(BUILD_DIR)
	$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) $(LDSCRIPT)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O binary -S $< $@

$(BUILD_DIR)/$(TARGET).hex: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR):
	mkdir -p $@

flash: $(BUILD_DIR)/$(TARGET).bin
	openocd \
	    -f interface/stlink.cfg \
	    -f target/stm32f1x.cfg \
	    -c "program $(BUILD_DIR)/$(TARGET).bin verify reset exit 0x08000000"

clean:
	rm -rf $(BUILD_DIR)

size: $(BUILD_DIR)/$(TARGET).elf
	$(SIZE) $<
