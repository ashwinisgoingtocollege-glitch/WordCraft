#include "splash.h"

#include "resource.h"

#include <limits.h>
#include <strsafe.h>

#define WORDCRAFT_SPLASH_TITLE L"WordCraft"
#define WORDCRAFT_SPLASH_SUBTITLE L"Write boldly. Your homework is safe here."
#define WORDCRAFT_SPLASH_DEFAULT_STATUS L"Preparing your writing space..."

#define SPLASH_STATUS_CAPACITY 256
#define SPLASH_BASE_WIDTH 520
#define SPLASH_BASE_HEIGHT 300
#define SPLASH_BASE_CORNER_RADIUS 18
#define SPLASH_FADE_TIMER_ID 1
#define SPLASH_ANIMATION_TIMER_ID 2
#define SPLASH_FADE_INTERVAL_MS 16
#define SPLASH_FADE_ALPHA_STEP 24
#define SPLASH_ANIMATION_INTERVAL_MS 83
#define SPLASH_SEND_TIMEOUT_MS 5000
#define SPLASH_STARTUP_TIMEOUT_MS 5000
#define SPLASH_SHUTDOWN_TIMEOUT_MS 3000
#define SPLASH_CONTROLLER_MAGIC 0x57435350u
#define SPLASH_MESSAGE_SET_STATUS (WM_APP + 0x531)
#define SPLASH_MESSAGE_CLOSE (WM_APP + 0x532)
#define SPLASH_PROBE_ENVIRONMENT L"WORDCRAFT_INTERNAL_SPLASH_PROBE_HOLD"

#define SPLASH_COLOR_NAVY RGB(32, 58, 95)
#define SPLASH_COLOR_BLUE RGB(62, 111, 158)
#define SPLASH_COLOR_GOLD RGB(242, 176, 94)
#define SPLASH_COLOR_CREAM RGB(255, 252, 245)
#define SPLASH_COLOR_MUTED RGB(202, 219, 231)

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

typedef UINT(WINAPI *SplashGetDpiForWindowFn)(HWND window);

typedef struct SplashCreateParameters {
    HINSTANCE instance;
    int dpi;
    LPCWSTR status;
    WordcraftSplash *controller;
} SplashCreateParameters;

struct WordcraftSplash {
    DWORD magic;
    DWORD processId;
    HINSTANCE instance;
    HWND monitorOwner;
    HWND window;
    HANDLE thread;
    HANDLE readyEvent;
    HANDLE stoppedEvent;
    CRITICAL_SECTION statusLock;
    WCHAR status[SPLASH_STATUS_CAPACITY];
    LONG references;
    LONG cancelRequested;
    LONG readySignaled;
};

typedef struct SplashState {
    WordcraftSplash *controller;
    HINSTANCE instance;
    HICON icon;
    BOOL ownsIcon;
    HFONT titleFont;
    HFONT subtitleFont;
    HFONT statusFont;
    WCHAR status[SPLASH_STATUS_CAPACITY];
    int dpi;
    BYTE alpha;
    BOOL fading;
    BOOL highContrast;
    BOOL motionEnabled;
    BOOL animationTimerActive;
    BOOL probeHold;
    UINT animationFrame;
    UINT paintCount;
    UINT animationTimerTickCount;
    int windowCornerRadius;
    UINT logoAccentCurveCount;
    UINT logoAccentPaintCount;
} SplashState;

static void splash_controller_release(WordcraftSplash *controller)
{
    if (controller == NULL ||
        InterlockedDecrement(&controller->references) != 0) {
        return;
    }
    controller->magic = 0;
    if (controller->thread != NULL) {
        CloseHandle(controller->thread);
    }
    if (controller->readyEvent != NULL) {
        CloseHandle(controller->readyEvent);
    }
    if (controller->stoppedEvent != NULL) {
        CloseHandle(controller->stoppedEvent);
    }
    DeleteCriticalSection(&controller->statusLock);
    HeapFree(GetProcessHeap(), 0, controller);
}

static BOOL splash_controller_valid(const WordcraftSplash *controller)
{
    return controller != NULL &&
           controller->magic == SPLASH_CONTROLLER_MAGIC &&
           controller->processId == GetCurrentProcessId();
}

static HWND splash_controller_get_window(const WordcraftSplash *controller)
{
    return (HWND)InterlockedCompareExchangePointer(
        (PVOID volatile *)&((WordcraftSplash *)controller)->window,
        NULL, NULL);
}

static void splash_controller_set_window(WordcraftSplash *controller,
                                         HWND window)
{
    InterlockedExchangePointer((PVOID volatile *)&controller->window,
                               (PVOID)window);
}

static void splash_controller_signal_ready(WordcraftSplash *controller)
{
    if (InterlockedExchange(&controller->readySignaled, 1) == 0) {
        SetEvent(controller->readyEvent);
    }
}

static int splash_scale(int value, int dpi)
{
    return max(1, MulDiv(value, dpi > 0 ? dpi : 96, 96));
}

static int splash_scale_offset(int value, int dpi)
{
    return MulDiv(value, dpi > 0 ? dpi : 96, 96);
}

