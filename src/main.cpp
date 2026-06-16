// yolo_mouse - move the mouse to an object detected by a YOLOv10 ONNX model.
//
// Two output modes:
//   * windows : move the OS cursor directly with SendInput.
//   * serial  : send relative move deltas to an RP2040 / RP2350 running the
//               companion firmware (firmware/mouse_hid.ino), which acts as a
//               real USB HID mouse.
//
// Designed for the simple test case "find a dot on a black screen and move the
// mouse to it", but works for any single-class or multi-class YOLOv10 model.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Configuration (loaded from config.ini next to the executable)
// ---------------------------------------------------------------------------
struct Config {
    std::string model    = "model.onnx";
    int   inputSize      = 640;     // YOLOv10 square input (must match export)
    float confThreshold  = 0.35f;   // minimum detection confidence
    int   targetClass    = -1;      // -1 = any class, otherwise filter to this id

    // Capture region in screen pixels. width/height = 0 means "full primary screen".
    int   regionX = 0, regionY = 0, regionW = 0, regionH = 0;

    std::string outputMode = "windows"; // "windows" or "serial"

    // Serial mode
    std::string comPort = "COM3";
    int   baud           = 115200;

    // Movement tuning
    float smoothing = 0.35f;  // 0..1 fraction of the remaining distance per frame
    float gain      = 1.0f;   // multiplier applied to the move delta
    int   deadzone  = 2;      // px; don't move if already this close to target

    // Behaviour
    bool  toggleStart = true; // start disabled; press F2 to enable
};

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

static Config loadConfig(const std::string& path) {
    Config c;
    std::ifstream f(path);
    if (!f) {
        std::cout << "[config] " << path << " not found, using defaults\n";
        return c;
    }
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq));
        std::string v = trim(line.substr(eq + 1));
        try {
            if      (k == "model")         c.model = v;
            else if (k == "inputSize")     c.inputSize = std::stoi(v);
            else if (k == "confThreshold") c.confThreshold = std::stof(v);
            else if (k == "targetClass")   c.targetClass = std::stoi(v);
            else if (k == "regionX")       c.regionX = std::stoi(v);
            else if (k == "regionY")       c.regionY = std::stoi(v);
            else if (k == "regionW")       c.regionW = std::stoi(v);
            else if (k == "regionH")       c.regionH = std::stoi(v);
            else if (k == "outputMode")    c.outputMode = v;
            else if (k == "comPort")       c.comPort = v;
            else if (k == "baud")          c.baud = std::stoi(v);
            else if (k == "smoothing")     c.smoothing = std::stof(v);
            else if (k == "gain")          c.gain = std::stof(v);
            else if (k == "deadzone")      c.deadzone = std::stoi(v);
            else if (k == "toggleStart")   c.toggleStart = (v == "1" || v == "true");
        } catch (...) {
            std::cout << "[config] bad value for '" << k << "'\n";
        }
    }
    return c;
}

// ---------------------------------------------------------------------------
// Screen capture: grab a region and stretch it straight into an inputSize^2
// 32-bit (BGRA) buffer using GDI HALFTONE scaling.
// ---------------------------------------------------------------------------
class ScreenGrabber {
public:
    ScreenGrabber(int size) : size_(size) {
        screenDC_ = GetDC(nullptr);
        memDC_    = CreateCompatibleDC(screenDC_);

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth       = size_;
        bmi.bmiHeader.biHeight      = -size_;   // top-down
        bmi.bmiHeader.biPlanes      = 1;
        bmi.bmiHeader.biBitCount     = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        dib_ = CreateDIBSection(memDC_, &bmi, DIB_RGB_COLORS,
                                reinterpret_cast<void**>(&bits_), nullptr, 0);
        old_ = static_cast<HBITMAP>(SelectObject(memDC_, dib_));
        SetStretchBltMode(memDC_, HALFTONE);
    }
    ~ScreenGrabber() {
        SelectObject(memDC_, old_);
        DeleteObject(dib_);
        DeleteDC(memDC_);
        ReleaseDC(nullptr, screenDC_);
    }

    // Capture (rx,ry,rw,rh) screen rect into the internal BGRA buffer.
    const uint8_t* grab(int rx, int ry, int rw, int rh) {
        StretchBlt(memDC_, 0, 0, size_, size_,
                   screenDC_, rx, ry, rw, rh, SRCCOPY);
        return bits_;
    }

private:
    int      size_;
    HDC      screenDC_, memDC_;
    HBITMAP  dib_, old_;
    uint8_t* bits_ = nullptr;
};

