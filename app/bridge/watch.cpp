#include "pch.h"

#include "bridge/watch.h"
#include "dbg.h"
#include "platform/window.h"
#include "text.h"

namespace watch {
    const DWORD SETTLE_MS = 1500;

    std::thread g_thread;
    HANDLE g_stop = nullptr;
    std::vector<HANDLE> g_handles;

    void run() {
        bool dirty = false;
        while (true) {
            DWORD wait = WaitForMultipleObjects((DWORD)g_handles.size(), g_handles.data(), FALSE, dirty ? SETTLE_MS : INFINITE);
            if (wait == WAIT_OBJECT_0)
                return;
            if (wait == WAIT_TIMEOUT) {
                dirty = false;
                window::post_rescan();
                continue;
            }
            DWORD i = wait - WAIT_OBJECT_0;
            if (i < g_handles.size()) {
                FindNextChangeNotification(g_handles[i]);
                dirty = true;
            }
        }
    }

    bool init(const std::vector<std::string>& folders) {
        g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        g_handles.push_back(g_stop);
        for (const std::string& f : folders) {
            if (g_handles.size() >= MAXIMUM_WAIT_OBJECTS)
                break;
            HANDLE h = FindFirstChangeNotificationW(text::wide(f).c_str(), TRUE,
                                                    FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
            if (h == INVALID_HANDLE_VALUE) {
                dbg::print("[watch] cant watch %s", f.c_str());
                continue;
            }
            g_handles.push_back(h);
        }
        g_thread = std::thread(run);
        return true;
    }

    void cleanup() {
        if (g_stop)
            SetEvent(g_stop);
        if (g_thread.joinable())
            g_thread.join();
        for (size_t i = 1; i < g_handles.size(); i++)
            FindCloseChangeNotification(g_handles[i]);
        if (g_stop)
            CloseHandle(g_stop);
        g_handles.clear();
        g_stop = nullptr;
    }
}
