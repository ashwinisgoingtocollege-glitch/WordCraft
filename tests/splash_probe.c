#include "editor.h"
#include "splash.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define POLL_MS 20
#define READY_POLLS 750
#define HANDOFF_POLLS 500
#define MESSAGE_TIMEOUT_MS 5000
#define ENV_CAPACITY 32768

typedef struct ProcessWindows {
    DWORD processId;
    HWND splash;
    HWND mainWindow;
} ProcessWindows;

typedef struct ControlSearch {
    int id;
    HWND window;
} ControlSearch;

typedef struct SplashPixels {
    int width;
    int height;
    DWORD *values;
} SplashPixels;

typedef struct MinimalUiTelemetry {
    LRESULT styleFlags;
    LRESULT panelRadius;
    LRESULT controlRadius;
    LRESULT tabRadius;
    LRESULT panelPaintCount;
    LRESULT controlPaintCount;
    LRESULT tabPaintCount;
    LRESULT logoCurveCount;
    LRESULT logoCurvePaintCount;
    LRESULT panelCurveCount;
} MinimalUiTelemetry;

static BOOL CALLBACK enum_process_windows(HWND window, LPARAM data)
{
    ProcessWindows *found = (ProcessWindows *)data;
    DWORD processId = 0;
    WCHAR className[96];

    GetWindowThreadProcessId(window, &processId);
    if (processId != found->processId ||
        GetClassNameW(window, className, ARRAYSIZE(className)) <= 0) {
        return TRUE;
    }
    if (lstrcmpW(className, APP_SPLASH_CLASS_NAME) == 0) {
        found->splash = window;
    } else if (lstrcmpW(className, APP_CLASS_NAME) == 0) {
        found->mainWindow = window;
    }
    return TRUE;
}

static void find_windows(DWORD processId, ProcessWindows *found)
{
    ZeroMemory(found, sizeof(*found));
    found->processId = processId;
    EnumWindows(enum_process_windows, (LPARAM)found);
}

