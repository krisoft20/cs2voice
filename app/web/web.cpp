#include "pch.h"

#include <shellapi.h>
#include <shlwapi.h>
#include <wrl.h>

#include "WebView2.h"
#include "bridge/bridge.h"
#include "dbg.h"
#include "json/json.h"
#include "platform/net.h"
#include "text.h"
#include "web/web.h"

using Microsoft::WRL::Callback;

namespace web {
    const wchar_t* HOST = L"cs2voice.example";
    const wchar_t* MAPS_HOST = L"cs2voice-maps.example";
    const wchar_t* AVATARS_HOST = L"cs2voice-avatars.example";

    HWND g_hwnd = nullptr;
    ICoreWebView2Environment* g_env = nullptr;
    ICoreWebView2Controller* g_ctl = nullptr;
    ICoreWebView2* g_view = nullptr;
    std::map<std::wstring, int> g_files;
    std::wstring g_dev_url;
    std::wstring g_data_dir;
    std::vector<std::string> g_pending;
    std::wstring g_shot;
    std::wstring g_page;
    const UINT_PTR SHOT_TIMER = 1;
    UINT g_shot_delay = 6000;

    struct fetch_t {
        std::wstring url;
        page_fn_t done;
    };

    const UINT_PTR FETCH_TIMER = 2;
    const UINT FETCH_TIMEOUT = 15000;
    std::deque<fetch_t> g_fetches;
    ICoreWebView2Controller* g_hidden_ctl = nullptr;
    ICoreWebView2* g_hidden = nullptr;
    bool g_hidden_starting = false;
    bool g_fetching = false;