// ---------------------------------------------------------------------------
// Mouse output backends
// ---------------------------------------------------------------------------
struct Detection { float cx, cy, conf; int cls; };

class IMouse {
public:
    virtual ~IMouse() {}
    // Move toward absolute screen point (tx,ty); cfg supplies smoothing/gain.
    virtual void moveToward(int tx, int ty, const Config& cfg) = 0;
};

// SendInput backend: drives the real OS cursor (absolute positioning).
class WindowsMouse : public IMouse {
public:
    WindowsMouse() {
        vsX_ = GetSystemMetrics(SM_XVIRTUALSCREEN);
        vsY_ = GetSystemMetrics(SM_YVIRTUALSCREEN);
        vsW_ = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        vsH_ = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    }
    void moveToward(int tx, int ty, const Config& cfg) override {
        POINT p; GetCursorPos(&p);
        double nx = p.x + (tx - p.x) * cfg.smoothing * cfg.gain;
        double ny = p.y + (ty - p.y) * cfg.smoothing * cfg.gain;

        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        in.mi.dx = (LONG)std::lround((nx - vsX_) * 65535.0 / (vsW_ - 1));
        in.mi.dy = (LONG)std::lround((ny - vsY_) * 65535.0 / (vsH_ - 1));
        SendInput(1, &in, sizeof(in));
    }
private:
    int vsX_, vsY_, vsW_, vsH_;
};

// Serial backend: sends "M,dx,dy\n" relative deltas to the microcontroller.
class SerialMouse : public IMouse {
public:
    SerialMouse(const std::string& port, int baud) {
        std::string dev = "\\\\.\\" + port; // \\.\COMx works for COM10+
        h_ = CreateFileA(dev.c_str(), GENERIC_WRITE, 0, nullptr,
                         OPEN_EXISTING, 0, nullptr);
        if (h_ == INVALID_HANDLE_VALUE) {
            std::cerr << "[serial] cannot open " << port << " (err "
                      << GetLastError() << ")\n";
            return;
        }
        DCB dcb{}; dcb.DCBlength = sizeof(dcb);
        GetCommState(h_, &dcb);
        dcb.BaudRate = baud;
        dcb.ByteSize = 8;
        dcb.Parity   = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        SetCommState(h_, &dcb);

        COMMTIMEOUTS to{}; to.WriteTotalTimeoutConstant = 50;
        SetCommTimeouts(h_, &to);
        std::cout << "[serial] opened " << port << " @ " << baud << "\n";
    }
    ~SerialMouse() { if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_); }

    bool ok() const { return h_ != INVALID_HANDLE_VALUE; }

    void moveToward(int tx, int ty, const Config& cfg) override {
        if (h_ == INVALID_HANDLE_VALUE) return;
        POINT p; GetCursorPos(&p); // OS cursor reflects HID device movement
        int dx = (int)std::lround((tx - p.x) * cfg.smoothing * cfg.gain);
        int dy = (int)std::lround((ty - p.y) * cfg.smoothing * cfg.gain);
        if (dx == 0 && dy == 0) return;
        char buf[32];
        int n = snprintf(buf, sizeof(buf), "M,%d,%d\n", dx, dy);
        DWORD written;
        WriteFile(h_, buf, n, &written, nullptr);
    }
