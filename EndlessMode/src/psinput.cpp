#include "psinput.h"

#include "log.h"

#include <Windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <atomic>
#include <string>
#include <thread>

namespace psinput {
    static std::atomic<bool> g_triangle = false, g_connected = false, g_started = false;
    static std::atomic<uint8_t> g_byte = 0x08;          // face nibble + hat; 0x08 = nothing pressed
    static std::atomic<unsigned> g_last_activity = 0;

    struct Pad { HANDLE h = INVALID_HANDLE_VALUE; USHORT pid = 0; };

    static bool IsPlayStation(USHORT vid, USHORT pid) {
        // Sony: DualShock 4 (05C4, 09CC), DualSense (0CE6), DualSense Edge (0DF2)
        return vid == 0x054C && (pid == 0x05C4 || pid == 0x09CC || pid == 0x0CE6 || pid == 0x0DF2);
    }

    static Pad Find() {
        GUID hid;
        HidD_GetHidGuid(&hid);
        HDEVINFO set = SetupDiGetClassDevsW(&hid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        Pad found;
        if (set == INVALID_HANDLE_VALUE) return found;
        SP_DEVICE_INTERFACE_DATA ifd{ sizeof(ifd) };
        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hid, i, &ifd) && found.h == INVALID_HANDLE_VALUE; i++) {
            DWORD need = 0;
            SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &need, nullptr);
            std::string buf(need, '\0');
            auto* det = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)buf.data();
            det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, det, need, nullptr, nullptr)) continue;
            HANDLE h = CreateFileW(det->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE) continue;
            HIDD_ATTRIBUTES a{ sizeof(a) };
            if (HidD_GetAttributes(h, &a) && IsPlayStation(a.VendorID, a.ProductID)) {
                found.h = h;
                found.pid = a.ProductID;
            } else {
                CloseHandle(h);
            }
        }
        SetupDiDestroyDeviceInfoList(set);
        return found;
    }

    // Byte holding the face buttons (high nibble: square 0x10, cross 0x20, circle 0x40, triangle 0x80) by report id.
    static int FaceButtonsOffset(const uint8_t* r, DWORD n, USHORT pid) {
        bool dualsense = pid == 0x0CE6 || pid == 0x0DF2;
        if (r[0] == 0x01) return dualsense ? (n >= 64 ? 8 : 5) : 5;   // USB (or BT simple mode: 5)
        if (r[0] == 0x31 && dualsense) return 9;                         // DualSense Bluetooth full report
        if (r[0] == 0x11 && !dualsense) return 7;                        // DualShock 4 Bluetooth full report
        return -1;
    }

    static void Loop() {
        for (;;) {
            Pad pad = Find();
            if (pad.h == INVALID_HANDLE_VALUE) { g_connected = false; g_triangle = false; Sleep(3000); continue; }
            g_connected = true;
            LOG("psinput: PlayStation controller found (pid %04x)", pad.pid);
            uint8_t report[128];
            for (;;) {
                DWORD n = 0;
                if (!ReadFile(pad.h, report, sizeof(report), &n, nullptr) || n == 0) break;
                int off = FaceButtonsOffset(report, n, pad.pid);
                if (off > 0 && off < (int)n) {
                    uint8_t b = report[off];
                    if (b != g_byte.load()) {
                        g_byte = b;
                        if (b != 0x08) g_last_activity = GetTickCount();
                    }
                    g_triangle = (b & 0x80) != 0;
                }
            }
            CloseHandle(pad.h);
            g_connected = false;
            g_triangle = false;
            g_byte = 0x08;
            LOG("psinput: controller disconnected");
            Sleep(1000);
        }
    }

    void Start() {
        if (g_started.exchange(true)) return;
        std::thread(Loop).detach();
    }

    bool TriangleDown() { return g_triangle; }

    bool Down(Button btn) {
        uint8_t b = g_byte, hat = b & 0x0F;
        switch (btn) {
        case kSquare: return b & 0x10;
        case kCross: return b & 0x20;
        case kCircle: return b & 0x40;
        case kTriangle: return b & 0x80;
        case kUp: return hat == 7 || hat == 0 || hat == 1;
        case kRight: return hat >= 1 && hat <= 3;
        case kDown: return hat >= 3 && hat <= 5;
        case kLeft: return hat >= 5 && hat <= 7;
        }
        return false;
    }

    unsigned LastActivityTick() { return g_last_activity; }
    bool Connected() { return g_connected; }
}
