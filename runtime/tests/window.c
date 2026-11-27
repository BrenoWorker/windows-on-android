#include <windows.h>
#include <stdio.h>

static wchar_t output[MAX_PATH];

static void note(const char* text) {
    FILE* f = _wfopen(output, L"a");
    if (!f) return;
    fputs(text, f);
    fclose(f);
}

static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wide, LPARAM extra) {
    if (message == WM_CHAR) {
        char line[32];
        snprintf(line, sizeof line, "U+%04X\n", (unsigned)wide);
        note(line);
        return 0;
    }
    if (message == WM_LBUTTONDOWN) {
        SetFocus(window);
        note("click\n");
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wide, extra);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show) {
    WNDCLASSW type = {0};
    MSG message;
    HWND window;
    lstrcpynW(output, command, MAX_PATH);
    type.lpfnWndProc = proc;
    type.hInstance = instance;
    type.hbrBackground = CreateSolidBrush(RGB(255, 0, 0));
    type.lpszClassName = L"windroid-test";
    RegisterClassW(&type);
    window = CreateWindowW(type.lpszClassName, L"WinDroid test", WS_POPUP | WS_VISIBLE, 0, 0, GetSystemMetrics(SM_CXSCREEN),
                           GetSystemMetrics(SM_CYSCREEN), NULL, NULL, instance, NULL);
    if (!window) return 1;
    UpdateWindow(window);
    SetForegroundWindow(window);
    SetFocus(window);
    note("shown\n");
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
