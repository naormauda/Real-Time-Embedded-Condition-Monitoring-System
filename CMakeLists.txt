cmake_minimum_required(VERSION 3.22)

# Set compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME smart_safe)

# Enable compile command to ease indexing
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# --- User Custom Sections ---
# Note: These are commented out to avoid "incorrect number of arguments" errors.
# Uncomment and add your files/paths when needed.

# target_link_directories(${CMAKE_PROJECT_NAME}
#     # Add user defined library search paths here
# )

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/actuator_driver.c
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Inc
)

# target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
#     # Add user defined symbols here
# )

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries - Matching CubeMX plain signature
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
)



# Force the output file to have the .elf extension
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME "${CMAKE_PROJECT_NAME}.elf")

# Post-build steps to generate .bin and .hex files
# =================================================

set(OBJCOPY "D:/bin/arm-none-eabi-objcopy.exe")
set(SIZE "D:/bin/arm-none-eabi-size.exe")

# Print the size of the generated ELF file
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${SIZE} "${CMAKE_PROJECT_NAME}.elf"
)

# Create .bin file from the .elf file
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${OBJCOPY} -O binary "${CMAKE_PROJECT_NAME}.elf" "${CMAKE_PROJECT_NAME}.bin"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Create .hex file from the .elf file
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${OBJCOPY} -O ihex "${CMAKE_PROJECT_NAME}.elf" "${CMAKE_PROJECT_NAME}.hex"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)