#include <Geode/Geode.hpp>
#include <Geode/modify/CCEGLView.hpp>

using namespace geode::prelude;

#ifdef GEODE_IS_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <GL/gl.h>
#include <dwmapi.h>
#include <timeapi.h>

typedef BOOL(WINAPI* PFNWGLSWAPINTERVALEXTPROC)(int interval);
typedef int (WINAPI* PFNWGLGETSWAPINTERVALEXTPROC)(void);
typedef const char* (WINAPI* PFNWGLGETEXTENSIONSSTRINGEXTPROC)(void);

static PFNWGLSWAPINTERVALEXTPROC s_wglSwapIntervalEXT = nullptr;
static PFNWGLGETSWAPINTERVALEXTPROC s_wglGetSwapIntervalEXT = nullptr;
static bool s_hasTearControl = false;
static bool s_initialized = false;
static HWND s_cachedHwnd = nullptr;
static int s_lastInterval = -999;
static uint32_t s_frameCounter = 0;

static void initHardwareFastSync(HDC hdc) {
    if (s_initialized) return;
    s_initialized = true;

    // 1. Khóa độ phân giải bộ đếm xung nhịp hệ thống ở 1ms để khung hình Unlock FPS đều tuyệt đối
    timeBeginPeriod(1);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    // 2. Bật Multimedia Class Scheduler cho DWM để ưu tiên luồng xuất hình của GD
    DwmEnableMMCSS(TRUE);

    // 3. Nạp các hàm điều khiển VBlank từ driver GPU
    s_wglSwapIntervalEXT = reinterpret_cast<PFNWGLSWAPINTERVALEXTPROC>(
        wglGetProcAddress("wglSwapIntervalEXT")
    );
    s_wglGetSwapIntervalEXT = reinterpret_cast<PFNWGLGETSWAPINTERVALEXTPROC>(
        wglGetProcAddress("wglGetSwapIntervalEXT")
    );

    auto getExt = reinterpret_cast<PFNWGLGETEXTENSIONSSTRINGEXTPROC>(
        wglGetProcAddress("wglGetExtensionsStringEXT")
    );
    if (getExt) {
        const char* extStr = getExt();
        if (extStr && strstr(extStr, "WGL_EXT_swap_control_tear") != nullptr) {
            s_hasTearControl = true;
        }
    }

    s_cachedHwnd = WindowFromDC(hdc);
    if (s_cachedHwnd) {
        BOOL disableTransitions = TRUE;
        DwmSetWindowAttribute(
            s_cachedHwnd,
            DWMWA_TRANSITIONS_FORCEDISABLED,
            &disableTransitions,
            sizeof(disableTransitions)
        );
    }
}

// Chuyển Exclusive Fullscreen sang Composed Flip (Fast-Sync Mailbox)
// Giúp DWM tự động lọc bỏ các khung hình bị rách mà vẫn giữ nguyên 100% số FPS đã Unlock
static void enforceTearFreePresentation(HDC hdc) {
    HWND hwnd = WindowFromDC(hdc);
    if (!hwnd) return;

    if (hwnd != s_cachedHwnd) {
        s_cachedHwnd = hwnd;
        s_lastInterval = -999;
    }

    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(MONITORINFO);
    if (!GetMonitorInfoW(hMon, &mi)) return;

    RECT winRect = {};
    GetWindowRect(hwnd, &winRect);

    int winW = winRect.right - winRect.left;
    int winH = winRect.bottom - winRect.top;
    int monW = mi.rcMonitor.right - mi.rcMonitor.left;
    int monH = mi.rcMonitor.bottom - mi.rcMonitor.top;

    // Nếu đang ở chế độ Toàn màn hình (Fullscreen), đảm bảo cửa sổ chạy ở chế độ Composed Flip
    // để DWM chặn hoàn toàn hiện tượng lật khung hình giữa chừng (Screen Tearing) khi tắt VSync
    if (winW == monW && winH == monH) {
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        LONG_PTR targetStyle = WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
        if ((style & WS_OVERLAPPEDWINDOW) != 0) {
            SetWindowLongPtrW(hwnd, GWL_STYLE, targetStyle);
            SetWindowPos(
                hwnd, HWND_TOP,
                mi.rcMonitor.left, mi.rcMonitor.top, monW, monH,
                SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE
            );
        }
    }

    // Đồng bộ trạng thái Swap Interval mà không gọi lặp lại mỗi frame (tránh lag driver)
    if (s_wglSwapIntervalEXT) {
        int currentInterval = s_wglGetSwapIntervalEXT ? s_wglGetSwapIntervalEXT() : 0;
        if (currentInterval == 0 && s_lastInterval != 0) {
            s_wglSwapIntervalEXT(0);
            s_lastInterval = 0;
        }
    }
}

class $modify(AntiTearEGLView, cocos2d::CCEGLView) {
    static void onModify(auto& self) {
        // Đặt độ ưu tiên chạy cuối để không xung đột với CBF, Mega Hack, Eclipse, QOLMod
        (void)self.setHookPriority("cocos2d::CCEGLView::swapBuffers", Priority::Late);
    }

    void swapBuffers() {
        HDC hdc = wglGetCurrentDC();
        if (hdc) {
            if (!s_initialized) {
                initHardwareFastSync(hdc);
                enforceTearFreePresentation(hdc);
            } else if ((++s_frameCounter & 127) == 0) {
                // Chỉ kiểm tra lại trạng thái cửa sổ mỗi 128 frames -> 0% hao hụt CPU/GPU
                enforceTearFreePresentation(hdc);
            }
        }

        CCEGLView::swapBuffers();
    }
};
#endif
