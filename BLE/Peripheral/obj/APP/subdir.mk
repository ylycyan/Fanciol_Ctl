################################################################################
# MRS Version: 2.3.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../APP/adc.c \
../APP/b_cdc.c \
../APP/board.c \
../APP/flash.c \
../APP/ir.c \
../APP/led.c \
../APP/lora.c \
../APP/peripheral.c \
../APP/peripheral_main.c \
../APP/timer.c \
../APP/util.c 

C_DEPS += \
./APP/adc.d \
./APP/b_cdc.d \
./APP/board.d \
./APP/flash.d \
./APP/ir.d \
./APP/led.d \
./APP/lora.d \
./APP/peripheral.d \
./APP/peripheral_main.d \
./APP/timer.d \
./APP/util.d 

OBJS += \
./APP/adc.o \
./APP/b_cdc.o \
./APP/board.o \
./APP/flash.o \
./APP/ir.o \
./APP/led.o \
./APP/lora.o \
./APP/peripheral.o \
./APP/peripheral_main.o \
./APP/timer.o \
./APP/util.o 

DIR_OBJS += \
./APP/*.o \

DIR_DEPS += \
./APP/*.d \

DIR_EXPANDS += \
./APP/*.234r.expand \


# Each subdirectory must supply rules for building sources it contributes
APP/%.o: ../APP/%.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -DBLE_BUFF_MAX_LEN=251 -I"f:/code/Fanciol_Ctl/BLE/Peripheral/Startup" -I"f:/code/Fanciol_Ctl/BLE/Peripheral/APP/include" -I"f:/code/Fanciol_Ctl/BLE/Peripheral/Profile/include" -I"f:/code/Fanciol_Ctl/SRC/StdPeriphDriver/inc" -I"f:/code/Fanciol_Ctl/BLE/HAL/include" -I"f:/code/Fanciol_Ctl/BLE/Peripheral/Ld" -I"f:/code/Fanciol_Ctl/BLE/LIB" -I"f:/code/Fanciol_Ctl/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

