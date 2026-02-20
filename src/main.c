#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdarg.h>
#include "cJSON.h"
#include "icon_data.h"
#include "version.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define CONFIG_FILENAME L"config.json"
#define APP_FOLDER L"MediaKeys"

#define APP_NAME L"MediaKeys"
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_ICON 1
#define ID_TRAY_EXIT 1001
#define ID_TRAY_STARTUP 1002
#define ID_TRAY_VIEWLOG 1003
#define ID_TRAY_EDITCONFIG 1004
#define MAX_BINDINGS 64
#define ID_TIMER_CONFIG_RELOAD 1
#define CONFIG_RELOAD_DELAY_MS 200

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
    ACTION_NONE,
    ACTION_VOLUME_UP,
    ACTION_VOLUME_DOWN,
    ACTION_VOLUME_MUTE,
    ACTION_PLAY_PAUSE,
    ACTION_PREV_TRACK,
    ACTION_NEXT_TRACK,
    ACTION_SCREENSHOT_CLIENT_CLIPBOARD,
    ACTION_SCREENSHOT_CLIENT_FILE,
    ACTION_SCREENSHOT_CLIENT_FILE_CLIPBOARD
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
static WCHAR logFilePath[MAX_PATH] = {0};
static WCHAR configFilePath[MAX_PATH] = {0};
static WCHAR dataDir[MAX_PATH] = {0};
static UINT WM_TASKBARCREATED = 0;

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
static BOOL InitDataDir(void);
static BOOL LoadConfig(void);
static BOOL GetConfigPath(WCHAR *path, DWORD pathLen);
static BOOL CreateDefaultConfig(const WCHAR *path);
static ModifierState ParseModifierState(const char *str);
static BOOL ParseTrigger(const char *str, TriggerType *type, HotkeyBinding *binding);
static MediaAction ParseAction(const char *str);
static HICON LoadIconFromMemory(const unsigned char *data, unsigned int size);
static BOOL GetStartupShortcutPath(WCHAR *path, DWORD pathLen);
static BOOL IsStartupEnabled(void);
static BOOL EnableStartup(void);
static BOOL DisableStartup(void);
static BOOL InitLogFile(void);
static void LogMessage(const char *format, ...);
static void ViewLogFile(void);
static void EditConfigFile(void);
static BOOL IsFirstRun(void);
static void MarkFirstRunComplete(void);
static HBITMAP CaptureClientArea(int *outWidth, int *outHeight);
static void CaptureClientAreaToClipboard(void);
static BOOL CaptureClientAreaToFile(WCHAR *outPath, DWORD outPathLen);
static void CaptureClientAreaToFileClipboard(void);
static void CopyFileToClipboard(const WCHAR *filePath);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    MSG msg;

    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    InitDataDir();
    InitLogFile();
    LogMessage("MediaKeys %s started", VERSION);

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

    WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");

    if (!InitTrayIcon(mainWindow)) {
        MessageBoxW(NULL, L"Failed to create tray icon", APP_NAME, MB_ICONERROR);
        DestroyWindow(mainWindow);
        return 1;
    }

    trayMenu = CreatePopupMenu();
    if (trayMenu) {
        UINT startupFlags = MF_STRING | (IsStartupEnabled() ? MF_CHECKED : MF_UNCHECKED);
        AppendMenuW(trayMenu, startupFlags, ID_TRAY_STARTUP, L"Run at startup");
        AppendMenuW(trayMenu, MF_STRING, ID_TRAY_EDITCONFIG, L"Edit Config");
        AppendMenuW(trayMenu, MF_STRING, ID_TRAY_VIEWLOG, L"View Log");
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

    HANDLE configWatch = FindFirstChangeNotificationW(dataDir, FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE);
    if (configWatch == INVALID_HANDLE_VALUE) {
        LogMessage("Warning: could not watch config directory for changes");
    }

    if (IsFirstRun()) {
        MessageBoxW(NULL,
            L"MediaKeys is now running in the background.\n\n"
            L"Right-click the system tray icon to access options.\n\n"
            L"(This message will only appear once.)",
            APP_NAME, MB_OK | MB_ICONINFORMATION);
        MarkFirstRunComplete();
    }

    BOOL running = TRUE;
    while (running) {
        DWORD waitCount = (configWatch != INVALID_HANDLE_VALUE) ? 1 : 0;
        HANDLE *waitHandles = &configWatch;
        DWORD result = MsgWaitForMultipleObjects(waitCount, waitHandles, FALSE, INFINITE, QS_ALLINPUT);

        if (result == WAIT_OBJECT_0 && configWatch != INVALID_HANDLE_VALUE) {
            SetTimer(mainWindow, ID_TIMER_CONFIG_RELOAD, CONFIG_RELOAD_DELAY_MS, NULL);
            FindNextChangeNotification(configWatch);
        } else {
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    running = FALSE;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }

    if (configWatch != INVALID_HANDLE_VALUE) {
        FindCloseChangeNotification(configWatch);
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
    "    { \"win\": \"left\", \"trigger\": \"mouse_middle\", \"action\": \"play_pause\" },\n"
    "    { \"win\": \"left\", \"shift\": \"left\", \"trigger\": \"key_printscreen\", \"action\": \"screenshot_client_clipboard\" }\n"
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
    LogMessage("Warning: unrecognized modifier '%s', using 'none'", str);
    return MODIFIER_NONE;
}

static MediaAction ParseAction(const char *str) {
    if (!str) {
        LogMessage("Warning: missing action");
        return ACTION_NONE;
    }
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
    if (strcmp(str, "screenshot_client_clipboard") == 0)
        return ACTION_SCREENSHOT_CLIENT_CLIPBOARD;
    if (strcmp(str, "screenshot_client_file") == 0)
        return ACTION_SCREENSHOT_CLIENT_FILE;
    if (strcmp(str, "screenshot_client_file_clipboard") == 0)
        return ACTION_SCREENSHOT_CLIENT_FILE_CLIPBOARD;
    LogMessage("Warning: unrecognized action '%s'", str);
    return ACTION_NONE;
}

static BOOL ParseTrigger(const char *str, TriggerType *type, HotkeyBinding *binding) {
    if (!str) {
        LogMessage("Warning: missing trigger");
        return FALSE;
    }

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
        static const struct { const char *name; DWORD vk; } namedKeys[] = {
            /* Letters */
            {"a", 0x41}, {"b", 0x42}, {"c", 0x43}, {"d", 0x44},
            {"e", 0x45}, {"f", 0x46}, {"g", 0x47}, {"h", 0x48},
            {"i", 0x49}, {"j", 0x4A}, {"k", 0x4B}, {"l", 0x4C},
            {"m", 0x4D}, {"n", 0x4E}, {"o", 0x4F}, {"p", 0x50},
            {"q", 0x51}, {"r", 0x52}, {"s", 0x53}, {"t", 0x54},
            {"u", 0x55}, {"v", 0x56}, {"w", 0x57}, {"x", 0x58},
            {"y", 0x59}, {"z", 0x5A},
            /* Digits */
            {"0", 0x30}, {"1", 0x31}, {"2", 0x32}, {"3", 0x33},
            {"4", 0x34}, {"5", 0x35}, {"6", 0x36}, {"7", 0x37},
            {"8", 0x38}, {"9", 0x39},
            /* Function keys */
            {"f1", VK_F1}, {"f2", VK_F2}, {"f3", VK_F3}, {"f4", VK_F4},
            {"f5", VK_F5}, {"f6", VK_F6}, {"f7", VK_F7}, {"f8", VK_F8},
            {"f9", VK_F9}, {"f10", VK_F10}, {"f11", VK_F11}, {"f12", VK_F12},
            /* Common keys */
            {"space", VK_SPACE}, {"enter", VK_RETURN}, {"tab", VK_TAB},
            {"escape", VK_ESCAPE}, {"backspace", VK_BACK}, {"delete", VK_DELETE},
            {"insert", VK_INSERT}, {"home", VK_HOME}, {"end", VK_END},
            {"pageup", VK_PRIOR}, {"pagedown", VK_NEXT},
            {"up", VK_UP}, {"down", VK_DOWN}, {"left", VK_LEFT}, {"right", VK_RIGHT},
            {"printscreen", VK_SNAPSHOT}, {"scrolllock", VK_SCROLL}, {"pause", VK_PAUSE},
            {"numlock", VK_NUMLOCK}, {"capslock", VK_CAPITAL},
            /* Numpad */
            {"num0", VK_NUMPAD0}, {"num1", VK_NUMPAD1}, {"num2", VK_NUMPAD2},
            {"num3", VK_NUMPAD3}, {"num4", VK_NUMPAD4}, {"num5", VK_NUMPAD5},
            {"num6", VK_NUMPAD6}, {"num7", VK_NUMPAD7}, {"num8", VK_NUMPAD8},
            {"num9", VK_NUMPAD9}, {"nummultiply", VK_MULTIPLY}, {"numadd", VK_ADD},
            {"numsubtract", VK_SUBTRACT}, {"numdecimal", VK_DECIMAL}, {"numdivide", VK_DIVIDE},
            /* Punctuation */
            {"semicolon", VK_OEM_1}, {"equals", VK_OEM_PLUS}, {"comma", VK_OEM_COMMA},
            {"minus", VK_OEM_MINUS}, {"period", VK_OEM_PERIOD}, {"slash", VK_OEM_2},
            {"backtick", VK_OEM_3}, {"lbracket", VK_OEM_4}, {"backslash", VK_OEM_5},
            {"rbracket", VK_OEM_6}, {"quote", VK_OEM_7},
        };

        const char *keyName = str + 4;

        for (int i = 0; i < (int)(sizeof(namedKeys) / sizeof(namedKeys[0])); i++) {
            if (strcmp(keyName, namedKeys[i].name) == 0) {
                *type = TRIGGER_KEYBOARD;
                binding->trigger.keyCode = namedKeys[i].vk;
                return TRUE;
            }
        }

        char *endptr;
        unsigned long code = strtoul(keyName, &endptr, 0);
        if (endptr == keyName || *endptr != '\0') {
            LogMessage("Warning: invalid key code '%s'", str);
            return FALSE;
        }
        *type = TRIGGER_KEYBOARD;
        binding->trigger.keyCode = (DWORD)code;
        return TRUE;
    }

    LogMessage("Warning: unrecognized trigger '%s'", str);
    return FALSE;
}

static BOOL InitDataDir(void) {
    WCHAR exePath[MAX_PATH];
    if (GetModuleFileNameW(NULL, exePath, MAX_PATH) > 0) {
        WCHAR *lastSlash = wcsrchr(exePath, L'\\');
        if (lastSlash) {
            *lastSlash = L'\0';
            WCHAR localConfig[MAX_PATH];
            swprintf_s(localConfig, MAX_PATH, L"%s\\%s", exePath, CONFIG_FILENAME);
            if (GetFileAttributesW(localConfig) != INVALID_FILE_ATTRIBUTES) {
                wcscpy_s(dataDir, MAX_PATH, exePath);
                return TRUE;
            }
        }
    }

    WCHAR appDataPath[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appDataPath))) {
        return FALSE;
    }

    swprintf_s(dataDir, MAX_PATH, L"%s\\%s", appDataPath, APP_FOLDER);
    CreateDirectoryW(dataDir, NULL);
    return TRUE;
}