static BOOL CALLBACK enum_control(HWND window, LPARAM data)
{
    ControlSearch *search = (ControlSearch *)data;

    if (GetDlgCtrlID(window) == search->id) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

static HWND find_control(HWND parent, int id)
{
    ControlSearch search = {id, NULL};

    EnumChildWindows(parent, enum_control, (LPARAM)&search);
    return search.window;
}

static BOOL send_bounded(HWND window, UINT message, WPARAM wParam,
                         LPARAM lParam, LRESULT *result)
{
    DWORD_PTR value = 0;

    if (window == NULL ||
        SendMessageTimeoutW(window, message, wParam, lParam,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK,
                            MESSAGE_TIMEOUT_MS, &value) == 0) {
        return FALSE;
    }
    if (result != NULL) {
        *result = (LRESULT)value;
    }
    return TRUE;
}

static BOOL process_running(const PROCESS_INFORMATION *process)
{
    return process->hProcess != NULL &&
           WaitForSingleObject(process->hProcess, 0) == WAIT_TIMEOUT;
}

static BOOL wait_for_ready(const PROCESS_INFORMATION *process,
                           ProcessWindows *found, WCHAR *title,
                           size_t titleCapacity)
{
    int attempt;

    for (attempt = 0; attempt < READY_POLLS; ++attempt) {
        if (!process_running(process)) {
            return FALSE;
        }
        find_windows(process->dwProcessId, found);
        if (found->splash != NULL && found->mainWindow != NULL &&
            IsWindowVisible(found->splash) &&
            !IsWindowVisible(found->mainWindow) &&
            GetWindowTextW(found->splash, title, (int)titleCapacity) > 0 &&
            wcsstr(title, L"Ready") != NULL) {
            return TRUE;
        }
        Sleep(POLL_MS);
    }
    return FALSE;
}

static BOOL wait_for_handoff(const PROCESS_INFORMATION *process,
                             HWND originalSplash, HWND *mainWindow)
{
    int attempt;

    for (attempt = 0; attempt < HANDOFF_POLLS; ++attempt) {
        ProcessWindows found;

        if (!process_running(process)) {
            return FALSE;
        }
        find_windows(process->dwProcessId, &found);
        if (!IsWindow(originalSplash) && found.splash == NULL &&
            found.mainWindow != NULL && IsWindowVisible(found.mainWindow)) {
            *mainWindow = found.mainWindow;
            return TRUE;
        }
        Sleep(POLL_MS);
    }
    return FALSE;
}

static BOOL valid_splash(HWND splash)
{
    RECT rect;
    MONITORINFO monitorInfo;
    LONG_PTR style;
    LONG_PTR extendedStyle;
    LONGLONG width;
    LONGLONG height;
    LONGLONG centerX;
    LONGLONG centerY;
    HICON icon;

    if (!IsWindowVisible(splash) || !GetWindowRect(splash, &rect)) {
        return FALSE;
    }
    width = (LONGLONG)rect.right - rect.left;
    height = (LONGLONG)rect.bottom - rect.top;
    ZeroMemory(&monitorInfo, sizeof(monitorInfo));
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (width < 320 || height < 180 || width <= height ||
        width > 8192 || height > 8192 ||
        !GetMonitorInfoW(MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST),
                         &monitorInfo)) {
        return FALSE;
    }
    centerX = (LONGLONG)rect.left + width / 2;
    centerY = (LONGLONG)rect.top + height / 2;
    style = GetWindowLongPtrW(splash, GWL_STYLE);
    extendedStyle = GetWindowLongPtrW(splash, GWL_EXSTYLE);
    icon = (HICON)(ULONG_PTR)GetClassLongPtrW(splash, GCLP_HICON);
    if (icon == NULL) {
        icon = (HICON)(ULONG_PTR)GetClassLongPtrW(splash, GCLP_HICONSM);
    }
    return centerX >= monitorInfo.rcWork.left &&
           centerX < monitorInfo.rcWork.right &&
           centerY >= monitorInfo.rcWork.top &&
           centerY < monitorInfo.rcWork.bottom &&
           (style & (LONG_PTR)WS_POPUP) != 0 &&
           (extendedStyle & WS_EX_TOOLWINDOW) != 0 &&
           (extendedStyle & WS_EX_LAYERED) != 0 &&
           (extendedStyle & WS_EX_NOACTIVATE) != 0 &&
           GetWindow(splash, GW_OWNER) == NULL && icon != NULL;
}

static BOOL query_high_contrast(BOOL *enabled)
{
    HIGHCONTRASTW contrast;

    if (enabled == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ZeroMemory(&contrast, sizeof(contrast));
    contrast.cbSize = sizeof(contrast);
    if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast),
                               &contrast, 0)) {
        return FALSE;
    }
    *enabled = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    return TRUE;
}

static BOOL valid_rounded_splash_region(HWND splash, LRESULT radius)
{
    RECT client;
    HRGN region;
    int regionKind;
    int width;
    int height;
    BOOL valid;

    if (splash == NULL || radius <= 0 || radius > INT_MAX ||
        !GetClientRect(splash, &client)) {
        return FALSE;
    }
    width = client.right - client.left;
    height = client.bottom - client.top;
    if (width <= 0 || height <= 0 ||
        (LONGLONG)radius * 2 >= min(width, height)) {
        return FALSE;
    }
    region = CreateRectRgn(0, 0, 1, 1);
    if (region == NULL) {
        return FALSE;
    }
    regionKind = GetWindowRgn(splash, region);
    valid = regionKind != ERROR && regionKind != NULLREGION &&
            !PtInRegion(region, 0, 0) &&
            !PtInRegion(region, width - 1, 0) &&
            !PtInRegion(region, 0, height - 1) &&
            !PtInRegion(region, width - 1, height - 1) &&
            PtInRegion(region, width / 2, height / 2);
    DeleteObject(region);
    return valid;
}

static void splash_pixels_free(SplashPixels *capture)
{
    if (capture != NULL && capture->values != NULL) {
        HeapFree(GetProcessHeap(), 0, capture->values);
        ZeroMemory(capture, sizeof(*capture));
    }
}

