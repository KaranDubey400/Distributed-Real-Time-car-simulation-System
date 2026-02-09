# Distributed Car Simulation System

A multi-process vehicle simulation system written in C using **Socket Programming** for inter-process communication (IPC) and **Shared Memory** for real-time monitoring. The project simulates a car's powertrain, transmission logic, and fuel consumption across independent client nodes.

## 🏎️ Architecture Overview

The system follows a star topology where a central **Server** orchestrates data flow between specialized **Clients**:

* **Server (`server.c`)**: The central hub. It manages the simulation loop, synchronizes state between clients via TCP sockets, and maintains the global state in Shared Memory.
* **Engine Client (`engine_client.c`)**: Handles physics, steering, and user input. It uses `ncurses` for a dashboard and calculates torque/RPM based on speed and throttle.
* **Transmission Client (`transmission_client.c`)**: An automated manual transmission (AMT) logic provider. It decides when to upshift or downshift based on RPM thresholds ($1500$–$3500$ RPM).
* **Fuel Client (`fuel_client.c`)**: Calculates fuel burn rates based on engine power output and efficiency constants.
* **Monitor (`monitor.c`)**: A standalone process that maps to Shared Memory to provide a real-time, read-only telemetry dashboard.

## 🛠️ Key Features

-   **Real-time Physics**: Includes rolling resistance, drag force, and RPM-based torque curves.
-   **Automated Transmission**: Multi-gear logic ($5$ Forward, $1$ Reverse) with shift cooldowns.
-   **Hybrid IPC**: Uses **TCP Sockets** for the control loop and **POSIX Shared Memory** for telemetry.
-   **Interactive UI**: Terminal-based dashboards using `ncurses` for both the Engine and Monitor.

## 🚀 Getting Started

### Prerequisites
- GCC Compiler
- Ncurses Library (`libncurses5-dev` on Ubuntu)
- Real-time library (`-lrt`) and Pthreads (`-lpthread`)

### Compilation
Compile all components using the following commands:

```bash
gcc server.c -o server -lrt -lpthread
gcc engine_client.c -o engine -lncurses -lm
gcc transmission_client.c -o transmission
gcc fuel_client.c -o fuel
gcc monitor.c -o monitor -lncurses -lrt -lm
