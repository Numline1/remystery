// ReMystery for Windows: a screensaver (.scr) that hosts the scene (index.html) in WebView2.
//
// Command line (standard screensaver protocol):
//   /s            run full screen on every monitor
//   /p <hwnd>     draw the preview inside the given window
//   /c[:<hwnd>]   show the settings dialog (also the default with no arguments)

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl/client.h>

#include <cstdlib>
#include <cwctype>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "WebView2.h"
#include "resource.h"

using Microsoft::WRL::ComPtr;

namespace {

// Minimal COM handler that forwards Invoke() to a lambda. Works with MSVC and MinGW alike.
template <typename I, typename Method> class Handler;
template <typename I, typename... Args>
class Handler<I, HRESULT (STDMETHODCALLTYPE I::*)(Args...)> final : public I {
public:
    Handler(REFIID iid, std::function<HRESULT(Args...)> fn) : iid_(iid), fn_(std::move(fn)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, iid_)) {
            *out = static_cast<I*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n = InterlockedDecrement(&refs_);
        if (n == 0) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override { return fn_(args...); }

private:
    IID iid_;
    std::function<HRESULT(Args...)> fn_;
    LONG refs_ = 1;
};

template <typename I, typename F>
ComPtr<I> MakeHandler(REFIID iid, F fn) {
    ComPtr<I> handler;
    handler.Attach(new Handler<I, decltype(&I::Invoke)>(iid, std::move(fn)));
    return handler;
}
#define WV2_HANDLER(I, ...) MakeHandler<I>(IID_##I, __VA_ARGS__)

enum class Mode { Saver, Preview, Config };

struct Screen {
    HWND hwnd = nullptr;
    bool sound = false;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
};

const wchar_t kClassName[] = L"ReMysteryScreen";
const wchar_t kRegKey[] = L"Software\\ReMystery";
const UINT_PTR kInputTimer = 1;

Mode g_mode = Mode::Config;
std::vector<Screen*> g_screens;
ComPtr<ICoreWebView2Environment> g_env;
std::wstring g_html;
POINT g_cursor{};
DWORD g_lastInput = 0;
ULONGLONG g_started = 0;
bool g_closing = false;

bool LoadSoundSetting() {
    DWORD value = 1, size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, kRegKey, L"Sound", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value != 0;
}

void SaveSoundSetting(bool on) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        DWORD value = on ? 1 : 0;
        RegSetValueExW(key, L"Sound", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(key);
    }
}

std::wstring LoadScene() {
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_SCENE), RT_RCDATA);
    if (!res) return L"";
    HGLOBAL data = LoadResource(nullptr, res);
    const char* bytes = static_cast<const char*>(LockResource(data));
    int size = static_cast<int>(SizeofResource(nullptr, res));
    int wide = MultiByteToWideChar(CP_UTF8, 0, bytes, size, nullptr, 0);
    std::wstring html(wide, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes, size, html.data(), wide);
    return html;
}

std::wstring UserDataFolder() {
    wchar_t base[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, base))) return L"";
    return std::wstring(base) + L"\\ReMystery";
}

void Close() {
    if (g_closing) return;
    g_closing = true;
    for (Screen* s : g_screens) {
        if (s->controller) s->controller->Close();
        s->controller.Reset();
        s->webview.Reset();
    }
    PostQuitMessage(0);
}

