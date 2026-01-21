#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include "cJSON.h"
#include "icon_data.h"

#define CONFIG_FILENAME L"config.json"
#define APP_FOLDER L"MediaKeys"

#define APP_NAME L"MediaKeys"
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_ICON 1
#define ID_TRAY_EXIT 1001
#define ID_TRAY_STARTUP 1002
#define MAX_BINDINGS 64

typedef enum {
    MODIFIER_NONE,
    MODIFIER_LEFT,
    MODIFIER_RIGHT,
    MODIFIER_EITHER,
    MODIFIER_BOTH
} ModifierState;

typedef enum { TRIGGER_KEYBOARD, TRIGGER_MOUSE_BUTTON, TRIGGER_MOUSE_WHEEL } TriggerType;

typedef enum {
    MOUSE_BUTTON_LEFT,
    MOUSE_BUTTON_RIGHT,
    MOUSE_BUTTON_MIDDLE,
    MOUSE_BUTTON_X1,
    MOUSE_BUTTON_X2
} MouseButton;

typedef enum { WHEEL_UP, WHEEL_DOWN } WheelDirection;

typedef enum {
    ACTION_VOLUME_UP,
    ACTION_VOLUME_DOWN,
    ACTION_VOLUME_MUTE,
    ACTION_PLAY_PAUSE,
    ACTION_PREV_TRACK,
    ACTION_NEXT_TRACK
} MediaAction;

typedef struct {
    ModifierState ctrl;
    ModifierState shift;
    ModifierState alt;
    ModifierState win;

    TriggerType triggerType;
    union {
        DWORD keyCode;
        MouseButton mouseButton;
        WheelDirection wheelDir;
    } trigger;

    MediaAction action;
} HotkeyBinding;

