cat << 'EOF' > README.md
# MD_spi_bridge

An open-source, high-speed ESP32-based USB-to-SPI transport programmer and custom Windows DLL proxy (`usbspi.dll`) designed for the **Qualcomm / CSR (Cambridge Silicon Radio) BlueSuite** toolchain (PSTool, pscli, CSR ROM Configuration Tool).

Tested and verified on **CSR8645 (B04G, K02SWS / F-3188)** modules for Persistent Store (EEPROM/RAM) reading and writing.

---

## Features
- **High-Speed Binary Protocol**: Serial transport running at 921,600 baud between PC and ESP32.
- **Bit-Banged Precision SPI**: Avoids hardware SPI glitches and handles strict BlueCore SPI timings (~25–50 kHz).
- **Anti-Lockup State Tracking**: Resolves the legacy *"Failed to unpause the chip after reading the look-up table"* error.
- **EEPROM Store Auto-Remapping**: Automatically redirects PSTool GUI writes (`0x0080` Flash) to default EEPROM routing (`0x0000`) for seamless in-GUI parameter customization.
- **Zero Heavy DllMain**: Lazy hardware initialization to prevent host application deadlocks.

---

## Hardware Requirements & Wiring

> **CRITICAL WARNING:** The CSR8645 SPI interface operates at **1.8V logic levels**. Connecting 3.3V or 5V directly to the SPI lines will destroy the CSR chip. A **bi-directional logic level shifter (3.3V <-> 1.8V)** is mandatory.

### Pinout Connection Table

| ESP32 Pin (3.3V Side) | Level Shifter HV | Level Shifter LV | CSR8645 Module Pin (1.8V Side) | Pin Function |
| :--- | :--- | :--- | :--- | :--- |
| **GPIO 5** | HV1 | LV1 | **Pin 6 (SPI_CS#)** | SPI Chip Select |
| **GPIO 18** | HV2 | LV2 | **Pin 7 (SPI_CLK)** | SPI Clock |
| **GPIO 23** | HV3 | LV3 | **Pin 8 (SPI_MOSI)** | Master Out Slave In |
| **GPIO 19** | HV4 | LV4 | **Pin 5 (SPI_MISO)** | Master In Slave Out |
| — | — | **1.8V Rail** | **Pin 9 (SPI_PCM#)** | Mode Select (Tie to 1.8V via 1k pull-up) |
| **GND** | **GND** | **GND** | **Pin 17 (GND)** | Common Ground |

### EEPROM Hardware Write-Protect (WP) Note
On commercial F-3188 / CSR8645 modules, the manufacturer may tie **Pin 7 (WP)** of the external 8-pin I2C EEPROM to 1.8V. To enable permanent parameter writes, ensure **Pin 7 (WP) is tied to GND (0.0V)**.

---
How to Build
1. Flash the ESP32
Open firmware/esp32_csr_bitbang_bridge/esp32_csr_bitbang_bridge.ino in the Arduino IDE and flash it to your ESP32 board.
2. Build the Windows DLL (Cross-compile via WSL/Linux)
Install the 32-bit MinGW toolchain:
code
Bash
sudo apt-get install gcc-mingw-w64-i686 g++-mingw-w64-i686 make
Compile the 32-bit x86 DLL:
code
Bash
make -f Makefile.mingw clean
make -f Makefile.mingw all
The compiled library will be output to obj-win32/usbspi.dll.
3. Deploy to BlueSuite
Copy usbspi.dll to your BlueSuite installation directory (typically C:\Program Files (x86)\CSR\BlueSuite 2.6.6\):
code
Bash
cp obj-win32/usbspi.dll /mnt/c/Program\ Files\ \(x86\)/CSR/BlueSuite\ 2.6.6/
Usage with BlueSuite
1. Persistent Store Tool (PSTool GUI)
Set your COM port environment variable and launch PSTool:
code
Cmd
set SPICOMPORT=COM5
PSTool.exe -TRANS "SPITRANS=USB SPIPORT=1"
Transport name will identify as MD_spi_bridge.
Uncheck Use Cache when prompted.
Under Stores, select All (TIFR) to edit parameters live.
Click Reset BC to apply changes.
2. PS-Cli Batch Tool (pscli.exe)
Dump persistent store:
code
Cmd
set SPICOMPORT=COM5
pscli.exe -TRANS "SPITRANS=USB SPIPORT=1" dump backup.psr
Merge a configuration patch:
code
Cmd
pscli.exe -TRANS "SPITRANS=USB SPIPORT=1" merge my_patch.psr
Citations & Acknowledgments
Architecture and interface definitions derived from the original reverse-engineered csr-spi-ftdi project by lorf and CsrUsbSpiDeviceRE.
Developed and optimized for custom microcontroller-based bit-banged bridges by Mahesh (2026).