void StartWebView(Screen* s) {
    g_env->CreateCoreWebView2Controller(s->hwnd,
        WV2_HANDLER(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
            [s](HRESULT hr, ICoreWebView2Controller* controller) -> HRESULT {
                if (FAILED(hr) || !controller || g_closing) return S_OK;
                s->controller = controller;
                controller->get_CoreWebView2(&s->webview);

                ComPtr<ICoreWebView2Controller2> controller2;
                if (SUCCEEDED(controller->QueryInterface(IID_ICoreWebView2Controller2,
                                                         reinterpret_cast<void**>(controller2.GetAddressOf())))) {
                    controller2->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{255, 0, 0, 0});
                }
                RECT rc;
                GetClientRect(s->hwnd, &rc);
                controller->put_Bounds(rc);
                controller->put_IsVisible(TRUE);

                ComPtr<ICoreWebView2Settings> settings;
                s->webview->get_Settings(&settings);
                settings->put_AreDefaultContextMenusEnabled(FALSE);
                settings->put_AreDevToolsEnabled(FALSE);
                settings->put_IsStatusBarEnabled(FALSE);
                settings->put_IsZoomControlEnabled(FALSE);

                // The page asks to quit on any mouse or keyboard input.
                s->webview->add_WebMessageReceived(
                    WV2_HANDLER(ICoreWebView2WebMessageReceivedEventHandler,
                        [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*) -> HRESULT {
                            if (g_mode == Mode::Saver) Close();
                            return S_OK;
                        }).Get(),
                    nullptr);

                // Pass the options the way the page expects them (as a query string).
                std::wstring config = L"window.__remysteryQuery='?sound=";
                config += s->sound ? L"1" : L"0";
                config += g_mode == Mode::Preview ? L"&preview=1';" : L"&preview=0';";
                s->webview->AddScriptToExecuteOnDocumentCreated(config.c_str(),
                    WV2_HANDLER(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler,
                        [s](HRESULT, LPCWSTR) -> HRESULT {
                            if (s->webview) s->webview->NavigateToString(g_html.c_str());
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
}

void StartEnvironment() {
    // Let the scene start its music without a click.
    SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", L"--autoplay-policy=no-user-gesture-required");
    std::wstring folder = UserDataFolder();
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, folder.empty() ? nullptr : folder.c_str(), nullptr,
        WV2_HANDLER(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
            [](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(hr) || !env) return S_OK;   // no runtime: stay a black screen
                g_env = env;
                for (Screen* s : g_screens) StartWebView(s);
                return S_OK;
            }).Get());
    if (FAILED(hr) && g_mode == Mode::Preview) Close();
}

bool InputSinceStart() {
    // WebView2 swallows input in its own window, so also watch the system-wide state.
    if (GetTickCount64() - g_started < 600) {
        GetCursorPos(&g_cursor);
        LASTINPUTINFO li{};
        li.cbSize = sizeof(li);
        if (GetLastInputInfo(&li)) g_lastInput = li.dwTime;
        return false;
    }
    POINT p;
    GetCursorPos(&p);
    if (std::abs(p.x - g_cursor.x) + std::abs(p.y - g_cursor.y) > 12) return true;
    LASTINPUTINFO li{};
    li.cbSize = sizeof(li);
    return GetLastInputInfo(&li) && li.dwTime != g_lastInput;
}

LRESULT CALLBACK ScreenProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Screen* s = reinterpret_cast<Screen*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        break;
    case WM_SIZE:
        if (s && s->controller) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            s->controller->put_Bounds(rc);
        }
        return 0;
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(reinterpret_cast<HDC>(wp), &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        return 1;
    }
    case WM_SETCURSOR:
        if (g_mode == Mode::Saver) { SetCursor(nullptr); return TRUE; }
        break;
    case WM_TIMER:
        if (wp == kInputTimer && g_mode == Mode::Saver && InputSinceStart()) Close();
        return 0;
    case WM_ACTIVATEAPP:
        if (!wp && g_mode == Mode::Saver && GetTickCount64() - g_started > 1000) Close();
        break;
    case WM_SYSCOMMAND:
        if (g_mode == Mode::Saver && (wp & 0xFFF0) == SC_SCREENSAVE) return 0;   // already running
        break;
    case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
        if (g_mode == Mode::Saver) Close();
        break;
    case WM_CLOSE:
        Close();
        return 0;
    case WM_DESTROY:
        if (g_mode == Mode::Preview) Close();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

BOOL CALLBACK AddMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM sound) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);
    auto* s = new Screen();
    s->sound = sound && (info.dwFlags & MONITORINFOF_PRIMARY);
    const RECT& r = info.rcMonitor;
    s->hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClassName, L"ReMystery", WS_POPUP | WS_VISIBLE,
                              r.left, r.top, r.right - r.left, r.bottom - r.top, nullptr, nullptr,
                              GetModuleHandleW(nullptr), s);
    g_screens.push_back(s);
    return TRUE;
}

INT_PTR CALLBACK ConfigProc(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG:
        CheckDlgButton(dlg, IDC_SOUND, LoadSoundSetting() ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) SaveSoundSetting(IsDlgButtonChecked(dlg, IDC_SOUND) == BST_CHECKED);
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) { EndDialog(dlg, LOWORD(wp)); return TRUE; }
        break;
    }
    return FALSE;
}

// Parses "/s", "-S", "/p 1234", "/p:1234", "/c:1234" and friends.
Mode ParseArgs(HWND* target) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    Mode mode = Mode::Config;
    if (argc > 1) {
        const wchar_t* a = argv[1];
        if (*a == L'/' || *a == L'-') a++;
        wchar_t c = static_cast<wchar_t>(std::towlower(*a));
        const wchar_t* rest = a[1] == L':' ? a + 2 : (argc > 2 ? argv[2] : nullptr);
        if (rest) *target = reinterpret_cast<HWND>(static_cast<INT_PTR>(_wcstoui64(rest, nullptr, 10)));
        if (c == L's') mode = Mode::Saver;
        else if (c == L'p') mode = Mode::Preview;
    }
    LocalFree(argv);
    return mode;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HWND target = nullptr;
    g_mode = ParseArgs(&target);

    if (g_mode == Mode::Config) {
        DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_CONFIG), IsWindow(target) ? target : nullptr, ConfigProc, 0);
        return 0;
    }
    if (g_mode == Mode::Preview && !IsWindow(target)) return 0;

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    g_html = LoadScene();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ScreenProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    if (g_mode == Mode::Saver) {
        EnumDisplayMonitors(nullptr, nullptr, AddMonitor, LoadSoundSetting() ? 1 : 0);
        g_started = GetTickCount64();
        if (!g_screens.empty()) SetTimer(g_screens.front()->hwnd, kInputTimer, 100, nullptr);
    } else {
        RECT rc;
        GetClientRect(target, &rc);
        auto* s = new Screen();
        s->hwnd = CreateWindowExW(0, kClassName, L"ReMystery", WS_CHILD | WS_VISIBLE, 0, 0, rc.right, rc.bottom,
                                  target, nullptr, instance, s);
        g_screens.push_back(s);
    }

    StartEnvironment();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    for (Screen* s : g_screens) {
        if (IsWindow(s->hwnd)) DestroyWindow(s->hwnd);
        delete s;
    }
    g_env.Reset();
    CoUninitialize();
    return 0;
}
