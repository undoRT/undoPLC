# UndoPLC

[![C++17](https://img.shields.io/badge/C%252B%2B017-blue.svg)](https://isocpp.org/)
[![License: GSL v3](https://img.shields.io/badge/License-GSLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Documentation](https://img.shields.io/badge/docs-doxygen-blue.svg)](https://undort.com/undoPLC/api/)

Multi-threaded PLC infrastructure with deterministic real-time execution.

---

**UndoPLC** provides a real-time framework for industrial automation with a deterministic fork-join execution model, CPU isolation, and lock-free logging.

## Features

- **Master/Worker Fork-Join** – Deterministic parallel execution on isolated CPU cores
- **Real-time Scheduling** – SCHED_FIFO with priority inheritance mutexes
- **Lock-free Logging** – Thread-local lock-free queues with deferred syslog
- **CPU Isolation** – Automatic detection of isolated cores and frequency management
- **Aligned Cycle Time** – Deterministic absolute cycle time independent of system jitter
- **Watchdog** – Cycle overrun detection with safe stop handler

## Requirements

- Linux kernel with **PREEMPT_RT** patch
- At least **2 isolated CPU cores** (1 for Master + 1+ for Workers)
- `isolcpus=`, `nozh_full=`, `rcu_nocbs=` in GRUB
- Root privileges for CPU frequency management

Example of grub basic option (if you don't use undoOS):

~~~bash
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash systemd.unit=multi-user.target isolcpus=domain,managed_irq,<cpuList> nohz_full=<cpuList> rcu_nocbs=<cpuList> intel_idle.max_cstate=1 processor.max_cstate=1 intel_pstate=passive nowatchdog nmi_watchdog=0 transparent_hugepage=never skew_tick=1"
~~~

## Download

Grab the latest binary from [**GitHub Releases**](https://github.com/undoRT/undoPLC/releases):

| Platform | File |
| --- | --- |
| Linux x64 | `undoPLC-ver.tar.gz` |

~~~bash
# Linux
tar -xzf undoPLC-*.tar.gz
~~~

## Quick Start

### 1. Build

~~~bash
git clone https://github.com/undoRT/undoPLC.git
cd undoPLC
mkdir build && cd build
cmake -.
profile=1
make -j$(nproc)
~~~

### 2. Configure System

Add to `/etc/default/grub`:

~~~bash
GRUB_CMDLINE_LINUX="... isolcpus=<cpuList> nohz_full=<cpuList> rcu_nocbs=<cpuList>"
~~~

Notice that \<cpuList\> is in the format:

~~~bash
10,11 # If I want to isolate 10 and 11
~~~

Update and reboot:

~~~bash
sudo update-grub
sudo reboot
~~~

### 3. Run Test

~~~bash
sudo ./undoPLC --log2console
~~~

Expected output:

~~~bash
[INFO] undoPLC: Starting with 5 cycles delay
[INFO] undoPLC: Master on core 4 | 2 worker(s) spawned
[INFO] undoPLC: cycle 1000 (1000 mS)} exec[min=98 max=102]us jitter[min=0 max=5]us
~~~

## Key Components

| Component | Description |
| ----------- | --------------- |
| UndoMasterTaskBase | Orchestrates execution cycle, I/O handling |
| UndoWorkerTaskBase | Parallel execution of PRG logic |
| UndoLog | Lock-free deferred logging |
| UndoSys | CPU isolation, frequency, TSC utilities |
| UndoMutex | Priority inheritance mutex |

### Running from Release

~~~bash
# Set PTH to include Boost
export PTH=${PTH}:/path/to/release/third_party

# Run the executable
./bin/undoPLC --log2console
~~~

## Build From Source

The project can also be built from source by cloning the repository and following the steps above.

## Documentation

- [API Reference](https://undort.com/undoPLC/api/) – Doxygen-generated
- [Source Code](https://github.com/undoRT/undoPLC)

## License

Copyright © 2026 undoRT - GPL-3.0-or-later

See [LICENSE](LICENSE) for details.