    void capture() {
        IStream* out = nullptr;
        if (FAILED(SHCreateStreamOnFileEx(g_shot.c_str(), STGM_CREATE | STGM_WRITE, FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, &out))) {
            dbg::print("[-] cant write screenshot");
            PostQuitMessage(2);
            return;
        }
        g_view->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, out,
                               Callback<ICoreWebView2CapturePreviewCompletedHandler>([out](HRESULT hr) -> HRESULT {
                                   out->Release();
                                   dbg::print("[web] screenshot 0x%08lx", hr);
                                   PostQuitMessage(SUCCEEDED(hr) ? 0 : 2);
                                   return S_OK;
                               }).Get());
    }

    void CALLBACK on_timer(HWND hwnd, UINT, UINT_PTR id, DWORD) {
        KillTimer(hwnd, id);
        capture();
    }

    bool resource(int id, const BYTE*& data, DWORD& size) {
        HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
        HGLOBAL h = r ? LoadResource(nullptr, r) : nullptr;
        data = h ? (const BYTE*)LockResource(h) : nullptr;
        size = r ? SizeofResource(nullptr, r) : 0;
        return data != nullptr;
    }

    void load_files() {
        const BYTE* data = nullptr;
        DWORD size = 0;
        if (!resource(999, data, size)) {
            dbg::print("[-] no ui inside the exe");
            return;
        }
        std::string index((const char*)data, size);
        size_t at = 0;
        while (at < index.size()) {
            size_t end = index.find('\n', at);
            if (end == std::string::npos)
                end = index.size();
            std::string line = index.substr(at, end - at);
            at = end + 1;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            size_t space = line.find(' ');
            if (space != std::string::npos)
                g_files[text::wide(line.substr(space + 1))] = atoi(line.c_str());
        }
        dbg::print("[web] %zu ui files inside the exe", g_files.size());
    }

    const wchar_t* mime(const std::wstring& path) {
        static const std::pair<const wchar_t*, const wchar_t*> types[] = {
            {L".html", L"text/html"}, {L".js", L"text/javascript"}, {L".css", L"text/css"}, {L".woff2", L"font/woff2"},
            {L".woff", L"font/woff"}, {L".svg", L"image/svg+xml"}, {L".png", L"image/png"}, {L".json", L"application/json"},
        };
        for (auto& [ext, type] : types) {
            size_t n = wcslen(ext);
            if (path.size() >= n && path.compare(path.size() - n, n, ext) == 0)
                return type;
        }
        return L"application/octet-stream";
    }

    void on_request(ICoreWebView2WebResourceRequestedEventArgs* args) {
        ICoreWebView2WebResourceRequest* req = nullptr;
        LPWSTR uri = nullptr;
        if (FAILED(args->get_Request(&req)) || FAILED(req->get_Uri(&uri))) {
            if (req)
                req->Release();
            return;
        }
        std::wstring path = uri;
        CoTaskMemFree(uri);
        req->Release();
        size_t host = path.find(HOST);
        path = host == std::wstring::npos ? L"/" : path.substr(host + wcslen(HOST));
        path = path.substr(0, path.find_first_of(L"?#"));
        if (path.empty() || path == L"/")
            path = L"/index.html";
        auto it = g_files.find(path);
        const BYTE* data = nullptr;
        DWORD size = 0;
        ICoreWebView2WebResourceResponse* resp = nullptr;
        if (it != g_files.end() && resource(it->second, data, size)) {
            IStream* body = SHCreateMemStream(data, size);
            std::wstring headers = std::wstring(L"Content-Type: ") + mime(path);
            g_env->CreateWebResourceResponse(body, 200, L"OK", headers.c_str(), &resp);
            if (body)
                body->Release();
        } else {
            dbg::print("[web] no ui file %s", text::utf8(path).c_str());
            g_env->CreateWebResourceResponse(nullptr, 404, L"Not Found", L"", &resp);
        }
        if (resp) {
            args->put_Response(resp);
            resp->Release();
        }
    }

    void on_message(ICoreWebView2WebMessageReceivedEventArgs* args) {
        LPWSTR json = nullptr;
        if (FAILED(args->get_WebMessageAsJson(&json)) || !json)
            return;
        std::string msg = text::utf8(json);
        CoTaskMemFree(json);
        bridge::handle(msg);
    }

    void setup_view() {
        ICoreWebView2Settings* s = nullptr;
        if (SUCCEEDED(g_view->get_Settings(&s))) {
            s->put_IsStatusBarEnabled(FALSE);
            s->put_IsZoomControlEnabled(FALSE);
            s->put_AreDefaultContextMenusEnabled(g_dev_url.empty() ? FALSE : TRUE);
            s->put_AreDevToolsEnabled(g_dev_url.empty() ? FALSE : TRUE);
            ICoreWebView2Settings9* s9 = nullptr;
            if (SUCCEEDED(s->QueryInterface(IID_PPV_ARGS(&s9)))) {
                s9->put_IsNonClientRegionSupportEnabled(TRUE);
                s9->Release();
            }
            s->Release();
        }
        EventRegistrationToken token;
        g_view->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                           [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                               on_message(args);
                                               return S_OK;
                                           })
                                           .Get(),
                                       &token);
        if (g_dev_url.empty()) {
            load_files();
            g_view->AddWebResourceRequestedFilter((std::wstring(L"https://") + HOST + L"/*").c_str(), COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
            g_view->add_WebResourceRequested(Callback<ICoreWebView2WebResourceRequestedEventHandler>(
                                                 [](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
                                                     on_request(args);
                                                     return S_OK;
                                                 })
                                                 .Get(),
                                             &token);
        }
        g_view->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                            [](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                                                dbg::print("[web] page loaded");
                                                return S_OK;
                                            })
                                            .Get(),
                                        &token);
        ICoreWebView2_3* v3 = nullptr;
        if (FAILED(g_view->QueryInterface(IID_PPV_ARGS(&v3)))) {
            dbg::print("[-] webview2 runtime too old for folder mapping");
            return;
        }
        v3->SetVirtualHostNameToFolderMapping(MAPS_HOST, (g_data_dir + L"\\maps").c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        v3->SetVirtualHostNameToFolderMapping(AVATARS_HOST, (g_data_dir + L"\\avatars").c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        v3->Release();
        std::wstring url = g_dev_url.empty() ? std::wstring(L"https://") + HOST + L"/index.html" : g_dev_url;
        if (!g_page.empty())
            url += L"#" + g_page;
        g_view->Navigate(url.c_str());
    }

    HRESULT on_controller(HRESULT hr, ICoreWebView2Controller* ctl) {
        if (FAILED(hr) || !ctl) {
            dbg::print("[-] webview2 controller failed 0x%08lx", hr);
            return S_OK;
        }
        g_ctl = ctl;
        g_ctl->AddRef();
        ICoreWebView2Controller2* c2 = nullptr;
        if (SUCCEEDED(g_ctl->QueryInterface(IID_PPV_ARGS(&c2)))) {
            COREWEBVIEW2_COLOR bg = {255, 14, 15, 18};
            c2->put_DefaultBackgroundColor(bg);
            c2->Release();
        }
        g_ctl->get_CoreWebView2(&g_view);
        resize();
        setup_view();
        if (!g_shot.empty())
            SetTimer(g_hwnd, SHOT_TIMER, g_shot_delay, on_timer);
        dbg::print("[web] ready");
        return S_OK;
    }

    HRESULT on_environment(HRESULT hr, ICoreWebView2Environment* env) {
        if (FAILED(hr) || !env) {
            dbg::print("[-] webview2 environment failed 0x%08lx", hr);
            MessageBoxW(g_hwnd, L"cs2voice needs the Microsoft Edge WebView2 Runtime. Get it from https://go.microsoft.com/fwlink/p/?LinkId=2124703",
                        L"cs2voice", MB_ICONERROR);
            PostQuitMessage(1);
            return S_OK;
        }
        g_env = env;
        g_env->AddRef();
        dbg::print("[web] environment");
        return g_env->CreateCoreWebView2Controller(
            g_hwnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(on_controller).Get());
    }

    void fetch_next();

    void hidden_close() {
        if (g_hidden)
            g_hidden->Release();
        if (g_hidden_ctl) {
            g_hidden_ctl->Close();
            g_hidden_ctl->Release();
        }
        g_hidden = nullptr;
        g_hidden_ctl = nullptr;
    }

    void fetch_done(bool ok, const std::string& body) {
        KillTimer(g_hwnd, FETCH_TIMER);
        g_fetching = false;
        if (g_fetches.empty())
            return;
        fetch_t f = std::move(g_fetches.front());
        g_fetches.pop_front();
        f.done(ok, body);
        if (!g_fetching)
            fetch_next();
    }

    void CALLBACK on_fetch_timer(HWND hwnd, UINT, UINT_PTR id, DWORD) {
        KillTimer(hwnd, id);
        dbg::print("[web] page fetch timed out");
        fetch_done(false, "");
    }

    void fetch_next() {
        if (g_fetches.empty()) {
            hidden_close();
            return;
        }
        if (!g_hidden)
            return;
        g_fetching = true;
        SetTimer(g_hwnd, FETCH_TIMER, FETCH_TIMEOUT, on_fetch_timer);
        g_hidden->Navigate(g_fetches.front().url.c_str());
    }

    void on_fetch_loaded() {
        if (!g_fetching)
            return;
        g_hidden->ExecuteScript(L"document.body ? document.body.innerText : ''",
                                Callback<ICoreWebView2ExecuteScriptCompletedHandler>([](HRESULT hr, LPCWSTR result) -> HRESULT {
                                    json_t v;
                                    std::string err;
                                    if (FAILED(hr) || !result || !json::parse(text::utf8(result), v, err))
                                        return S_OK;
                                    std::string body = json::str(&v);
                                    if (g_fetching && !body.empty() && body[0] == '{')
                                        fetch_done(true, body);
                                    return S_OK;
                                }).Get());
    }

    HRESULT on_hidden_controller(HRESULT hr, ICoreWebView2Controller* ctl) {
        g_hidden_starting = false;
        if (FAILED(hr) || !ctl) {
            dbg::print("[-] hidden webview failed 0x%08lx", hr);
            while (!g_fetches.empty())
                fetch_done(false, "");
            return S_OK;
        }
        g_hidden_ctl = ctl;
        g_hidden_ctl->AddRef();
        g_hidden_ctl->put_IsVisible(FALSE);
        g_hidden_ctl->get_CoreWebView2(&g_hidden);
        EventRegistrationToken token;
        g_hidden->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                              [](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                                                  on_fetch_loaded();
                                                  return S_OK;
                                              })
                                              .Get(),
                                          &token);
        fetch_next();
        return S_OK;
    }

    void fetch_page(const std::wstring& url, page_fn_t done) {
        g_fetches.push_back({url, std::move(done)});
        if (g_fetching || g_hidden_starting)
            return;
        if (g_hidden) {
            fetch_next();
            return;
        }
        if (!g_env) {
            fetch_done(false, "");
            return;
        }
        g_hidden_starting = true;
        g_env->CreateCoreWebView2Controller(g_hwnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(on_hidden_controller).Get());
    }

    bool installed() {
        LPWSTR ver = nullptr;
        if (FAILED(GetAvailableCoreWebView2BrowserVersionString(nullptr, &ver)) || !ver)
            return false;
        dbg::print("[web] webview2 %s", text::utf8(ver).c_str());
        CoTaskMemFree(ver);
        return true;
    }

    bool runtime() {
        if (installed())
            return true;
        dbg::print("[-] no webview2 runtime");
        if (MessageBoxW(nullptr, L"cs2voice needs the Microsoft Edge WebView2 Runtime, which is missing on this PC.\n\nDownload and install it from Microsoft now?",
                        L"cs2voice", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return false;
        std::string setup;
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        std::wstring path = std::wstring(tmp) + L"MicrosoftEdgeWebview2Setup.exe";
        FILE* f = net::get(L"https://go.microsoft.com/fwlink/p/?LinkId=2124703", setup) ? _wfopen(path.c_str(), L"wb") : nullptr;
        bool ok = f && fwrite(setup.data(), 1, setup.size(), f) == setup.size();
        if (f)
            fclose(f);
        SHELLEXECUTEINFOW sei = {sizeof(sei)};
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpFile = path.c_str();
        sei.lpParameters = L"/silent /install";
        sei.nShow = SW_HIDE;
        if (ok && ShellExecuteExW(&sei) && sei.hProcess) {
            WaitForSingleObject(sei.hProcess, INFINITE);
            CloseHandle(sei.hProcess);
        }
        DeleteFileW(path.c_str());
        if (installed())
            return true;
        MessageBoxW(nullptr, L"The WebView2 Runtime could not be installed. Get it from https://go.microsoft.com/fwlink/p/?LinkId=2124703", L"cs2voice",
                    MB_ICONERROR);
        return false;
    }

    bool init(HWND hwnd, const std::wstring& dev_url, const std::wstring& profile_dir, const std::wstring& data_dir) {
        g_hwnd = hwnd;
        g_data_dir = data_dir;
        g_dev_url = dev_url;
        HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, profile_dir.c_str(), nullptr, Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(on_environment).Get());
        if (FAILED(hr)) {
            dbg::print("[-] webview2 create failed 0x%08lx", hr);
            return false;
        }
        return true;
    }

    void cleanup() {
        g_fetches.clear();
        hidden_close();
        if (g_view)
            g_view->Release();
        if (g_ctl) {
            g_ctl->Close();
            g_ctl->Release();
        }
        if (g_env)
            g_env->Release();
        g_view = nullptr;
        g_ctl = nullptr;
        g_env = nullptr;
    }

    void resize() {
        if (!g_ctl)
            return;
        RECT r;
        GetClientRect(g_hwnd, &r);
        g_ctl->put_Bounds(r);
    }

    void screenshot(const std::wstring& path, const std::wstring& page, UINT delay_ms) {
        if (delay_ms)
            g_shot_delay = delay_ms;
        g_shot = path;
        g_page = page;
    }

    void post(const std::string& json) {
        if (!g_view) {
            g_pending.push_back(json);
            return;
        }
        for (const std::string& p : g_pending)
            g_view->PostWebMessageAsJson(text::wide(p).c_str());
        g_pending.clear();
        g_view->PostWebMessageAsJson(text::wide(json).c_str());
    }

    void post_buffer(const std::string& bytes, const std::string& json) {
        ICoreWebView2Environment12* env = nullptr;
        ICoreWebView2_17* view = nullptr;
        ICoreWebView2SharedBuffer* buf = nullptr;
        BYTE* p = nullptr;
        bool ok = g_env && g_view && SUCCEEDED(g_env->QueryInterface(IID_PPV_ARGS(&env))) &&
                  SUCCEEDED(g_view->QueryInterface(IID_PPV_ARGS(&view))) && SUCCEEDED(env->CreateSharedBuffer(bytes.size(), &buf)) &&
                  SUCCEEDED(buf->get_Buffer(&p));
        if (ok) {
            memcpy(p, bytes.data(), bytes.size());
            ok = SUCCEEDED(view->PostSharedBufferToScript(buf, COREWEBVIEW2_SHARED_BUFFER_ACCESS_READ_ONLY, text::wide(json).c_str()));
        }
        if (!ok)
            dbg::print("[web] shared buffer of %zu bytes failed", bytes.size());
        if (buf)
            buf->Release();
        if (view)
            view->Release();
        if (env)
            env->Release();
    }
}
