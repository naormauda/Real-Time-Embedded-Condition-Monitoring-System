# Toolchain file for STM32H563ZI (Cortex-M33)
# =================================================

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)


set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_COMPILER "D:/bin/arm-none-eabi-gcc.exe")
set(CMAKE_CXX_COMPILER "D:/bin/arm-none-eabi-g++.exe")
set(CMAKE_ASM_COMPILER "D:/bin/arm-none-eabi-gcc.exe")

set(MCU_FLAGS "-mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard")

set(CMAKE_C_FLAGS "${MCU_FLAGS} -ffunction-sections -fdata-sections -Wall")
set(CMAKE_CXX_FLAGS "${MCU_FLAGS} -ffunction-sections -fdata-sections -Wall")
set(CMAKE_ASM_FLAGS "${MCU_FLAGS}")

set(LINKER_SCRIPT "D:/project_real_time_embedded/smart_safe/ld/STM32H563ZI_FLASH.ld")
set(CMAKE_EXE_LINKER_FLAGS "${MCU_FLAGS} -T\"${LINKER_SCRIPT}\" -Wl,--gc-sections")