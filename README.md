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

- **MCU**: STM32H563 (MB1404C Discovery Board)
- **RTOS**: FreeRTOS
- **Language**: C / C++
- **Development Tools**:
  - STM32CubeMX
  - STM32CubeIDE
  - Git / GitHub

---

## 📦 Hardware Components (Planned)

- Accelerometer / Vibration sensor (SPI, DMA)
- Motion / proximity sensor (event-driven)
- OLED display (I2C)
- LEDs and buzzer for alerts
- Servo motor for lock demonstration

> Hardware selection is driven by architectural requirements,
> not the other way around.

---

## 🚧 Project Status

**Current Phase**:  
🟡 Stage 3 – RTOS Skeleton & System Bring-Up

Completed:
- System definition and requirements
- FSM design
- High-level software architecture
- Task and data-flow design

In progress:
- FreeRTOS project skeleton
- Task and interrupt wiring
- Timing validation

---

## 📂 Repository Structure (Planned)

/Core
/Src
sensor_task.c
ml_task.c
fsm_task.c
output_task.c
/Inc
sensor_task.h
ml_task.h
fsm_task.h
output_task.h

/docs
architecture.md
fsm.md
timing.md

README.md


---

## 🧪 Design Philosophy

- Deterministic over fast
- Clear over clever
- Maintainable over minimal
- Architecture before implementation

---

## 📜 License

This project is released under the MIT License.
