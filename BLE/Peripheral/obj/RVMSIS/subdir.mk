################################################################################
# MRS Version: 2.3.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
f:/code/Fanciol_Ctl/SRC/RVMSIS/core_riscv.c 

C_DEPS += \
./RVMSIS/core_riscv.d 

OBJS += \
./RVMSIS/core_riscv.o 

DIR_OBJS += \
./RVMSIS/*.o \

DIR_DEPS += \
./RVMSIS/*.d \

DIR_EXPANDS += \
./RVMSIS/*.234r.expand \


# Each subdirectory must supply rules for building sources it contributes
RVMSIS/core_riscv.o: f:/code/Fanciol_Ctl/SRC/RVMSIS/core_riscv.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -DBLE_BUFF_MAX_LEN=251 -I"f:/code/Fanciol_Ctl/BLE/Peripheral/Startup" -I"f:/code/Fanciol_Ctl/BLE/Peripheral/APP/include" -I"f:/code/Fanciol_Ctl/BLE/Peripheral/Profile/include" -I"f:/code/Fanciol_Ctl/SRC/StdPeriphDriver/inc" -I"f:/code/Fanciol_Ctl/BLE/HAL/include" -I"f:/code/Fanciol_Ctl/BLE/Peripheral/Ld" -I"f:/code/Fanciol_Ctl/BLE/LIB" -I"f:/code/Fanciol_Ctl/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

