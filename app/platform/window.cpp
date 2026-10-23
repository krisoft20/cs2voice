#include "pch.h"

#include <dwmapi.h>
#include <shellapi.h>

#include "bridge/bridge.h"
#include "dbg.h"
#include "platform/window.h"
#include "web/web.h"

namespace window {
    const wchar_t* CLASS_NAME = L"cs2voice";
    const COLORREF BACKGROUND = RGB(16, 19, 24);

    HWND g_hwnd = nullptr;
    HINSTANCE g_inst = nullptr;
    HBRUSH g_brush = nullptr;
    std::string g_close = "ask";
    bool g_asking = false;
    bool g_tray = false;
    UINT g_taskbar = 0;
    std::wstring g_open_text = L"Open cs2voice";
    std::wstring g_quit_text = L"Quit";

    int frame_y(HWND hwnd) {
        UINT dpi = GetDpiForWindow(hwnd);
        return GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    }

    void tray_add() {
        if (g_tray)
            return;
        NOTIFYICONDATAW n = {sizeof(n)};
        n.hWnd = g_hwnd;
        n.uID = 1;
        n.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        n.uCallbackMessage = WM_TRAY;
        n.hIcon = (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
        wcscpy_s(n.szTip, L"cs2voice");
        g_tray = Shell_NotifyIconW(NIM_ADD, &n) != FALSE;
        if (n.hIcon)
            DestroyIcon(n.hIcon);
        if (!g_tray)
            dbg::print("[window] tray icon failed %lu", GetLastError());
    }

    void tray_remove() {
        if (!g_tray)
            return;
        NOTIFYICONDATAW n = {sizeof(n)};
        n.hWnd = g_hwnd;
        n.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &n);
        g_tray = false;
    }

    void hide() {
        tray_add();
        ShowWindow(g_hwnd, SW_HIDE);
        dbg::print("[window] hidden to the tray");
    }