static BOOL GetConfigPath(WCHAR *path, DWORD pathLen) {
    swprintf_s(path, pathLen, L"%s\\%s", dataDir, CONFIG_FILENAME);
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

    if (!GetConfigPath(configPath, MAX_PATH)) {
        return FALSE;
    }
    wcscpy_s(configFilePath, MAX_PATH, configPath);

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
    if (fileSize < 0) {
        fclose(f);
        return FALSE;
    }
    fseek(f, 0, SEEK_SET);

    char *jsonStr = (char *)malloc(fileSize + 1);
    if (!jsonStr) {
        fclose(f);
        return FALSE;
    }

    size_t bytesRead = fread(jsonStr, 1, fileSize, f);
    fclose(f);
    if (bytesRead != (size_t)fileSize) {
        free(jsonStr);
        return FALSE;
    }
    jsonStr[fileSize] = '\0';

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
    case ACTION_NONE:
        return;
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
    case ACTION_SCREENSHOT_CLIENT_CLIPBOARD:
        CaptureClientAreaToClipboard();
        return;
    case ACTION_SCREENSHOT_CLIENT_FILE:
        CaptureClientAreaToFile(NULL, 0);
        return;
    case ACTION_SCREENSHOT_CLIENT_FILE_CLIPBOARD:
        CaptureClientAreaToFileClipboard();
        return;
    default:
        return;
    }

    INPUT inputs[2] = {0};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vk;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vk;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