static HWND mainWindow = NULL;
static NOTIFYICONDATAW notifyIconData = {0};
static HMENU trayMenu = NULL;
static HHOOK keyboardHook = NULL;
static HHOOK mouseHook = NULL;
static HotkeyBinding bindings[MAX_BINDINGS] = {0};
static int bindingCount = 0;
static BOOL suppressWinKeyUp = FALSE;
static HICON appIcon = NULL;

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam);
static BOOL InitTrayIcon(HWND hwnd);
static void RemoveTrayIcon(void);
static void ShowTrayMenu(HWND hwnd);
static BOOL RegisterWindowClass(HINSTANCE hInstance);
static HWND CreateMessageWindow(HINSTANCE hInstance);
static BOOL InstallHooks(void);
static void RemoveHooks(void);
static BOOL CheckModifiers(const HotkeyBinding *binding);
static void ExecuteAction(MediaAction action);
static BOOL LoadConfig(void);
static BOOL GetConfigPath(WCHAR *path, DWORD pathLen, BOOL *isAppData);
static BOOL CreateDefaultConfig(const WCHAR *path);
static ModifierState ParseModifierState(const char *str);
static BOOL ParseTrigger(const char *str, TriggerType *type, HotkeyBinding *binding);
static MediaAction ParseAction(const char *str);
static HICON LoadIconFromMemory(const unsigned char *data, unsigned int size);
static BOOL GetStartupShortcutPath(WCHAR *path, DWORD pathLen);
static BOOL IsStartupEnabled(void);
static BOOL EnableStartup(void);
static BOOL DisableStartup(void);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    MSG msg;

    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
    }

    appIcon = LoadIconFromMemory(icon_ico, icon_ico_len);
    if (!appIcon) {
        appIcon = LoadIconW(NULL, IDI_APPLICATION);
    }

    if (!RegisterWindowClass(hInstance)) {
        MessageBoxW(NULL, L"Failed to register window class", APP_NAME, MB_ICONERROR);
        return 1;
    }

    mainWindow = CreateMessageWindow(hInstance);
    if (!mainWindow) {
        MessageBoxW(NULL, L"Failed to create window", APP_NAME, MB_ICONERROR);
        return 1;
    }

    if (!InitTrayIcon(mainWindow)) {
        MessageBoxW(NULL, L"Failed to create tray icon", APP_NAME, MB_ICONERROR);
        DestroyWindow(mainWindow);
        return 1;
    }

    trayMenu = CreatePopupMenu();
    if (trayMenu) {
        UINT startupFlags = MF_STRING | (IsStartupEnabled() ? MF_CHECKED : MF_UNCHECKED);
        AppendMenuW(trayMenu, startupFlags, ID_TRAY_STARTUP, L"Run at startup");
        AppendMenuW(trayMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(trayMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");
    }

    if (!LoadConfig()) {
        MessageBoxW(NULL, L"Failed to load configuration", APP_NAME, MB_ICONERROR);
        RemoveTrayIcon();
        DestroyWindow(mainWindow);
        return 1;
    }

    if (!InstallHooks()) {
        MessageBoxW(NULL, L"Failed to install hooks", APP_NAME, MB_ICONERROR);
        RemoveTrayIcon();
        DestroyWindow(mainWindow);
        return 1;
    }

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    RemoveHooks();
    RemoveTrayIcon();
    if (trayMenu) {
        DestroyMenu(trayMenu);
    }
    if (appIcon) {
        DestroyIcon(appIcon);
    }

    return (int)msg.wParam;
}

static const char *DEFAULT_CONFIG =
    "{\n"
    "  \"bindings\": [\n"
    "    { \"win\": \"left\", \"trigger\": \"wheel_up\", \"action\": \"volume_up\" },\n"
    "    { \"win\": \"left\", \"trigger\": \"wheel_down\", \"action\": \"volume_down\" },\n"
    "    { \"win\": \"left\", \"trigger\": \"mouse_x2\", \"action\": \"next_track\" },\n"
    "    { \"win\": \"left\", \"trigger\": \"mouse_x1\", \"action\": \"prev_track\" },\n"
    "    { \"win\": \"left\", \"trigger\": \"mouse_middle\", \"action\": \"play_pause\" }\n"
    "  ]\n"
    "}\n";

static ModifierState ParseModifierState(const char *str) {
    if (!str || strcmp(str, "none") == 0)
        return MODIFIER_NONE;
    if (strcmp(str, "left") == 0)
        return MODIFIER_LEFT;
    if (strcmp(str, "right") == 0)
        return MODIFIER_RIGHT;
    if (strcmp(str, "either") == 0)
        return MODIFIER_EITHER;
    if (strcmp(str, "both") == 0)
        return MODIFIER_BOTH;
    return MODIFIER_NONE;
}

static MediaAction ParseAction(const char *str) {
    if (!str)
        return ACTION_VOLUME_UP;
    if (strcmp(str, "volume_up") == 0)
        return ACTION_VOLUME_UP;
    if (strcmp(str, "volume_down") == 0)
        return ACTION_VOLUME_DOWN;
    if (strcmp(str, "volume_mute") == 0)
        return ACTION_VOLUME_MUTE;
    if (strcmp(str, "play_pause") == 0)
        return ACTION_PLAY_PAUSE;
    if (strcmp(str, "prev_track") == 0)
        return ACTION_PREV_TRACK;
    if (strcmp(str, "next_track") == 0)
        return ACTION_NEXT_TRACK;
    return ACTION_VOLUME_UP;
}

static BOOL ParseTrigger(const char *str, TriggerType *type, HotkeyBinding *binding) {
    if (!str)
        return FALSE;

    if (strcmp(str, "wheel_up") == 0) {
        *type = TRIGGER_MOUSE_WHEEL;
        binding->trigger.wheelDir = WHEEL_UP;
        return TRUE;
    }
    if (strcmp(str, "wheel_down") == 0) {
        *type = TRIGGER_MOUSE_WHEEL;
        binding->trigger.wheelDir = WHEEL_DOWN;
        return TRUE;
    }

    if (strcmp(str, "mouse_left") == 0) {
        *type = TRIGGER_MOUSE_BUTTON;
        binding->trigger.mouseButton = MOUSE_BUTTON_LEFT;
        return TRUE;
    }
    if (strcmp(str, "mouse_right") == 0) {
        *type = TRIGGER_MOUSE_BUTTON;
        binding->trigger.mouseButton = MOUSE_BUTTON_RIGHT;
        return TRUE;
    }
    if (strcmp(str, "mouse_middle") == 0) {
        *type = TRIGGER_MOUSE_BUTTON;
        binding->trigger.mouseButton = MOUSE_BUTTON_MIDDLE;
        return TRUE;
    }
    if (strcmp(str, "mouse_x1") == 0) {
        *type = TRIGGER_MOUSE_BUTTON;
        binding->trigger.mouseButton = MOUSE_BUTTON_X1;
        return TRUE;
    }
    if (strcmp(str, "mouse_x2") == 0) {
        *type = TRIGGER_MOUSE_BUTTON;
        binding->trigger.mouseButton = MOUSE_BUTTON_X2;
        return TRUE;
    }

    if (strncmp(str, "key_", 4) == 0) {
        *type = TRIGGER_KEYBOARD;
        binding->trigger.keyCode = (DWORD)strtoul(str + 4, NULL, 0);
        return TRUE;
    }

    return FALSE;
}

static BOOL GetConfigPath(WCHAR *path, DWORD pathLen, BOOL *isAppData) {
    if (GetFileAttributesW(CONFIG_FILENAME) != INVALID_FILE_ATTRIBUTES) {
        wcscpy_s(path, pathLen, CONFIG_FILENAME);
        *isAppData = FALSE;
        return TRUE;
    }

    WCHAR appDataPath[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appDataPath))) {
        return FALSE;
    }

    swprintf_s(path, pathLen, L"%s\\%s\\%s", appDataPath, APP_FOLDER, CONFIG_FILENAME);
    *isAppData = TRUE;

    return TRUE;
}

