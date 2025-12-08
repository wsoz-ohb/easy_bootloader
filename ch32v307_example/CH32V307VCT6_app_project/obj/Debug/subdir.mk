################################################################################
# MRS Version: 1.9.2
# 自动生成的文件。不要编辑！
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Debug/debug.c 

OBJS += \
./Debug/debug.o 

C_DEPS += \
./Debug/debug.d 


# Each subdirectory must supply rules for building sources it contributes
Debug/%.o: ../Debug/%.c
	@	@	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized  -g -I"D:\easy_bootloader_project\ch32v307_example\CH32V307VCT6_app_project\Debug" -I"D:\easy_bootloader_project\ch32v307_example\CH32V307VCT6_app_project\Core" -I"D:\easy_bootloader_project\ch32v307_example\CH32V307VCT6_app_project\User" -I"D:\easy_bootloader_project\ch32v307_example\CH32V307VCT6_app_project\Peripheral\inc" -I"D:\easy_bootloader_project\ch32v307_example\CH32V307VCT6_app_project\Components" -I"D:\easy_bootloader_project\ch32v307_example\CH32V307VCT6_app_project\Myapp" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@	@