static HBITMAP CaptureClientArea(int *outWidth, int *outHeight) {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        LogMessage("Screenshot: no foreground window");
        return NULL;
    }

    RECT windowRect;
    if (!GetWindowRect(hwnd, &windowRect)) {
        LogMessage("Screenshot: GetWindowRect failed");
        return NULL;
    }

    int winWidth = windowRect.right - windowRect.left;
    int winHeight = windowRect.bottom - windowRect.top;
    if (winWidth <= 0 || winHeight <= 0) {
        LogMessage("Screenshot: invalid window rect (%dx%d)", winWidth, winHeight);
        return NULL;
    }

    POINT clientOrigin = {0, 0};
    ClientToScreen(hwnd, &clientOrigin);
    int clientOffsetX = clientOrigin.x - windowRect.left;
    int clientOffsetY = clientOrigin.y - windowRect.top;

    RECT clientRect;
    if (!GetClientRect(hwnd, &clientRect)) {
        LogMessage("Screenshot: GetClientRect failed");
        return NULL;
    }

    int clientWidth = clientRect.right - clientRect.left;
    int clientHeight = clientRect.bottom - clientRect.top;
    if (clientWidth <= 0 || clientHeight <= 0) {
        LogMessage("Screenshot: invalid client rect (%dx%d)", clientWidth, clientHeight);
        return NULL;
    }

    HDC screenDC = GetDC(NULL);
    if (!screenDC) {
        LogMessage("Screenshot: GetDC failed");
        return NULL;
    }

    HDC fullDC = CreateCompatibleDC(screenDC);
    HDC clientDC = CreateCompatibleDC(screenDC);
    HBITMAP fullBitmap = CreateCompatibleBitmap(screenDC, winWidth, winHeight);
    HBITMAP clientBitmap = CreateCompatibleBitmap(screenDC, clientWidth, clientHeight);
    ReleaseDC(NULL, screenDC);

    if (!fullDC || !clientDC || !fullBitmap || !clientBitmap) {
        LogMessage("Screenshot: failed to allocate GDI resources");
        if (clientBitmap) DeleteObject(clientBitmap);
        if (fullBitmap) DeleteObject(fullBitmap);
        if (clientDC) DeleteDC(clientDC);
        if (fullDC) DeleteDC(fullDC);
        return NULL;
    }

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

    HBITMAP oldFull = (HBITMAP)SelectObject(fullDC, fullBitmap);
    if (!PrintWindow(hwnd, fullDC, PW_RENDERFULLCONTENT)) {
        LogMessage("Screenshot: PrintWindow failed");
        SelectObject(fullDC, oldFull);
        DeleteObject(clientBitmap);
        DeleteObject(fullBitmap);
        DeleteDC(clientDC);
        DeleteDC(fullDC);
        return NULL;
    }

    HBITMAP oldClient = (HBITMAP)SelectObject(clientDC, clientBitmap);
    BitBlt(clientDC, 0, 0, clientWidth, clientHeight, fullDC, clientOffsetX, clientOffsetY, SRCCOPY);

    SelectObject(clientDC, oldClient);
    SelectObject(fullDC, oldFull);
    DeleteObject(fullBitmap);
    DeleteDC(clientDC);
    DeleteDC(fullDC);

    *outWidth = clientWidth;
    *outHeight = clientHeight;
    return clientBitmap;
}

