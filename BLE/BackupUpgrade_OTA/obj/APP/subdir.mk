################################################################################
# MRS Version: 2.3.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../APP/peripheral.c \
../APP/peripheral_main.c 

C_DEPS += \
./APP/peripheral.d \
./APP/peripheral_main.d 

OBJS += \
./APP/peripheral.o \
./APP/peripheral_main.o 

DIR_OBJS += \
./APP/*.o \

DIR_DEPS += \
./APP/*.d \

DIR_EXPANDS += \
./APP/*.234r.expand \


# Each subdirectory must supply rules for building sources it contributes
APP/%.o: ../APP/%.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -DBLE_BUFF_MAX_LEN=251 -I"f:/code/Fanciol_Ctl/BLE/BackupUpgrade_OTA/Startup" -I"f:/code/Fanciol_Ctl/BLE/BackupUpgrade_OTA/APP/include" -I"f:/code/Fanciol_Ctl/BLE/BackupUpgrade_OTA/Profile/include" -I"f:/code/Fanciol_Ctl/SRC/StdPeriphDriver/inc" -I"f:/code/Fanciol_Ctl/BLE/HAL/include" -I"f:/code/Fanciol_Ctl/BLE/BackupUpgrade_OTA/Ld" -I"f:/code/Fanciol_Ctl/BLE/LIB" -I"f:/code/Fanciol_Ctl/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

