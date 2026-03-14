# Focus Points

This project implements a **semantic GPU execution framework** capable of executing CUDA and PTX programs without requiring a cycle-accurate microarchitectural model.

The system provides configurable GPU instruction definitions and modular hardware abstractions, together with a CUDA compatibility shim that enables seamless integration with CUDA C programs.

New instructions can be rapidly prototyped by mapping them to existing **IR operations**, which are internally expanded into a sequence of micro-operations executed by the SIMT semantic engine. This mechanism allows fast validation of instruction semantics and integration feasibility during the **pre-silicon design stage**.

The framework focuses on **functional correctness and integration validation**, rather than detailed hardware timing simulation.

# Target

A **GPU ISA prototyping framework** for rapid exploration and validation of new GPU instructions.

# Value

- **GPU ISA prototyping**
  
  Enables rapid integration and validation of new instructions without requiring a full hardware implementation.

- **Hardware–software co-design research**
  
  Allows early evaluation of ISA changes across the software stack, including CUDA programs and PTX execution.

- **Compiler and programming model research**
  
  Provides an execution environment to study how new instructions interact with compiler code generation and GPU programming patterns.