static BOOL CreateDefaultConfig(const WCHAR *path) {
    WCHAR dir[MAX_PATH];
    wcscpy_s(dir, MAX_PATH, path);

    WCHAR *lastSlash = wcsrchr(dir, L'\\');
    if (lastSlash) {
        *lastSlash = L'\0';
        CreateDirectoryW(dir, NULL);
    }

    FILE *f = _wfopen(path, L"w");
    if (!f)
        return FALSE;

    fputs(DEFAULT_CONFIG, f);
    fclose(f);
    return TRUE;
}

static BOOL LoadConfig(void) {
    WCHAR configPath[MAX_PATH];
    BOOL isAppData;

    if (!GetConfigPath(configPath, MAX_PATH, &isAppData)) {
        return FALSE;
    }

    if (GetFileAttributesW(configPath) == INVALID_FILE_ATTRIBUTES) {
        if (!CreateDefaultConfig(configPath)) {
            return FALSE;
        }
    }

    FILE *f = _wfopen(configPath, L"rb");
    if (!f)
        return FALSE;

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *jsonStr = (char *)malloc(fileSize + 1);
    if (!jsonStr) {
        fclose(f);
        return FALSE;
    }

    fread(jsonStr, 1, fileSize, f);
    jsonStr[fileSize] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(jsonStr);
    free(jsonStr);

    if (!root) {
        return FALSE;
    }

    cJSON *bindingsArray = cJSON_GetObjectItem(root, "bindings");
    if (!cJSON_IsArray(bindingsArray)) {
        cJSON_Delete(root);
        return FALSE;
    }

    bindingCount = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, bindingsArray) {
        if (bindingCount >= MAX_BINDINGS)
            break;

        HotkeyBinding *b = &bindings[bindingCount];

        cJSON *ctrl = cJSON_GetObjectItem(item, "ctrl");
        cJSON *shift = cJSON_GetObjectItem(item, "shift");
        cJSON *alt = cJSON_GetObjectItem(item, "alt");
        cJSON *win = cJSON_GetObjectItem(item, "win");
        cJSON *trigger = cJSON_GetObjectItem(item, "trigger");
        cJSON *action = cJSON_GetObjectItem(item, "action");

        b->ctrl = ParseModifierState(cJSON_GetStringValue(ctrl));
        b->shift = ParseModifierState(cJSON_GetStringValue(shift));
        b->alt = ParseModifierState(cJSON_GetStringValue(alt));
        b->win = ParseModifierState(cJSON_GetStringValue(win));

        if (!ParseTrigger(cJSON_GetStringValue(trigger), &b->triggerType, b)) {
            continue;
        }

        b->action = ParseAction(cJSON_GetStringValue(action));
        bindingCount++;
    }

    cJSON_Delete(root);
    return TRUE;
}

