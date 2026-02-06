################################################################################
# MRS Version: 2.3.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../APP/b_cdc.c \
../APP/board.c \
../APP/flash.c \
../APP/ir.c \
../APP/lora.c \
../APP/peripheral.c \
../APP/peripheral_main.c \
../APP/timer.c 

C_DEPS += \
./APP/b_cdc.d \
./APP/board.d \
./APP/flash.d \
./APP/ir.d \
./APP/lora.d \
./APP/peripheral.d \
./APP/peripheral_main.d \
./APP/timer.d 

OBJS += \
./APP/b_cdc.o \
./APP/board.o \
./APP/flash.o \
./APP/ir.o \
./APP/lora.o \
./APP/peripheral.o \
./APP/peripheral_main.o \
./APP/timer.o 

DIR_OBJS += \
./APP/*.o \

DIR_DEPS += \
./APP/*.d \

DIR_EXPANDS += \
./APP/*.234r.expand \


# Each subdirectory must supply rules for building sources it contributes
APP/%.o: ../APP/%.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

