# Real-Time Embedded Condition Monitoring System  
### with On-Device Anomaly Detection

## 📖 Project Overview

This project implements a **real-time embedded condition monitoring system**
designed to run entirely on-device (edge), without reliance on cloud processing.

The system continuously monitors sensor data (vibration, motion, events),
learns normal behavior patterns, detects anomalies in real time,
and reacts immediately using local actuators and alerts.

The project is designed as an **industrial-style embedded system**, focusing on:
- Deterministic timing
- Robust architecture
- Clear separation of responsibilities
- Real-time constraints

This is **not a gadget** and **not a demo-only project** —  
it is a structured embedded system intended to demonstrate professional
real-time embedded design practices.

---

## 🎯 Motivation

In real industrial, automotive, and security systems:

- Continuous cloud streaming is not feasible
- Simple threshold-based logic is insufficient
- Real-time response is mandatory
- Systems must operate reliably and autonomously at the edge

This project demonstrates how such a system can be architected and implemented
on a resource-constrained microcontroller.

---

## 🧠 System Capabilities (High-Level)

- Real-time sensor sampling using hardware timers
- Interrupt-driven, DMA-based data acquisition
- On-device anomaly detection (TinyML)
- Event-driven finite state machine (FSM)
- Immediate local reaction (alerts, locking, UI)
- Modular, maintainable software architecture

---

## 🏗️ System Architecture (Conceptual)

[ Sensors ]
↓
[ Timer ISR / DMA ]
↓
[ RTOS Tasks ]
↓
[ ML Processing ]
↓
[ Decision FSM ]
↓
[ Actuators / UI / Logging ]


Key architectural principles:
- ISRs are minimal and non-blocking
- All heavy processing runs in RTOS tasks
- Data flows through queues and events
- Decision logic is isolated from hardware drivers

---

## 🧵 Software Architecture

The system is structured into layered components:

- **Hardware Layer**  
  Low-level drivers (Timers, SPI, I2C, DMA, GPIO)

- **Sensor Tasks**  
  Responsible for data acquisition and buffering

- **Processing / ML Task**  
  Filtering, feature extraction, anomaly scoring

- **Decision Layer (FSM)**  
  State management and system logic

- **Output / UI Tasks**  
  LEDs, buzzer, servo lock, display

Each component has a clearly defined responsibility to ensure
deterministic behavior and maintainability.

---

## ⚙️ Target Platform

- **MCU**: STM32H563ZI (Cortex-M33 @ 250MHz)
- **Board**: NUCLEO-H563ZI
- **RTOS**: FreeRTOS
- **Language**: C / C++

---

## 📦 Hardware Components

**Implemented:**
- ✅ **LIS3DH** - 3-axis accelerometer (SPI1)
  - Connected via SPI (CPOL=1, CPHA=1, Mode 3)
  - Chip Select: PA4 (GPIO manual control)
  - Configurable ODR: 1Hz - 400Hz
  - Configurable range: ±2g / ±4g / ±8g / ±16g
  - 12-bit high resolution mode

**Planned:**
- ⏳ Motion / proximity sensor (event-driven)
- ⏳ OLED display (I2C1 @ 400kHz)
- ⏳ LEDs and buzzer for alerts
- ⏳ Servo motor for lock demonstration

> Hardware selection is driven by architectural requirements,
> not the other way around.

---

## 🚧 Project Status

**Current Phase**:  
🟢 Stage 2 – Hardware Bring-Up **COMPLETED**  
🟢 Stage 3 – Driver Development **IN PROGRESS**  
🟡 Stage 4 – RTOS Integration (Next)

**Completed:**
- ✅ Clock tree configuration (250 MHz via PLL)
- ✅ Peripheral configuration (SPI1, I2C1, GPIO)
- ✅ CubeMX code generation
- ✅ LIS3DH accelerometer driver (full implementation)
  - Hardware abstraction layer with SPI communication
  - Support for all ODR rates (1Hz - 400Hz)
  - Support for all ranges (±2g, ±4g, ±8g, ±16g)
  - Support for all operating modes (low power, normal, high resolution)
  - Simulation mode for development without hardware
  - Data conversion (raw to milli-g)

**In Progress:**
- 🔄 Driver testing and validation
- 🔄 Basic sensor reading implementation

**Next Steps:**
- FreeRTOS integration via CubeMX
- Sensor acquisition task
- Data buffering and queue management

---

## 📂 Repository Structure

```
/Core
  /Inc
    lis3dh_driver.h       ✅ LIS3DH accelerometer driver header
    main.h                ✅ Main application header
    stm32h5xx_hal_conf.h  ✅ HAL configuration
    stm32h5xx_it.h        ✅ Interrupt handlers
  /Src
    lis3dh_driver.c       ✅ LIS3DH driver implementation
    main.c                ✅ Main application
    stm32h5xx_hal_msp.c   ✅ HAL MSP initialization
    stm32h5xx_it.c        ✅ Interrupt service routines
    system_stm32h5xx.c    ✅ System initialization

/Drivers
  /CMSIS                  ✅ ARM CMSIS libraries
  /STM32H5xx_HAL_Driver   ✅ STM32 HAL drivers (GPIO, SPI, DMA, etc.)

/cmake                    ✅ CMake build configuration
/build                    ✅ Build artifacts (excluded from git)

CMakeLists.txt            ✅ Root CMake configuration
smart_safe.ioc            ✅ STM32CubeMX project file
README.md                 ✅ This file
LICENSE                   ✅ MIT License

Future additions:
  /Core/Src
    sensor_task.c         ⏳ Sensor acquisition task
    ml_task.c             ⏳ ML processing task
    fsm_task.c            ⏳ State machine task
    output_task.c         ⏳ Output control task
  /docs
    architecture.md       ⏳ Architecture documentation
    fsm.md                ⏳ FSM design
    timing.md             ⏳ Timing analysis
```


---

## 🧪 Design Philosophy

- Deterministic over fast
- Clear over clever
- Maintainable over minimal
- Architecture before implementation

---

## ⏱️ System Configuration

**Clock Configuration:**
- External HSE crystal: 8 MHz (board-mounted)
- System clock (SYSCLK): 250 MHz via PLL
- AHB clock: 250 MHz
- APB1/APB2/APB3: Configured via CubeMX
- Clock tree validated and stable

**Peripheral Configuration:**
- **SPI1** (Accelerometer):
  - Mode: Full-Duplex Master
  - Clock polarity: High (CPOL=1)
  - Clock phase: 2nd Edge (CPHA=1)
  - Data size: 8-bit
  - First bit: MSB first
  - CS: PA4 (GPIO manual control)
  - SCK: PA5, MISO: PA6, MOSI: PA7

- **I2C1** (Future - Display/Sensors):
  - Speed: 400 kHz (Fast Mode)
  - Configuration ready, not yet used

- **GPIO**:
  - PA4: LIS3DH_CS (Output, initially HIGH)
  - Additional GPIOs configured for LEDs/buttons (standard NUCLEO)

---

## 📜 License

This project is released under the MIT License.