static BOOL splash_capture_pixels(HWND splash, SplashPixels *capture)
{
    BITMAPINFO bitmapInfo;
    RECT client;
    HDC windowDc = NULL;
    HDC memoryDc = NULL;
    HBITMAP bitmap = NULL;
    HGDIOBJ previousBitmap = NULL;
    DWORD *pixels = NULL;
    SIZE_T pixelCount;
    int width;
    int height;
    BOOL copied;
    BOOL captured = FALSE;

    if (capture == NULL || !GetClientRect(splash, &client)) {
        return FALSE;
    }
    ZeroMemory(capture, sizeof(*capture));
    width = client.right - client.left;
    height = client.bottom - client.top;
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        return FALSE;
    }
    pixelCount = (SIZE_T)width * (SIZE_T)height;
    windowDc = GetDC(splash);
    if (windowDc == NULL) {
        goto cleanup;
    }
    memoryDc = CreateCompatibleDC(windowDc);
    if (memoryDc == NULL) {
        goto cleanup;
    }
    ZeroMemory(&bitmapInfo, sizeof(bitmapInfo));
    bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    bitmap = CreateDIBSection(windowDc, &bitmapInfo, DIB_RGB_COLORS,
                              (void **)&pixels, NULL, 0);
    if (bitmap == NULL || pixels == NULL) {
        goto cleanup;
    }
    previousBitmap = SelectObject(memoryDc, bitmap);
    if (previousBitmap == NULL || previousBitmap == HGDI_ERROR) {
        previousBitmap = NULL;
        goto cleanup;
    }
    copied = PrintWindow(splash, memoryDc, PW_CLIENTONLY);
    if (!copied) {
        copied = BitBlt(memoryDc, 0, 0, width, height,
                        windowDc, 0, 0, SRCCOPY | CAPTUREBLT);
    }
    if (!copied) {
        goto cleanup;
    }
    capture->values = HeapAlloc(GetProcessHeap(), 0,
                                pixelCount * sizeof(*capture->values));
    if (capture->values == NULL) {
        goto cleanup;
    }
    CopyMemory(capture->values, pixels,
               pixelCount * sizeof(*capture->values));
    capture->width = width;
    capture->height = height;
    captured = TRUE;

cleanup:
    if (previousBitmap != NULL) {
        SelectObject(memoryDc, previousBitmap);
    }
    if (bitmap != NULL) {
        DeleteObject(bitmap);
    }
    if (memoryDc != NULL) {
        DeleteDC(memoryDc);
    }
    if (windowDc != NULL) {
        ReleaseDC(splash, windowDc);
    }
    if (!captured) {
        splash_pixels_free(capture);
    }
    return captured;
}

static BOOL splash_has_painted_content(HWND splash)
{
    SplashPixels capture;
    DWORD colors[16];
    size_t colorCount = 0;
    int stepX;
    int stepY;
    int x;
    int y;

    if (!splash_capture_pixels(splash, &capture)) {
        return FALSE;
    }
    stepX = max(1, capture.width / 96);
    stepY = max(1, capture.height / 64);
    for (y = 0; y < capture.height && colorCount < ARRAYSIZE(colors);
         y += stepY) {
        for (x = 0; x < capture.width && colorCount < ARRAYSIZE(colors);
             x += stepX) {
            DWORD color = capture.values[
                              (size_t)y * (size_t)capture.width +
                              (size_t)x] &
                          0x00F0F0F0u;
            size_t index;

            for (index = 0; index < colorCount; ++index) {
                if (colors[index] == color) {
                    break;
                }
            }
            if (index == colorCount) {
                colors[colorCount++] = color;
            }
        }
    }
    splash_pixels_free(&capture);
    return colorCount >= 8;
}

