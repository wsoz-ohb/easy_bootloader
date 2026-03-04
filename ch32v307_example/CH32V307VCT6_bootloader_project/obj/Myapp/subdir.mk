################################################################################
# MRS Version: 1.9.2
# 自动生成的文件。不要编辑！
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Myapp/mytimer.c \
../Myapp/myuart.c \
../Myapp/scheduler.c 

OBJS += \
./Myapp/mytimer.o \
./Myapp/myuart.o \
./Myapp/scheduler.o 

C_DEPS += \
./Myapp/mytimer.d \
./Myapp/myuart.d \
./Myapp/scheduler.d 


# Each subdirectory must supply rules for building sources it contributes
Myapp/%.o: ../Myapp/%.c
	@	@	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized  -g -I"D:\easy_bootloader_project\ch32v307_example\CH32V307VCT6_bootloader_project\Debug" -I"D:\easy_bootloader_project\ch32v307_example\CH32V307VCT6_bootloader_project\Core" -I"D:\easy_bootloader_project\ch32v307_example\CH32V307VCT6_bootloader_project\User" -I"D:\easy_bootloader_project\ch32v307_example\CH32V307VCT6_bootloader_project\Peripheral\inc" -I"D:\easy_bootloader_project\ch32v307_example\CH32V307VCT6_bootloader_project\Myapp" -I"D:\easy_bootloader_project\ch32v307_example\CH32V307VCT6_bootloader_project\Components" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@	@

