################################################################################
# MRS Version: 2.3.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_adc.c \
f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_clk.c \
f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_flash.c \
f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_gpio.c \
f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_i2c.c \
f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_pwm.c \
f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_pwr.c \
f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_spi0.c \
f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_spi1.c \
f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_sys.c \
f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_timer0.c \
f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_timer1.c \
f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_timer2.c \
f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_timer3.c \
f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_uart0.c \
f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_uart1.c \
f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_uart2.c \
f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_uart3.c \
f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_usb2dev.c \
f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_usb2hostBase.c \
f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_usb2hostClass.c \
f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_usbdev.c \
f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_usbhostBase.c \
f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_usbhostClass.c 

C_DEPS += \
./StdPeriphDriver/CH58x_adc.d \
./StdPeriphDriver/CH58x_clk.d \
./StdPeriphDriver/CH58x_flash.d \
./StdPeriphDriver/CH58x_gpio.d \
./StdPeriphDriver/CH58x_i2c.d \
./StdPeriphDriver/CH58x_pwm.d \
./StdPeriphDriver/CH58x_pwr.d \
./StdPeriphDriver/CH58x_spi0.d \
./StdPeriphDriver/CH58x_spi1.d \
./StdPeriphDriver/CH58x_sys.d \
./StdPeriphDriver/CH58x_timer0.d \
./StdPeriphDriver/CH58x_timer1.d \
./StdPeriphDriver/CH58x_timer2.d \
./StdPeriphDriver/CH58x_timer3.d \
./StdPeriphDriver/CH58x_uart0.d \
./StdPeriphDriver/CH58x_uart1.d \
./StdPeriphDriver/CH58x_uart2.d \
./StdPeriphDriver/CH58x_uart3.d \
./StdPeriphDriver/CH58x_usb2dev.d \
./StdPeriphDriver/CH58x_usb2hostBase.d \
./StdPeriphDriver/CH58x_usb2hostClass.d \
./StdPeriphDriver/CH58x_usbdev.d \
./StdPeriphDriver/CH58x_usbhostBase.d \
./StdPeriphDriver/CH58x_usbhostClass.d 

OBJS += \
./StdPeriphDriver/CH58x_adc.o \
./StdPeriphDriver/CH58x_clk.o \
./StdPeriphDriver/CH58x_flash.o \
./StdPeriphDriver/CH58x_gpio.o \
./StdPeriphDriver/CH58x_i2c.o \
./StdPeriphDriver/CH58x_pwm.o \
./StdPeriphDriver/CH58x_pwr.o \
./StdPeriphDriver/CH58x_spi0.o \
./StdPeriphDriver/CH58x_spi1.o \
./StdPeriphDriver/CH58x_sys.o \
./StdPeriphDriver/CH58x_timer0.o \
./StdPeriphDriver/CH58x_timer1.o \
./StdPeriphDriver/CH58x_timer2.o \
./StdPeriphDriver/CH58x_timer3.o \
./StdPeriphDriver/CH58x_uart0.o \
./StdPeriphDriver/CH58x_uart1.o \
./StdPeriphDriver/CH58x_uart2.o \
./StdPeriphDriver/CH58x_uart3.o \
./StdPeriphDriver/CH58x_usb2dev.o \
./StdPeriphDriver/CH58x_usb2hostBase.o \
./StdPeriphDriver/CH58x_usb2hostClass.o \
./StdPeriphDriver/CH58x_usbdev.o \
./StdPeriphDriver/CH58x_usbhostBase.o \
./StdPeriphDriver/CH58x_usbhostClass.o 

DIR_OBJS += \
./StdPeriphDriver/*.o \

DIR_DEPS += \
./StdPeriphDriver/*.d \

DIR_EXPANDS += \
./StdPeriphDriver/*.234r.expand \


# Each subdirectory must supply rules for building sources it contributes
StdPeriphDriver/CH58x_adc.o: f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_adc.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH58x_clk.o: f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_clk.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH58x_flash.o: f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_flash.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH58x_gpio.o: f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_gpio.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH58x_i2c.o: f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_i2c.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH58x_pwm.o: f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_pwm.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH58x_pwr.o: f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_pwr.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH58x_spi0.o: f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_spi0.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH58x_spi1.o: f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_spi1.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH58x_sys.o: f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_sys.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH58x_timer0.o: f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_timer0.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH58x_timer1.o: f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_timer1.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH58x_timer2.o: f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_timer2.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH58x_timer3.o: f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_timer3.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH58x_uart0.o: f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_uart0.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH58x_uart1.o: f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_uart1.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH58x_uart2.o: f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_uart2.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH58x_uart3.o: f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_uart3.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH58x_usb2dev.o: f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_usb2dev.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH58x_usb2hostBase.o: f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_usb2hostBase.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH58x_usb2hostClass.o: f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_usb2hostClass.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH58x_usbdev.o: f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_usbdev.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH58x_usbhostBase.o: f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_usbhostBase.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH58x_usbhostClass.o: f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/CH58x_usbhostClass.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Startup" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/APP/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/Peripheral/Profile/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/StdPeriphDriver/inc" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/HAL/include" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/Ld" -I"f:/下载工具包/CH583EVT/EVT/EXAM/BLE/LIB" -I"f:/下载工具包/CH583EVT/EVT/EXAM/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

