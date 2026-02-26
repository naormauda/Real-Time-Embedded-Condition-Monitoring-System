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

### Implemented Task Pipeline

```
SensorTask (10ms)          DistanceTask (50ms)
   ↓ (accel data)             ↓ (ToF distance)
   └─→ sensorQueue             └─→ distanceQueue
            ↓                          ↓
            └──────→ ProcessingTask ←──┘
                    (fuses sensors)
                          ↓
                    processingQueue
                          ↓
                     FsmTask
                  (state machine)
                          ↓
                    outputQueue
                          ↓
                    OutputTask
                 (execute decisions)
```

**Task Details:**
- **SensorTask**: Reads LIS3DSH accelerometer, computes motion magnitude, calibrates baseline
- **DistanceTask**: Reads VL53L1X ToF sensor, filters distance changes >50mm
- **ProcessingTask**: Drains both queues, fuses motion + proximity data at 50Hz
- **FsmTask**: Implements 3-state FSM (IDLE/ALERT/LOCK) with dual-threat detection
- **OutputTask**: Executes state-based actions (currently serial logging, ready for actuators)

---

## ⚙️ Target Platform

- **MCU**: STM32H563ZI (Cortex-M33 @ 250MHz)
- **Board**: NUCLEO-H563ZI
- **RTOS**: FreeRTOS
- **Language**: C / C++

---

## 📦 Hardware Components

**Implemented:**
- ✅ **LIS3DSH** - 3-axis accelerometer (SPI1)
  - Connected via SPI (CPOL=1, CPHA=1, Mode 3)
  - Chip Select: PD14 (GPIO manual control)
  - WHO_AM_I: 0x3F
  - Note: Module silkscreen may show LIS3DH, but measured ID is LIS3DSH

- ✅ **VL53L1X** - Time-of-Flight distance sensor (I2C1)
  - Address: 0x29 (7-bit)
  - Pins: PB8 (SCL), PB9 (SDA)
  - Mode: Long distance, 10 Hz (50 ms timing budget, 100 ms inter-measurement)
  - Driver: Official ST VL53L1 API

**Planned:**
- ⏳ OLED display (I2C1 @ 400kHz)
- ⏳ LEDs and buzzer for alerts
- ⏳ Servo motor for lock demonstration

> Hardware selection is driven by architectural requirements,
> not the other way around.

---

## 🚧 Project Status

**Current Phase**:  
🟢 Stage 2 – Hardware Bring-Up **COMPLETED**  
🟢 Stage 3 – Driver Development **COMPLETED**  
� Stage 4 – RTOS Integration **COMPLETED**  
🟡 Stage 5 – ML Integration (Next)

**Completed:**
- ✅ Clock tree configuration (250 MHz via PLL)
- ✅ Peripheral configuration (SPI1, I2C1, GPIO)
- ✅ CubeMX code generation with FreeRTOS
- ✅ LIS3DSH accelerometer SPI bring-up
  - WHO_AM_I verification (0x3F)
  - Basic initialization (CTRL_REG4 = 0x67)
  - Raw data read + mg conversion
  - Calibration + EMA smoothing
- ✅ VL53L1X ToF sensor I2C bring-up
  - Full ST VL53L1 API integration
  - Ranging at 10 Hz (long distance mode)
- ✅ **Multi-sensor fusion pipeline**
  - SensorTask: Accelerometer acquisition (10Hz)
  - DistanceTask: ToF ranging (10Hz)
  - ProcessingTask: Dual-sensor data fusion
  - FsmTask: 3-state decision logic (IDLE/ALERT/LOCK)
  - OutputTask: State-based actions
- ✅ **FSM State Machine**
  - IDLE → ALERT: Object proximity (<500mm) OR motion (>1500mg)
  - ALERT → LOCK: Sustained dual threat (motion + proximity)
  - LOCK → IDLE: Auto-reset after 5s timeout
- ✅ Queue-based inter-task communication
- ✅ Stack monitoring and health logging

**In Progress:**
- 🔄 Feature extraction for ML input

**Next Steps:**
- TinyML model integration (TensorFlow Lite Micro)
- Train anomaly detection model
- Physical actuators (LED, buzzer, servo)

---

## 📂 Repository Structure

```
/Core
  /Inc
    lis3dsh_driver.h      ✅ LIS3DSH accelerometer driver header
    main.h                ✅ Main application header
    stm32h5xx_hal_conf.h  ✅ HAL configuration
    stm32h5xx_it.h        ✅ Interrupt handlers
    app_freertos.h        ✅ FreeRTOS task declarations
    FreeRTOSConfig.h      ✅ FreeRTOS configuration
  /Src
    lis3dsh_driver.c      ✅ LIS3DSH driver implementation
    app_freertos.c        ✅ FreeRTOS tasks and sensor fusion pipeline
    main.c                ✅ Main application and peripheral init
    stm32h5xx_hal_msp.c   ✅ HAL MSP initialization
    stm32h5xx_it.c        ✅ Interrupt service routines
    system_stm32h5xx.c    ✅ System initialization

/Drivers
  /CMSIS                  ✅ ARM CMSIS libraries
  /STM32H5xx_HAL_Driver   ✅ STM32 HAL drivers (GPIO, SPI, I2C, TIM, DMA, etc.)
  /VL53L1X_API            ✅ ST VL53L1X driver (core + platform)
    /API
      /core               ✅ VL53L1 core ranging functions
      /platform           ✅ STM32 HAL I2C platform layer

/Middlewares
  /Third_Party
    /FreeRTOS             ✅ FreeRTOS kernel and CMSIS-RTOS v2

/cmake                    ✅ CMake build configuration
/build                    ✅ Build artifacts (excluded from git)

CMakeLists.txt            ✅ Root CMake configuration
smart_safe.ioc            ✅ STM32CubeMX project file
README.md                 ✅ This file
LICENSE                   ✅ MIT License

Future additions:
  /Core/Src
    ml_model.c/h          ⏳ TensorFlow Lite Micro model
    feature_extraction.c  ⏳ Signal processing for ML features
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
  - CS: PD14 (GPIO manual control)
  - SCK: PA5, MISO: PG9, MOSI: PB5

- **I2C1** (VL53L1X ToF):
  - Speed: 400 kHz
  - SCL: PB8, SDA: PB9
  - 7-bit address: 0x29

- **GPIO**:
  - PD14: LIS3DSH_CS (Output, initially HIGH)
  - Additional GPIOs configured for LEDs/buttons (standard NUCLEO)

**FreeRTOS Configuration:**
- Kernel: FreeRTOS 10.5.1
- API: CMSIS-RTOS v2
- Heap: heap_4 (fragmentation-safe)
- Tasks: 6 (Default, Sensor, Distance, Processing, FSM, Output)
- Queues: 4 (sensor data, distance data, processing results, FSM decisions)
- Tick rate: 1000 Hz (1ms tick)

**FSM States:**
- **IDLE**: System normal, monitoring sensors
- **ALERT**: Single threat detected (proximity OR motion)
  - Triggers: Distance <500mm OR Motion >1500mg
- **LOCK**: Dual threat confirmed (proximity AND motion sustained)
  - Triggers: Both conditions true for 2+ consecutive cycles (100ms)
  - Auto-reset: Returns to IDLE after 5s timeout

---

## 📜 License

This project is released under the MIT License.
