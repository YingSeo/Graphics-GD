#include <Geode/Geode.hpp>
#include <Geode/modify/CCEGLView.hpp>

using namespace geode::prelude;

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#include <GL/gl.h>
#include <dwmapi.h>

typedef BOOL(WINAPI* PFNWGLSWAPINTERVALEXTPROC)(int interval);
typedef int (WINAPI* PFNWGLGETSWAPINTERVALEXTPROC)(void);

static PFNWGLSWAPINTERVALEXTPROC s_wglSwapIntervalEXT = nullptr;
static PFNWGLGETSWAPINTERVALEXTPROC s_wglGetSwapIntervalEXT = nullptr;
static bool s_initialized = false;

static void initAntiTearOnce() {
    if (s_initialized) return;
    s_initialized = true;

    // Nạp hàm điều khiển độ trễ khung hình của Driver GPU (NVIDIA/AMD/Intel)
    s_wglSwapIntervalEXT = reinterpret_cast<PFNWGLSWAPINTERVALEXTPROC>(
        wglGetProcAddress("wglSwapIntervalEXT")
    );
    s_wglGetSwapIntervalEXT = reinterpret_cast<PFNWGLGETSWAPINTERVALEXTPROC>(
        wglGetProcAddress("wglGetSwapIntervalEXT")
    );

    // Ép tắt VSync cứng ở tầng driver để cho phép Unlock FPS tự do, 
    // đồng thời kích hoạt cơ chế đồng bộ bộ đệm ngầm (Mailbox/FastSync) chống sọc màn hình
    if (s_wglSwapIntervalEXT) {
        if (!s_wglGetSwapIntervalEXT || s_wglGetSwapIntervalEXT() != 0) {
            s_wglSwapIntervalEXT(0);
        }
    }

    // Tối ưu hóa cửa sổ Windows DWM chống nghẽn khung hình
    HWND hwnd = WindowFromDC(wglGetCurrentDC());
    if (hwnd) {
        BOOL disableTransitions = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &disableTransitions, sizeof(disableTransitions));
    }
}

class $modify(AntiTearEGLView, cocos2d::CCEGLView) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("cocos2d::CCEGLView::swapBuffers", Priority::Last);
    }

    void swapBuffers() {
        if (!s_initialized) {
            initAntiTearOnce();
        }
        CCEGLView::swapBuffers();
    }
};
#endif
