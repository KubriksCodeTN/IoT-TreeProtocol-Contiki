# Any-cast Communication Protocol for IoT Networks

This project implements an **Any-cast communication protocol** for IoT networks, extending a standard converge-cast tree-based approach. Developed as part of the Internet of Things course (2024), the protocol analyses network robustness, routing table management, and energy-aware performance. A detailed description and evaluation of the results can be read in report.pdf

## Key Features

The protocol introduces several mechanisms to ensure reliability and efficiency in constrained environments:

* **Dynamic Tree Construction**: Periodic beaconing for topology discovery and parent selection.
* **Targeted Recovery Mechanism**: Handles node isolation and routing inconsistencies through localized recovery beacons, minimizing control traffic overhead.
* **Any-cast Routing**: Maintains local routing tables via periodic topology reports and information piggybacking.
* **Loop Detection & Prevention**: Prevents routing cycles by monitoring hop counts and detecting topology inconsistencies in real-time.
* **Configurable Metrics**: Support for both **Hop Count** and **LQI (Link Quality Indicator)** metrics, selectable at compile-time.

## Evaluation & Performance

The protocol was validated through both simulation and experimental hardware deployments (on the UNITN Cloves testbed):

* **Simulation (Cooja)**: Evaluated on 10-node topologies to analyze Packet Delivery Ratio (PDR), Duty Cycle, and Latency.
* **UniTN Testbed**: Deployed on real hardware (16 and 36-node setups) to verify scalability and performance in noisy environments.
* **Data Link Layer**: Tested with **ContikiMAC** (for power-constrained scenarios) and **nullrdc**.

### Results at a Glance
* **PDR**: Achieved up to 99-100% in simulation and 82-93% on the physical testbed.
* **Duty Cycle**: High energy efficiency with ContikiMAC (~2-3% radio-on time).
* **Latency**: Average latency of ~150ms in duty-cycled configurations.

## Tech Stack

* **Operating System**: Contiki OS
* **Simulation Tools**: Cooja
* **Language**: C
* **Key Concepts**: Distance Vector Routing, MAC protocols, Link Quality Estimation.