static int splash_query_dpi(HWND window)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    FARPROC procedure = user32 != NULL
                            ? GetProcAddress(user32, "GetDpiForWindow")
                            : NULL;
    SplashGetDpiForWindowFn getDpiForWindow = NULL;
    HDC dc;
    int dpi = 96;

    if (procedure != NULL &&
        sizeof(getDpiForWindow) == sizeof(procedure)) {
        CopyMemory(&getDpiForWindow, &procedure, sizeof(procedure));
    }
    if (getDpiForWindow != NULL && window != NULL) {
        UINT windowDpi = getDpiForWindow(window);
        if (windowDpi > 0) {
            return (int)windowDpi;
        }
    }
    dc = GetDC(window);
    if (dc != NULL) {
        int deviceDpi = GetDeviceCaps(dc, LOGPIXELSX);
        if (deviceDpi > 0) {
            dpi = deviceDpi;
        }
        ReleaseDC(window, dc);
    }
    return dpi;
}

static BOOL splash_high_contrast_enabled(void)
{
    HIGHCONTRASTW contrast;

    ZeroMemory(&contrast, sizeof(contrast));
    contrast.cbSize = sizeof(contrast);
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast),
                                 &contrast, 0) &&
           (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

static BOOL splash_animation_enabled(void)
{
    BOOL enabled = TRUE;

    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0)) {
        return TRUE;
    }
    return enabled;
}

static BOOL splash_probe_hold_enabled(void)
{
    WCHAR value[8];
    DWORD length = GetEnvironmentVariableW(SPLASH_PROBE_ENVIRONMENT,
                                            value, ARRAYSIZE(value));

    return length == 1 && value[0] == L'1';
}

static void splash_update_animation_policy(HWND window, SplashState *state)
{
    BOOL shouldRun;

    state->motionEnabled = splash_animation_enabled() &&
                           !state->highContrast;
    shouldRun = state->motionEnabled && !state->fading;
    if (shouldRun && !state->animationTimerActive) {
        state->animationTimerActive =
            SetTimer(window, SPLASH_ANIMATION_TIMER_ID,
                     SPLASH_ANIMATION_INTERVAL_MS, NULL) != 0;
    } else if (!shouldRun && state->animationTimerActive) {
        KillTimer(window, SPLASH_ANIMATION_TIMER_ID);
        state->animationTimerActive = FALSE;
    }
    if (!state->motionEnabled) {
        state->animationFrame = 0;
    }
}

static void splash_update_accessible_title(HWND window, LPCWSTR status)
{
    WCHAR title[SPLASH_STATUS_CAPACITY + 32];

    if (status == NULL || status[0] == L'\0') {
        status = WORDCRAFT_SPLASH_DEFAULT_STATUS;
    }
    if (SUCCEEDED(StringCchPrintfW(title, ARRAYSIZE(title),
                                   L"WordCraft - %s", status))) {
        SetWindowTextW(window, title);
        NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, window, OBJID_WINDOW,
                       CHILDID_SELF);
    }
}

static COLORREF splash_background(const SplashState *state)
{
    return state->highContrast ? GetSysColor(COLOR_WINDOW)
                               : SPLASH_COLOR_NAVY;
}

static COLORREF splash_primary_text(const SplashState *state)
{
    return state->highContrast ? GetSysColor(COLOR_WINDOWTEXT)
                               : SPLASH_COLOR_CREAM;
}

static COLORREF splash_secondary_text(const SplashState *state)
{
    return state->highContrast ? GetSysColor(COLOR_GRAYTEXT)
                               : SPLASH_COLOR_MUTED;
}

static COLORREF splash_accent(const SplashState *state)
{
    return state->highContrast ? GetSysColor(COLOR_HIGHLIGHT)
                               : SPLASH_COLOR_GOLD;
}

static void splash_fill(HDC dc, const RECT *rect, COLORREF color)
{
    COLORREF previous = SetDCBrushColor(dc, color);

    FillRect(dc, rect, (HBRUSH)GetStockObject(DC_BRUSH));
    if (previous != CLR_INVALID) {
        SetDCBrushColor(dc, previous);
    }
}

