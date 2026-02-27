################################################################################
# MRS Version: 2.3.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
f:/code/Fanciol_Ctl/BLE/HAL/MCU.c \
f:/code/Fanciol_Ctl/BLE/HAL/RTC.c \
f:/code/Fanciol_Ctl/BLE/HAL/SLEEP.c 

C_DEPS += \
./HAL/MCU.d \
./HAL/RTC.d \
./HAL/SLEEP.d 

OBJS += \
./HAL/MCU.o \
./HAL/RTC.o \
./HAL/SLEEP.o 

DIR_OBJS += \
./HAL/*.o \

DIR_DEPS += \
./HAL/*.d \

DIR_EXPANDS += \
./HAL/*.234r.expand \


# Each subdirectory must supply rules for building sources it contributes
HAL/MCU.o: f:/code/Fanciol_Ctl/BLE/HAL/MCU.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -DBLE_BUFF_MAX_LEN=251 -I"f:/code/Fanciol_Ctl/BLE/BackupUpgrade_OTA/Startup" -I"f:/code/Fanciol_Ctl/BLE/BackupUpgrade_OTA/APP/include" -I"f:/code/Fanciol_Ctl/BLE/BackupUpgrade_OTA/Profile/include" -I"f:/code/Fanciol_Ctl/SRC/StdPeriphDriver/inc" -I"f:/code/Fanciol_Ctl/BLE/HAL/include" -I"f:/code/Fanciol_Ctl/BLE/BackupUpgrade_OTA/Ld" -I"f:/code/Fanciol_Ctl/BLE/LIB" -I"f:/code/Fanciol_Ctl/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
HAL/RTC.o: f:/code/Fanciol_Ctl/BLE/HAL/RTC.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -DBLE_BUFF_MAX_LEN=251 -I"f:/code/Fanciol_Ctl/BLE/BackupUpgrade_OTA/Startup" -I"f:/code/Fanciol_Ctl/BLE/BackupUpgrade_OTA/APP/include" -I"f:/code/Fanciol_Ctl/BLE/BackupUpgrade_OTA/Profile/include" -I"f:/code/Fanciol_Ctl/SRC/StdPeriphDriver/inc" -I"f:/code/Fanciol_Ctl/BLE/HAL/include" -I"f:/code/Fanciol_Ctl/BLE/BackupUpgrade_OTA/Ld" -I"f:/code/Fanciol_Ctl/BLE/LIB" -I"f:/code/Fanciol_Ctl/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
HAL/SLEEP.o: f:/code/Fanciol_Ctl/BLE/HAL/SLEEP.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -DBLE_BUFF_MAX_LEN=251 -I"f:/code/Fanciol_Ctl/BLE/BackupUpgrade_OTA/Startup" -I"f:/code/Fanciol_Ctl/BLE/BackupUpgrade_OTA/APP/include" -I"f:/code/Fanciol_Ctl/BLE/BackupUpgrade_OTA/Profile/include" -I"f:/code/Fanciol_Ctl/SRC/StdPeriphDriver/inc" -I"f:/code/Fanciol_Ctl/BLE/HAL/include" -I"f:/code/Fanciol_Ctl/BLE/BackupUpgrade_OTA/Ld" -I"f:/code/Fanciol_Ctl/BLE/LIB" -I"f:/code/Fanciol_Ctl/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