static void CaptureClientAreaToClipboard(void) {
    int width, height;
    HBITMAP bitmap = CaptureClientArea(&width, &height);
    if (!bitmap) return;

    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        SetClipboardData(CF_BITMAP, bitmap);
        CloseClipboard();
        LogMessage("Screenshot: copied to clipboard (%dx%d)", width, height);
    } else {
        DeleteObject(bitmap);
        LogMessage("Screenshot: OpenClipboard failed");
    }
}

static BOOL CaptureClientAreaToFile(WCHAR *outPath, DWORD outPathLen) {
    int width, height;
    HBITMAP bitmap = CaptureClientArea(&width, &height);
    if (!bitmap) return FALSE;

    BITMAPINFOHEADER bi = {0};
    bi.biSize = sizeof(bi);
    bi.biWidth = width;
    bi.biHeight = -height;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    unsigned char *pixels = (unsigned char *)malloc(width * height * 4);
    if (!pixels) {
        DeleteObject(bitmap);
        LogMessage("Screenshot: malloc failed");
        return FALSE;
    }

    HDC dc = GetDC(NULL);
    GetDIBits(dc, bitmap, 0, height, pixels, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    ReleaseDC(NULL, dc);
    DeleteObject(bitmap);

    for (int i = 0; i < width * height; i++) {
        unsigned char tmp = pixels[i * 4];
        pixels[i * 4] = pixels[i * 4 + 2];
        pixels[i * 4 + 2] = tmp;
    }

    WCHAR picturesPath[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_MYPICTURES, NULL, 0, picturesPath))) {
        free(pixels);
        LogMessage("Screenshot: failed to get Pictures path");
        return FALSE;
    }

    WCHAR screenshotsDir[MAX_PATH];
    swprintf_s(screenshotsDir, MAX_PATH, L"%s\\Screenshots", picturesPath);
    CreateDirectoryW(screenshotsDir, NULL);

    SYSTEMTIME st;
    GetLocalTime(&st);

    WCHAR filePath[MAX_PATH];
    swprintf_s(filePath, MAX_PATH,
               L"%s\\Screenshot_%04d%02d%02d_%02d%02d%02d.png",
               screenshotsDir, st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond);

    char filePathA[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, filePath, -1, filePathA, MAX_PATH, NULL, NULL);

    BOOL success = FALSE;
    if (stbi_write_png(filePathA, width, height, 4, pixels, width * 4)) {
        LogMessage("Screenshot: saved to %s", filePathA);
        if (outPath)
            wcscpy_s(outPath, outPathLen, filePath);
        success = TRUE;
    } else {
        LogMessage("Screenshot: failed to write PNG");
    }

    free(pixels);
    return success;
}