static BOOL CheckSingleModifier(ModifierState required, int vkLeft, int vkRight) {
    BOOL leftDown = (GetAsyncKeyState(vkLeft) & 0x8000) != 0;
    BOOL rightDown = (GetAsyncKeyState(vkRight) & 0x8000) != 0;

    switch (required) {
    case MODIFIER_NONE:
        return !leftDown && !rightDown;
    case MODIFIER_LEFT:
        return leftDown && !rightDown;
    case MODIFIER_RIGHT:
        return !leftDown && rightDown;
    case MODIFIER_EITHER:
        return leftDown || rightDown;
    case MODIFIER_BOTH:
        return leftDown && rightDown;
    }
    return FALSE;
}

static BOOL CheckModifiers(const HotkeyBinding *binding) {
    if (!CheckSingleModifier(binding->ctrl, VK_LCONTROL, VK_RCONTROL))
        return FALSE;
    if (!CheckSingleModifier(binding->shift, VK_LSHIFT, VK_RSHIFT))
        return FALSE;
    if (!CheckSingleModifier(binding->alt, VK_LMENU, VK_RMENU))
        return FALSE;
    if (!CheckSingleModifier(binding->win, VK_LWIN, VK_RWIN))
        return FALSE;
    return TRUE;
}

static void MarkWinKeyForSuppression(void) {
    if ((GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000)) {
        suppressWinKeyUp = TRUE;
    }
}

static void ExecuteAction(MediaAction action) {
    WORD vk = 0;

    switch (action) {
    case ACTION_VOLUME_UP:
        vk = VK_VOLUME_UP;
        break;
    case ACTION_VOLUME_DOWN:
        vk = VK_VOLUME_DOWN;
        break;
    case ACTION_VOLUME_MUTE:
        vk = VK_VOLUME_MUTE;
        break;
    case ACTION_PLAY_PAUSE:
        vk = VK_MEDIA_PLAY_PAUSE;
        break;
    case ACTION_PREV_TRACK:
        vk = VK_MEDIA_PREV_TRACK;
        break;
    case ACTION_NEXT_TRACK:
        vk = VK_MEDIA_NEXT_TRACK;
        break;
    default:
        return;
    }

    keybd_event((BYTE)vk, 0, 0, 0);
    keybd_event((BYTE)vk, 0, KEYEVENTF_KEYUP, 0);
}

static BOOL IsModifierKey(DWORD vkCode) {
    return vkCode == VK_LCONTROL || vkCode == VK_RCONTROL || vkCode == VK_LSHIFT ||
           vkCode == VK_RSHIFT || vkCode == VK_LMENU || vkCode == VK_RMENU || vkCode == VK_LWIN ||
           vkCode == VK_RWIN;
}

static BOOL ProcessKeyboardTrigger(DWORD vkCode) {
    for (int i = 0; i < bindingCount; i++) {
        HotkeyBinding *b = &bindings[i];

        if (b->triggerType != TRIGGER_KEYBOARD)
            continue;
        if (b->trigger.keyCode != vkCode)
            continue;
        if (!CheckModifiers(b))
            continue;

        MarkWinKeyForSuppression();
        ExecuteAction(b->action);
        return TRUE;
    }
    return FALSE;
}

static BOOL ProcessMouseButtonTrigger(MouseButton button) {
    for (int i = 0; i < bindingCount; i++) {
        HotkeyBinding *b = &bindings[i];

        if (b->triggerType != TRIGGER_MOUSE_BUTTON)
            continue;
        if (b->trigger.mouseButton != button)
            continue;
        if (!CheckModifiers(b))
            continue;

        MarkWinKeyForSuppression();
        ExecuteAction(b->action);
        return TRUE;
    }
    return FALSE;
}