static HFONT splash_create_font(int pointSize, int weight, int dpi)
{
    return CreateFontW(-MulDiv(pointSize, dpi > 0 ? dpi : 96, 72),
                       0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

static void splash_release_fonts(SplashState *state)
{
    if (state->titleFont != NULL) {
        DeleteObject(state->titleFont);
        state->titleFont = NULL;
    }
    if (state->subtitleFont != NULL) {
        DeleteObject(state->subtitleFont);
        state->subtitleFont = NULL;
    }
    if (state->statusFont != NULL) {
        DeleteObject(state->statusFont);
        state->statusFont = NULL;
    }
}

static void splash_create_fonts(SplashState *state)
{
    splash_release_fonts(state);
    state->titleFont = splash_create_font(31, FW_SEMIBOLD, state->dpi);
    state->subtitleFont = splash_create_font(12, FW_NORMAL, state->dpi);
    state->statusFont = splash_create_font(10, FW_NORMAL, state->dpi);
}

static void splash_load_icon(SplashState *state)
{
    int iconSize = splash_scale(112, state->dpi);

    if (state->icon != NULL && state->ownsIcon) {
        DestroyIcon(state->icon);
    }
    state->icon = (HICON)LoadImageW(
        state->instance, MAKEINTRESOURCEW(IDI_WORDCRAFT), IMAGE_ICON,
        iconSize, iconSize, LR_DEFAULTCOLOR);
    state->ownsIcon = state->icon != NULL;
    if (state->icon == NULL) {
        state->icon = LoadIconW(NULL, IDI_APPLICATION);
        state->ownsIcon = FALSE;
    }
}

static void splash_update_shape(HWND window, SplashState *state)
{
    RECT client;
    HRGN region;
    int radius;

    GetClientRect(window, &client);
    radius = splash_scale(SPLASH_BASE_CORNER_RADIUS, state->dpi);
    state->windowCornerRadius = radius;
    region = CreateRoundRectRgn(client.left, client.top,
                                client.right + 1, client.bottom + 1,
                                radius, radius);
    if (region != NULL && !SetWindowRgn(window, region, TRUE)) {
        DeleteObject(region);
    }
}

static void splash_select_font(HDC dc, HFONT font, HGDIOBJ *previous)
{
    if (font == NULL) {
        font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
    *previous = SelectObject(dc, font);
}

static void splash_draw_paper_scraps(HDC dc, const SplashState *state,
                                      int iconLeft, int iconTop,
                                      int iconSize)
{
    UINT frame = state->animationFrame %
                 WORDCRAFT_SPLASH_ANIMATION_FRAME_COUNT;
    int scrap;

    if (frame == 0 || frame == 7) {
        return;
    }
    for (scrap = 0; scrap < 3; ++scrap) {
        int phase = (int)frame - (scrap + 1);
        int size;
        int x;
        int y;
        POINT points[4];
        COLORREF paperColor;
        COLORREF inkColor;
        HBRUSH brush;
        HPEN pen;
        HGDIOBJ previousBrush;
        HGDIOBJ previousPen;

        if (phase < 0 || phase > 3) {
            continue;
        }
        size = splash_scale(3 + scrap, state->dpi);
        x = iconLeft + iconSize - splash_scale(15 - scrap * 3,
                                                state->dpi) +
            splash_scale_offset(phase * 4, state->dpi);
        y = iconTop + splash_scale(72 + scrap * 8, state->dpi) +
            splash_scale_offset(phase * 6, state->dpi);
        points[0].x = x;
        points[0].y = y;
        points[1].x = x + size;
        points[1].y = y + splash_scale_offset((phase + scrap) % 3 - 1,
                                               state->dpi);
        points[2].x = x + size + splash_scale(2, state->dpi);
        points[2].y = y + size;
        points[3].x = x - splash_scale(1, state->dpi);
        points[3].y = y + size + splash_scale_offset(scrap - 1,
                                                     state->dpi);
        paperColor = state->highContrast
                         ? GetSysColor(COLOR_WINDOW)
                         : (phase <= 1
                                ? SPLASH_COLOR_CREAM
                                : (phase == 2 ? SPLASH_COLOR_MUTED
                                              : splash_background(state)));
        inkColor = phase == 3
                       ? splash_background(state)
                       : (state->highContrast
                              ? GetSysColor(COLOR_WINDOWTEXT)
                              : SPLASH_COLOR_BLUE);
        brush = CreateSolidBrush(paperColor);
        pen = CreatePen(PS_SOLID, max(1, splash_scale(1, state->dpi)),
                        inkColor);
        if (brush == NULL || pen == NULL) {
            if (brush != NULL) {
                DeleteObject(brush);
            }
            if (pen != NULL) {
                DeleteObject(pen);
            }
            continue;
        }
        previousBrush = SelectObject(dc, brush);
        previousPen = SelectObject(dc, pen);
        Polygon(dc, points, ARRAYSIZE(points));
        MoveToEx(dc, points[0].x + 1, points[0].y + size / 2, NULL);
        LineTo(dc, points[2].x - 1, points[2].y - 1);
        SelectObject(dc, previousPen);
        SelectObject(dc, previousBrush);
        DeleteObject(pen);
        DeleteObject(brush);
    }
}

static UINT splash_draw_logo_curves(HDC dc, const SplashState *state,
                                    int iconLeft, int iconTop,
                                    int iconSize)
{
    POINT upper[4];
    POINT lower[4];
    HPEN pen;
    HGDIOBJ previousPen;
    UINT count = 0;

    if (dc == NULL || state == NULL || state->highContrast || iconSize <= 0) {
        return 0;
    }
    upper[0].x = iconLeft - splash_scale(9, state->dpi);
    upper[0].y = iconTop + iconSize * 3 / 4;
    upper[1].x = iconLeft - splash_scale(15, state->dpi);
    upper[1].y = iconTop + iconSize / 3;
    upper[2].x = iconLeft + iconSize / 5;
    upper[2].y = iconTop - splash_scale(13, state->dpi);
    upper[3].x = iconLeft + iconSize * 3 / 5;
    upper[3].y = iconTop - splash_scale(9, state->dpi);
    lower[0].x = iconLeft + iconSize * 2 / 5;
    lower[0].y = iconTop + iconSize + splash_scale(9, state->dpi);
    lower[1].x = iconLeft + iconSize * 4 / 5;
    lower[1].y = iconTop + iconSize + splash_scale(13, state->dpi);
    lower[2].x = iconLeft + iconSize + splash_scale(15, state->dpi);
    lower[2].y = iconTop + iconSize * 2 / 3;
    lower[3].x = iconLeft + iconSize + splash_scale(9, state->dpi);
    lower[3].y = iconTop + iconSize / 4;

    pen = CreatePen(PS_SOLID, max(1, splash_scale(2, state->dpi)),
                    SPLASH_COLOR_GOLD);
    if (pen != NULL) {
        previousPen = SelectObject(dc, pen);
        if (PolyBezier(dc, upper, ARRAYSIZE(upper))) {
            ++count;
        }
        SelectObject(dc, previousPen);
        DeleteObject(pen);
    }
    pen = CreatePen(PS_SOLID, max(1, splash_scale(1, state->dpi)),
                    SPLASH_COLOR_BLUE);
    if (pen != NULL) {
        previousPen = SelectObject(dc, pen);
        if (PolyBezier(dc, lower, ARRAYSIZE(lower))) {
            ++count;
        }
        SelectObject(dc, previousPen);
        DeleteObject(pen);
    }
    return count;
}

static void splash_render(HWND window, HDC dc, SplashState *state)
{
    static const int bobY[WORDCRAFT_SPLASH_ANIMATION_FRAME_COUNT] =
        {0, 1, 2, 1, 0, -1, 0, 0};
    RECT client;
    RECT accentBar;
    RECT titleRect;
    RECT subtitleRect;
    RECT statusRect;
    RECT statusMark;
    HGDIOBJ previousFont = NULL;
    HPEN borderPen;
    HGDIOBJ previousPen;
    HGDIOBJ previousBrush;
    int padding;
    int iconSize;
    int iconTop;
    int textLeft;
    int animatedIconTop;
    UINT frame;

    if (dc == NULL || state == NULL) {
        return;
    }
    GetClientRect(window, &client);
    padding = splash_scale(34, state->dpi);
    iconSize = splash_scale(112, state->dpi);
    iconTop = splash_scale(48, state->dpi);
    textLeft = padding + iconSize + splash_scale(28, state->dpi);
    frame = state->animationFrame %
            WORDCRAFT_SPLASH_ANIMATION_FRAME_COUNT;
    animatedIconTop = iconTop + splash_scale_offset(bobY[frame],
                                                    state->dpi);

    splash_fill(dc, &client, splash_background(state));
    SetBkMode(dc, TRANSPARENT);

    state->logoAccentCurveCount = splash_draw_logo_curves(
        dc, state, padding, iconTop, iconSize);
    if (state->logoAccentCurveCount > 0) {
        state->logoAccentPaintCount =
            state->logoAccentPaintCount == UINT_MAX
                ? 1
                : state->logoAccentPaintCount + 1;
    }
    if (state->icon != NULL) {
        DrawIconEx(dc, padding, animatedIconTop, state->icon,
                   iconSize, iconSize, 0, NULL, DI_NORMAL);
    }
    splash_draw_paper_scraps(dc, state, padding, iconTop, iconSize);

    SetRect(&titleRect, textLeft, splash_scale(55, state->dpi),
            client.right - padding, splash_scale(108, state->dpi));
    splash_select_font(dc, state->titleFont, &previousFont);
    SetTextColor(dc, splash_primary_text(state));
    DrawTextW(dc, WORDCRAFT_SPLASH_TITLE, -1, &titleRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (previousFont != NULL) {
        SelectObject(dc, previousFont);
    }

    SetRect(&subtitleRect, textLeft, splash_scale(111, state->dpi),
            client.right - padding, splash_scale(164, state->dpi));
    splash_select_font(dc, state->subtitleFont, &previousFont);
    SetTextColor(dc, splash_secondary_text(state));
    DrawTextW(dc, WORDCRAFT_SPLASH_SUBTITLE, -1, &subtitleRect,
              DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    if (previousFont != NULL) {
        SelectObject(dc, previousFont);
    }

    SetRect(&statusMark, padding,
            client.bottom - splash_scale(61, state->dpi),
            padding + splash_scale(7, state->dpi),
            client.bottom - splash_scale(54, state->dpi));
    splash_fill(dc, &statusMark, splash_accent(state));
    SetRect(&statusRect,
            statusMark.right + splash_scale(11, state->dpi),
            client.bottom - splash_scale(73, state->dpi),
            client.right - padding,
            client.bottom - splash_scale(42, state->dpi));
    splash_select_font(dc, state->statusFont, &previousFont);
    SetTextColor(dc, splash_primary_text(state));
    DrawTextW(dc, state->status, -1, &statusRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                  DT_NOPREFIX);
    if (previousFont != NULL) {
        SelectObject(dc, previousFont);
    }

    SetRect(&accentBar, 0,
            client.bottom - splash_scale(7, state->dpi),
            client.right, client.bottom);
    splash_fill(dc, &accentBar, splash_accent(state));

    borderPen = CreatePen(PS_SOLID, 1,
                          state->highContrast
                              ? GetSysColor(COLOR_WINDOWFRAME)
                              : SPLASH_COLOR_BLUE);
    if (borderPen != NULL) {
        previousPen = SelectObject(dc, borderPen);
        previousBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, client.left, client.top, client.right - 1,
                  client.bottom - 1,
                  splash_scale(SPLASH_BASE_CORNER_RADIUS, state->dpi),
                  splash_scale(SPLASH_BASE_CORNER_RADIUS, state->dpi));
        SelectObject(dc, previousBrush);
        SelectObject(dc, previousPen);
        DeleteObject(borderPen);
    }
    state->paintCount += 1;
}

static void splash_paint(HWND window, SplashState *state)
{
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window, &paint);
    RECT client;
    HDC memoryDc = NULL;
    HBITMAP bitmap = NULL;
    HGDIOBJ previousBitmap = NULL;

    if (dc == NULL) {
        return;
    }
    GetClientRect(window, &client);
    memoryDc = CreateCompatibleDC(dc);
    if (memoryDc != NULL) {
        bitmap = CreateCompatibleBitmap(dc, client.right - client.left,
                                        client.bottom - client.top);
    }
    if (memoryDc != NULL && bitmap != NULL) {
        previousBitmap = SelectObject(memoryDc, bitmap);
    }
    if (previousBitmap != NULL && previousBitmap != HGDI_ERROR) {
        splash_render(window, memoryDc, state);
        BitBlt(dc, 0, 0, client.right - client.left,
               client.bottom - client.top, memoryDc, 0, 0, SRCCOPY);
        SelectObject(memoryDc, previousBitmap);
    } else {
        splash_render(window, dc, state);
    }
    if (bitmap != NULL) {
        DeleteObject(bitmap);
    }
    if (memoryDc != NULL) {
        DeleteDC(memoryDc);
    }
    EndPaint(window, &paint);
}

static SplashState *splash_state(HWND window)
{
    return (SplashState *)GetWindowLongPtrW(window, GWLP_USERDATA);
}

static LRESULT splash_advance_animation(HWND window, SplashState *state)
{
    state->animationFrame =
        (state->animationFrame + 1) %
        WORDCRAFT_SPLASH_ANIMATION_FRAME_COUNT;
    RedrawWindow(window, NULL, NULL,
                 RDW_INVALIDATE | RDW_NOERASE | RDW_UPDATENOW);
    return (LRESULT)state->animationFrame;
}

static LRESULT CALLBACK splash_window_proc(HWND window, UINT message,
                                           WPARAM wParam, LPARAM lParam)
{
    SplashState *state = splash_state(window);

    switch (message) {
    case WM_NCCREATE: {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lParam;
        SplashCreateParameters *parameters =
            (SplashCreateParameters *)create->lpCreateParams;

        state = (SplashState *)HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*state));
        if (state == NULL || parameters == NULL) {
            if (state != NULL) {
                HeapFree(GetProcessHeap(), 0, state);
            }
            return FALSE;
        }
        state->controller = parameters->controller;
        state->instance = parameters->instance;
        state->dpi = parameters->dpi > 0 ? parameters->dpi : 96;
        state->alpha = 255;
        state->highContrast = splash_high_contrast_enabled();
        state->probeHold = splash_probe_hold_enabled();
        StringCchCopyW(state->status, ARRAYSIZE(state->status),
                       parameters->status != NULL &&
                               parameters->status[0] != L'\0'
                           ? parameters->status
                           : WORDCRAFT_SPLASH_DEFAULT_STATUS);
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)state);
        return TRUE;
    }
    case WM_CREATE:
        splash_create_fonts(state);
        splash_load_icon(state);
        SetLayeredWindowAttributes(window, 0, state->alpha, LWA_ALPHA);
        splash_update_accessible_title(window, state->status);
        splash_update_animation_policy(window, state);
        return 0;
    case WM_SIZE:
        if (state != NULL) {
            splash_update_shape(window, state);
        }
        return 0;
    case WM_PAINT:
        if (state != NULL) {
            splash_paint(window, state);
            return 0;
        }
        break;
    case WM_PRINTCLIENT:
        if (state != NULL && wParam != 0) {
            splash_render(window, (HDC)wParam, state);
            return 0;
        }
        break;
    case WM_PRINT:
        if (state != NULL && wParam != 0 &&
            ((LPARAM)lParam & PRF_CLIENT) != 0) {
            splash_render(window, (HDC)wParam, state);
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETTINGCHANGE:
        if (state != NULL) {
            state->highContrast = splash_high_contrast_enabled();
            if ((state->highContrast || !splash_animation_enabled()) &&
                state->fading) {
                KillTimer(window, SPLASH_FADE_TIMER_ID);
                DestroyWindow(window);
                return 0;
            }
            splash_update_animation_policy(window, state);
            InvalidateRect(window, NULL, TRUE);
        }
        return 0;
    case WM_DPICHANGED:
        if (state != NULL) {
            RECT *suggested = (RECT *)lParam;
            state->dpi = max(96, (int)LOWORD(wParam));
            splash_create_fonts(state);
            splash_load_icon(state);
            if (suggested != NULL) {
                SetWindowPos(window, NULL, suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOACTIVATE | SWP_NOZORDER);
            }
            splash_update_shape(window, state);
            InvalidateRect(window, NULL, TRUE);
        }
        return 0;
    case SPLASH_MESSAGE_SET_STATUS:
        if (state == NULL || state->controller == NULL) {
            return FALSE;
        }
        EnterCriticalSection(&state->controller->statusLock);
        StringCchCopyW(state->status, ARRAYSIZE(state->status),
                       state->controller->status);
        LeaveCriticalSection(&state->controller->statusLock);
        splash_update_accessible_title(window, state->status);
        RedrawWindow(window, NULL, NULL,
                     RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
        return TRUE;
    case WCM_SPLASH_QUERY_ANIMATION:
        if (state == NULL) {
            return 0;
        }
        switch ((UINT)wParam) {
        case WCSQ_ANIMATION_FRAME:
            return (LRESULT)state->animationFrame;
        case WCSQ_ANIMATION_FRAME_COUNT:
            return WORDCRAFT_SPLASH_ANIMATION_FRAME_COUNT;
        case WCSQ_ANIMATION_MOTION_ENABLED:
            return state->motionEnabled;
        case WCSQ_ANIMATION_TIMER_ACTIVE:
            return state->animationTimerActive;
        case WCSQ_ANIMATION_PAINT_COUNT:
            return (LRESULT)state->paintCount;
        case WCSQ_ANIMATION_PROBE_HOLD:
            return state->probeHold;
        case WCSQ_ANIMATION_TIMER_TICK_COUNT:
            return (LRESULT)state->animationTimerTickCount;
        case WCSQ_WINDOW_CORNER_RADIUS_PIXELS:
            return state->windowCornerRadius;
        case WCSQ_LOGO_ACCENT_CURVE_COUNT:
            return (LRESULT)state->logoAccentCurveCount;
        case WCSQ_LOGO_ACCENT_PAINT_COUNT:
            return (LRESULT)state->logoAccentPaintCount;
        default:
            return 0;
        }
    case WCM_SPLASH_STEP_ANIMATION:
        if (state == NULL || !state->probeHold) {
            return 0;
        }
        return splash_advance_animation(window, state);
    case WCM_SPLASH_RESUME_ANIMATION:
        if (state == NULL || !state->probeHold) {
            return FALSE;
        }
        state->probeHold = FALSE;
        return TRUE;
    case SPLASH_MESSAGE_CLOSE:
        if (state == NULL) {
            return FALSE;
        }
        if (wParam != 0 && splash_animation_enabled() &&
            !state->highContrast) {
            if (!state->fading) {
                state->fading = TRUE;
                splash_update_animation_policy(window, state);
                if (SetTimer(window, SPLASH_FADE_TIMER_ID,
                             SPLASH_FADE_INTERVAL_MS, NULL) == 0) {
                    DestroyWindow(window);
                }
            }
        } else {
            DestroyWindow(window);
        }
        return TRUE;
    case WM_TIMER:
        if (state != NULL && wParam == SPLASH_ANIMATION_TIMER_ID &&
            state->animationTimerActive && state->motionEnabled) {
            state->animationTimerTickCount += 1;
            if (!state->probeHold) {
                splash_advance_animation(window, state);
            }
            return 0;
        }
        if (state != NULL && wParam == SPLASH_FADE_TIMER_ID &&
            state->fading) {
            if (state->alpha <= SPLASH_FADE_ALPHA_STEP) {
                KillTimer(window, SPLASH_FADE_TIMER_ID);
                DestroyWindow(window);
                return 0;
            }
            state->alpha = (BYTE)(state->alpha - SPLASH_FADE_ALPHA_STEP);
            SetLayeredWindowAttributes(window, 0, state->alpha, LWA_ALPHA);
            return 0;
        }
        break;
    case WM_CLOSE:
        SendMessageW(window, SPLASH_MESSAGE_CLOSE, TRUE, 0);
        return 0;
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(window, message, wParam, lParam);
        return hit == HTCLIENT ? HTCAPTION : hit;
    }
    case WM_NCDESTROY:
        if (state != NULL) {
            KillTimer(window, SPLASH_FADE_TIMER_ID);
            KillTimer(window, SPLASH_ANIMATION_TIMER_ID);
            state->animationTimerActive = FALSE;
            splash_release_fonts(state);
            if (state->icon != NULL && state->ownsIcon) {
                DestroyIcon(state->icon);
            }
            if (state->controller != NULL) {
                splash_controller_set_window(state->controller, NULL);
            }
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            HeapFree(GetProcessHeap(), 0, state);
        }
        PostQuitMessage(0);
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

static BOOL splash_register_class(HINSTANCE instance)
{
    WNDCLASSEXW windowClass;

    ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_DROPSHADOW;
    windowClass.lpfnWndProc = splash_window_proc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_WORDCRAFT));
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.hbrBackground = NULL;
    windowClass.lpszClassName = APP_SPLASH_CLASS_NAME;
    if (RegisterClassExW(&windowClass) != 0) {
        return TRUE;
    }
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

static void splash_center_rect(HWND owner, int width, int height,
                               RECT *rect)
{
    MONITORINFO monitorInfo;
    HMONITOR monitor;
    POINT origin = {0, 0};
    RECT workArea;

    if (owner == NULL) {
        GetCursorPos(&origin);
    }
    monitor = owner != NULL
                  ? MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST)
                  : MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    ZeroMemory(&monitorInfo, sizeof(monitorInfo));
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (monitor != NULL && GetMonitorInfoW(monitor, &monitorInfo)) {
        workArea = monitorInfo.rcWork;
    } else {
        SetRect(&workArea, 0, 0, GetSystemMetrics(SM_CXSCREEN),
                GetSystemMetrics(SM_CYSCREEN));
    }
    rect->left = workArea.left +
                 max(0, (workArea.right - workArea.left - width) / 2);
    rect->top = workArea.top +
                max(0, (workArea.bottom - workArea.top - height) / 2);
    rect->right = rect->left + width;
    rect->bottom = rect->top + height;
}

