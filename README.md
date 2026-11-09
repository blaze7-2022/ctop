# ctop 🖥️

A lightweight, interactive terminal-based system monitor written in C using `ncursesw`. It provides real-time tracking of your CPU, RAM, Swap memory, and Network activity with a clean, box-based interface.

![C Language](https://img.shields.io/badge/Language-C-blue.svg)

---

## ⚠️ Disclaimer

**This is a work in progress** Some features or metrics may be refined as development continues.

---

## ✨ Features

- **CPU Monitoring:** Real-time usage percentage bar, plus an expandable details view showing processor name, total core count, and system architecture.

- **Memory & Swap Tracking:** Monitor physical RAM usage and toggle into a dedicated Swap memory view with its own progress bar.

- **Network Activity:** Tracks live download/upload speeds, active network interface detection (e.g., `eth0`, `wlan0`), and cumulative session data transfer totals.

- **Interactive Navigation:** Use the **Up/Down arrow keys** (or `k` / `j`) to select different metric boxes, and press **Enter** to toggle detailed views.


---

## 📋 Prerequisites

To compile and run this project, you will need:
- A C compiler (like `clang`)

- The `ncursesw` development library (wide-character ncurses support)

---

## 🚀 Building and Running (NixOS)

Because NixOS handles libraries differently than traditional Linux distributions, standard compilation commands will not automatically locate the `ncursesw` headers. You can use the provided Nix development environment (`devShell`) to build and run **ctop**:

1. **Clone the repository and enter the directory:**
   ```fish
   git clone [https://github.com/blaze7-2022/ctop.git](https://github.com/blaze7-2022/ctop.git)
   cd ctop

2. **Enter the Nix DevShell and compile:**
   ```fish
   nix develop
   clang main.c -o ctop   -O3   -flto   -fuse-ld=lld   -fomit-frame-pointer   -ffunction-sections   -fdata-sections   -Wl,--gc-sections   -Wall -Wextra -Wpedantic   -DNDEBUG   $(pkg-config --cflags --libs ncursesw)
3. **Run the executable**
   ```fish
   ./ctop
