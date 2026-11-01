# Firmware guide

## Windows build setup

Install:

* Git and Python 3.10 or newer, added to PATH.
* GNU Arm Embedded 7 2017 q4 (GCC 7.2.1). The script checks `ARM_TOOLCHAIN`, then PlatformIO's `toolchain-gccarmnoneeabi` package folder. Newer compiler versions are not supported.
* MSYS2 at `C:\msys64`. In its UCRT64 terminal, run `pacman -S --needed make mingw-w64-ucrt-x86_64-gcc`.
* Betaflight Configurator 10.6.0 for Windows.

```powershell
.\build.ps1 all   # firmware, configurator, flasher or all
```

Build firmware before the flasher.

For other install locations, use `-ArmToolchain`, `-Msys` or `-ConfiguratorRuntime`. For example, `-ArmToolchain C:\tools\gcc-arm-7` points to the folder containing `bin`.

## Editing Betaflight and Configurator

1. Run `.\build.ps1 prepare`.
2. Edit `firmware/build/betaflight` or `configurator/build/source`.
3. Run `.\build.ps1 firmware` or `.\build.ps1 configurator` and check your changes.
4. Run `.\build.ps1 export`.
5. Review and commit the exported patches and assets. Keep useful findings in source comments.

Leave the generated checkouts at their pinned revisions. Export brings your edits back into this repository as patches and assets.

## Editing EC and LEDC

1. Install Ghidra 12.1.4 and JDK 21 or newer. Run `.\build.ps1 prepare`, then open `firmware/build/ghidra/DRL.gpr` in Ghidra.
2. Open `ec_v25.bin` or `ledc_v22.bin` to inspect the stock code, or `ec_modified.bin` and `ledc_modified.bin` for our changes. The project includes the processor settings, memory map and named functions.
3. Edit `firmware/ec/src` or `firmware/ledc/src`. Hook locations are in `firmware/tools` and stock function addresses are in the ABI files.
4. Run `.\build.ps1 firmware`. This also runs the EC bridge tests. Check the generated patch records and test the change on hardware.
5. Review and commit the edited source and build files. Keep useful findings in ABI files or source comments.

The EC and LEDC builds patch the stock dumps with compiled additions. Keep the baselines, bootloader and settings regions intact. The linker scripts place the added code, and the build scripts check its size. Stock function addresses are listed in the ABI files.

When distributing a new build, add its identification entry to `flasher/catalog` so the flasher can recognise it later. Update the settings migration if their layout changes.

`VERSION` sets the release number shown in the flasher. Firmware bundles and Configurator builds also record the source commit and whether there were local edits.

## Hardware

### Flight controllers

Both targets use Betaflight 3.5.7 and MSP API 1.40. The Windows Configurator is based on version 10.6.0.

| | F7 | F4 |
|---|---|---|
| MCU family | STM32F722 | STM32F405 |
| Flash size | 512 KiB | 1 MiB |
| Firmware target | RACER4S | RACER4F4, derived from REVO |
| Motor 1 | PB9 | PB0 |
| Motor 2 | PB8 | PB1 |
| Motor 3 | PB6 | PA3 |
| Motor 4 | PB7 | PA2 |
| Receiver | UART1, RX PA10, TX PA9 | UART1, RX PA10, TX PA9 |
| EC connection | UART6, TX PC6, RX PC7 | UART6, TX PC6, RX PC7 |
| Blackbox storage | microSD on SPI2, CS PB12 | SPI flash on SPI3, CS PB3 |
| Battery voltage input | PC2 | PC3 |

The motor outputs use ONESHOT125. Connect receiver TX to adapter RX and receiver RX to adapter TX. The receiver signal is on CH1. Telemetry is on EC PB13, which sends CRSF telemetry using TCC3 and DMA because its SERCOM pad cannot transmit UART data. SmartAudio is on EC PB12. The adapter TX and SA bridges must be connected.

UART6 is reserved for the EC bridge. OSD, font programming, LED commands, SmartAudio and receiver telemetry share this connection.

The F4 target retains the REVO UART1 inverter control on PC0 and PINIO1 on PC8. Its sensor orientation and motor resources differ from the F7 target. The F4 configuration leaves the accelerometer disabled.

### EC

The EC uses a SAMD51J19A with 512 KiB internal flash. Both FC variants use the v25 EC application. Settings from v21 are migrated to the v25 layout. The bootloader is preserved.

The EC drives the analog OSD through the MAX7456 register interface. Betaflight sends DisplayPort data over UART6. The display is 30 columns by 13 rows in NTSC or 16 rows in PAL. The character memory contains 256 glyphs. The DRL font uses the Betaflight font layout in `resources/osd/1`.

LEDC commands use the stock DAN packet transport. The bridge adds sequence numbers and confirmed replies for LED control and file transfers.

### LED controller

The LEDC uses a SAMD51J19A with 512 KiB internal flash and separate QSPI file storage. The application is based on v22.

| | A / B | C |
|---|---|---|
| Original firmware | v20 and v22 | v21 |
| Consolidated build | A / B | C |
| Spine data | PA19 | PA18 |
| Arm output 0, 52 LEDs | PB16 | PB09 |
| Arm output 1, 52 LEDs | PA21 | PA21 |
| Arm output 2, 60 LEDs | PB15 | PB08 |
| Arm output 3, 60 LEDs | PA04 | PA06 |
| Arm output gates | PA05, PA06 | PA05, PA04 |
| Button input | PA18 | PA19 |
| Onboard status LED | PB08 | PB15 |

These labels identify the boards used during development. They are independent of the F4 or F7 flight controller. Detection matches the installed firmware against known images.

The original spine has 152 LEDs. The firmware also contains maps for 78, 100, 309 and 338 LEDs. Arm outputs contain 52 or 60 LEDs per output.

Animations use a 42 by 13 pixel canvas. Columns 0 to 38 feed the spine. Columns 39 and 40 feed two pairs of arm outputs. Column 41 is unused by the current mapping. The original spine mapping fits the image to 25 physical rows.

There are 32 animation slots, numbered 0 to 31. Each uploaded GIF can contain up to 65,535 bytes. Files are stored in QSPI, so a microSD card is not required. Animation brightness ranges from 0 to 255. Dim is 5 and full is 255. Color profiles retain their own brightness settings.

### Flash and settings

FC settings occupy `0x08004000` to `0x08007fff`. The flasher resets these to the selected target's compiled defaults.

EC and LEDC bootloaders occupy `0x00000000` to `0x00003fff`. Their settings are migrated from the connected board before writing. The LEDC selection journal occupies `0x0002a000` to `0x0002bfff`, with rollover records in QSPI.

FC flashing uses STM32 DFU. EC and LEDC flashing use the METROM4BOOT UF2 bootloader. The tested LEDC direct USB lead carries data only and requires board power. The flasher compares a complete flash readback with the prepared image.
