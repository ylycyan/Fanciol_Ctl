################################################################################
# MRS Version: 2.3.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/MCU.c \
f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/RTC.c \
f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/SLEEP.c 

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
HAL/MCU.o: f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/MCU.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
HAL/RTC.o: f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/RTC.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
HAL/SLEEP.o: f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/SLEEP.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