static BOOL ProcessMouseWheelTrigger(WheelDirection dir) {
    for (int i = 0; i < bindingCount; i++) {
        HotkeyBinding *b = &bindings[i];

        if (b->triggerType != TRIGGER_MOUSE_WHEEL)
            continue;
        if (b->trigger.wheelDir != dir)
            continue;
        if (!CheckModifiers(b))
            continue;

        MarkWinKeyForSuppression();
        ExecuteAction(b->action);
        return TRUE;
    }
    return FALSE;
}

static LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        KBDLLHOOKSTRUCT *kb = (KBDLLHOOKSTRUCT *)lParam;

        if ((kb->vkCode == VK_LWIN || kb->vkCode == VK_RWIN) &&
            (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) && suppressWinKeyUp) {

            suppressWinKeyUp = FALSE;

            INPUT inputs[2] = {0};
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.wVk = VK_CONTROL;
            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki.wVk = VK_CONTROL;
            inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, inputs, sizeof(INPUT));
        }

        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            if (!IsModifierKey(kb->vkCode)) {
                if (ProcessKeyboardTrigger(kb->vkCode)) {
                    return 1;
                }
            }
        }
    }
    return CallNextHookEx(keyboardHook, nCode, wParam, lParam);
}

static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        MSLLHOOKSTRUCT *ms = (MSLLHOOKSTRUCT *)lParam;

        switch (wParam) {
        case WM_LBUTTONDOWN:
            if (ProcessMouseButtonTrigger(MOUSE_BUTTON_LEFT))
                return 1;
            break;

        case WM_RBUTTONDOWN:
            if (ProcessMouseButtonTrigger(MOUSE_BUTTON_RIGHT))
                return 1;
            break;

        case WM_MBUTTONDOWN:
            if (ProcessMouseButtonTrigger(MOUSE_BUTTON_MIDDLE))
                return 1;
            break;

        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP: {
            WORD xButton = HIWORD(ms->mouseData);
            MouseButton btn = (xButton == XBUTTON1) ? MOUSE_BUTTON_X1 : MOUSE_BUTTON_X2;
            BOOL hasBinding = FALSE;
            for (int i = 0; i < bindingCount; i++) {
                if (bindings[i].triggerType == TRIGGER_MOUSE_BUTTON &&
                    bindings[i].trigger.mouseButton == btn && CheckModifiers(&bindings[i])) {
                    hasBinding = TRUE;
                    break;
                }
            }
            if (hasBinding) {
                if (wParam == WM_XBUTTONDOWN)
                    ProcessMouseButtonTrigger(btn);
                return 1;
            }
        } break;

        case WM_MOUSEWHEEL: {
            short delta = (short)HIWORD(ms->mouseData);
            WheelDirection dir = (delta > 0) ? WHEEL_UP : WHEEL_DOWN;
            if (ProcessMouseWheelTrigger(dir))
                return 1;
        } break;
        }
    }
    return CallNextHookEx(mouseHook, nCode, wParam, lParam);
}

static BOOL InstallHooks(void) {
    keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHookProc, NULL, 0);
    if (!keyboardHook)
        return FALSE;

    mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc, NULL, 0);
    if (!mouseHook) {
        UnhookWindowsHookEx(keyboardHook);
        keyboardHook = NULL;
        return FALSE;
    }

    return TRUE;
}

static void RemoveHooks(void) {
    if (mouseHook) {
        UnhookWindowsHookEx(mouseHook);
        mouseHook = NULL;
    }
    if (keyboardHook) {
        UnhookWindowsHookEx(keyboardHook);
        keyboardHook = NULL;
    }
}

static HICON LoadIconFromMemory(const unsigned char *data, unsigned int size) {
    if (size < 22)
        return NULL;

    int offset = data[18] | (data[19] << 8) | (data[20] << 16) | (data[21] << 24);
    int imageSize = size - offset;

    return CreateIconFromResourceEx(
        (PBYTE)(data + offset), imageSize, TRUE, 0x00030000, 32, 32, LR_DEFAULTCOLOR);
}

