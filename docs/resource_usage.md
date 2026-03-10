# Resource Usage Report

Date: 2026-03-10  
Project: `smart_safe`  
Scope: Resume checklist item `R7`

## Build Artifact Inputs

Commands used:

```powershell
D:/bin/arm-none-eabi-size.exe build/smart_safe.elf
D:/bin/arm-none-eabi-objdump.exe -h build/smart_safe.elf
Get-Item build/smart_safe.elf, build/smart_safe.bin, build/smart_safe.hex | Select-Object Name,Length
```

Memory limits (linker):
- FLASH: `2032 KiB`
- RAM: `640 KiB`

Reference: `ld/STM32H563ZI_FLASH.ld:40`

## ELF Section Summary

`arm-none-eabi-size` output:

- `text = 216996`
- `data = 2268`
- `bss = 42680`
- `dec = 261944`

Derived values:
- Flash runtime footprint (`text + data`): `219264 B` (`214.13 KiB`)
- RAM runtime footprint (`data + bss`): `44948 B` (`43.89 KiB`)
- RAM with linker min heap/stack reservation (`+ 0x604`): `46488 B` (`45.40 KiB`)

## Capacity Utilization

Against linker limits:

- Flash usage: `219264 / 2080768 = 10.54%`
- RAM usage (`data+bss`): `44948 / 655360 = 6.86%`
- RAM usage incl. min heap/stack reservation: `46488 / 655360 = 7.09%`

## Binary Artifact Sizes

From file system:

- `build/smart_safe.elf`: `945420 B`
- `build/smart_safe.bin`: `219268 B`
- `build/smart_safe.hex`: `616821 B`

Note:
- `.elf` includes symbols/debug sections and is expected to be larger than flash image size.
- `.bin` tracks loadable image size and aligns with `text + data` accounting.

## Key Section Notes

Important sections from `objdump -h`:
- `.text`: `0x25bb0` (code)
- `.rodata`: `0x0f194` (const data)
- `.data`: `0x008dc` (initialized RAM)
- `.bss`: `0x0a0b4` (zero-init RAM)
- `._user_heap_stack`: `0x00604` (linker reservation)

## Conclusion

`R7` acceptance achieved: firmware memory/flash footprint is measured from build outputs and comfortably within STM32H563 configured memory budgets.