static DWORD WINAPI splash_thread_proc(LPVOID parameter)
{
    WordcraftSplash *controller = (WordcraftSplash *)parameter;
    SplashCreateParameters parameters;
    SplashState *state;
    HINSTANCE instance;
    HWND owner;
    WCHAR status[SPLASH_STATUS_CAPACITY];
    RECT position;
    HWND window = NULL;
    MSG message;
    int getMessageResult = 0;
    int actualDpi;
    int width;
    int height;
    DWORD exitCode = 1;

    if (!splash_controller_valid(controller)) {
        return 1;
    }
    instance = controller->instance;
    owner = controller->monitorOwner;
    EnterCriticalSection(&controller->statusLock);
    StringCchCopyW(status, ARRAYSIZE(status), controller->status);
    LeaveCriticalSection(&controller->statusLock);
    if (InterlockedCompareExchange(&controller->cancelRequested, 0, 0) != 0 ||
        !splash_register_class(instance)) {
        goto cleanup;
    }
    parameters.instance = instance;
    parameters.dpi = splash_query_dpi(owner);
    parameters.status = status;
    parameters.controller = controller;
    width = splash_scale(SPLASH_BASE_WIDTH, parameters.dpi);
    height = splash_scale(SPLASH_BASE_HEIGHT, parameters.dpi);
    splash_center_rect(owner, width, height, &position);
    window = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        APP_SPLASH_CLASS_NAME, L"WordCraft is starting", WS_POPUP,
        position.left, position.top, width, height, NULL, NULL, instance,
        &parameters);
    if (window == NULL) {
        goto cleanup;
    }
    actualDpi = splash_query_dpi(window);
    state = splash_state(window);
    if (state != NULL && actualDpi > 0 && actualDpi != state->dpi) {
        state->dpi = actualDpi;
        splash_create_fonts(state);
        splash_load_icon(state);
        width = splash_scale(SPLASH_BASE_WIDTH, state->dpi);
        height = splash_scale(SPLASH_BASE_HEIGHT, state->dpi);
        splash_center_rect(owner, width, height, &position);
        SetWindowPos(window, NULL, position.left, position.top,
                     width, height,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        splash_update_shape(window, state);
    }
    if (InterlockedCompareExchange(&controller->cancelRequested, 0, 0) != 0) {
        DestroyWindow(window);
        window = NULL;
        goto cleanup;
    }
    ShowWindow(window, SW_SHOWNOACTIVATE);
    UpdateWindow(window);
    splash_controller_set_window(controller, window);
    splash_controller_signal_ready(controller);
    if (InterlockedCompareExchange(&controller->cancelRequested, 0, 0) != 0) {
        DestroyWindow(window);
        window = NULL;
        goto cleanup;
    }

    while ((getMessageResult = GetMessageW(&message, NULL, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (getMessageResult == 0) {
        exitCode = 0;
    }

cleanup:
    if (window != NULL && IsWindow(window)) {
        DestroyWindow(window);
    }
    splash_controller_set_window(controller, NULL);
    splash_controller_signal_ready(controller);
    SetEvent(controller->stoppedEvent);
    splash_controller_release(controller);
    return exitCode;
}

static BOOL splash_window_is_ours(HWND window)
{
    WCHAR className[64];
    DWORD processId = 0;

    if (window == NULL || !IsWindow(window)) {
        return FALSE;
    }
    GetWindowThreadProcessId(window, &processId);
    return processId == GetCurrentProcessId() &&
           GetClassNameW(window, className, ARRAYSIZE(className)) > 0 &&
           lstrcmpW(className, APP_SPLASH_CLASS_NAME) == 0;
}

static BOOL splash_send_bounded(WordcraftSplash *controller, UINT message,
                                WPARAM wParam, LPARAM lParam,
                                LRESULT *result)
{
    HWND window;
    DWORD_PTR value = 0;

    if (!splash_controller_valid(controller)) {
        return FALSE;
    }
    window = splash_controller_get_window(controller);
    if (!splash_window_is_ours(window) ||
        SendMessageTimeoutW(window, message, wParam, lParam,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK,
                            SPLASH_SEND_TIMEOUT_MS, &value) == 0) {
        return FALSE;
    }
    if (result != NULL) {
        *result = (LRESULT)value;
    }
    return TRUE;
}

WordcraftSplash *wordcraft_splash_show(HINSTANCE instance, HWND owner,
                                       LPCWSTR initialStatus)
{
    WordcraftSplash *controller;
    HANDLE waits[2];
    DWORD waitResult;
    DWORD startupError = ERROR_SUCCESS;

    if (instance == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    controller = (WordcraftSplash *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*controller));
    if (controller == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    controller->magic = SPLASH_CONTROLLER_MAGIC;
    controller->processId = GetCurrentProcessId();
    controller->instance = instance;
    controller->monitorOwner = owner;
    controller->references = 1;
    if (!InitializeCriticalSectionAndSpinCount(&controller->statusLock,
                                               2000)) {
        HeapFree(GetProcessHeap(), 0, controller);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    StringCchCopyW(controller->status, ARRAYSIZE(controller->status),
                   initialStatus != NULL && initialStatus[0] != L'\0'
                       ? initialStatus
                       : WORDCRAFT_SPLASH_DEFAULT_STATUS);
    controller->readyEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    controller->stoppedEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (controller->readyEvent == NULL || controller->stoppedEvent == NULL) {
        startupError = GetLastError();
        splash_controller_release(controller);
        SetLastError(startupError);
        return NULL;
    }
    InterlockedIncrement(&controller->references);
    controller->thread = CreateThread(NULL, 0, splash_thread_proc,
                                      controller, 0, NULL);
    if (controller->thread == NULL) {
        startupError = GetLastError();
        splash_controller_release(controller);
        splash_controller_release(controller);
        SetLastError(startupError);
        return NULL;
    }
    waits[0] = controller->readyEvent;
    waits[1] = controller->thread;
    waitResult = WaitForMultipleObjects(ARRAYSIZE(waits), waits, FALSE,
                                        SPLASH_STARTUP_TIMEOUT_MS);
    if (waitResult == WAIT_OBJECT_0 &&
        splash_window_is_ours(splash_controller_get_window(controller))) {
        return controller;
    }
    startupError = waitResult == WAIT_TIMEOUT ? ERROR_TIMEOUT
                                               : ERROR_DLL_INIT_FAILED;
    InterlockedExchange(&controller->cancelRequested, 1);
    if (splash_window_is_ours(splash_controller_get_window(controller))) {
        PostMessageW(splash_controller_get_window(controller),
                     WM_CLOSE, 0, 0);
    }
    WaitForSingleObject(controller->thread, 1000);
    splash_controller_release(controller);
    SetLastError(startupError);
    return NULL;
}

BOOL wordcraft_splash_set_status(WordcraftSplash *splash, LPCWSTR status)
{
    LRESULT result = FALSE;

    if (!splash_controller_valid(splash) || status == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    EnterCriticalSection(&splash->statusLock);
    StringCchCopyW(splash->status, ARRAYSIZE(splash->status), status);
    LeaveCriticalSection(&splash->statusLock);
    return splash_send_bounded(splash, SPLASH_MESSAGE_SET_STATUS,
                               0, 0, &result) &&
           result != FALSE;
}

BOOL wordcraft_splash_close(WordcraftSplash *splash, BOOL fade)
{
    LRESULT result = FALSE;
    BOOL sent = TRUE;
    DWORD waitResult;
    HWND window;

    if (!splash_controller_valid(splash)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    window = splash_controller_get_window(splash);
    if (splash_window_is_ours(window)) {
        sent = splash_send_bounded(splash, SPLASH_MESSAGE_CLOSE,
                                   fade, 0, &result) && result != FALSE;
        if (!sent) {
            ShowWindowAsync(window, SW_HIDE);
            PostMessageW(window, WM_CLOSE, 0, 0);
        }
    }
    waitResult = WaitForSingleObject(splash->thread,
                                     SPLASH_SHUTDOWN_TIMEOUT_MS);
    if (waitResult != WAIT_OBJECT_0 &&
        splash_window_is_ours(splash_controller_get_window(splash))) {
        HWND remainingWindow = splash_controller_get_window(splash);

        ShowWindowAsync(remainingWindow, SW_HIDE);
        PostMessageW(remainingWindow, WM_CLOSE, 0, 0);
        waitResult = WaitForSingleObject(splash->thread, 1000);
    }
    splash_controller_release(splash);
    return sent && waitResult == WAIT_OBJECT_0;
}

BOOL wordcraft_splash_is_window(const WordcraftSplash *splash)
{
    return splash_controller_valid(splash) &&
           splash_window_is_ours(splash_controller_get_window(splash));
}
