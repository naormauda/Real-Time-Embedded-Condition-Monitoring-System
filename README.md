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

- ✅ **LEDs** - State indicators (GPIO)
  - Green LED (IDLE): PB0
  - Yellow LED (ALERT): PE4
  - Red LED (LOCK): PC6

- ✅ **Buzzer** - Audio alert (PWM)
  - Pin: PC7 (TIM3_CH2)
  - Frequency: 2 kHz
  - Patterns: Off, Slow beep (1 Hz), Fast beep (~3 Hz), Continuous

- ✅ **Servo Motor** - Lock mechanism (PWM)
  - Pin: PA6 (TIM3_CH1)
  - Standard hobby servo (50 Hz PWM)
  - Positions: 0° (unlocked), 90° (locked)

**Planned:**
- ⏳ OLED display (I2C1 @ 400kHz)

> Hardware selection is driven by architectural requirements,
> not the other way around.

---

## 🔌 Hardware Wiring Guide

### STM32H563ZI NUCLEO-144 Pinout

**Sensors:**
| Component | Pin | Function | Notes |
|-----------|-----|----------|-------|
| LIS3DSH   | PD14 | Chip Select | SPI1 (CPOL=1, CPHA=1) |
|           | PA5 | SCK | |
|           | PG9 | MISO | |
|           | PB5 | MOSI | |
| VL53L1X   | PB8 | I2C1_SCL | 400 kHz |
|           | PB9 | I2C1_SDA | Address: 0x29 |

**Actuators:**
| Component | Pin | Function | Notes |
|-----------|-----|----------|-------|
| Green LED | PB0 | GPIO Output | IDLE state indicator (Arduino A6) |
| Yellow LED | PE4 | GPIO Output | ALERT state indicator (Morpho CN11-6) |
| Red LED | PC6 | GPIO Output | LOCK state indicator (Arduino D5) |
| Buzzer | PC7 | TIM3_CH2 PWM | Active buzzer or passive with driver |
| Servo | PA6 | TIM3_CH1 PWM | Standard 50Hz servo (SG90, MG90S, etc.) |

**Power:**
- All components: 3.3V from NUCLEO board
- Servo: May require external 5V supply if high torque
- Common GND for all components

**Wiring Notes:**
- LEDs: Use 220Ω-330Ω current-limiting resistors
- Buzzer: Active buzzer can connect directly; passive buzzer may need NPN transistor driver
- Servo: Red wire = 5V, Brown/Black = GND, Orange/Yellow = Signal (PA6)

---

## 🚧 Project Status

**Current Phase**:  
🟢 Stage 2 – Hardware Bring-Up **COMPLETED**  
🟢 Stage 3 – Driver Development **COMPLETED**  
🟢 Stage 4 – RTOS Integration **COMPLETED**  
� Stage 5 – Actuator Control **COMPLETED** (Phase 1)  
🟡 Stage 5 – ML Feature Extraction (Phase 2 - In Progress)

**Completed (Stage 4 - RTOS Integration):**
- ✅ Clock tree configuration (250 MHz via PLL)
- ✅ Peripheral configuration (SPI1, I2C1, TIM3 PWM, GPIO)
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
  - ALERT → LOCK: Sustained dual threat (motion + proximity, count≥1)
  - LOCK → IDLE: Auto-reset after 5s timeout
- ✅ Queue-based inter-task communication
- ✅ Stack monitoring and health logging

**Completed (Stage 5 - Actuator Control - Phase 1):**
- ✅ **Actuator Driver Module** (`actuator_driver.h/.c`)
  - LED control API for 3-state indicators
  - PWM buzzer control with pattern support (off, slow beep, fast beep, continuous)
  - Servo PWM control with position mapping
  - Integrated state machine for coordinated actuator response

- ✅ **GPIO Configuration**
  - PB0: Green LED (IDLE indicator)
  - PE4: Yellow LED (ALERT indicator)
  - PC6: Red LED (LOCK indicator)
  - All initialized as output push-pull with proper clock enable

- ✅ **PWM Configuration (TIM3)**
  - Prescaler: 249 (1 MHz base clock from 250 MHz APB1)
  - Period: 19999 (50 Hz output for servo, 20ms cycle)
  - Channel 1 (PA6): Servo PWM (500-2500μs pulse width)
  - Channel 2 (PC7): Buzzer PWM (2 kHz tone when active)

- ✅ **Hardware Integration & Testing**
  - All LEDs verified working in their respective states
  - Buzzer PWM signal verified at correct frequency
  - Servo PWM signal verified with proper pulse widths
  - Actuator commands properly coordinated via OutputTask
  - 5-state to actuator mapping validated on hardware

- ✅ **Debug Infrastructure**
  - Comprehensive logging for all actuator operations
  - GPIO read/write verification
  - PWM pulse width confirmation
  - State transition visibility in serial output

**Current Status (Stage 5 - Phase 2 - In Progress):**
- 🔄 Feature extraction module (time-domain & frequency-domain)
- ⏳ TinyML model integration (TensorFlow Lite Micro)
- ⏳ Anomaly detection model training and deployment

**Known Deferred Tasks:**
- ⏳ Servo external power supply (requires 5V source, not NUCLEO 5V) - Phase 4

**Next Steps:**
- Complete feature extraction module
- Prepare dataset with labeled normal/anomaly samples
- Train TensorFlow Lite model on embedded device
- Deploy on-device inference
- Integrate ML decisions into FSM

---

## 📂 Repository Structure

