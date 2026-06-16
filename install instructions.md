# yolo_mouse

Move the Windows mouse onto an object detected by a **YOLOv10** model (`.onnx`).
It captures the screen, runs inference with ONNX Runtime, finds the target's
center, and moves the cursor there either through the **standard Windows mouse
event** (`SendInput`) or through an **RP2040 / RP2350** acting as a real USB HID
mouse.

```
yolo_mouse/
├── src/main.cpp          PC application (C++17)
├── firmware/mouse_hid.ino  RP2040 / RP2350 USB-HID firmware
├── config.ini            runtime settings (no recompile needed)
├── CMakeLists.txt
└── README.md
```

---

## 1. Get a YOLOv10 model as `.onnx`

Train or download a YOLOv10 model, then export to ONNX (Python):

```bash
pip install ultralytics
yolo export model=yolov10n.pt format=onnx imgsz=640 opset=12
```

For the "dot on a black screen" test, train a 1-class model on screenshots of
the dot, or just use any model and set `targetClass` to the dot's class id.
Put the resulting file next to the exe and set `model = model.onnx` in
`config.ini`. The export `imgsz` must match `inputSize` in the config.

> The output tensor is expected to be YOLOv10's NMS-free `[1, N, 6]`
> (`x1,y1,x2,y2,conf,cls`). That is the default Ultralytics YOLOv10 ONNX export.

## 2. Get ONNX Runtime

Download the prebuilt Windows x64 package from
<https://github.com/microsoft/onnxruntime/releases> (e.g.
`onnxruntime-win-x64-1.20.1.zip`) and extract it anywhere.

## 3. Build the PC app

Requires Visual Studio 2019/2022 (Desktop C++) and CMake.

```powershell
cd "yolo_mouse"
cmake -B build -DONNXRUNTIME_DIR="C:/path/to/onnxruntime-win-x64-1.20.1"
cmake --build build --config Release
```

The exe, `onnxruntime.dll`, and `config.ini` end up in `build/Release/`.
Copy your `model.onnx` into that same folder.

## 4. Flash the firmware (only for serial mode)

1. Install the **Arduino-Pico** core (Earle Philhower) via the Arduino IDE
   Boards Manager.
2. Open `firmware/mouse_hid.ino`.
3. Set **Tools → USB Stack → Adafruit TinyUSB**.
4. Select board: **Raspberry Pi Pico** (RP2040) or **Raspberry Pi Pico 2**
   (RP2350).
5. Upload. The board now enumerates as a USB mouse **and** a serial port.
6. Note its COM port (Device Manager) and put it in `config.ini` as `comPort`.

## 5. Configure

Edit `config.ini` (next to the exe):

- `outputMode = windows` → moves the OS cursor directly (no hardware needed).
- `outputMode = serial`  → sends moves to the RP2040/RP2350; set `comPort`.
- `confThreshold`, `smoothing`, `gain`, `deadzone`, capture `region*`, etc.

## 6. Run

```powershell
cd build/Release
./yolo_mouse.exe
```

Controls:

| Key | Action            |
|-----|-------------------|
| F2  | toggle tracking   |
| F3  | quit              |

It starts with tracking **off** (`toggleStart = 1`). Press **F2** to start.
With a dot on a black screen, the cursor will home in on the dot.

---

### Notes & tuning

- **Two modes, same loop.** Only the final "move" step differs: `WindowsMouse`
  uses absolute `SendInput`; `SerialMouse` sends relative `M,dx,dy` deltas that
  the firmware turns into HID reports. In both cases `GetCursorPos` closes the
  loop, so smoothing/gain behave the same way.
- **Speed.** The default build uses the CPU execution provider. For higher FPS,
  use the DirectML ONNX Runtime package and add the DirectML execution provider
  in `main.cpp`, or run a smaller model (yolov10n) / smaller `inputSize`.
- **Capture region.** Capturing the full screen each frame and stretching to
  `inputSize` is simplest; if your target is always in one area, set
  `regionX/Y/W/H` to a smaller box for more resolution on the target and speed.
- **Aspect ratio.** The screen region is stretched (not letterboxed) to the
  square input. Center mapping stays correct because X and Y are scaled
  independently. If your model was trained with letterboxing and accuracy
  matters, retrain/export without letterbox or add letterbox handling.