    void show() {
        ShowWindow(g_hwnd, IsIconic(g_hwnd) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(g_hwnd);
    }

    void quit() {
        tray_remove();
        DestroyWindow(g_hwnd);
    }

    void tray_menu() {
        HMENU m = CreatePopupMenu();
        AppendMenuW(m, MF_STRING, 1, g_open_text.c_str());
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, 2, g_quit_text.c_str());
        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(g_hwnd);
        int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, g_hwnd, nullptr);
        PostMessageW(g_hwnd, WM_NULL, 0, 0);
        DestroyMenu(m);
        if (cmd == 1)
            show();
        else if (cmd == 2)
            quit();
    }

    LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == g_taskbar && g_taskbar && g_tray) {
            g_tray = false;
            tray_add();
            return 0;
        }
        switch (msg) {
        case WM_NCCALCSIZE: {
            if (!wp)
                break;
            NCCALCSIZE_PARAMS* p = (NCCALCSIZE_PARAMS*)lp;
            LONG top = p->rgrc[0].top;
            DefWindowProcW(hwnd, msg, wp, lp);
            p->rgrc[0].top = top + (IsZoomed(hwnd) ? frame_y(hwnd) : 0);
            return 0;
        }
        case WM_SIZE:
            web::resize();
            web::post(std::string("{\"type\":\"window\",\"maximized\":") + (IsZoomed(hwnd) ? "true" : "false") + "}");
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mm = (MINMAXINFO*)lp;
            mm->ptMinTrackSize.x = 900;
            mm->ptMinTrackSize.y = 560;
            return 0;
        }
        case WM_POST_JSON: {
            std::string* json = (std::string*)lp;
            web::post(*json);
            delete json;
            return 0;
        }
        case WM_POST_BUFFER: {
            buffer_t* b = (buffer_t*)lp;
            web::post_buffer(b->bytes, b->json);
            delete b;
            return 0;
        }
        case WM_RESCAN:
            bridge::rescan();
            return 0;
        case WM_CLOSE:
            if (g_close == "tray") {
                hide();
                return 0;
            }
            if (g_close == "quit" || g_asking)
                break;
            g_asking = true;
            web::post("{\"type\":\"close_ask\"}");
            return 0;
        case WM_TRAY:
            if (LOWORD(lp) == WM_LBUTTONUP)
                show();
            else if (LOWORD(lp) == WM_RBUTTONUP)
                tray_menu();
            return 0;
        case WM_SHOW:
            show();
            return 0;
        case WM_HIDE:
            hide();
            return 0;
        case WM_QUIT_APP:
            quit();
            return 0;
        case WM_DESTROY:
            tray_remove();
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    bool init(HINSTANCE inst, int w, int h, bool hidden) {
        g_inst = inst;
        g_taskbar = RegisterWindowMessageW(L"TaskbarCreated");
        g_brush = CreateSolidBrush(BACKGROUND);
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.lpfnWndProc = proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = g_brush;
        wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
        wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
        wc.lpszClassName = CLASS_NAME;
        if (!RegisterClassExW(&wc)) {
            dbg::print("[-] window class failed %lu", GetLastError());
            return false;
        }
        UINT dpi = GetDpiForSystem();
        int sw = MulDiv(w, dpi, 96), sh = MulDiv(h, dpi, 96);
        RECT work;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        if (!hidden) {
            sw = std::min(sw, (int)(work.right - work.left));
            sh = std::min(sh, (int)(work.bottom - work.top));
        }
        int x = work.left + (work.right - work.left - sw) / 2;
        int y = work.top + (work.bottom - work.top - sh) / 2;
        if (hidden) {
            x = -sw - 200;
            y = -sh - 200;
        }
        g_hwnd = CreateWindowExW(hidden ? WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE : 0, CLASS_NAME, L"cs2voice", WS_OVERLAPPEDWINDOW, x, y, sw,
                                 sh, nullptr, nullptr, inst, nullptr);
        if (!g_hwnd) {
            dbg::print("[-] window failed %lu", GetLastError());
            return false;
        }
        SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
        BOOL dark = TRUE;
        DwmSetWindowAttribute(g_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        COLORREF caption = BACKGROUND;
        DwmSetWindowAttribute(g_hwnd, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
        ShowWindow(g_hwnd, hidden ? SW_SHOWNOACTIVATE : SW_SHOW);
        UpdateWindow(g_hwnd);
        if (!hidden)
            tray_add();
        return true;
    }

    void cleanup() {
        if (g_hwnd)
            DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
        UnregisterClassW(CLASS_NAME, g_inst);
        if (g_brush)
            DeleteObject(g_brush);
        g_brush = nullptr;
    }

    HWND handle() {
        return g_hwnd;
    }

    void post_json(const std::string& json) {
        std::string* copy = new std::string(json);
        if (!PostMessageW(g_hwnd, WM_POST_JSON, 0, (LPARAM)copy))
            delete copy;
    }

    void post_buffer(std::string&& bytes, const std::string& json) {
        buffer_t* b = new buffer_t{std::move(bytes), json};
        if (!PostMessageW(g_hwnd, WM_POST_BUFFER, 0, (LPARAM)b))
            delete b;
    }

    void command(const std::string& action) {
        if (action == "minimize")
            ShowWindow(g_hwnd, SW_MINIMIZE);
        else if (action == "maximize")
            ShowWindow(g_hwnd, IsZoomed(g_hwnd) ? SW_RESTORE : SW_MAXIMIZE);
        else if (action == "close")
            PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
        else if (action == "tray")
            PostMessageW(g_hwnd, WM_HIDE, 0, 0);
        else if (action == "quit")
            PostMessageW(g_hwnd, WM_QUIT_APP, 0, 0);
        else if (action == "stay")
            g_asking = false;
        if (action == "tray" || action == "quit")
            g_asking = false;
    }

    void set_close(const std::string& mode) {
        g_close = mode;
    }

    void set_menu(const std::wstring& open, const std::wstring& quit) {
        g_open_text = open;
        g_quit_text = quit;
    }

    void post_rescan() {
        PostMessageW(g_hwnd, WM_RESCAN, 0, 0);
    }

    bool pump() {
        MSG msg;
        if (GetMessageW(&msg, nullptr, 0, 0) <= 0)
            return false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        return true;
    }
}
