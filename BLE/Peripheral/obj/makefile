################################################################################
# MRS Version: 2.3.0
# Automatically-generated file. Do not edit!
################################################################################
-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include StdPeriphDriver/subdir.mk
-include Startup/subdir.mk
-include RVMSIS/subdir.mk
-include Profile/subdir.mk
-include HAL/subdir.mk
-include APP/subdir.mk
-include subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(S_DEPS)),)
-include $(S_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(ASM_DEPS)),)
-include $(ASM_DEPS)
endif
ifneq ($(strip $(ASM_UPPER_DEPS)),)
-include $(ASM_UPPER_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables 
SECONDARY_FLASH += \
BackupUpgrade_OTA.hex \

SECONDARY_LIST += \
BackupUpgrade_OTA.lst \

SECONDARY_SIZE += \
BackupUpgrade_OTA.siz \


# All Target
all: 
	$(MAKE) --no-print-directory main-build 

main-build: BackupUpgrade_OTA.elf secondary-outputs

# Tool invocations
BackupUpgrade_OTA.elf: $(OBJS) $(USER_OBJS_ESCAPE)
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -T "f:/code/Fanciol_Ctl/BLE/Peripheral/Ld/Link.ld" -nostartfiles -Xlinker --gc-sections -L"f:\code\Fanciol_Ctl\BLE\Peripheral" -L"f:/code/Fanciol_Ctl/BLE/LIB" -L"f:/code/Fanciol_Ctl/SRC/StdPeriphDriver" -Xlinker --print-memory-usage -Wl,-Map,"BackupUpgrade_OTA.map" --specs=nano.specs --specs=nosys.specs -o "BackupUpgrade_OTA.elf" $(OBJS) $(LIBS)
BackupUpgrade_OTA.hex: BackupUpgrade_OTA.elf
	@	riscv-none-embed-objcopy -O ihex "BackupUpgrade_OTA.elf" "BackupUpgrade_OTA.hex"
BackupUpgrade_OTA.lst: BackupUpgrade_OTA.elf
	@	riscv-none-embed-objdump --source --all-headers --demangle --line-numbers --wide "BackupUpgrade_OTA.elf" > "BackupUpgrade_OTA.lst"
BackupUpgrade_OTA.siz: BackupUpgrade_OTA.elf
	riscv-none-embed-size --format=berkeley "BackupUpgrade_OTA.elf"

# Other Targets
clean:
	-$(RM) $(DIR_OBJS) $(SECONDARY_FLASH)$(SECONDARY_LIST)$(SECONDARY_SIZE) BackupUpgrade_OTA.elf
	-$(RM) $(DIR_EXPANDS) $(CALLGRAPH_DOT)
	-$(RM) $(DIR_DEPS)
secondary-outputs: $(SECONDARY_FLASH) $(SECONDARY_LIST) $(SECONDARY_SIZE)

.PHONY: all clean dependents

-include ../makefile.targets