static BOOL GetStartupShortcutPath(WCHAR *path, DWORD pathLen) {
    WCHAR startupPath[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_STARTUP, NULL, 0, startupPath)))
        return FALSE;

    swprintf_s(path, pathLen, L"%s\\%s.lnk", startupPath, APP_NAME);
    return TRUE;
}

static BOOL IsStartupEnabled(void) {
    WCHAR shortcutPath[MAX_PATH];
    if (!GetStartupShortcutPath(shortcutPath, MAX_PATH))
        return FALSE;

    return GetFileAttributesW(shortcutPath) != INVALID_FILE_ATTRIBUTES;
}

static BOOL EnableStartup(void) {
    WCHAR shortcutPath[MAX_PATH];
    if (!GetStartupShortcutPath(shortcutPath, MAX_PATH))
        return FALSE;

    WCHAR exePath[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exePath, MAX_PATH))
        return FALSE;

    CoInitialize(NULL);

    IShellLinkW *shellLink = NULL;
    HRESULT hr = CoCreateInstance(
        &CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&shellLink);

    if (FAILED(hr)) {
        CoUninitialize();
        return FALSE;
    }

    shellLink->lpVtbl->SetPath(shellLink, exePath);
    shellLink->lpVtbl->SetDescription(shellLink, APP_NAME);

    IPersistFile *persistFile = NULL;
    hr = shellLink->lpVtbl->QueryInterface(shellLink, &IID_IPersistFile, (void **)&persistFile);

    if (SUCCEEDED(hr)) {
        hr = persistFile->lpVtbl->Save(persistFile, shortcutPath, TRUE);
        persistFile->lpVtbl->Release(persistFile);
    }

    shellLink->lpVtbl->Release(shellLink);
    CoUninitialize();

    return SUCCEEDED(hr);
}

static BOOL DisableStartup(void) {
    WCHAR shortcutPath[MAX_PATH];
    if (!GetStartupShortcutPath(shortcutPath, MAX_PATH))
        return FALSE;

    return DeleteFileW(shortcutPath);
}

static BOOL RegisterWindowClass(HINSTANCE hInstance) {
    WNDCLASSEXW wc = {0};

    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MediaKeysClass";
    wc.hIcon = appIcon;
    wc.hIconSm = appIcon;

    return RegisterClassExW(&wc) != 0;
}

static HWND CreateMessageWindow(HINSTANCE hInstance) {
    return CreateWindowExW(
        0, L"MediaKeysClass", APP_NAME, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);
}

static BOOL InitTrayIcon(HWND hwnd) {
    notifyIconData.cbSize = sizeof(NOTIFYICONDATAW);
    notifyIconData.hWnd = hwnd;
    notifyIconData.uID = ID_TRAY_ICON;
    notifyIconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    notifyIconData.uCallbackMessage = WM_TRAYICON;
    notifyIconData.hIcon = appIcon;
    wcscpy_s(notifyIconData.szTip, sizeof(notifyIconData.szTip) / sizeof(WCHAR), APP_NAME);

    return Shell_NotifyIconW(NIM_ADD, &notifyIconData);
}

static void RemoveTrayIcon(void) {
    Shell_NotifyIconW(NIM_DELETE, &notifyIconData);
}

static void ShowTrayMenu(HWND hwnd) {
    POINT pt;

    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);

    TrackPopupMenu(
        trayMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);

    PostMessageW(hwnd, WM_NULL, 0, 0);
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAYICON:
        switch (LOWORD(lParam)) {
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowTrayMenu(hwnd);
            return 0;
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_STARTUP:
            if (IsStartupEnabled()) {
                DisableStartup();
                CheckMenuItem(trayMenu, ID_TRAY_STARTUP, MF_UNCHECKED);
            } else {
                EnableStartup();
                CheckMenuItem(trayMenu, ID_TRAY_STARTUP, MF_CHECKED);
            }
            return 0;
        case ID_TRAY_EXIT:
            PostQuitMessage(0);
            return 0;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
