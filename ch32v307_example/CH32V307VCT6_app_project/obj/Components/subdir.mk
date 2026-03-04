################################################################################
# MRS Version: 1.9.2
# 自动生成的文件。不要编辑！
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Components/boot_port_app_ch32v307.c \
../Components/easy_bootloader_app.c \
../Components/ringbuffer.c 

OBJS += \
./Components/boot_port_app_ch32v307.o \
./Components/easy_bootloader_app.o \
./Components/ringbuffer.o 

C_DEPS += \
./Components/boot_port_app_ch32v307.d \
./Components/easy_bootloader_app.d \
./Components/ringbuffer.d 


# Each subdirectory must supply rules for building sources it contributes
Components/%.o: ../Components/%.c
	@	@	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized  -g -I"D:\easy_bootloader_project\ch32v307_example\CH32V307VCT6_app_project\Debug" -I"D:\easy_bootloader_project\ch32v307_example\CH32V307VCT6_app_project\Core" -I"D:\easy_bootloader_project\ch32v307_example\CH32V307VCT6_app_project\User" -I"D:\easy_bootloader_project\ch32v307_example\CH32V307VCT6_app_project\Peripheral\inc" -I"D:\easy_bootloader_project\ch32v307_example\CH32V307VCT6_app_project\Components" -I"D:\easy_bootloader_project\ch32v307_example\CH32V307VCT6_app_project\Myapp" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@	@

