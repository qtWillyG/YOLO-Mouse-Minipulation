<h1 align="center">YOLO Mouse C++ Edition</h1>

<p align="center">
  Move the mouse onto an on-screen object detected by a <b>YOLOv10</b> model —
  through the standard Windows mouse event <i>or</i> a real USB HID mouse on an
  RP2040 / RP2350.
</p>

<p align="center">
  <img alt="language" src="https://img.shields.io/badge/language-C%2B%2B17-00599C">
  <img alt="platform" src="https://img.shields.io/badge/platform-Windows%20x64-0078D6">
  <img alt="inference" src="https://img.shields.io/badge/inference-ONNX%20Runtime-005CED">
  <img alt="model" src="https://img.shields.io/badge/model-YOLOv10-ff6f00">
  <img alt="mcu" src="https://img.shields.io/badge/MCU-RP2040%20%2F%20RP2350-7CB342">
</p>

---

## Overview

`yolo_mouse` captures the screen, runs a YOLOv10 `.onnx` model with ONNX
Runtime, finds the highest-confidence target, and moves the cursor to it. The
detection loop is identical in both output modes — only the final "move" step
differs:

- **Windows mode** — drives the OS cursor with `SendInput`. No hardware needed.
- **Serial mode** — streams relative deltas to an RP2040 / RP2350 that enumerates
  as a genuine USB HID mouse, so the movement comes from real hardware.

It was built for the simple test case *"find a dot on a blank black screen and
move the mouse to it"*, but works with any single- or multi-class YOLOv10 model.

## Features

- 🎯 **YOLOv10** detection (NMS-free `[1, N, 6]` output, no post-processing)
- 🖱️ **Two output backends** — Windows `SendInput` or hardware USB HID
- 🔌 **RP2040 & RP2350** firmware included (Arduino-Pico + TinyUSB)
- ⚙️ **Live config** via `config.ini` — no recompile to retune
- 🎚️ Smoothing, gain, deadzone, confidence threshold & class filtering
- ⌨️ Hotkeys: **F2** toggle tracking, **F3** quit

## How it works

```
┌────────────┐   ┌──────────────┐   ┌──────────────┐   ┌─────────────────────┐
│ Screen grab│ → │ YOLOv10 ONNX │ → │ pick target  │ → │ move cursor         │
│ (GDI)      │   │ (ONNX RT)    │   │ + map to px  │   │ Windows  |  Serial  │
└────────────┘   └──────────────┘   └──────────────┘   └─────────────────────┘
                                                          SendInput   M,dx,dy → MCU
```

In both modes `GetCursorPos` closes the loop, so `smoothing` / `gain` /
`deadzone` behave the same way regardless of backend.

## Project layout

| Path | Description |
|------|-------------|
| `src/main.cpp` | PC application (C++17) |
| `firmware/mouse_hid.ino` | RP2040 / RP2350 USB-HID firmware |
| `config.ini` | Runtime settings |
| `CMakeLists.txt` | Build configuration |
| `install instructions.md` | Full setup & build guide |

## Quick start

```powershell
# 1. export a model:  yolo export model=yourmodel.pt format=onnx imgsz=640
# 2. grab ONNX Runtime (onnxruntime-win-x64-*.zip) and extract it
cmake -B build -DONNXRUNTIME_DIR="C:/path/to/onnxruntime-win-x64-1.20.1"
cmake --build build --config Release
# copy model.onnx next to build/Release/yolo_mouse.exe, then:
cd build/Release; ./yolo_mouse.exe   # press F2 to start, F3 to quit
```

📖 **Full instructions** (model export, ONNX Runtime, firmware flashing,
configuration, tuning) are in **[install instructions.md](install%20instructions.md)**.

## Configuration

Edit `config.ini` next to the executable. Key options:

| Setting | Meaning |
|---------|---------|
| `model` | Path to the YOLOv10 `.onnx` file |
| `outputMode` | `windows` or `serial` |
| `comPort` | COM port of the RP2040 / RP2350 (serial mode) |
| `confThreshold` | Minimum detection confidence |
| `targetClass` | `-1` for any class, or a specific class id |
| `regionX/Y/W/H` | Capture region (`0` width/height = full screen) |
| `smoothing` / `gain` / `deadzone` | Movement tuning |

## Requirements

- Windows x64, Visual Studio 2019/2022 (Desktop C++), CMake ≥ 3.15
- [ONNX Runtime](https://github.com/microsoft/onnxruntime/releases) (Windows x64)
- A YOLOv10 model exported to ONNX ([Ultralytics](https://github.com/THU-MIG/yolov10))
- *Serial mode only:* RP2040 / RP2350 board + [Arduino-Pico](https://github.com/earlephilhower/arduino-pico) core

## License

MIT
