# Vendored CMSIS and LL sources

Copied unmodified from ST's repositories; only `stm32f4xx_ll_bus.h`,
`stm32f4xx_ll_gpio.h`, `stm32f4xx_ll_pwr.h`, `stm32f4xx_ll_rcc.h` and
`stm32f4xx_ll_system.h` are included, not the whole HAL. Each file keeps
its original ST license header.

| Source | Commit | Path |
|---|---|---|
| [STMicroelectronics/cmsis_device_f4](https://github.com/STMicroelectronics/cmsis_device_f4) | `8c79dcf2f1d866d139a5d66240083ae6194b6dd1` | `Include/stm32f4xx.h`, `Include/stm32f411xe.h`, `Include/system_stm32f4xx.h`, `Source/Templates/system_stm32f4xx.c`, `Source/Templates/gcc/startup_stm32f411xe.s` |
| [STMicroelectronics/cmsis_core](https://github.com/STMicroelectronics/cmsis_core) | `01d18147527e30ed69ace1cb03b818de78bc7062` | `CMSIS/Core/Include/core_cm4.h`, `cmsis_compiler.h`, `cmsis_gcc.h`, `cmsis_version.h`, `m-profile/armv7m_mpu.h`, `m-profile/cmsis_gcc_m.h` |
| [STMicroelectronics/stm32f4xx_hal_driver](https://github.com/STMicroelectronics/stm32f4xx_hal_driver) | `6ef30cf0ef6cc29ad518f37b9e6de89b6b815f90` | `Inc/stm32f4xx_ll_bus.h`, `stm32f4xx_ll_gpio.h`, `stm32f4xx_ll_pwr.h`, `stm32f4xx_ll_rcc.h`, `stm32f4xx_ll_system.h` |

`startup_stm32f411xe.s` has one local change: the call to
`__libc_init_array` is removed, because this bare-metal slice has no C++
constructors and does not link newlib startup files.