static void CopyFileToClipboard(const WCHAR *filePath) {
    DWORD len = (DWORD)(wcslen(filePath) + 1);
    DWORD dropFilesSize = sizeof(DROPFILES) + (len + 1) * sizeof(WCHAR);

    HGLOBAL hGlobal = GlobalAlloc(GHND, dropFilesSize);
    if (!hGlobal) {
        LogMessage("Screenshot: GlobalAlloc failed");
        return;
    }

    DROPFILES *df = (DROPFILES *)GlobalLock(hGlobal);
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;
    WCHAR *fileList = (WCHAR *)((char *)df + sizeof(DROPFILES));
    wcscpy_s(fileList, len, filePath);
    fileList[len] = L'\0';
    GlobalUnlock(hGlobal);

    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        SetClipboardData(CF_HDROP, hGlobal);
        CloseClipboard();
        LogMessage("Screenshot: file copied to clipboard");
    } else {
        GlobalFree(hGlobal);
        LogMessage("Screenshot: OpenClipboard failed");
    }
}

static void CaptureClientAreaToFileClipboard(void) {
    WCHAR filePath[MAX_PATH];
    if (CaptureClientAreaToFile(filePath, MAX_PATH)) {
        CopyFileToClipboard(filePath);
    }
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
            for (int i = 0; i < bindingCount; i++) {
                HotkeyBinding *b = &bindings[i];
                if (b->triggerType == TRIGGER_MOUSE_BUTTON &&
                    b->trigger.mouseButton == btn && CheckModifiers(b)) {
                    if (wParam == WM_XBUTTONDOWN) {
                        MarkWinKeyForSuppression();
                        ExecuteAction(b->action);
                    }
                    return 1;
                }
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
    if (offset < 0 || (unsigned int)offset >= size)
        return NULL;
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
    DWORD exePathLen = GetModuleFileNameW(NULL, exePath, MAX_PATH);
    if (exePathLen == 0 || exePathLen >= MAX_PATH)
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

static BOOL InitLogFile(void) {
    if (dataDir[0] == L'\0')
        return FALSE;

    swprintf_s(logFilePath, MAX_PATH, L"%s\\log.txt", dataDir);
    return TRUE;
}

static void LogMessage(const char *format, ...) {
    if (logFilePath[0] == L'\0')
        return;

    FILE *f = _wfopen(logFilePath, L"a");
    if (!f)
        return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] ",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    va_list args;
    va_start(args, format);
    vfprintf(f, format, args);
    va_end(args);

    fprintf(f, "\n");
    fclose(f);
}

static void ViewLogFile(void) {
    if (logFilePath[0] == L'\0')
        return;

    ShellExecuteW(NULL, L"open", logFilePath, NULL, NULL, SW_SHOWNORMAL);
}

static void EditConfigFile(void) {
    if (configFilePath[0] == L'\0')
        return;

    ShellExecuteW(NULL, L"open", configFilePath, NULL, NULL, SW_SHOWNORMAL);
}

static BOOL IsFirstRun(void) {
    if (dataDir[0] == L'\0')
        return FALSE;

    WCHAR markerPath[MAX_PATH];
    swprintf_s(markerPath, MAX_PATH, L"%s\\.firstrun", dataDir);

    return GetFileAttributesW(markerPath) == INVALID_FILE_ATTRIBUTES;
}

static void MarkFirstRunComplete(void) {
    if (dataDir[0] == L'\0')
        return;

    WCHAR markerPath[MAX_PATH];
    swprintf_s(markerPath, MAX_PATH, L"%s\\.firstrun", dataDir);

    HANDLE hFile = CreateFileW(markerPath, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                               FILE_ATTRIBUTE_HIDDEN, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);
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
        WS_EX_TOOLWINDOW, L"MediaKeysClass", APP_NAME, 0, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
}

static BOOL InitTrayIcon(HWND hwnd) {
    notifyIconData.cbSize = sizeof(NOTIFYICONDATAW);
    notifyIconData.hWnd = hwnd;
    notifyIconData.uID = ID_TRAY_ICON;
    notifyIconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    notifyIconData.uCallbackMessage = WM_TRAYICON;
    notifyIconData.hIcon = appIcon;
    swprintf_s(notifyIconData.szTip, sizeof(notifyIconData.szTip) / sizeof(WCHAR),
               L"%s v%S", APP_NAME, VERSION);

    return Shell_NotifyIconW(NIM_ADD, &notifyIconData);
}

static void RemoveTrayIcon(void) {
    Shell_NotifyIconW(NIM_DELETE, &notifyIconData);
}

static void ShowTrayMenu(HWND hwnd) {
    if (!trayMenu)
        return;

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
        case ID_TRAY_EDITCONFIG:
            EditConfigFile();
            return 0;
        case ID_TRAY_VIEWLOG:
            ViewLogFile();
            return 0;
        case ID_TRAY_EXIT:
            PostQuitMessage(0);
            return 0;
        }
        break;

    case WM_TIMER:
        if (wParam == ID_TIMER_CONFIG_RELOAD) {
            KillTimer(hwnd, ID_TIMER_CONFIG_RELOAD);
            if (LoadConfig()) {
                LogMessage("Config reloaded (%d bindings)", bindingCount);
            } else {
                LogMessage("Warning: config reload failed, keeping previous bindings");
            }
            return 0;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    if (msg == WM_TASKBARCREATED && WM_TASKBARCREATED != 0) {
        Shell_NotifyIconW(NIM_ADD, &notifyIconData);
        LogMessage("Explorer restarted, re-added tray icon");
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