static BOOL splash_frames_differ(const SplashPixels *first,
                                 const SplashPixels *second)
{
    SIZE_T count;
    SIZE_T index;
    SIZE_T changed = 0;

    if (first == NULL || second == NULL || first->values == NULL ||
        second->values == NULL || first->width != second->width ||
        first->height != second->height) {
        return FALSE;
    }
    count = (SIZE_T)first->width * (SIZE_T)first->height;
    for (index = 0; index < count; ++index) {
        DWORD left = first->values[index];
        DWORD right = second->values[index];
        int difference = abs((int)(left & 0xFFu) -
                             (int)(right & 0xFFu)) +
                         abs((int)((left >> 8) & 0xFFu) -
                             (int)((right >> 8) & 0xFFu)) +
                         abs((int)((left >> 16) & 0xFFu) -
                             (int)((right >> 16) & 0xFFu));

        if (difference >= 24 && ++changed >= 12) {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL flag_enabled(LPCWSTR name)
{
    WCHAR value[16];
    DWORD length = GetEnvironmentVariableW(name, value, ARRAYSIZE(value));

    return length > 0 && length < ARRAYSIZE(value) &&
           (lstrcmpiW(value, L"1") == 0 ||
            lstrcmpiW(value, L"true") == 0 ||
            lstrcmpiW(value, L"yes") == 0 ||
            lstrcmpiW(value, L"on") == 0);
}

static BOOL query_state(HWND mainWindow, WPARAM query, LRESULT *value)
{
    return send_bounded(mainWindow, WCM_QUERY_STATE, query, 0, value);
}

static BOOL query_minimal_ui(HWND mainWindow, MinimalUiTelemetry *telemetry)
{
    if (telemetry == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ZeroMemory(telemetry, sizeof(*telemetry));
    return query_state(mainWindow, WCQ_UI_STYLE_FLAGS,
                       &telemetry->styleFlags) &&
           query_state(mainWindow, WCQ_UI_PANEL_RADIUS_PIXELS,
                       &telemetry->panelRadius) &&
           query_state(mainWindow, WCQ_UI_CONTROL_RADIUS_PIXELS,
                       &telemetry->controlRadius) &&
           query_state(mainWindow, WCQ_UI_TAB_RADIUS_PIXELS,
                       &telemetry->tabRadius) &&
           query_state(mainWindow, WCQ_UI_PANEL_PAINT_COUNT,
                       &telemetry->panelPaintCount) &&
           query_state(mainWindow, WCQ_UI_CONTROL_PAINT_COUNT,
                       &telemetry->controlPaintCount) &&
           query_state(mainWindow, WCQ_UI_TAB_PAINT_COUNT,
                       &telemetry->tabPaintCount) &&
           query_state(mainWindow, WCQ_UI_LOGO_CURVE_COUNT,
                       &telemetry->logoCurveCount) &&
           query_state(mainWindow, WCQ_UI_LOGO_CURVE_PAINT_COUNT,
                       &telemetry->logoCurvePaintCount) &&
           query_state(mainWindow, WCQ_UI_PANEL_CURVE_COUNT,
                       &telemetry->panelCurveCount);
}

static BOOL radius_fits_window(HWND window, LRESULT radius)
{
    RECT client;
    int height;

    if (window == NULL || radius < 2 || radius > INT_MAX ||
        !GetClientRect(window, &client)) {
        return FALSE;
    }
    height = client.bottom - client.top;
    return height > 0 && (LONGLONG)radius * 2 <= height;
}

static BOOL validate_minimal_ui(HWND mainWindow, BOOL highContrast,
                                MinimalUiTelemetry *telemetry)
{
    const LRESULT requiredFlags =
        WORDCRAFT_UI_STYLE_MINIMAL |
        WORDCRAFT_UI_STYLE_ROUNDED_PANEL |
        WORDCRAFT_UI_STYLE_ROUNDED_TABS |
        WORDCRAFT_UI_STYLE_ROUNDED_CONTROLS |
        WORDCRAFT_UI_STYLE_LOGO_CURVES;
    HWND formatBar = find_control(mainWindow, IDC_FORMAT_BAR);
    HWND ribbonTabs = find_control(mainWindow, IDC_RIBBON_TABS);
    HWND formatButton = find_control(mainWindow, IDC_FORMAT_BOLD);
    MinimalUiTelemetry before;
    int attempt;

    if (formatBar == NULL || ribbonTabs == NULL || formatButton == NULL ||
        !query_minimal_ui(mainWindow, &before) ||
        !RedrawWindow(mainWindow, NULL, NULL,
                      RDW_INVALIDATE | RDW_ERASE | RDW_FRAME |
                          RDW_ALLCHILDREN | RDW_UPDATENOW)) {
        return FALSE;
    }
    for (attempt = 0; attempt < 50; ++attempt) {
        if (!query_minimal_ui(mainWindow, telemetry)) {
            return FALSE;
        }
        if (highContrast) {
            return telemetry->styleFlags == 0 &&
                   telemetry->logoCurveCount == 0 &&
                   telemetry->panelCurveCount == 0;
        }
        if ((telemetry->styleFlags & requiredFlags) == requiredFlags &&
            radius_fits_window(formatBar, telemetry->panelRadius) &&
            radius_fits_window(formatButton, telemetry->controlRadius) &&
            radius_fits_window(ribbonTabs, telemetry->tabRadius) &&
            telemetry->panelPaintCount > before.panelPaintCount &&
            telemetry->controlPaintCount > before.controlPaintCount &&
            telemetry->tabPaintCount > before.tabPaintCount &&
            telemetry->logoCurveCount == 2 &&
            telemetry->logoCurvePaintCount > before.logoCurvePaintCount &&
            telemetry->panelCurveCount == 2) {
            return TRUE;
        }
        Sleep(POLL_MS);
    }
    return FALSE;
}

static BOOL query_splash_animation(HWND splash, WPARAM query,
                                   LRESULT *value)
{
    return send_bounded(splash, WCM_SPLASH_QUERY_ANIMATION,
                        query, 0, value);
}

static BOOL wait_for_animation_tick(HWND splash, LRESULT *tickCount)
{
    int attempt;

    for (attempt = 0; attempt < 100; ++attempt) {
        if (!query_splash_animation(
                splash, WCSQ_ANIMATION_TIMER_TICK_COUNT, tickCount)) {
            return FALSE;
        }
        if (*tickCount > 0) {
            return TRUE;
        }
        Sleep(POLL_MS);
    }
    return FALSE;
}

static BOOL wait_for_animation_frame_change(HWND splash,
                                            LRESULT originalFrame)
{
    int attempt;
    LRESULT frame = originalFrame;

    for (attempt = 0; attempt < 100; ++attempt) {
        if (!query_splash_animation(splash, WCSQ_ANIMATION_FRAME,
                                    &frame)) {
            return FALSE;
        }
        if (frame != originalFrame) {
            return TRUE;
        }
        Sleep(POLL_MS);
    }
    return FALSE;
}

static void stop_failed_process(PROCESS_INFORMATION *process,
                                HWND splash, HWND mainWindow)
{
    DWORD exitCode = 0;

    if (IsWindow(splash)) {
        PostMessageW(splash, WM_CLOSE, 0, 0);
    }
    if (IsWindow(mainWindow)) {
        PostMessageW(mainWindow, WM_CLOSE, 0, 0);
    }
    if (process->hThread != NULL) {
        CloseHandle(process->hThread);
    }
    if (process->hProcess == NULL) {
        return;
    }
    if (WaitForSingleObject(process->hProcess, 1000) == WAIT_TIMEOUT &&
        GetExitCodeProcess(process->hProcess, &exitCode) &&
        exitCode == STILL_ACTIVE) {
        TerminateProcess(process->hProcess, 99);
        WaitForSingleObject(process->hProcess, MESSAGE_TIMEOUT_MS);
    }
    CloseHandle(process->hProcess);
}

int wmain(void)
{
    WCHAR executable[PATH_CAPACITY];
    WCHAR commandLine[PATH_CAPACITY + 4];
    WCHAR previousHold[ENV_CAPACITY];
    WCHAR splashTitle[256];
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    ProcessWindows found;
    SplashPixels firstFrame;
    SplashPixels secondFrame;
    HWND mainWindow = NULL;
    HWND editor;
    LRESULT backend = RENDER_ENGINE_BACKEND_NONE;
    LRESULT fallback = RENDER_FALLBACK_NONE;
    LRESULT animationFrame = 0;
    LRESULT animationFrameCount = 0;
    LRESULT motionEnabled = 0;
    LRESULT timerActive = 0;
    LRESULT paintCount = 0;
    LRESULT probeHold = 0;
    LRESULT steppedFrame = 0;
    LRESULT animationTimerTickCount = 0;
    LRESULT splashCornerRadius = 0;
    LRESULT splashLogoCurveCount = 0;
    LRESULT splashLogoCurvePaintCount = 0;
    MinimalUiTelemetry minimalUi;
    DWORD previousLength;
    DWORD previousError;
    DWORD exitCode = 1;
    BOOL previousExisted;
    BOOL holdChanged = FALSE;
    BOOL forcedGdi = flag_enabled(L"WORDCRAFT_DISABLE_D2D");
    BOOL highContrast = FALSE;
    BOOL rendererValid;
    DWORD splashThreadId;
    DWORD mainThreadId;
    int step;
    int result = 1;

    ZeroMemory(&process, sizeof(process));
    ZeroMemory(&found, sizeof(found));
    ZeroMemory(&firstFrame, sizeof(firstFrame));
    ZeroMemory(&secondFrame, sizeof(secondFrame));
    ZeroMemory(&minimalUi, sizeof(minimalUi));
    previousHold[0] = L'\0';
    if (!query_high_contrast(&highContrast) ||
        GetFullPathNameW(L"wordcraft.exe", ARRAYSIZE(executable),
                         executable, NULL) == 0 ||
        FAILED(StringCchPrintfW(commandLine, ARRAYSIZE(commandLine),
                                L"\"%s\"", executable))) {
        fwprintf(stderr, L"splash probe could not prepare wordcraft.exe\n");
        goto cleanup;
    }
    SetLastError(ERROR_SUCCESS);
    previousLength = GetEnvironmentVariableW(
        L"WORDCRAFT_INTERNAL_SPLASH_PROBE_HOLD", previousHold,
        ARRAYSIZE(previousHold));
    previousError = GetLastError();
    if (previousLength >= ARRAYSIZE(previousHold) ||
        (previousLength == 0 && previousError != ERROR_SUCCESS &&
         previousError != ERROR_ENVVAR_NOT_FOUND) ||
        !SetEnvironmentVariableW(
            L"WORDCRAFT_INTERNAL_SPLASH_PROBE_HOLD", L"1")) {
        fwprintf(stderr, L"splash probe could not set its startup hold\n");
        goto cleanup;
    }
    previousExisted = previousLength != 0 ||
                      previousError != ERROR_ENVVAR_NOT_FOUND;
    holdChanged = TRUE;
    ZeroMemory(&startup, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNOACTIVATE;
    if (!CreateProcessW(executable, commandLine, NULL, NULL, FALSE, 0,
                        NULL, NULL, &startup, &process)) {
        fwprintf(stderr, L"splash probe could not launch WordCraft\n");
        goto cleanup;
    }
    CloseHandle(process.hThread);
    process.hThread = NULL;
    if (!SetEnvironmentVariableW(
                                 L"WORDCRAFT_INTERNAL_SPLASH_PROBE_HOLD",
                                 previousExisted ? previousHold : NULL)) {
        fwprintf(stderr, L"splash probe could not restore its environment\n");
        goto cleanup;
    }
    holdChanged = FALSE;
    if (!wait_for_ready(&process, &found, splashTitle,
                        ARRAYSIZE(splashTitle))) {
        fwprintf(stderr,
                 L"WordCraft did not expose its Ready splash with a hidden main window\n");
        goto cleanup;
    }
    mainWindow = found.mainWindow;
    if (!valid_splash(found.splash) ||
        !splash_has_painted_content(found.splash)) {
        fwprintf(stderr,
                 L"the splash window or its painted content was invalid\n");
        goto cleanup;
    }
    if (!query_splash_animation(found.splash,
                                WCSQ_WINDOW_CORNER_RADIUS_PIXELS,
                                &splashCornerRadius) ||
        !query_splash_animation(found.splash,
                                WCSQ_LOGO_ACCENT_CURVE_COUNT,
                                &splashLogoCurveCount) ||
        !query_splash_animation(found.splash,
                                WCSQ_LOGO_ACCENT_PAINT_COUNT,
                                &splashLogoCurvePaintCount) ||
        !valid_rounded_splash_region(found.splash, splashCornerRadius) ||
        (!highContrast &&
         (splashLogoCurveCount != 2 || splashLogoCurvePaintCount < 1)) ||
        (highContrast &&
         (splashLogoCurveCount != 0 || splashLogoCurvePaintCount != 0))) {
        fwprintf(stderr,
                 L"the rounded splash or logo curves were invalid (radius=%lld curves=%lld paints=%lld high_contrast=%d)\n",
                 (long long)splashCornerRadius,
                 (long long)splashLogoCurveCount,
                 (long long)splashLogoCurvePaintCount, highContrast);
        goto cleanup;
    }
    splashThreadId = GetWindowThreadProcessId(found.splash, NULL);
    mainThreadId = GetWindowThreadProcessId(mainWindow, NULL);
    if (splashThreadId == 0 || mainThreadId == 0 ||
        splashThreadId == mainThreadId ||
        !query_splash_animation(found.splash, WCSQ_ANIMATION_FRAME,
                                &animationFrame) ||
        !query_splash_animation(found.splash,
                                WCSQ_ANIMATION_FRAME_COUNT,
                                &animationFrameCount) ||
        !query_splash_animation(found.splash,
                                WCSQ_ANIMATION_MOTION_ENABLED,
                                &motionEnabled) ||
        !query_splash_animation(found.splash,
                                WCSQ_ANIMATION_TIMER_ACTIVE,
                                &timerActive) ||
        !query_splash_animation(found.splash,
                                WCSQ_ANIMATION_PAINT_COUNT,
                                &paintCount) ||
        !query_splash_animation(found.splash,
                                WCSQ_ANIMATION_PROBE_HOLD,
                                &probeHold) ||
        !query_splash_animation(found.splash,
                                WCSQ_ANIMATION_TIMER_TICK_COUNT,
                                &animationTimerTickCount) ||
        animationFrameCount != WORDCRAFT_SPLASH_ANIMATION_FRAME_COUNT ||
        animationFrame < 0 || animationFrame >= animationFrameCount ||
        timerActive != (motionEnabled != 0) ||
        paintCount < 1 || probeHold != 1 ||
        !splash_capture_pixels(found.splash, &firstFrame) ||
        !send_bounded(found.splash, WCM_SPLASH_STEP_ANIMATION,
                      0, 0, &steppedFrame) ||
        steppedFrame != (animationFrame + 1) % animationFrameCount ||
        !splash_capture_pixels(found.splash, &secondFrame) ||
        !splash_frames_differ(&firstFrame, &secondFrame)) {
        fwprintf(stderr,
                 L"the dedicated dog animation did not advance and repaint deterministically\n");
        goto cleanup;
    }
    for (step = 1; step < (int)animationFrameCount; ++step) {
        if (!send_bounded(found.splash, WCM_SPLASH_STEP_ANIMATION,
                          0, 0, &steppedFrame)) {
            fwprintf(stderr, L"the dog animation cycle could not be stepped\n");
            goto cleanup;
        }
    }
    if (steppedFrame != animationFrame) {
        fwprintf(stderr, L"the dog animation did not wrap its full cycle\n");
        goto cleanup;
    }
    if (motionEnabled != 0 &&
        !wait_for_animation_tick(found.splash,
                                 &animationTimerTickCount)) {
        fwprintf(stderr,
                 L"the automatic dog animation timer did not run\n");
        goto cleanup;
    }
    if (motionEnabled != 0 &&
        (!send_bounded(found.splash, WCM_SPLASH_RESUME_ANIMATION,
                       0, 0, &probeHold) ||
         probeHold == 0 ||
         !wait_for_animation_frame_change(found.splash,
                                          animationFrame))) {
        fwprintf(stderr,
                 L"the automatic dog animation did not advance a frame\n");
        goto cleanup;
    }
    splash_pixels_free(&secondFrame);
    splash_pixels_free(&firstFrame);
    if (!PostMessageW(found.splash, WM_CLOSE, 0, 0) ||
        !wait_for_handoff(&process, found.splash, &mainWindow)) {
        fwprintf(stderr, L"the splash-to-editor handoff failed\n");
        goto cleanup;
    }
    editor = find_control(mainWindow, IDC_EDITOR);
    if (editor == NULL ||
        !query_state(mainWindow, WCQ_RENDER_ENGINE_BACKEND, &backend) ||
        !query_state(mainWindow, WCQ_RENDER_ENGINE_FALLBACK_REASON,
                     &fallback)) {
        fwprintf(stderr, L"the initialized editor state was unavailable\n");
        goto cleanup;
    }
    rendererValid = forcedGdi
                        ? backend == RENDER_ENGINE_BACKEND_GDI &&
                              fallback == RENDER_FALLBACK_FORCED
                        : (backend == RENDER_ENGINE_BACKEND_DIRECTWRITE &&
                               fallback == RENDER_FALLBACK_NONE) ||
                              (backend == RENDER_ENGINE_BACKEND_GDI &&
                               fallback >= RENDER_FALLBACK_NO_TEXT_SERVICES2 &&
                               fallback <= RENDER_FALLBACK_DRAW_FAILURE);
    if (!rendererValid) {
        fwprintf(stderr,
                 L"invalid renderer after splash (backend=%lld fallback=%lld forced=%d)\n",
                 (long long)backend, (long long)fallback, forcedGdi);
        goto cleanup;
    }
    if (!validate_minimal_ui(mainWindow, highContrast, &minimalUi)) {
        fwprintf(stderr,
                 L"minimal UI telemetry was invalid (flags=0x%llX radii=%lld/%lld/%lld paints=%lld/%lld/%lld logo=%lld/%lld panel_curves=%lld high_contrast=%d)\n",
                 (unsigned long long)minimalUi.styleFlags,
                 (long long)minimalUi.panelRadius,
                 (long long)minimalUi.controlRadius,
                 (long long)minimalUi.tabRadius,
                 (long long)minimalUi.panelPaintCount,
                 (long long)minimalUi.controlPaintCount,
                 (long long)minimalUi.tabPaintCount,
                 (long long)minimalUi.logoCurveCount,
                 (long long)minimalUi.logoCurvePaintCount,
                 (long long)minimalUi.panelCurveCount, highContrast);
        goto cleanup;
    }
    if (!send_bounded(mainWindow, WM_CLOSE, 0, 0, NULL) ||
        WaitForSingleObject(process.hProcess, MESSAGE_TIMEOUT_MS) !=
            WAIT_OBJECT_0 ||
        !GetExitCodeProcess(process.hProcess, &exitCode) || exitCode != 0) {
        fwprintf(stderr, L"WordCraft did not close cleanly\n");
        goto cleanup;
    }
    CloseHandle(process.hProcess);
    process.hProcess = NULL;
    printf("splash_startup=ok ready_title=ok hidden_handoff=ok "
           "styles=ok geometry=ok rounded_splash=ok icon=ok paint=ok "
           "logo_curves=ok minimalist_ui=ok rounded_ribbon=ok "
           "rounded_controls=ok dog_animation=8_frames "
           "animation_timer=ok splash_thread=ok motion_policy=%s "
           "visual_policy=%s renderer=%s fallback=%lld "
           "clean_exit=ok\n",
           motionEnabled ? "enabled" : "reduced",
           highContrast ? "high_contrast" : "branded",
           backend == RENDER_ENGINE_BACKEND_DIRECTWRITE
               ? "directwrite"
               : "gdi",
           (long long)fallback);
    result = 0;

cleanup:
    splash_pixels_free(&secondFrame);
    splash_pixels_free(&firstFrame);
    if (holdChanged) {
        SetEnvironmentVariableW(L"WORDCRAFT_INTERNAL_SPLASH_PROBE_HOLD",
                                previousExisted ? previousHold : NULL);
    }
    if (result != 0) {
        stop_failed_process(&process, found.splash, mainWindow);
    }
    return result;
}
