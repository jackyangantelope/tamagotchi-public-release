# picoTamagotchi for Adafruit Fruit Jam

This repository is a public-source candidate of the TamaLib-based first-generation Tamagotchi emulator port for Adafruit Fruit Jam (RP2350B). It is not a hardware-validated Fruit Jam release.

The earlier playable build was tested on a different development board. Its 0.4.0 HDMI-audio version added stereo buzzer output; the user's basic test pass is acknowledged, but no Fruit Jam audio, input, save, or frame-rate result is claimed here. This source tree changes the board integration and therefore needs a fresh hardware acceptance pass before a binary release.

## Use

Supply your own lawful, unmodified Tamagotchi ROM on the SD card in `/roms/TAMAGOTCHI/`. The selector accepts `.b`, `.bin`, and `.rom` files containing either 12,288 or 16,384 bytes of big-endian 12-bit instruction words. No ROM, extracted artwork, or save data is included. Save files are written to `/SAVES/TAMAGOTCHI/`; the existing `JTAMA02` format is retained. The user interface maps three Tamagotchi buttons to keyboard A/B/X or compatible controller A/B/X/Y.

The default build targets the resident-loader application partition at `0x10080000`. Do not flash its UF2 as a standalone full-flash bootloader image.

## Build

Use CMake, Ninja, an ARM GCC toolchain, Pico SDK 2.3 or newer, and a Pico-PIO-USB checkout:

```powershell
./scripts/build-release.ps1 -PicoSdkPath C:\path\to\pico-sdk -PicoPioUsbPath C:\path\to\Pico-PIO-USB -ToolchainPath C:\path\to\toolchain -NinjaPath C:\path\to\ninja.exe
```

Optional `-PioasmDir`, `-PicotoolDir`, and `-TinyUsbPath` parameters support prebuilt host tools. The build output is `build/fruit-jam-release/picoTamagotchi.uf2`.

Check that a built UF2 stays in the loader application partition:

```powershell
python ./scripts/validate-uf2-layout.py ./build/fruit-jam-release/picoTamagotchi.uf2 --app-base 0x10080000 --metadata 0x10FFFF00
```

## Testing status

The release candidate builds from a fresh clone with both the project SDK copy and an independently installed Pico SDK 2.3.0 on Windows. Both builds pass the loader-partition UF2 layout check. These are build checks, not hardware validation.

The source, simulator, and build checks described in the release notes do not replace a Fruit Jam test. Before distributing a binary, verify SD mounting and ROM selection, display and three-button input, both startup test tones and in-game buzzer, 60-second automatic save, Select+Start manual save, restart recovery, and sustained frame rate.

## License and credit

The combined firmware is distributed under GPLv3; the original TamaLib source is GPLv2-or-later. See [LICENSE](LICENSE) and [THIRD_PARTY.md](THIRD_PARTY.md). The port is based on [jcrona/tamalib](https://github.com/jcrona/tamalib) commit `ce304d55f9a73c60232ce3f552e7983db3fa399c`. Its original `cpu.c`, `hw.c`, and `tamalib.c` plus their headers remain in `deps/tamalib/`. The platform-owned `deps/hal_types.h` fulfills TamaLib's `../hal_types.h` integration contract.

The port-specific changes are in `platform/tama/tama_port.c` (TamaLib HAL), `app/playable_main.cpp` (ROM selection, UI, input, save flow), `app/tama_buzzer_audio.h` (tick-synchronous PCM), and `platform/wave1/` (Fruit Jam video, USB, SD, and HDMI audio integration). See the exact upstream and dependency attributions in [THIRD_PARTY.md](THIRD_PARTY.md).