```
/Core
  /Inc
    actuator_driver.h     ✅ Actuator driver (LEDs, buzzer, servo)
    lis3dsh_driver.h      ✅ LIS3DSH accelerometer driver header
    vl53l1x_driver.h      ✅ VL53L1X ToF sensor wrapper
    main.h                ✅ Main application header
    stm32h5xx_hal_conf.h  ✅ HAL configuration
    stm32h5xx_it.h        ✅ Interrupt handlers
    app_freertos.h        ✅ FreeRTOS task declarations
    FreeRTOSConfig.h      ✅ FreeRTOS configuration
  /Src
    actuator_driver.c     ✅ Actuator driver implementation
    lis3dsh_driver.c      ✅ LIS3DSH driver implementation
    vl53l1x_driver.c      ✅ VL53L1X driver wrapper
    app_freertos.c        ✅ FreeRTOS tasks and sensor fusion pipeline
    main.c                ✅ Main application and peripheral init (TIM3 PWM)
    stm32h5xx_hal_msp.c   ✅ HAL MSP initialization (TIM3 GPIO AF config)
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

## 📚 Implementation Notes & Lessons Learned (Stage 5 Phase 1)

### Actuator Control Implementation

**Key Design Decisions:**
1. **Separate Actuator Driver Module**: Isolated `actuator_driver.h/.c` provides clean API for state-based actuator coordination
2. **State-Centric Architecture**: Instead of individual LED/buzzer/servo commands, all actuators respond to system state (IDLE/ALERT/LOCK)
3. **PWM Pulse Width Selection**: 
   - Initial range (1000-2000μs) worked for some servo types
   - Extended to 500-2500μs for broader compatibility (SG90, MG90S, etc.)
   - Empirically determined through hardware testing

**Hardware Discoveries:**
1. **Servo Power Requirements**:
   - NUCLEO 5V pin supplies max ~100mA
   - Servos draw 200-500mA, especially during movement
   - Result: Board brownout, ST-LINK connection loss if servo powered from NUCLEO
   - **Solution**: Use external 5V power supply with common ground

2. **GPIO Pin Allocation**:
   - PE4 (not PB7) is on Arduino connector CN9
   - PC6 works better than PB14 for red LED (stays on connector)
   - PA6 is correct for servo (CN9, verified)

3. **Format String Issues**:
   - uint32_t variables require `%lu` format specifier
   - uint16_t variables require `%u` format specifier
   - Mixed with printf() on embedded systems caught by compiler warnings

4. **CMake Integration**:
   - User source files must be explicitly added to `target_sources()`
   - Include directories must be added to `target_include_directories()`
   - Default STM32CubeMX setup doesn't include application-specific files

**FSM Tuning for Testing:**
- Reduced LOCK trigger threshold from `alert_count >= 2` to `>= 1`
- Reason: Rapidly changing distance readings made original threshold hard to reach
- Production systems may need tuning based on actual sensor characteristics

### Code Quality & Testing

**Debug Instrumentation:**
- Added comprehensive logging to actuator operations
- Verified GPIO writes with readback confirmation
- CCR1 register verification shows PWM update success
- Serial output provides full visibility into state transitions

**Compilation & Linking:**
- All warnings resolved (except VL53L1X deprecated API warnings)
- Final binary size: 122656 bytes text, 164504 total (leaving ~46KB for ML model)
- No runtime errors or stack overflow

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

## ⚖️ Known Limitations & Future Work

### Current Limitations

1. **Servo Motor Power Supply**
   - ❌ Servo currently non-functional
   - Cause: NUCLEO-H563ZI 5V pin supplies only ~100mA; servo draws 200-500mA
   - Connecting servo to on-board 5V causes board brownout
   - **Fix**: Use external 5V power supply with common GND connection
   - Timeline: Deferred for Phase 2

2. **Buzzer Audio Verification**
   - ⚠️ PWM signal verified at correct frequency (2kHz)
   - Audio output not yet tested on physical hardware
   - Expected behavior: Slow beeps (300ms on/700ms off) in ALERT, continuous in LOCK

3. **ML Model Size Constraint**
   - Total available memory: 2MB Flash, 250KB SRAM
   - Current firmware: 123KB text, 39.6KB BSS
   - ML model budget: ~46KB remaining (limited to small quantized models)
   - TensorFlow Lite Micro integration planned but not yet implemented

4. **Sensor Fusion Simplicity**
   - Current design: Basic state machine (single OR two clauses)
   - No machine learning integration yet
   - No historical pattern recognition
   - Susceptible to false positives/negatives from isolated events

### Planned Improvements (Stage 5 Phase 2+)

1. **Feature Extraction Module** (Phase 2)
   - Extract time-domain features from accelerometer stream
   - Compute statistics: mean, variance, RMS, peak-to-peak
   - Optional: FFT for frequency-domain features
   - Target: Feed feature vectors to ML model

2. **TensorFlow Lite Micro Integration** (Phase 3)
   - Deploy pre-trained anomaly detection model
   - Replace rule-based FSM with ML-based decision making
   - Support OTA model updates if space permits

3. **Hardware Fixes** (Phase 4)
   - External 5V power supply and relay for servo
   - Servo unlocking mechanism once power resolved
   - Consider boost converter if power routing unavailable

4. **Advanced Sensor Processing**
   - Adaptive thresholds based on environment
   - Multi-sensor fusion weighting
   - Temporal pattern recognition

---

## 📜 License

This project is released under the MIT License.