private:
    HANDLE h_ = INVALID_HANDLE_VALUE;
};

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
    Config cfg = loadConfig("config.ini");

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int rx = cfg.regionX, ry = cfg.regionY;
    int rw = cfg.regionW ? cfg.regionW : screenW;
    int rh = cfg.regionH ? cfg.regionH : screenH;

    std::cout << "=== yolo_mouse ===\n"
              << "model:   " << cfg.model << "\n"
              << "region:  " << rx << "," << ry << " " << rw << "x" << rh << "\n"
              << "output:  " << cfg.outputMode << "\n";

    // --- ONNX Runtime session ---
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "yolo_mouse");
    Ort::SessionOptions so;
    so.SetIntraOpNumThreads(4);
    so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    std::wstring wmodel(cfg.model.begin(), cfg.model.end());
    Ort::Session session(nullptr);
    try {
        session = Ort::Session(env, wmodel.c_str(), so);
    } catch (const Ort::Exception& e) {
        std::cerr << "[onnx] failed to load model: " << e.what() << "\n";
        return 1;
    }

    Ort::AllocatorWithDefaultOptions alloc;
    auto inNamePtr  = session.GetInputNameAllocated(0, alloc);
    auto outNamePtr = session.GetOutputNameAllocated(0, alloc);
    std::string inName  = inNamePtr.get();
    std::string outName = outNamePtr.get();
    const char* inNames[]  = { inName.c_str() };
    const char* outNames[] = { outName.c_str() };

    const int   S = cfg.inputSize;
    const size_t inCount = (size_t)3 * S * S;
    std::vector<float> input(inCount);
    std::array<int64_t, 4> inShape{1, 3, S, S};
    auto memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    ScreenGrabber grabber(S);

    // --- Mouse backend ---
    std::unique_ptr<IMouse> mouse;
    if (cfg.outputMode == "serial") {
        auto sm = std::make_unique<SerialMouse>(cfg.comPort, cfg.baud);
        if (!sm->ok()) return 1;
        mouse = std::move(sm);
    } else {
        mouse = std::make_unique<WindowsMouse>();
    }

    bool enabled = !cfg.toggleStart;
    std::cout << "\nControls:  F2 = toggle tracking   F3 = quit\n"
              << (enabled ? "tracking ON\n" : "tracking OFF (press F2)\n");

    bool prevF2 = false;
    while (true) {
        // --- hotkeys ---
        bool f2 = (GetAsyncKeyState(VK_F2) & 0x8000) != 0;
        if (f2 && !prevF2) {
            enabled = !enabled;
            std::cout << (enabled ? "tracking ON\n" : "tracking OFF\n");
        }
        prevF2 = f2;
        if (GetAsyncKeyState(VK_F3) & 0x8000) break;

        if (!enabled) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        // --- capture + preprocess (BGRA -> CHW RGB float [0,1]) ---
        const uint8_t* px = grabber.grab(rx, ry, rw, rh);
        const size_t plane = (size_t)S * S;
        for (int i = 0; i < S * S; ++i) {
            uint8_t b = px[i * 4 + 0];
            uint8_t g = px[i * 4 + 1];
            uint8_t r = px[i * 4 + 2];
            input[0 * plane + i] = r / 255.0f;
            input[1 * plane + i] = g / 255.0f;
            input[2 * plane + i] = b / 255.0f;
        }

        // --- inference ---
        Ort::Value inTensor = Ort::Value::CreateTensor<float>(
            memInfo, input.data(), inCount, inShape.data(), inShape.size());
        std::vector<Ort::Value> outputs;
        try {
            outputs = session.Run(Ort::RunOptions{nullptr}, inNames,
                                   &inTensor, 1, outNames, 1);
        } catch (const Ort::Exception& e) {
            std::cerr << "[onnx] run failed: " << e.what() << "\n";
            break;
        }

        // YOLOv10 output: [1, N, 6] = x1,y1,x2,y2,conf,cls (already NMS-free,
        // coordinates in input-pixel space, sorted by confidence).
        float* out = outputs[0].GetTensorMutableData<float>();
        auto shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        int N = (shape.size() == 3) ? (int)shape[1] : 0;
        int stride = (shape.size() == 3) ? (int)shape[2] : 6;

        Detection best{0, 0, -1.f, -1};
        for (int i = 0; i < N; ++i) {
            float* d = out + (size_t)i * stride;
            float conf = d[4];
            int   cls  = (int)d[5];
            if (conf < cfg.confThreshold) continue;
            if (cfg.targetClass >= 0 && cls != cfg.targetClass) continue;
            if (conf > best.conf) {
                best.cx = (d[0] + d[2]) * 0.5f;
                best.cy = (d[1] + d[3]) * 0.5f;
                best.conf = conf;
                best.cls = cls;
            }
        }

        if (best.conf > 0.f) {
            // map input-space center -> screen coordinates
            int tx = rx + (int)std::lround(best.cx * (double)rw / S);
            int ty = ry + (int)std::lround(best.cy * (double)rh / S);

            POINT p; GetCursorPos(&p);
            int dist = std::abs(tx - p.x) + std::abs(ty - p.y);
            if (dist > cfg.deadzone)
                mouse->moveToward(tx, ty, cfg);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::cout << "bye\n";
    return 0;
}
