# GameBoy Emulator

A Game Boy (DMG) emulator written in C++23.

## Architecture

```mermaid
graph TD
    subgraph GameBoy
        CPU["CPU<br/><i>SM83</i>"]
        Bus["Bus<br/><i>Memory Map</i>"]
        PPU["PPU<br/><i>Video</i>"]
        APU["APU<br/><i>Audio</i>"]
        Timer
        Cart["Cartridge<br/><i>MBC1 / MBC3 / MBC5</i>"]
        Joypad
    end

    CPU -- "Read / Write" --> Bus

    Bus -- "0x0000–0x7FFF<br/>ROM" --> Cart
    Bus -- "0xA000–0xBFFF<br/>Cart RAM" --> Cart
    Bus -- "0x8000–0x9FFF<br/>VRAM" --> PPU
    Bus -- "0xFF40–0xFF4B<br/>LCD regs" --> PPU
    Bus -- "0xFF10–0xFF3F<br/>Sound regs" --> APU
    Bus -- "0xFF04–0xFF07<br/>Timer regs" --> Timer
    Bus -- "0xFF00" --> Joypad

    Timer -- "IRQ bit 2" --> Bus
    PPU -- "IRQ bit 0, 1" --> Bus

    subgraph SDL2
        Video["Video<br/><i>160×144 → window</i>"]
        Audio["Audio<br/><i>44100 Hz mono</i>"]
        Input["Keyboard"]
    end

    PPU -. "framebuffer" .-> Video
    APU -. "samples" .-> Audio
    Input -. "key events" .-> Joypad
```

**Execution loop:** `GameBoy::Step()` calls `CPU::Step()`, then ticks `Timer`, `PPU`, and `APU`. Interrupt flags raised by Timer/PPU are written to the IF register via Bus, and dispatched by the CPU on the next step.

## Features

### Implemented
- [x] Full SM83 CPU (all instructions + CB prefix)
- [x] PPU with background, window and sprites
- [x] Timer and interrupts (VBlank, STAT, Timer, Joypad)
- [x] Joypad (keyboard input)
- [x] MBC1, MBC3, MBC5 (ROM/RAM banking)
- [x] 4-channel APU (2 square, wave, noise)
- [x] SDL2 rendering and audio

### TODO
- [ ] Battery-backed RAM (game saves)
- [ ] RTC (Real Time Clock) for MBC3
- [ ] Save states
- [ ] Game Boy Color (CGB) — double speed, palettes, VRAM banking
- [ ] Serial link
- [ ] Cycle-accurate timing

### Controls
| Key | GB Button |
|-----|-----------|
| Arrow keys | D-Pad |
| Z | A |
| X | B |
| Enter | Start |
| RShift | Select |
| Escape | Quit |

## Prerequisites

- CMake 3.20+
- C++23 compiler (MSVC 2022, GCC 13+, Clang 17+)
- vcpkg

## Setup

```bash
# Clone vcpkg (if not already done)
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg && bootstrap-vcpkg.bat  # Windows
cd vcpkg && ./bootstrap-vcpkg.sh # Linux/Mac

# Set VCPKG_ROOT
# Windows: setx VCPKG_ROOT "C:\path\to\vcpkg"
# Linux/Mac: export VCPKG_ROOT="/path/to/vcpkg"
```

## Build

```bash
cmake --preset default
cmake --build build/debug
```

## Test ROMs

Download Blargg's test ROMs:
```bash
git clone https://github.com/retrio/gb-test-roms.git test-roms
```

## Blargg Tests

```
cpu_instrs/01-special.gb        PASSED
cpu_instrs/02-interrupts.gb     PASSED
cpu_instrs/03-op sp,hl.gb       PASSED
cpu_instrs/04-op r,imm.gb       PASSED
cpu_instrs/05-op rp.gb          PASSED
cpu_instrs/06-ld r,r.gb         PASSED
cpu_instrs/07-jr,jp,call,ret    PASSED
cpu_instrs/08-misc instrs.gb    PASSED
cpu_instrs/09-op r,r.gb         PASSED
cpu_instrs/10-bit ops.gb        PASSED
cpu_instrs/11-op a,(hl).gb      PASSED
instr_timing.gb                 PASSED
halt_bug.gb                     PASSED
mem_timing/01-read_timing.gb    FAILED (needs cycle-accurate bus)
mem_timing/02-write_timing.gb   FAILED
mem_timing/03-modify_timing.gb  FAILED
```

## Resources

- [Pan Docs](https://gbdev.io/pandocs/) — Technical documentation
- [Opcodes](https://gbdev.io/gb-opcodes/optables/) — Interactive opcode table
- [Homebrew Hub](https://hh.gbdev.io/) — Legal homebrew ROMs
