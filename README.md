# NEO6502 MMU Firmware

Firmware for the **RP2350-based Memory Management Unit (MMU)** used in the NEO6502 architecture.
The RP2350 supervises the system while a **W65C02** executes the primary program logic.

The design follows a **co-processor architecture**: the 6502 runs application software, while the RP2350 provides system services such as memory management, cartridge loading, storage access, and video generation.

This repository contains the firmware responsible for system supervision, paging control, and the communication interface between the CPU and the RP2350.

---

# System Architecture

The platform combines a classic 8-bit processor with a modern microcontroller.

**W65C02**

* Executes user programs
* Owns the system bus during normal execution
* Accesses paged memory through the MMU

**RP2350**

* Implements the Memory Management Unit
* Provides system services and supervision
* Generates video output
* Loads ROM cartridges from flash storage
* Handles I/O requests from the CPU

The system is designed so that the **6502 remains the default bus owner**.
The RP2350 takes control only when required, for example during memory remapping or when servicing I/O operations.

This minimizes interference with CPU timing and preserves predictable execution behaviour.

---

# Memory Architecture

The platform extends the traditional **64 KB 6502 address space** using paging.

Key characteristics:

* Up to **512 KB physical RAM**
* **4 KB paging granularity**
* Dynamic page mapping controlled by the MMU
* Fixed communication region between the CPU and RP2350
* Support for cartridge overlays and BIOS regions

The paging mechanism allows large memory configurations while keeping the CPU interface simple and deterministic.

---

# I/O Page Mechanism

Communication between the CPU and the RP2350 is implemented using a **dedicated I/O memory page**.

Hardware logic detects accesses to this page by combining:

* the **PHI2 clock**
* the **address decode signal**

When an access occurs the RP2350 receives an interrupt and services the request.

This mechanism allows the RP2350 to implement system services such as:

* device access
* configuration commands
* memory management operations
* communication registers

The approach avoids continuous bus arbitration and keeps the design compatible with standard 6502 timing.

---

# Cartridge System

Programs are packaged as **ROM cartridges**.

Cartridges are stored on internal flash using **LittleFS** and loaded into memory during system initialization according to a configuration file.

The cartridge system supports:

* BIOS and monitor ROMs
* development cartridges
* multiple cartridge configurations
* memory overlays

This makes it possible to switch system configurations without rebuilding firmware.

---

# Firmware Responsibilities

The firmware implements the supervisory functionality of the system, including:

* MMU paging control
* bus ownership management
* CPU reset and startup sequencing
* I/O page interrupt handling
* cartridge loading from flash
* configuration parsing
* system monitoring

---

# Development Environment

The firmware is developed in the **Arduino ecosystem** using:

* Visual Studio 2022
* Visual Micro
* RP2350 Arduino core
* LittleFS for flash storage

The project targets RP2350 hardware acting as the MMU and system controller.

---

# Design Goals

The architecture explores a modern approach to classic 8-bit computing:

* extending memory beyond the 64 KB limit
* integrating modern storage systems
* providing video output through a supervisory controller
* preserving deterministic CPU behaviour
* enabling experimentation with alternative 8-bit processors

The MMU concept allows the supervisory RP2350 infrastructure to remain unchanged while experimenting with different CPUs or system configurations.

---

# License

See the LICENSE file included in this repository.
