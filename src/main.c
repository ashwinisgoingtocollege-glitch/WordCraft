#include "editor.h"
#include "live.h"
#include "rendereditor.h"
#include "splash.h"

#include <limits.h>
#include <ole2.h>
#include <stdlib.h>

#define STARTUP_SPLASH_TEST_TIMEOUT_MS 5000

static void startup_splash_set_status(AppState *app, LPCWSTR status)
{
    if (app != NULL && wordcraft_splash_is_window(app->startupSplash)) {
        wordcraft_splash_set_status(app->startupSplash, status);
    }
}

static BOOL environment_flag_enabled(LPCWSTR name)
{
    WCHAR value[8];
    DWORD length;

    if (name == NULL) {
        return FALSE;
    }
    length = GetEnvironmentVariableW(name, value, ARRAYSIZE(value));
    return length == 1 && value[0] == L'1';
}

/*
 * The integration probe uses this bounded, opt-in hold to inspect the fully
 * initialized splash deterministically.  Normal startup never enters it.
 */
static void startup_splash_test_hold(WordcraftSplash *splash)
{
    DWORD started;
    BOOL quitSeen = FALSE;

    if (!wordcraft_splash_is_window(splash) ||
        !environment_flag_enabled(
            L"WORDCRAFT_INTERNAL_SPLASH_PROBE_HOLD")) {
        return;
    }
    started = GetTickCount();
    while (wordcraft_splash_is_window(splash) && !quitSeen &&
           (DWORD)(GetTickCount() - started) <
               STARTUP_SPLASH_TEST_TIMEOUT_MS) {
        MSG pending;
        DWORD elapsed = (DWORD)(GetTickCount() - started);
        DWORD remaining = STARTUP_SPLASH_TEST_TIMEOUT_MS - elapsed;
        DWORD waitTime = min(remaining, 100u);
        DWORD waitResult = MsgWaitForMultipleObjects(
            0, NULL, FALSE, waitTime, QS_ALLINPUT);

        if (waitResult == WAIT_FAILED) {
            break;
        }
        while (PeekMessageW(&pending, NULL, 0, 0, PM_REMOVE)) {
            if (pending.message == WM_QUIT) {
                PostQuitMessage((int)pending.wParam);
                quitSeen = TRUE;
                break;
            }
            TranslateMessage(&pending);
            DispatchMessageW(&pending);
        }
    }
}

static BOOL query_brand_colors_enabled(void)
{
    HIGHCONTRASTW highContrast;

    ZeroMemory(&highContrast, sizeof(highContrast));
    highContrast.cbSize = sizeof(highContrast);
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(highContrast),
                              &highContrast, 0)) {
        return (highContrast.dwFlags & HCF_HIGHCONTRASTON) == 0;
    }
    return FALSE;
}

static void configure_palette(AppState *app)
{
    AppPalette *palette = &app->palette;

    if (!app->useBrandColors) {
        palette->toolbarBackground = GetSysColor(COLOR_BTNFACE);
        palette->toolbarText = GetSysColor(COLOR_BTNTEXT);
        palette->toolbarDisabledText = GetSysColor(COLOR_GRAYTEXT);
        palette->toolbarHotBackground = GetSysColor(COLOR_HIGHLIGHT);
        palette->toolbarHotText = GetSysColor(COLOR_HIGHLIGHTTEXT);
        palette->formatBackground = GetSysColor(COLOR_BTNFACE);
        palette->formatText = GetSysColor(COLOR_BTNTEXT);
        palette->controlBackground = GetSysColor(COLOR_WINDOW);
        palette->controlText = GetSysColor(COLOR_WINDOWTEXT);
        palette->controlBorder = GetSysColor(COLOR_WINDOWFRAME);
        palette->workspaceBackground = GetSysColor(COLOR_APPWORKSPACE);
        /* Document colors are stored explicitly, so keep physical paper light. */
        palette->pageBackground = RGB(255, 252, 245);
        palette->pageBorder = GetSysColor(COLOR_WINDOWFRAME);
        palette->pageShadow = GetSysColor(COLOR_3DSHADOW);
        palette->statusBackground = GetSysColor(COLOR_BTNFACE);
        palette->statusText = GetSysColor(COLOR_BTNTEXT);
        palette->statusDivider = GetSysColor(COLOR_3DSHADOW);
    } else if (app->darkMode) {
        palette->toolbarBackground = RGB(23, 40, 61);
        palette->toolbarText = RGB(255, 252, 245);
        palette->toolbarDisabledText = RGB(132, 147, 163);
        palette->toolbarHotBackground = RGB(242, 176, 94);
        palette->toolbarHotText = RGB(23, 40, 61);
        palette->formatBackground = RGB(37, 44, 53);
        palette->formatText = RGB(234, 239, 244);
        palette->controlBackground = RGB(48, 57, 70);
        palette->controlText = RGB(234, 239, 244);
        palette->controlBorder = RGB(80, 92, 108);
        palette->workspaceBackground = RGB(23, 26, 31);
        palette->pageBackground = RGB(255, 252, 245);
        palette->pageBorder = RGB(91, 105, 120);
        palette->pageShadow = RGB(8, 10, 13);
        palette->statusBackground = RGB(31, 37, 45);
        palette->statusText = RGB(226, 232, 239);
        palette->statusDivider = RGB(67, 77, 90);
    } else {
        palette->toolbarBackground = RGB(32, 58, 95);
        palette->toolbarText = RGB(255, 252, 245);
        palette->toolbarDisabledText = RGB(155, 177, 194);
        palette->toolbarHotBackground = RGB(242, 176, 94);
        palette->toolbarHotText = RGB(32, 54, 77);
        palette->formatBackground = RGB(231, 240, 247);
        palette->formatText = RGB(32, 54, 77);
        palette->controlBackground = RGB(249, 251, 253);
        palette->controlText = RGB(32, 54, 77);
        palette->controlBorder = RGB(166, 184, 198);
        palette->workspaceBackground = RGB(213, 227, 236);
        palette->pageBackground = RGB(255, 252, 245);
        palette->pageBorder = RGB(166, 184, 198);
        palette->pageShadow = RGB(116, 135, 150);
        palette->statusBackground = RGB(220, 234, 243);
        palette->statusText = RGB(32, 54, 77);
        palette->statusDivider = RGB(181, 200, 213);
    }
}

static void fill_solid_rect(HDC dc, const RECT *rect, COLORREF color)
{
    COLORREF previousColor = SetDCBrushColor(dc, color);
    FillRect(dc, rect, (HBRUSH)GetStockObject(DC_BRUSH));
    if (previousColor != CLR_INVALID) {
        SetDCBrushColor(dc, previousColor);
    }
}

static COLORREF blend_color(COLORREF base, COLORREF accent,
                            unsigned accentPercent)
{
    unsigned basePercent = 100u - min(accentPercent, 100u);
    return RGB((GetRValue(base) * basePercent +
                GetRValue(accent) * accentPercent) / 100u,
               (GetGValue(base) * basePercent +
                GetGValue(accent) * accentPercent) / 100u,
               (GetBValue(base) * basePercent +
                GetBValue(accent) * accentPercent) / 100u);
}

static void note_ui_paint(LONG *counter)
{
    if (counter == NULL) {
        return;
    }
    *counter = *counter == LONG_MAX ? 1 : *counter + 1;
}

static UINT draw_ribbon_edge_curves(AppState *app, HDC dc,
                                    const RECT *card)
{
    POINT upper[4];
    POINT lower[4];
    HPEN pen;
    HGDIOBJ previousPen;
    int stroke;
    int span;
    UINT count = 0;

    if (app == NULL || dc == NULL || card == NULL ||
        !app->useBrandColors || IsRectEmpty(card)) {
        return 0;
    }
    stroke = max(1, app_scale(app->mainWindow, 1));
    span = min(app_scale(app->mainWindow, 72),
               max(0, (card->right - card->left) / 4));
    if (span < app_scale(app->mainWindow, 24)) {
        return 0;
    }
    upper[0].x = card->right - span;
    upper[0].y = card->top + stroke;
    upper[1].x = card->right - span / 2;
    upper[1].y = card->top + stroke;
    upper[2].x = card->right - span / 3;
    upper[2].y = card->top + app_scale(app->mainWindow, 3);
    upper[3].x = card->right - app_scale(app->mainWindow, 9);
    upper[3].y = card->top + app_scale(app->mainWindow, 3);
    lower[0].x = card->left + app_scale(app->mainWindow, 9);
    lower[0].y = card->bottom - app_scale(app->mainWindow, 13);
    lower[1].x = card->left + span / 3;
    lower[1].y = card->bottom - app_scale(app->mainWindow, 11);
    lower[2].x = card->left + span / 2;
    lower[2].y = card->bottom - stroke;
    lower[3].x = card->left + span;
    lower[3].y = card->bottom - stroke;

    pen = CreatePen(PS_SOLID, stroke,
                    blend_color(app->palette.controlBorder,
                                app->palette.toolbarHotBackground, 52));
    if (pen == NULL) {
        return 0;
    }
    previousPen = SelectObject(dc, pen);
    if (PolyBezier(dc, upper, ARRAYSIZE(upper))) {
        ++count;
    }
    if (PolyBezier(dc, lower, ARRAYSIZE(lower))) {
        ++count;
    }
    SelectObject(dc, previousPen);
    DeleteObject(pen);
    return count;
}

static void draw_minimal_ribbon_surface(AppState *app, HWND window, HDC dc)
{
    RECT client;
    RECT card;
    HBRUSH brush;
    HPEN pen;
    HGDIOBJ previousBrush;
    HGDIOBJ previousPen;
    int inset;
    int radius;

    if (app == NULL || window == NULL || dc == NULL ||
        !app->useBrandColors) {
        return;
    }
    GetClientRect(window, &client);
    fill_solid_rect(dc, &client, app->palette.formatBackground);
    card = client;
    inset = max(1, app_scale(app->mainWindow, 4));
    InflateRect(&card, -inset, -inset);
    if (IsRectEmpty(&card)) {
        return;
    }
    radius = max(2, app_scale(app->mainWindow, 12));
    brush = CreateSolidBrush(app->palette.controlBackground);
    pen = CreatePen(PS_SOLID, max(1, app_scale(app->mainWindow, 1)),
                    blend_color(app->palette.controlBorder,
                                app->palette.formatBackground, 25));
    if (brush == NULL || pen == NULL) {
        if (brush != NULL) {
            DeleteObject(brush);
        }
        if (pen != NULL) {
            DeleteObject(pen);
        }
        return;
    }
    previousBrush = SelectObject(dc, brush);
    previousPen = SelectObject(dc, pen);
    if (RoundRect(dc, card.left, card.top, card.right, card.bottom,
                  radius * 2, radius * 2)) {
        app->uiPanelCornerRadius = radius;
        note_ui_paint(&app->uiPanelPaintCount);
    }
    SelectObject(dc, previousPen);
    SelectObject(dc, previousBrush);
    DeleteObject(pen);
    DeleteObject(brush);
    app->uiPanelCurveCount = draw_ribbon_edge_curves(app, dc, &card);
    ribbon_paint_home_groups(app, dc);
}

static const WCHAR *minimal_button_display_text(UINT id)
{
    switch (id) {
    case IDC_FORMAT_BOLD:
        return L"B";
    case IDC_FORMAT_ITALIC:
        return L"I";
    case IDC_FORMAT_UNDERLINE:
        return L"U";
    case IDC_FORMAT_STRIKE:
        return L"S";
    case IDM_FORMAT_GROW_FONT:
        return L"A+";
    case IDM_FORMAT_SHRINK_FONT:
        return L"A-";
    case IDM_FORMAT_SUBSCRIPT:
        return L"x\x2082";
    case IDM_FORMAT_SUPERSCRIPT:
        return L"x\x00B2";
    case IDM_FORMAT_HIGHLIGHT:
        return L"HL";
    case IDM_FORMAT_CLEAR:
        return L"Clear";
    case IDC_ALIGN_LEFT:
        return L"L";
    case IDC_ALIGN_CENTER:
        return L"C";
    case IDC_ALIGN_RIGHT:
        return L"R";
    case IDC_ALIGN_JUSTIFY:
        return L"J";
    case IDC_BULLETS:
        return L"\x2022";
    case IDM_FORMAT_NUMBERING:
        return L"1.";
    case IDM_FORMAT_INDENT_DECREASE:
        return L"\x2190";
    case IDM_FORMAT_INDENT_INCREASE:
        return L"\x2192";
    default:
        return NULL;
    }
}

static LRESULT draw_minimal_format_button(AppState *app, NMCUSTOMDRAW *draw)
{
    HWND button = draw->hdr.hwndFrom;
    RECT client;
    RECT shape;
    RECT textRect;
    WCHAR caption[64];
    HFONT font;
    HGDIOBJ previousFont = NULL;
    HBRUSH brush;
    HPEN pen;
    HGDIOBJ previousBrush;
    HGDIOBJ previousPen;
    BOOL checked;
    BOOL pressed;
    BOOL hot;
    BOOL disabled;
    COLORREF background;
    COLORREF textColor;
    COLORREF borderColor;
    int radius;
    int penWidth;
    const WCHAR *displayText;

    if (draw->dwDrawStage != CDDS_PREPAINT) {
        return CDRF_DODEFAULT;
    }
    GetClientRect(button, &client);
    shape = client;
    InflateRect(&shape, -1, -1);
    checked = SendMessageW(button, BM_GETCHECK, 0, 0) == BST_CHECKED;
    pressed = (draw->uItemState & CDIS_SELECTED) != 0;
    hot = (draw->uItemState & CDIS_HOT) != 0;
    disabled = (draw->uItemState & CDIS_DISABLED) != 0 || !IsWindowEnabled(button);
    if (app->useBrandColors) {
        background = checked || pressed
                         ? blend_color(app->palette.controlBackground,
                                       app->palette.toolbarHotBackground, 58)
                         : (hot
                                ? blend_color(app->palette.controlBackground,
                                              app->palette.toolbarHotBackground,
                                              20)
                                : app->palette.controlBackground);
        textColor = disabled
                        ? app->palette.toolbarDisabledText
                        : (checked || pressed
                               ? app->palette.toolbarHotText
                               : app->palette.controlText);
        borderColor = checked || pressed || hot
                          ? blend_color(app->palette.controlBorder,
                                        app->palette.toolbarHotBackground, 68)
                          : app->palette.controlBorder;
    } else {
        BOOL emphasized = (checked || pressed || hot) && !disabled;
        background = emphasized ? GetSysColor(COLOR_HIGHLIGHT)
                                : GetSysColor(COLOR_BTNFACE);
        textColor = disabled
                        ? GetSysColor(COLOR_GRAYTEXT)
                        : (emphasized ? GetSysColor(COLOR_HIGHLIGHTTEXT)
                                      : GetSysColor(COLOR_BTNTEXT));
        borderColor = GetSysColor(COLOR_3DSHADOW);
    }
    radius = app->useBrandColors
                 ? max(2, app_scale(app->mainWindow, 8))
                 : 0;
    penWidth = checked || pressed
                   ? max(1, app_scale(app->mainWindow, 2))
                   : max(1, app_scale(app->mainWindow, 1));
    fill_solid_rect(draw->hdc, &client,
                    app->useBrandColors
                        ? app->palette.controlBackground
                        : GetSysColor(COLOR_BTNFACE));
    brush = CreateSolidBrush(background);
    pen = CreatePen(PS_SOLID, penWidth, borderColor);
    if (brush == NULL || pen == NULL) {
        if (brush != NULL) {
            DeleteObject(brush);
        }
        if (pen != NULL) {
            DeleteObject(pen);
        }
        return CDRF_DODEFAULT;
    }
    previousBrush = SelectObject(draw->hdc, brush);
    previousPen = SelectObject(draw->hdc, pen);
    if ((app->useBrandColors &&
         RoundRect(draw->hdc, shape.left, shape.top, shape.right,
                   shape.bottom, radius * 2, radius * 2)) ||
        (!app->useBrandColors &&
         Rectangle(draw->hdc, shape.left, shape.top, shape.right,
                   shape.bottom))) {
        app->uiControlCornerRadius = radius;
        note_ui_paint(&app->uiControlPaintCount);
    }
    SelectObject(draw->hdc, previousPen);
    SelectObject(draw->hdc, previousBrush);
    DeleteObject(pen);
    DeleteObject(brush);
    textRect = shape;
    SetBkMode(draw->hdc, TRANSPARENT);
    SetTextColor(draw->hdc, textColor);
    font = (HFONT)SendMessageW(button, WM_GETFONT, 0, 0);
    if (font != NULL) {
        previousFont = SelectObject(draw->hdc, font);
    }
    displayText = minimal_button_display_text((UINT)GetDlgCtrlID(button));
    if (displayText != NULL) {
        StringCchCopyW(caption, ARRAYSIZE(caption), displayText);
    } else {
        GetWindowTextW(button, caption, ARRAYSIZE(caption));
    }
    DrawTextW(draw->hdc, caption, -1, &textRect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (previousFont != NULL) {
        SelectObject(draw->hdc, previousFont);
    }
    if ((draw->uItemState & CDIS_FOCUS) != 0 && !disabled) {
        InflateRect(&textRect, -3, -3);
        DrawFocusRect(draw->hdc, &textRect);
    }
    return CDRF_SKIPDEFAULT;
}

static LRESULT CALLBACK format_bar_subclass_proc(HWND hwnd, UINT message,
                                                  WPARAM wParam, LPARAM lParam,
                                                  UINT_PTR subclassId,
                                                  DWORD_PTR referenceData)
{
    AppState *app = (AppState *)referenceData;
    (void)subclassId;

    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, format_bar_subclass_proc, 2);
    } else if (app != NULL && message == WM_COMMAND &&
               app->mainWindow != NULL) {
        return SendMessageW(app->mainWindow, message, wParam, lParam);
    } else if (app != NULL && !app->useBrandColors &&
               message == WM_PAINT) {
        LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
        HDC dc = GetDC(hwnd);
        if (dc != NULL) {
            ribbon_paint_home_groups(app, dc);
            ReleaseDC(hwnd, dc);
        }
        return result;
    } else if (app != NULL && !app->useBrandColors &&
               message == WM_PRINTCLIENT && wParam != 0) {
        LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
        ribbon_paint_home_groups(app, (HDC)wParam);
        return result;
    } else if (app != NULL && message == WM_NOTIFY && lParam != 0 &&
               ((NMHDR *)lParam)->code == NM_CUSTOMDRAW) {
        WCHAR className[32];
        if (GetClassNameW(((NMHDR *)lParam)->hwndFrom, className,
                          ARRAYSIZE(className)) > 0 &&
            lstrcmpiW(className, WC_BUTTONW) == 0) {
            return draw_minimal_format_button(app, (NMCUSTOMDRAW *)lParam);
        }
    } else if (app != NULL && app->useBrandColors) {
        if (message == WM_PAINT) {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(hwnd, &paint);
            if (dc != NULL) {
                draw_minimal_ribbon_surface(app, hwnd, dc);
            }
            EndPaint(hwnd, &paint);
            return 0;
        }
        if (message == WM_PRINTCLIENT && wParam != 0) {
            draw_minimal_ribbon_surface(app, hwnd, (HDC)wParam);
            return 0;
        }
        if (message == WM_ERASEBKGND) {
            RECT client;
            GetClientRect(hwnd, &client);
            fill_solid_rect((HDC)wParam, &client, app->palette.formatBackground);
            return 1;
        }
        if (message == WM_CTLCOLORSTATIC) {
            HDC dc = (HDC)wParam;
            SetTextColor(dc, app->palette.formatText);
            SetBkColor(dc, app->palette.controlBackground);
            SetDCBrushColor(dc, app->palette.controlBackground);
            return (LRESULT)(HBRUSH)GetStockObject(DC_BRUSH);
        }
        if (message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX ||
            message == WM_CTLCOLORBTN) {
            HDC dc = (HDC)wParam;
            SetTextColor(dc, app->palette.controlText);
            SetBkColor(dc, app->palette.controlBackground);
            SetDCBrushColor(dc, app->palette.controlBackground);
            return (LRESULT)(HBRUSH)GetStockObject(DC_BRUSH);
        }
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

static UINT draw_toolbar_logo_curves(AppState *app, HDC dc,
                                     int iconLeft, int iconTop, int iconSize)
{
    POINT upper[4];
    POINT lower[4];
    HPEN pen;
    HGDIOBJ previousPen;
    int stroke;
    UINT count = 0;

    if (app == NULL || dc == NULL || !app->useBrandColors || iconSize <= 0) {
        return 0;
    }
    stroke = max(1, app_scale(app->mainWindow, 1));
    upper[0].x = iconLeft - app_scale(app->mainWindow, 3);
    upper[0].y = iconTop + iconSize * 2 / 3;
    upper[1].x = iconLeft - app_scale(app->mainWindow, 5);
    upper[1].y = iconTop + iconSize / 4;
    upper[2].x = iconLeft + iconSize / 4;
    upper[2].y = iconTop - app_scale(app->mainWindow, 4);
    upper[3].x = iconLeft + iconSize * 2 / 3;
    upper[3].y = iconTop - app_scale(app->mainWindow, 2);
    lower[0].x = iconLeft + iconSize / 3;
    lower[0].y = iconTop + iconSize + app_scale(app->mainWindow, 2);
    lower[1].x = iconLeft + iconSize * 3 / 4;
    lower[1].y = iconTop + iconSize + app_scale(app->mainWindow, 4);
    lower[2].x = iconLeft + iconSize + app_scale(app->mainWindow, 5);
    lower[2].y = iconTop + iconSize * 3 / 4;
    lower[3].x = iconLeft + iconSize + app_scale(app->mainWindow, 3);
    lower[3].y = iconTop + iconSize / 3;

    pen = CreatePen(PS_SOLID, stroke,
                    blend_color(app->palette.toolbarBackground,
                                app->palette.toolbarHotBackground, 72));
    if (pen != NULL) {
        previousPen = SelectObject(dc, pen);
        if (PolyBezier(dc, upper, ARRAYSIZE(upper))) {
            ++count;
        }
        SelectObject(dc, previousPen);
        DeleteObject(pen);
    }
    pen = CreatePen(PS_SOLID, stroke,
                    blend_color(app->palette.toolbarBackground,
                                app->palette.toolbarText, 52));
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

static void draw_toolbar_brand_mark(AppState *app, HDC dc)
{
    RECT client;
    HICON icon;
    int iconSize;
    int iconLeft;
    int iconTop;

    if (app == NULL || app->toolbar == NULL || dc == NULL ||
        !app->useBrandColors) {
        return;
    }
    GetClientRect(app->toolbar, &client);
    iconSize = min(app_scale(app->mainWindow, 20),
                   max(0, client.bottom - client.top -
                              app_scale(app->mainWindow, 7)));
    if (iconSize < app_scale(app->mainWindow, 12)) {
        return;
    }
    iconLeft = app_scale(app->mainWindow, 12);
    iconTop = client.top + (client.bottom - client.top - iconSize) / 2;
    icon = (HICON)SendMessageW(app->mainWindow, WM_GETICON, ICON_SMALL, 0);
    if (icon == NULL) {
        icon = (HICON)GetClassLongPtrW(app->mainWindow, GCLP_HICONSM);
    }
    if (icon == NULL) {
        return;
    }
    app->uiLogoCurveCount = draw_toolbar_logo_curves(
        app, dc, iconLeft, iconTop, iconSize);
    if (app->uiLogoCurveCount > 0) {
        note_ui_paint(&app->uiLogoCurvePaintCount);
    }
    DrawIconEx(dc, iconLeft, iconTop, icon, iconSize, iconSize,
               0, NULL, DI_NORMAL);
}

static LRESULT app_query_minimal_ui(const AppState *app, UINT query)
{
    UINT styleFlags;

    if (app == NULL) {
        return 0;
    }
    styleFlags = app->useBrandColors
                     ? WORDCRAFT_UI_STYLE_MINIMAL |
                           WORDCRAFT_UI_STYLE_ROUNDED_PANEL |
                           WORDCRAFT_UI_STYLE_ROUNDED_TABS |
                           WORDCRAFT_UI_STYLE_ROUNDED_CONTROLS |
                           WORDCRAFT_UI_STYLE_LOGO_CURVES
                     : 0;
    switch (query) {
    case WCQ_UI_STYLE_FLAGS:
        return styleFlags;
    case WCQ_UI_PANEL_RADIUS_PIXELS:
        return app->useBrandColors ? app->uiPanelCornerRadius : 0;
    case WCQ_UI_CONTROL_RADIUS_PIXELS:
        return app->useBrandColors ? app->uiControlCornerRadius : 0;
    case WCQ_UI_TAB_RADIUS_PIXELS:
        return app->useBrandColors ? app->uiTabCornerRadius : 0;
    case WCQ_UI_PANEL_PAINT_COUNT:
        return app->uiPanelPaintCount;
    case WCQ_UI_CONTROL_PAINT_COUNT:
        return app->uiControlPaintCount;
    case WCQ_UI_TAB_PAINT_COUNT:
        return app->uiTabPaintCount;
    case WCQ_UI_LOGO_CURVE_COUNT:
        return app->useBrandColors ? app->uiLogoCurveCount : 0;
    case WCQ_UI_LOGO_CURVE_PAINT_COUNT:
        return app->uiLogoCurvePaintCount;
    case WCQ_UI_PANEL_CURVE_COUNT:
        return app->useBrandColors ? app->uiPanelCurveCount : 0;
    default:
        return 0;
    }
}

static void app_apply_title_bar_theme(AppState *app)
{
    typedef HRESULT(WINAPI *DwmSetWindowAttributeFn)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE module;
    DwmSetWindowAttributeFn setAttribute;
    BOOL enabled;
    DWORD attribute;

    if (app == NULL || app->mainWindow == NULL) {
        return;
    }
    module = LoadLibraryW(L"dwmapi.dll");
    if (module == NULL) {
        return;
    }
    setAttribute = (DwmSetWindowAttributeFn)(void *)
        GetProcAddress(module, "DwmSetWindowAttribute");
    if (setAttribute != NULL) {
        enabled = app->darkMode && app->useBrandColors;
        attribute = 20;
        if (FAILED(setAttribute(app->mainWindow, attribute, &enabled,
                                sizeof(enabled)))) {
            attribute = 19;
            setAttribute(app->mainWindow, attribute, &enabled, sizeof(enabled));
        }
    }
    FreeLibrary(module);
}

static void app_apply_theme(AppState *app)
{
    if (app == NULL) {
        return;
    }
    app->useBrandColors = query_brand_colors_enabled();
    configure_palette(app);
    if (app->toolbar != NULL) {
        SendMessageW(app->toolbar, TB_SETINDENT,
                     app->useBrandColors
                         ? app_scale(app->mainWindow, 48)
                         : 0,
                     0);
        SendMessageW(app->toolbar, TB_AUTOSIZE, 0, 0);
        InvalidateRect(app->toolbar, NULL, TRUE);
    }
    if (app->formatBar != NULL) {
        RedrawWindow(app->formatBar, NULL, NULL,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
    ribbon_apply_theme(app);
    if (app->statusBar != NULL) {
        SendMessageW(app->statusBar, SB_SETBKCOLOR, 0,
                     app->palette.statusBackground);
        InvalidateRect(app->statusBar, NULL, TRUE);
    }
    pageview_apply_theme(app);
    app_apply_title_bar_theme(app);
    if (app->mainWindow != NULL) {
        DrawMenuBar(app->mainWindow);
        RedrawWindow(app->mainWindow, NULL, NULL,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
    }
}

static BOOL CALLBACK apply_ui_font(HWND child, LPARAM data)
{
    SendMessageW(child, WM_SETFONT, (WPARAM)(HFONT)data, TRUE);
    return TRUE;
}

static HFONT create_ui_font_for_dpi(HWND window, UINT dpi)
{
    typedef BOOL(WINAPI *SystemParametersInfoForDpiFn)(
        UINT, UINT, PVOID, UINT, UINT);
    NONCLIENTMETRICSW metrics;
    SystemParametersInfoForDpiFn systemParametersInfoForDpi = NULL;
    HMODULE user32;
    BOOL loaded = FALSE;

    ZeroMemory(&metrics, sizeof(metrics));
    metrics.cbSize = sizeof(metrics);
    if (dpi == 0) {
        dpi = (UINT)app_scale(window, 96);
    }
    user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != NULL) {
        systemParametersInfoForDpi =
            (SystemParametersInfoForDpiFn)(void *)GetProcAddress(
                user32, "SystemParametersInfoForDpi");
    }
    if (systemParametersInfoForDpi != NULL) {
        loaded = systemParametersInfoForDpi(
            SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi);
    }
    if (!loaded && SystemParametersInfoW(
                       SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        HDC screen = GetDC(NULL);
        int systemDpi = screen != NULL ? GetDeviceCaps(screen, LOGPIXELSY) : 96;
        if (screen != NULL) {
            ReleaseDC(NULL, screen);
        }
        if (systemDpi > 0 && dpi != (UINT)systemDpi) {
            metrics.lfMessageFont.lfHeight = MulDiv(
                metrics.lfMessageFont.lfHeight, (int)dpi, systemDpi);
            metrics.lfMessageFont.lfWidth = MulDiv(
                metrics.lfMessageFont.lfWidth, (int)dpi, systemDpi);
        }
        loaded = TRUE;
    }
    return loaded ? CreateFontIndirectW(&metrics.lfMessageFont) : NULL;
}

static void app_refresh_ui_font(AppState *app, UINT dpi)
{
    HFONT replacement;
    HFONT previous;

    if (app == NULL || app->mainWindow == NULL) {
        return;
    }
    replacement = create_ui_font_for_dpi(app->mainWindow, dpi);
    if (replacement == NULL) {
        replacement = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
    previous = app->uiFont;
    app->uiFont = replacement;
    if (app->formatBar != NULL) {
        EnumChildWindows(app->formatBar, apply_ui_font,
                         (LPARAM)app->uiFont);
    }
    if (app->toolbar != NULL) {
        SendMessageW(app->toolbar, WM_SETFONT, (WPARAM)app->uiFont, TRUE);
    }
    if (app->statusBar != NULL) {
        SendMessageW(app->statusBar, WM_SETFONT, (WPARAM)app->uiFont, TRUE);
    }
    if (app->fontCombo != NULL) {
        SendMessageW(app->fontCombo, CB_SETDROPPEDWIDTH,
                     (WPARAM)app_scale(app->mainWindow, 250), 0);
    }
    ribbon_apply_theme(app);
    if (previous != NULL &&
        previous != GetStockObject(DEFAULT_GUI_FONT) &&
        previous != replacement) {
        DeleteObject(previous);
    }
}

static LRESULT CALLBACK editor_subclass_proc(HWND hwnd, UINT message,
                                             WPARAM wParam, LPARAM lParam,
                                             UINT_PTR subclassId,
                                             DWORD_PTR referenceData)
{
    AppState *app = (AppState *)referenceData;
    LRESULT result;
    (void)subclassId;
    if (message == WM_PAINT) {
        result = DefSubclassProc(hwnd, message, wParam, lParam);
        if (app != NULL) {
            assist_paint_overlays(app, hwnd);
            comments_paint_overlays(app, hwnd);
        }
        return result;
    }
    if (app != NULL && message == WM_SETFOCUS) {
        /* Returning to the document abandons an unfinished comment draft and
         * restores the normal visible selection before editing resumes. */
        comments_cancel_draft(app);
    }
    if (app != NULL && message == WM_KEYDOWN) {
        if (wParam == VK_ESCAPE && assist_has_completion(app)) {
            assist_clear_completion(app);
            return 0;
        }
        if (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_UP ||
            wParam == VK_DOWN || wParam == VK_HOME || wParam == VK_END ||
            wParam == VK_PRIOR || wParam == VK_NEXT || wParam == VK_DELETE ||
            wParam == VK_BACK) {
            assist_clear_completion(app);
        }
    }
    if (app != NULL && (message == WM_LBUTTONDOWN ||
                        message == WM_RBUTTONDOWN ||
                        message == WM_MBUTTONDOWN)) {
        assist_clear_completion(app);
    }
    if (app != NULL && message == WM_KILLFOCUS) {
        /* Some text services cancel composition without sending
         * WM_IME_ENDCOMPOSITION.  Reset that state when focus leaves so
         * completions cannot remain disabled after focus returns. */
        assist_set_ime_composing(app, FALSE);
        KillTimer(app->mainWindow, COMPLETION_TIMER_ID);
        assist_clear_completion(app);
    }
    if (app != NULL && message == WM_IME_STARTCOMPOSITION) {
        assist_set_ime_composing(app, TRUE);
    } else if (app != NULL && message == WM_IME_ENDCOMPOSITION) {
        assist_set_ime_composing(app, FALSE);
    }
    if (message == WM_PASTE && app != NULL) {
        UINT rtfFormat = RegisterClipboardFormatW(L"Rich Text Format");
        if ((rtfFormat != 0 && IsClipboardFormatAvailable(rtfFormat)) ||
            IsClipboardFormatAvailable(CF_BITMAP) ||
            IsClipboardFormatAvailable(CF_DIB) ||
            IsClipboardFormatAvailable(CF_DIBV5) ||
            IsClipboardFormatAvailable(CF_ENHMETAFILE) ||
            IsClipboardFormatAvailable(CF_METAFILEPICT)) {
            app->richFormattingUsed = TRUE;
        }
    } else if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, editor_subclass_proc, 1);
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

static HWND create_format_button(AppState *app, int id, const WCHAR *caption)
{
    return CreateWindowExW(
        0, WC_BUTTONW, caption,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX | BS_PUSHLIKE,
        0, 0, 0, 0, app->formatBar, (HMENU)(INT_PTR)id, app->instance, NULL);
}

int app_scale(HWND hwnd, int value)
{
    typedef UINT(WINAPI *GetDpiForWindowFn)(HWND);
    static GetDpiForWindowFn getDpiForWindow;
    static BOOL resolved;
    HDC dc;
    int dpi = 96;

    if (!resolved) {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32 != NULL) {
            getDpiForWindow = (GetDpiForWindowFn)(void *)
                GetProcAddress(user32, "GetDpiForWindow");
        }
        resolved = TRUE;
    }
    if (getDpiForWindow != NULL && hwnd != NULL) {
        UINT windowDpi = getDpiForWindow(hwnd);
        if (windowDpi > 0) {
            dpi = (int)windowDpi;
        }
    } else {
        dc = GetDC(hwnd);
        if (dc != NULL) {
            int deviceDpi = GetDeviceCaps(dc, LOGPIXELSX);
            if (deviceDpi > 0) {
                dpi = deviceDpi;
            }
            ReleaseDC(hwnd, dc);
        }
    }
    return MulDiv(value, dpi, 96);
}

void app_show_error(HWND owner, const WCHAR *action, DWORD errorCode)
{
    WCHAR *systemMessage = NULL;
    WCHAR message[1024];
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                  FORMAT_MESSAGE_IGNORE_INSERTS;

    if (errorCode != ERROR_SUCCESS) {
        FormatMessageW(flags, NULL, errorCode, 0, (WCHAR *)&systemMessage, 0, NULL);
    }
    if (systemMessage != NULL) {
        StringCchPrintfW(message, ARRAYSIZE(message), L"%s\n\n%s", action, systemMessage);
        LocalFree(systemMessage);
    } else {
        StringCchPrintfW(message, ARRAYSIZE(message), L"%s", action);
    }
    MessageBoxW(owner, message, APP_NAME, MB_OK | MB_ICONERROR);
}

BOOL app_create_children(AppState *app)
{
    static const WCHAR *sizes[] = {
        L"8", L"9", L"10", L"11", L"12", L"14", L"16", L"18",
        L"20", L"22", L"24", L"28", L"32", L"36", L"48", L"72"
    };
    TBBUTTON buttons[] = {
        {I_IMAGENONE, IDM_FILE_NEW, TBSTATE_ENABLED,
         BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT, {0}, 0, (INT_PTR)L"New"},
        {I_IMAGENONE, IDM_FILE_OPEN, TBSTATE_ENABLED,
         BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT, {0}, 0, (INT_PTR)L"Open"},
        {I_IMAGENONE, IDM_FILE_SAVE, TBSTATE_ENABLED,
         BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT, {0}, 0, (INT_PTR)L"Save"},
        {0, 0, TBSTATE_ENABLED, BTNS_SEP, {0}, 0, 0},
        {I_IMAGENONE, IDM_EDIT_CUT, TBSTATE_ENABLED,
         BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT, {0}, 0, (INT_PTR)L"Cut"},
        {I_IMAGENONE, IDM_EDIT_COPY, TBSTATE_ENABLED,
         BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT, {0}, 0, (INT_PTR)L"Copy"},
        {I_IMAGENONE, IDM_EDIT_PASTE, TBSTATE_ENABLED,
         BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT, {0}, 0, (INT_PTR)L"Paste"},
        {0, 0, TBSTATE_ENABLED, BTNS_SEP, {0}, 0, 0},
        {I_IMAGENONE, IDM_EDIT_UNDO, TBSTATE_ENABLED,
         BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT, {0}, 0, (INT_PTR)L"Undo"},
        {I_IMAGENONE, IDM_EDIT_REDO, TBSTATE_ENABLED,
         BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT, {0}, 0, (INT_PTR)L"Redo"},
        {0, 0, TBSTATE_ENABLED, BTNS_SEP, {0}, 0, 0},
        {I_IMAGENONE, IDM_FILE_PRINT, TBSTATE_ENABLED,
         BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT, {0}, 0, (INT_PTR)L"Print"}
    };
    size_t index;
    LRESULT eventMask;

    if (app == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    app->toolbar = CreateWindowExW(
        0, TOOLBARCLASSNAMEW, NULL,
        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS |
            TBSTYLE_TRANSPARENT | CCS_NODIVIDER | CCS_NOPARENTALIGN,
        0, 0, 0, 0, app->mainWindow, (HMENU)(INT_PTR)IDC_TOOLBAR,
        app->instance, NULL);
    if (app->toolbar == NULL) {
        return FALSE;
    }
    SendMessageW(app->toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(app->toolbar, TB_SETEXTENDEDSTYLE, 0,
                 TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_DOUBLEBUFFER);
    SendMessageW(app->toolbar, TB_ADDBUTTONSW, ARRAYSIZE(buttons), (LPARAM)buttons);
    SendMessageW(app->toolbar, TB_SETINDENT,
                 app->useBrandColors ? app_scale(app->mainWindow, 48) : 0,
                 0);
    SendMessageW(app->toolbar, TB_AUTOSIZE, 0, 0);

    app->formatBar = CreateWindowExW(
        WS_EX_CONTROLPARENT, WC_STATICW, NULL,
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | SS_NOTIFY,
        0, 0, 0, 0, app->mainWindow, (HMENU)(INT_PTR)IDC_FORMAT_BAR,
        app->instance, NULL);
    if (app->formatBar == NULL) {
        return FALSE;
    }
    if (!SetWindowSubclass(app->formatBar, format_bar_subclass_proc, 2,
                           (DWORD_PTR)app)) {
        return FALSE;
    }

    app->fontLabel = CreateWindowExW(
        0, WC_STATICW, L"Font:", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        0, 0, 0, 0, app->formatBar, NULL, app->instance, NULL);
    app->fontCombo = CreateWindowExW(
        0, WC_COMBOBOXW, NULL,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST |
            CBS_SORT | CBS_HASSTRINGS,
        0, 0, 0, 300, app->formatBar, (HMENU)(INT_PTR)IDC_FONT_COMBO,
        app->instance, NULL);
    app->sizeCombo = CreateWindowExW(
        0, WC_COMBOBOXW, NULL,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN |
            CBS_AUTOHSCROLL | CBS_HASSTRINGS,
        0, 0, 0, 300, app->formatBar, (HMENU)(INT_PTR)IDC_SIZE_COMBO,
        app->instance, NULL);

    app->boldButton = create_format_button(app, IDC_FORMAT_BOLD, L"Bold");
    app->italicButton = create_format_button(app, IDC_FORMAT_ITALIC, L"Italic");
    app->underlineButton = create_format_button(app, IDC_FORMAT_UNDERLINE,
                                                L"Underline");
    app->strikeButton = create_format_button(app, IDC_FORMAT_STRIKE,
                                             L"Strikethrough");
    app->colorButton = CreateWindowExW(
        0, WC_BUTTONW, L"Color", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, app->formatBar, (HMENU)(INT_PTR)IDC_TEXT_COLOR,
        app->instance, NULL);
    app->alignLeftButton = create_format_button(app, IDC_ALIGN_LEFT,
                                                L"Align Left");
    app->alignCenterButton = create_format_button(app, IDC_ALIGN_CENTER,
                                                  L"Align Center");
    app->alignRightButton = create_format_button(app, IDC_ALIGN_RIGHT,
                                                 L"Align Right");
    app->alignJustifyButton = create_format_button(app, IDC_ALIGN_JUSTIFY,
                                                   L"Justify");
    app->bulletsButton = create_format_button(app, IDC_BULLETS,
                                              L"Bulleted List");

    if (app->fontLabel == NULL || app->fontCombo == NULL ||
        app->sizeCombo == NULL ||
        app->boldButton == NULL || app->italicButton == NULL ||
        app->underlineButton == NULL || app->strikeButton == NULL ||
        app->colorButton == NULL || app->alignLeftButton == NULL ||
        app->alignCenterButton == NULL || app->alignRightButton == NULL ||
        app->alignJustifyButton == NULL || app->bulletsButton == NULL) {
        return FALSE;
    }

    app->uiFont = create_ui_font_for_dpi(
        app->mainWindow, (UINT)app_scale(app->mainWindow, 96));
    if (app->uiFont == NULL) {
        app->uiFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
    if (!ribbon_create(app)) {
        return FALSE;
    }
    EnumChildWindows(app->formatBar, apply_ui_font, (LPARAM)app->uiFont);
    SendMessageW(app->toolbar, WM_SETFONT, (WPARAM)app->uiFont, TRUE);

    fonts_populate_combo(app->fontCombo, app->mainWindow);
    SendMessageW(app->fontCombo, CB_SETMINVISIBLE, 20, 0);
    SendMessageW(app->fontCombo, CB_SETDROPPEDWIDTH,
                 (WPARAM)app_scale(app->mainWindow, 250), 0);
    for (index = 0; index < ARRAYSIZE(sizes); ++index) {
        SendMessageW(app->sizeCombo, CB_ADDSTRING, 0, (LPARAM)sizes[index]);
    }

    app->statusBar = CreateWindowExW(
        0, STATUSCLASSNAMEW, NULL,
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, app->mainWindow, (HMENU)(INT_PTR)IDC_STATUS,
        app->instance, NULL);
    if (app->statusBar == NULL) {
        return FALSE;
    }

    if (!pageview_create(app)) {
        return FALSE;
    }

    app->editor = CreateWindowExW(
        0, WORDCRAFT_RENDER_EDITOR_CLASS, NULL,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN |
            ES_NOHIDESEL | ES_SAVESEL,
        0, 0, 0, 0, app->pageView, (HMENU)(INT_PTR)IDC_EDITOR,
        app->instance, NULL);
    if (app->editor == NULL) {
        return FALSE;
    }
    if (!SetWindowSubclass(app->editor, editor_subclass_proc, 1, (DWORD_PTR)app)) {
        return FALSE;
    }

    SendMessageW(app->editor, EM_EXLIMITTEXT, 0, 0x7FFFFFFE);
    SendMessageW(app->editor, EM_SETUNDOLIMIT, 1000, 0);
    SendMessageW(app->editor, EM_SETVIEWKIND, VM_PAGE, 0);
    eventMask = SendMessageW(app->editor, EM_GETEVENTMASK, 0, 0);
    SendMessageW(app->editor, EM_SETEVENTMASK, 0,
                 eventMask | ENM_CHANGE | ENM_SELCHANGE | ENM_UPDATE |
                     ENM_PAGECHANGE | ENM_SCROLL);
    if (!text_engine_initialize(app)) {
        return FALSE;
    }
    pageview_mark_dirty(app);
    app_apply_theme(app);

    DragAcceptFiles(app->mainWindow, TRUE);
    format_set_word_wrap(app, app->wordWrap);
    format_set_zoom(app, app->zoomPercent);
    app_layout(app);
    return TRUE;
}

void app_layout(AppState *app)
{
    RECT client;
    RECT toolbarRect;
    RECT statusRect;
    int toolbarHeight;
    int tabHeight;
    int ribbonHeight;
    int statusHeight = 0;
    int width;
    int editorTop;
    int editorHeight;
    BOOL compact;

    if (app->toolbar == NULL || app->ribbonTabs == NULL ||
        app->formatBar == NULL || app->pageView == NULL ||
        app->editor == NULL) {
        return;
    }
    GetClientRect(app->mainWindow, &client);
    width = client.right - client.left;
    compact = width < app_scale(app->mainWindow, 850);
    tabHeight = app_scale(app->mainWindow, 32);
    ribbonHeight = app_scale(app->mainWindow, 100);

    SendMessageW(app->toolbar, TB_SETINDENT,
                 app->useBrandColors ? app_scale(app->mainWindow, 48) : 0,
                 0);
    SendMessageW(app->toolbar, TB_AUTOSIZE, 0, 0);
    GetWindowRect(app->toolbar, &toolbarRect);
    toolbarHeight = toolbarRect.bottom - toolbarRect.top;
    if (toolbarHeight <= 0) {
        toolbarHeight = app_scale(app->mainWindow, 30);
    }
    MoveWindow(app->toolbar, 0, 0, width, toolbarHeight, TRUE);
    MoveWindow(app->ribbonTabs, 0, toolbarHeight, width, tabHeight, TRUE);
    MoveWindow(app->formatBar, 0, toolbarHeight + tabHeight,
               width, ribbonHeight, TRUE);
    ribbon_layout(app, width, ribbonHeight, compact);

    if (app->showStatusBar) {
        ShowWindow(app->statusBar, SW_SHOW);
        SendMessageW(app->statusBar, WM_SIZE, 0, 0);
        GetWindowRect(app->statusBar, &statusRect);
        statusHeight = statusRect.bottom - statusRect.top;
        MoveWindow(app->statusBar, 0, client.bottom - statusHeight, width, statusHeight, TRUE);
    } else {
        ShowWindow(app->statusBar, SW_HIDE);
    }

    editorTop = toolbarHeight + tabHeight + ribbonHeight;
    editorHeight = client.bottom - editorTop - statusHeight;
    if (editorHeight < 0) {
        editorHeight = 0;
    }
    MoveWindow(app->pageView, 0, editorTop, width, editorHeight, TRUE);
    pageview_layout(app);

    if (app->showStatusBar) {
        int parts[4] = {app_scale(app->mainWindow, 185),
                        app_scale(app->mainWindow, 400),
                        app_scale(app->mainWindow, 520), -1};
        SendMessageW(app->statusBar, SB_SETPARTS, ARRAYSIZE(parts), (LPARAM)parts);
    }
}

static LONG count_words(AppState *app)
{
    SIZE_T length = 0;
    WCHAR *text = NULL;
    LONG words = 0;
    SIZE_T i;
    BOOL inWord = FALSE;
    DWORD error = ERROR_SUCCESS;

    if (!editor_get_text_length(app->editor, FALSE, &length, &error)) {
        return app->cachedWordCount;
    }
    if (length == 0) {
        return 0;
    }
    if (length > 5000000) {
        return -1;
    }
    if (!editor_get_all_text(app->editor, FALSE, &text, &length, &error)) {
        return app->cachedWordCount;
    }
    for (i = 0; i < length; ++i) {
        if (iswspace(text[i])) {
            inWord = FALSE;
        } else if (!inWord) {
            ++words;
            inWord = TRUE;
        }
    }
    HeapFree(GetProcessHeap(), 0, text);
    return words;
}

void app_update_status(AppState *app, BOOL recountWords)
{
    CHARRANGE selection;
    LONG line;
    LONG lineStart;
    SIZE_T characters = 0;
    DWORD textError = ERROR_SUCCESS;
    WCHAR positionText[128];
    WCHAR countText[128];
    WCHAR pageText[128];
    WCHAR viewText[128];
    const PaperSizePreset *paper;
    LRESULT liveRole;
    LRESULT liveState;
    LRESULT liveClients;
    int part;

    if (app == NULL || app->editor == NULL) {
        return;
    }
    if (app->paginationDirty || app->pageCount < 1) {
        pageview_paginate(app);
    }
    pageview_sync_to_caret(app, FALSE);
    if (!app->showStatusBar || app->statusBar == NULL) {
        return;
    }
    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
    line = (LONG)SendMessageW(app->editor, EM_EXLINEFROMCHAR, 0, selection.cpMin);
    lineStart = (LONG)SendMessageW(app->editor, EM_LINEINDEX, line, 0);
    if (lineStart < 0) {
        lineStart = selection.cpMin;
    }
    if (!editor_get_text_length(app->editor, FALSE, &characters, &textError)) {
        characters = 0;
    }

    if (recountWords || app->wordCountDirty) {
        app->cachedWordCount = count_words(app);
        app->wordCountDirty = FALSE;
    }
    StringCchPrintfW(positionText, ARRAYSIZE(positionText), L"Line %ld, Column %ld",
                     line + 1, selection.cpMin - lineStart + 1);
    if (app->cachedWordCount < 0) {
        StringCchPrintfW(countText, ARRAYSIZE(countText),
                         L"Words: large document   Characters: %llu",
                         (unsigned long long)characters);
    } else {
        StringCchPrintfW(countText, ARRAYSIZE(countText),
                         L"Words: %ld   Characters: %llu",
                         app->cachedWordCount, (unsigned long long)characters);
    }
    StringCchPrintfW(pageText, ARRAYSIZE(pageText), L"Page %ld of %ld",
                     app->currentPage > 0 ? app->currentPage : 1,
                     app->pageCount > 0 ? app->pageCount : 1);
    paper = paper_size_by_id(app->paperSizeId);
    liveRole = live_share_query_state(app, WCQ_LIVE_ROLE);
    liveState = live_share_query_state(app, WCQ_LIVE_STATE);
    liveClients = live_share_query_state(app, WCQ_LIVE_CLIENT_COUNT);
    if (liveRole == LIVE_ROLE_HOST && liveState == LIVE_STATE_LISTENING) {
        StringCchPrintfW(viewText, ARRAYSIZE(viewText),
                         L"Live: Host (%ld)   Zoom: %d%%   %s",
                         (long)liveClients, app->zoomPercent,
                         paper != NULL ? paper->name : L"Custom");
    } else if (liveRole == LIVE_ROLE_HOST &&
               liveState == LIVE_STATE_STARTING) {
        StringCchPrintfW(viewText, ARRAYSIZE(viewText),
                         L"Live: Starting host   Zoom: %d%%   %s",
                         app->zoomPercent,
                         paper != NULL ? paper->name : L"Custom");
    } else if (liveRole == LIVE_ROLE_CLIENT &&
               liveState == LIVE_STATE_CONNECTED) {
        StringCchPrintfW(viewText, ARRAYSIZE(viewText),
                         L"Live: Connected   Zoom: %d%%   %s",
                         app->zoomPercent,
                         paper != NULL ? paper->name : L"Custom");
    } else if (liveRole == LIVE_ROLE_CLIENT &&
               (liveState == LIVE_STATE_STARTING ||
                liveState == LIVE_STATE_CONNECTING)) {
        StringCchPrintfW(viewText, ARRAYSIZE(viewText),
                         L"Live: Connecting   Zoom: %d%%   %s",
                         app->zoomPercent,
                         paper != NULL ? paper->name : L"Custom");
    } else if (liveRole != LIVE_ROLE_NONE && liveState == LIVE_STATE_ERROR) {
        StringCchPrintfW(viewText, ARRAYSIZE(viewText),
                         L"Live: Error   Zoom: %d%%   %s", app->zoomPercent,
                         paper != NULL ? paper->name : L"Custom");
    } else if (liveRole != LIVE_ROLE_NONE &&
               liveState == LIVE_STATE_STOPPING) {
        StringCchPrintfW(viewText, ARRAYSIZE(viewText),
                         L"Live: Stopping   Zoom: %d%%   %s",
                         app->zoomPercent,
                         paper != NULL ? paper->name : L"Custom");
    } else {
        StringCchPrintfW(viewText, ARRAYSIZE(viewText), L"Zoom: %d%%   %s",
                         app->zoomPercent,
                         paper != NULL ? paper->name : L"Custom");
    }
    StringCchCopyW(app->statusText[0], ARRAYSIZE(app->statusText[0]), positionText);
    StringCchCopyW(app->statusText[1], ARRAYSIZE(app->statusText[1]), countText);
    StringCchCopyW(app->statusText[2], ARRAYSIZE(app->statusText[2]), pageText);
    StringCchCopyW(app->statusText[3], ARRAYSIZE(app->statusText[3]), viewText);
    for (part = 0; part < 4; ++part) {
        SendMessageW(app->statusBar, SB_SETTEXTW,
                     (WPARAM)(part | SBT_OWNERDRAW), (LPARAM)part);
    }
    live_share_apply_status_notice(app);
}

void app_set_status_message(AppState *app, const WCHAR *message)
{
    if (app->showStatusBar && app->statusBar != NULL) {
        StringCchCopyW(app->statusText[3], ARRAYSIZE(app->statusText[3]), message);
        SendMessageW(app->statusBar, SB_SETTEXTW, 3 | SBT_OWNERDRAW, 3);
    }
}

static void set_menu_enabled(HMENU menu, UINT id, BOOL enabled)
{
    EnableMenuItem(menu, id, MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
}

void app_update_command_ui(AppState *app)
{
    HMENU menu = GetMenu(app->mainWindow);
    CHARRANGE selection;
    BOOL hasSelection;
    BOOL canUndo;
    BOOL canRedo;
    BOOL canPaste;
    BOOL hasComments;
    LRESULT liveRole;
    LRESULT liveState;
    LRESULT liveWorker;
    WCHAR liveCaption[48];

    if (app->editor == NULL) {
        return;
    }
    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
    hasSelection = selection.cpMin != selection.cpMax;
    canUndo = (BOOL)SendMessageW(app->editor, EM_CANUNDO, 0, 0);
    canRedo = (BOOL)SendMessageW(app->editor, EM_CANREDO, 0, 0);
    canPaste = (BOOL)SendMessageW(app->editor, EM_CANPASTE, 0, 0);
    hasComments = comments_count(app) > 0;
    liveRole = live_share_query_state(app, WCQ_LIVE_ROLE);
    liveState = live_share_query_state(app, WCQ_LIVE_STATE);
    liveWorker = live_share_query_state(app, WCQ_LIVE_WORKER_RUNNING);

    if (menu != NULL) {
        set_menu_enabled(menu, IDM_EDIT_UNDO, canUndo);
        set_menu_enabled(menu, IDM_EDIT_REDO, canRedo);
        set_menu_enabled(menu, IDM_EDIT_CUT, hasSelection);
        set_menu_enabled(menu, IDM_EDIT_COPY, hasSelection);
        set_menu_enabled(menu, IDM_EDIT_DELETE, hasSelection);
        set_menu_enabled(menu, IDM_EDIT_PASTE, canPaste);
        set_menu_enabled(menu, IDM_REVIEW_PREVIOUS_COMMENT, hasComments);
        set_menu_enabled(menu, IDM_REVIEW_NEXT_COMMENT, hasComments);
        set_menu_enabled(menu, IDM_REVIEW_DELETE_COMMENT, hasComments);
        set_menu_enabled(menu, IDM_LIVE_START_HOST,
                         liveRole == LIVE_ROLE_NONE && !liveWorker);
        set_menu_enabled(menu, IDM_LIVE_JOIN_SESSION,
                         liveRole == LIVE_ROLE_NONE && !liveWorker);
        set_menu_enabled(menu, IDM_LIVE_COPY_INVITATION,
                         liveRole == LIVE_ROLE_HOST &&
                             liveState == LIVE_STATE_LISTENING);
        set_menu_enabled(menu, IDM_LIVE_LEAVE_SESSION,
                         liveRole != LIVE_ROLE_NONE || liveWorker);

        CheckMenuItem(menu, IDM_VIEW_WORD_WRAP,
                      MF_BYCOMMAND |
                          (app->wordWrap ? MF_CHECKED : MF_UNCHECKED));
        set_menu_enabled(menu, IDM_VIEW_WORD_WRAP, FALSE);
        CheckMenuItem(menu, IDM_VIEW_STATUS_BAR,
                      MF_BYCOMMAND |
                          (app->showStatusBar ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(menu, IDM_VIEW_DARK_MODE,
                      MF_BYCOMMAND |
                          (app->darkMode ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(menu, IDM_TOOLS_SPELL_CHECK,
                      MF_BYCOMMAND |
                          (app->spellCheckEnabled ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(menu, IDM_TOOLS_AUTOCOMPLETE,
                      MF_BYCOMMAND |
                          (app->autoCompleteEnabled ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuRadioItem(
            menu, IDM_VIEW_ZOOM_50, IDM_VIEW_ZOOM_200,
            (UINT)(IDM_VIEW_ZOOM_100 + (app->zoomPercent - 100)),
            MF_BYCOMMAND);
    }

    SendMessageW(app->toolbar, TB_ENABLEBUTTON, IDM_EDIT_UNDO, MAKELONG(canUndo, 0));
    SendMessageW(app->toolbar, TB_ENABLEBUTTON, IDM_EDIT_REDO, MAKELONG(canRedo, 0));
    SendMessageW(app->toolbar, TB_ENABLEBUTTON, IDM_EDIT_CUT, MAKELONG(hasSelection, 0));
    SendMessageW(app->toolbar, TB_ENABLEBUTTON, IDM_EDIT_COPY, MAKELONG(hasSelection, 0));
    SendMessageW(app->toolbar, TB_ENABLEBUTTON, IDM_EDIT_PASTE,
                 MAKELONG(canPaste, 0));

    ribbon_update_command_ui(app, hasSelection, canUndo, canRedo, canPaste);
    if (liveRole == LIVE_ROLE_HOST && liveState == LIVE_STATE_LISTENING) {
        StringCchPrintfW(liveCaption, ARRAYSIZE(liveCaption), L"Live: %ld",
                         (long)live_share_query_state(app,
                                                     WCQ_LIVE_CLIENT_COUNT));
        ribbon_set_live_share_status(app, liveCaption, TRUE);
    } else if (liveRole == LIVE_ROLE_HOST &&
               liveState == LIVE_STATE_STARTING) {
        ribbon_set_live_share_status(app, L"Live: Starting", TRUE);
    } else if (liveRole == LIVE_ROLE_CLIENT &&
               liveState == LIVE_STATE_CONNECTED) {
        ribbon_set_live_share_status(app, L"Live: Joined", TRUE);
    } else if (liveRole == LIVE_ROLE_CLIENT &&
               (liveState == LIVE_STATE_STARTING ||
                liveState == LIVE_STATE_CONNECTING)) {
        ribbon_set_live_share_status(app, L"Live: Connecting", TRUE);
    } else if (liveRole != LIVE_ROLE_NONE && liveState == LIVE_STATE_ERROR) {
        ribbon_set_live_share_status(app, L"Live: Error", TRUE);
    } else if (liveRole != LIVE_ROLE_NONE || liveWorker) {
        ribbon_set_live_share_status(app, L"Live: Stopping", TRUE);
    } else {
        ribbon_set_live_share_status(app, L"Live Share...", FALSE);
    }
    format_sync_controls(app);
}

static void handle_font_combo(AppState *app)
{
    WCHAR fontName[LF_FACESIZE + 32];
    WCHAR status[128];
    WCHAR *unavailable;

    if (!fonts_combo_selection_is_installed(app->fontCombo)) {
        if (GetWindowTextW(app->fontCombo, fontName,
                           ARRAYSIZE(fontName)) > 0) {
            unavailable = wcsstr(fontName, L"  (not installed)");
            if (unavailable != NULL) {
                *unavailable = L'\0';
            }
            StringCchPrintfW(status, ARRAYSIZE(status),
                             L"%s is not installed in Windows.", fontName);
            app_set_status_message(app, status);
        }
        MessageBeep(MB_ICONWARNING);
        format_sync_controls(app);
        SetFocus(app->editor);
        return;
    }
    if (GetWindowTextW(app->fontCombo, fontName, ARRAYSIZE(fontName)) > 0) {
        format_set_font_name(app, fontName);
        SetFocus(app->editor);
    }
}

static void handle_size_combo(AppState *app)
{
    WCHAR sizeText[32];
    WCHAR *end = NULL;
    double points;
    if (GetWindowTextW(app->sizeCombo, sizeText, ARRAYSIZE(sizeText)) <= 0) {
        return;
    }
    points = wcstod(sizeText, &end);
    while (end != NULL && iswspace(*end)) {
        ++end;
    }
    if (end != sizeText && end != NULL && *end == L'\0' &&
        points >= 1.0 && points <= 500.0) {
        format_set_font_size(app, points);
        SetFocus(app->editor);
    } else {
        MessageBeep(MB_ICONWARNING);
        format_sync_controls(app);
    }
}

static void handle_command(AppState *app, UINT command)
{
    WCHAR commentText[COMMENT_TEXT_CAPACITY + 1];

    switch (command) {
    case IDM_FILE_NEW:
        if (document_new(app, TRUE) &&
            live_share_query_state(app, WCQ_LIVE_ROLE) != 0) {
            live_share_leave_for_document_replacement(app);
        }
        break;
    case IDM_FILE_OPEN:
        if (document_open_dialog(app) &&
            live_share_query_state(app, WCQ_LIVE_ROLE) != 0) {
            live_share_leave_for_document_replacement(app);
        }
        break;
    case IDM_FILE_SAVE:
        document_save(app);
        break;
    case IDM_FILE_SAVE_AS:
        document_save_as(app);
        break;
    case IDM_FILE_PAGE_SETUP:
        printing_page_setup(app);
        break;
    case IDM_FILE_PRINT:
        printing_print_document(app);
        break;
    case IDM_FILE_EXIT:
        SendMessageW(app->mainWindow, WM_CLOSE, 0, 0);
        break;
    case IDM_EDIT_UNDO:
        if (SendMessageW(app->editor, EM_UNDO, 0, 0)) {
            ribbon_set_active_style(app, -1);
        }
        break;
    case IDM_EDIT_REDO:
        if (SendMessageW(app->editor, EM_REDO, 0, 0)) {
            ribbon_set_active_style(app, -1);
        }
        break;
    case IDM_EDIT_CUT:
        SendMessageW(app->editor, WM_CUT, 0, 0);
        break;
    case IDM_EDIT_COPY:
        SendMessageW(app->editor, WM_COPY, 0, 0);
        break;
    case IDM_EDIT_PASTE:
        if (IsClipboardFormatAvailable(RegisterClipboardFormatW(L"Rich Text Format"))) {
            app->richFormattingUsed = TRUE;
        }
        SendMessageW(app->editor, WM_PASTE, 0, 0);
        break;
    case IDM_EDIT_DELETE:
        SendMessageW(app->editor, WM_CLEAR, 0, 0);
        break;
    case IDM_EDIT_SELECT_ALL:
        SendMessageW(app->editor, EM_SETSEL, 0, -1);
        break;
    case IDM_EDIT_FIND:
        dialogs_show_find(app, FALSE);
        break;
    case IDM_EDIT_REPLACE:
        dialogs_show_find(app, TRUE);
        break;
    case IDM_FORMAT_FONT:
        format_choose_font(app);
        break;
    case IDM_FORMAT_COLOR:
    case IDC_TEXT_COLOR:
        format_choose_color(app);
        break;
    case IDM_FORMAT_BOLD:
    case IDC_FORMAT_BOLD:
        format_toggle_character_effect(app, CFM_BOLD, CFE_BOLD);
        break;
    case IDM_FORMAT_ITALIC:
    case IDC_FORMAT_ITALIC:
        format_toggle_character_effect(app, CFM_ITALIC, CFE_ITALIC);
        break;
    case IDM_FORMAT_UNDERLINE:
    case IDC_FORMAT_UNDERLINE:
        format_toggle_character_effect(app, CFM_UNDERLINE, CFE_UNDERLINE);
        break;
    case IDM_FORMAT_STRIKE:
    case IDC_FORMAT_STRIKE:
        format_toggle_character_effect(app, CFM_STRIKEOUT, CFE_STRIKEOUT);
        break;
    case IDM_FORMAT_GROW_FONT:
        format_adjust_font_size(app, 1);
        break;
    case IDM_FORMAT_SHRINK_FONT:
        format_adjust_font_size(app, -1);
        break;
    case IDM_FORMAT_SUBSCRIPT:
        format_toggle_script(app, FALSE);
        break;
    case IDM_FORMAT_SUPERSCRIPT:
        format_toggle_script(app, TRUE);
        break;
    case IDM_FORMAT_HIGHLIGHT:
        format_toggle_highlight(app);
        break;
    case IDM_FORMAT_CLEAR:
        format_clear_formatting(app);
        break;
    case IDM_FORMAT_ALIGN_LEFT:
    case IDC_ALIGN_LEFT:
        format_set_alignment(app, PFA_LEFT);
        break;
    case IDM_FORMAT_ALIGN_CENTER:
    case IDC_ALIGN_CENTER:
        format_set_alignment(app, PFA_CENTER);
        break;
    case IDM_FORMAT_ALIGN_RIGHT:
    case IDC_ALIGN_RIGHT:
        format_set_alignment(app, PFA_RIGHT);
        break;
    case IDM_FORMAT_ALIGN_JUSTIFY:
    case IDC_ALIGN_JUSTIFY:
        format_set_alignment(app, PFA_JUSTIFY);
        break;
    case IDM_FORMAT_BULLETS:
    case IDC_BULLETS:
        format_toggle_bullets(app);
        break;
    case IDM_FORMAT_NUMBERING:
        format_toggle_numbering(app);
        break;
    case IDM_FORMAT_INDENT_INCREASE:
        format_change_indent(app, 360);
        break;
    case IDM_FORMAT_INDENT_DECREASE:
        format_change_indent(app, -360);
        break;
    case IDM_FORMAT_LINE_SPACING:
        format_cycle_line_spacing(app);
        break;
    case IDM_STYLE_NORMAL:
        format_apply_style(app, WORDCRAFT_STYLE_NORMAL);
        break;
    case IDM_STYLE_NO_SPACING:
        format_apply_style(app, WORDCRAFT_STYLE_NO_SPACING);
        break;
    case IDM_STYLE_HEADING_1:
        format_apply_style(app, WORDCRAFT_STYLE_HEADING_1);
        break;
    case IDM_STYLE_HEADING_2:
        format_apply_style(app, WORDCRAFT_STYLE_HEADING_2);
        break;
    case IDM_STYLE_TITLE:
        format_apply_style(app, WORDCRAFT_STYLE_TITLE);
        break;
    case IDM_INSERT_DATETIME:
        dialogs_insert_datetime(app);
        break;
    case IDM_VIEW_WORD_WRAP:
        format_set_word_wrap(app, !app->wordWrap);
        break;
    case IDM_VIEW_STATUS_BAR:
        app->showStatusBar = !app->showStatusBar;
        app_layout(app);
        app_update_status(app, TRUE);
        break;
    case IDM_VIEW_DARK_MODE:
        app->darkMode = !app->darkMode;
        app_apply_theme(app);
        app_update_status(app, FALSE);
        break;
    case IDM_VIEW_ZOOM_50:
        format_set_zoom(app, 50);
        break;
    case IDM_VIEW_ZOOM_75:
        format_set_zoom(app, 75);
        break;
    case IDM_VIEW_ZOOM_100:
        format_set_zoom(app, 100);
        break;
    case IDM_VIEW_ZOOM_125:
        format_set_zoom(app, 125);
        break;
    case IDM_VIEW_ZOOM_150:
        format_set_zoom(app, 150);
        break;
    case IDM_VIEW_ZOOM_200:
        format_set_zoom(app, 200);
        break;
    case IDM_TOOLS_SPELL_CHECK:
        assist_set_spell_check(app, !app->spellCheckEnabled);
        break;
    case IDM_TOOLS_AUTOCOMPLETE:
        assist_set_auto_complete(app, !app->autoCompleteEnabled);
        break;
    case IDM_REVIEW_ADD_COMMENT:
        if (!ribbon_get_comment_text(app, commentText,
                                     ARRAYSIZE(commentText)) ||
            commentText[0] == L'\0') {
            if (comments_begin_draft(app)) {
                if (comments_query_state(
                        app, WCQ_COMMENT_HIGHLIGHT_VISIBLE, 0)) {
                    app_set_status_message(
                        app, L"Selected text highlighted; type your comment, then choose Add Comment.");
                } else {
                    app_set_status_message(
                        app, L"Type a comment, then choose Add Comment.");
                }
            }
            ribbon_focus_comment_editor(app);
        } else if (comments_add(app, commentText)) {
            ribbon_clear_comment_text(app);
        }
        break;
    case IDM_REVIEW_PREVIOUS_COMMENT:
        comments_previous(app);
        break;
    case IDM_REVIEW_NEXT_COMMENT:
        comments_next(app);
        break;
    case IDM_REVIEW_DELETE_COMMENT:
        comments_delete_active(app);
        break;
    case IDM_REVIEW_LIVE_SHARE:
        live_share_show_dialog(app);
        break;
    case IDM_LIVE_START_HOST:
        live_share_start_host_command(app);
        break;
    case IDM_LIVE_JOIN_SESSION:
        live_share_join_command(app);
        break;
    case IDM_LIVE_COPY_INVITATION:
        live_share_copy_invitation_command(app);
        break;
    case IDM_LIVE_LEAVE_SESSION:
        live_share_leave_command(app);
        break;
    case IDM_RIBBON_FOCUS:
        ribbon_focus(app);
        break;
    case IDM_HELP_ABOUT:
        dialogs_show_about(app);
        break;
    default:
        break;
    }
    app_update_command_ui(app);
}

LRESULT CALLBACK main_window_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    AppState *app = (AppState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    if (message == WM_NCCREATE) {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lParam;
        app = (AppState *)create->lpCreateParams;
        app->mainWindow = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)app);
    }
    if (app != NULL && message == app->findMessage) {
        dialogs_handle_find_replace(app, (FINDREPLACEW *)lParam);
        return 0;
    }
    if (app != NULL && message == WCM_ASSIST_RESULT) {
        assist_handle_result(app, lParam);
        return 0;
    }
    if (app != NULL && message == WCM_RENDERER_CHANGED) {
        pageview_mark_dirty(app);
        text_engine_note_layout_change(app);
        return 0;
    }
    if (app != NULL && message == WCM_LIVE_EVENT) {
        live_share_handle_event(app);
        return 0;
    }
    if (app != NULL && message == WCM_QUERY_STATE) {
        if ((UINT)wParam == WCQ_DARK_MODE) {
            return app->darkMode;
        }
        if ((UINT)wParam >= WCQ_SPELL_ERROR_COUNT &&
            (UINT)wParam <= WCQ_SPELL_RESULT_READY) {
            return assist_query_state(app, (UINT)wParam);
        }
        if ((UINT)wParam >= WCQ_SCROLL_ANIMATING &&
            (UINT)wParam <= WCQ_SCROLL_FRAME_INTERVAL_MS) {
            return pageview_query_state(app, (UINT)wParam, lParam);
        }
        if ((UINT)wParam >= WCQ_RIBBON_TAB_COUNT &&
            (UINT)wParam <= WCQ_RIBBON_FOCUS_AREA) {
            return ribbon_query_state(app, (UINT)wParam, lParam);
        }
        if ((UINT)wParam >= WCQ_COMMENT_COUNT &&
            (UINT)wParam <= WCQ_COMMENT_TEXT_HASH) {
            return comments_query_state(app, (UINT)wParam, lParam);
        }
        if ((UINT)wParam >= WCQ_COMMENT_MARGIN_VISIBLE &&
            (UINT)wParam <= WCQ_COMMENT_ACTIVE_CARD_BOTTOM) {
            if (app->paginationDirty || app->pageCount < 1) {
                pageview_paginate(app);
            }
            pageview_sync_to_caret(app, FALSE);
            return comments_query_state(app, (UINT)wParam, lParam);
        }
        if ((UINT)wParam >= WCQ_COMMENT_HIGHLIGHT_VISIBLE &&
            (UINT)wParam <= WCQ_COMMENT_COMPOSITION_ACTIVE) {
            return comments_query_state(app, (UINT)wParam, lParam);
        }
        if ((UINT)wParam >= WCQ_PAGE_MARGIN_THOUSANDTHS &&
            (UINT)wParam <= WCQ_PAGE_LAYOUT_SIZE_PIXELS) {
            pageview_layout(app);
            return pageview_query_state(app, (UINT)wParam, lParam);
        }
        if ((UINT)wParam >= WCQ_TEXT_ENGINE_ENABLED &&
            (UINT)wParam <= WCQ_TEXT_ENGINE_LAYOUT_GENERATION) {
            return text_engine_query_state(app, (UINT)wParam);
        }
        if ((UINT)wParam >= WCQ_RENDER_ENGINE_WINDOWLESS &&
            (UINT)wParam <= WCQ_RENDER_ENGINE_LAST_SELECTION_PAGE) {
            return render_editor_query_state(app->editor, (UINT)wParam);
        }
        if ((UINT)wParam >= WCQ_PAPER_SIZE_ID &&
            (UINT)wParam <= WCQ_PAPER_PRESET_NAME_HASH) {
            return paper_size_query_state(app, (UINT)wParam, lParam);
        }
        if ((UINT)wParam >= WCQ_UI_STYLE_FLAGS &&
            (UINT)wParam <= WCQ_UI_PANEL_CURVE_COUNT) {
            return app_query_minimal_ui(app, (UINT)wParam);
        }
        if ((UINT)wParam >= WCQ_HOME_GROUP_COUNT &&
            (UINT)wParam <= WCQ_RIBBON_FOCUSED_CONTROL_ID) {
            return ribbon_query_state(app, (UINT)wParam, lParam);
        }
        if ((UINT)wParam >= WCQ_LIVE_ROLE &&
            (UINT)wParam <= WCQ_LIVE_DOCUMENT_PENDING) {
            return live_share_query_state(app, (UINT)wParam);
        }
        if (app->paginationDirty || app->pageCount < 1) {
            pageview_paginate(app);
        }
        pageview_sync_to_caret(app, FALSE);
        switch ((UINT)wParam) {
        case WCQ_PAGE_COUNT:
            return app->pageCount;
        case WCQ_CURRENT_PAGE:
            return app->currentPage;
        case WCQ_PAGE_START: {
            LONG pageIndex = (LONG)lParam;
            if (pageIndex < 0 || pageIndex >= app->pageCount) {
                return -1;
            }
            return pageview_page_start(app, pageIndex + 1);
        }
        case WCQ_FIRST_VISIBLE_PAGE:
        case WCQ_LAST_VISIBLE_PAGE:
        case WCQ_VISIBLE_PAGE_COUNT:
        case WCQ_VIEW_SCROLL_Y:
        case WCQ_VIEW_SCROLL_MAX:
        case WCQ_FULLY_VISIBLE_PAGE_COUNT:
            return pageview_query_state(app, (UINT)wParam, lParam);
        default:
            return 0;
        }
    }

    switch (message) {
    case WM_ERASEBKGND:
        if (app != NULL && app->useBrandColors) {
            RECT client;
            GetClientRect(hwnd, &client);
            fill_solid_rect((HDC)wParam, &client,
                            app->palette.workspaceBackground);
            return 1;
        }
        break;
    case WM_CTLCOLORSTATIC:
        if (app != NULL && (HWND)lParam == app->formatBar &&
            app->useBrandColors) {
            HDC dc = (HDC)wParam;
            SetBkColor(dc, app->palette.formatBackground);
            SetDCBrushColor(dc, app->palette.formatBackground);
            return (LRESULT)(HBRUSH)GetStockObject(DC_BRUSH);
        }
        break;
    case WM_DRAWITEM:
        if (app != NULL && lParam != 0 &&
            ribbon_draw_item(app, (const DRAWITEMSTRUCT *)lParam)) {
            return TRUE;
        }
        if (app != NULL && lParam != 0 &&
            ((DRAWITEMSTRUCT *)lParam)->CtlID == IDC_STATUS) {
            DRAWITEMSTRUCT *draw = (DRAWITEMSTRUCT *)lParam;
            int part = (int)draw->itemData;
            RECT textRect = draw->rcItem;
            RECT divider = draw->rcItem;
            HFONT font;
            HGDIOBJ previousFont = NULL;

            fill_solid_rect(draw->hDC, &draw->rcItem,
                            app->palette.statusBackground);
            if (part >= 0 && part < 4) {
                SetBkMode(draw->hDC, TRANSPARENT);
                SetTextColor(draw->hDC, app->palette.statusText);
                font = (HFONT)SendMessageW(app->statusBar, WM_GETFONT, 0, 0);
                if (font != NULL) {
                    previousFont = SelectObject(draw->hDC, font);
                }
                textRect.left += app_scale(hwnd, 7);
                textRect.right -= app_scale(hwnd, 5);
                DrawTextW(draw->hDC, app->statusText[part], -1, &textRect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                              DT_NOPREFIX);
                if (previousFont != NULL) {
                    SelectObject(draw->hDC, previousFont);
                }
            }
            divider.left = divider.right - 1;
            fill_solid_rect(draw->hDC, &divider, app->palette.statusDivider);
            return TRUE;
        }
        break;
    case WM_CREATE:
        startup_splash_set_status(app, L"Loading fonts and page layout...");
        if (!app_create_children(app)) {
            goto startup_failed;
        }
        startup_splash_set_status(app, L"Preparing comments...");
        if (!comments_initialize(app)) {
            goto startup_failed;
        }
        startup_splash_set_status(app, L"Creating your document...");
        if (!document_new(app, FALSE)) {
            goto startup_failed;
        }
        startup_splash_set_status(
            app, L"Starting spelling and autocomplete...");
        if (!assist_initialize(app)) {
            goto startup_failed;
        }
        startup_splash_set_status(app, L"Preparing live sharing...");
        if (!live_share_initialize(app)) {
            app_set_status_message(app, L"Live sharing is unavailable on this computer");
        }
        assist_document_changed(app);
        app_update_command_ui(app);
        SetFocus(app->editor);
        return 0;

startup_failed: {
        DWORD startupError = GetLastError();

        live_share_shutdown(app);
        comments_shutdown(app);
        text_engine_shutdown(app);
        ribbon_free(app);
        SetLastError(startupError != ERROR_SUCCESS
                         ? startupError
                         : ERROR_DLL_INIT_FAILED);
        return -1;
    }
    case WM_SIZE:
        if (app != NULL) {
            app_layout(app);
        }
        return 0;
    case WM_DPICHANGED:
        if (app != NULL) {
            RECT *suggested = (RECT *)lParam;
            SetWindowPos(hwnd, NULL, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
            app_refresh_ui_font(app, HIWORD(wParam));
            app_layout(app);
        }
        return 0;
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
    case WM_THEMECHANGED:
        if (app != NULL) {
            app_apply_theme(app);
        }
        break;
    case WM_FONTCHANGE:
        if (app != NULL && app->fontCombo != NULL) {
            fonts_populate_combo(app->fontCombo, app->mainWindow);
            format_sync_controls(app);
            text_engine_note_layout_change(app);
            pageview_mark_dirty(app);
        }
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lParam)->ptMinTrackSize.x = app_scale(hwnd, 560);
        ((MINMAXINFO *)lParam)->ptMinTrackSize.y = app_scale(hwnd, 420);
        return 0;
    case WM_SETFOCUS:
        if (app != NULL && app->editor != NULL) {
            SetFocus(app->editor);
        }
        return 0;
    case WM_ACTIVATE:
        if (app != NULL && LOWORD(wParam) != WA_INACTIVE) {
            app_update_command_ui(app);
        }
        break;
    case WM_COMMAND:
        if (app == NULL) {
            break;
        }
        if (ribbon_handle_command(app, wParam, lParam)) {
            return 0;
        }
        if ((HWND)lParam == app->editor && HIWORD(wParam) == EN_CHANGE) {
            if (!app->loading) {
                document_mark_modified(app, TRUE);
                app->wordCountDirty = TRUE;
                text_engine_note_layout_change(app);
                pageview_mark_dirty(app);
                assist_schedule(app);
            }
            return 0;
        }
        if (LOWORD(wParam) == IDC_FONT_COMBO && HIWORD(wParam) == CBN_SELENDOK) {
            handle_font_combo(app);
            return 0;
        }
        if (LOWORD(wParam) == IDC_SIZE_COMBO &&
            (HIWORD(wParam) == CBN_SELENDOK || HIWORD(wParam) == CBN_KILLFOCUS)) {
            handle_size_combo(app);
            return 0;
        }
        handle_command(app, LOWORD(wParam));
        return 0;
    case WM_NOTIFY:
        if (app == NULL || lParam == 0) {
            break;
        }
        if (ribbon_handle_notify(app, (const NMHDR *)lParam)) {
            return 0;
        }
        if (((NMHDR *)lParam)->hwndFrom == app->toolbar &&
            ((NMHDR *)lParam)->code == NM_CUSTOMDRAW &&
            app->useBrandColors) {
            NMTBCUSTOMDRAW *draw = (NMTBCUSTOMDRAW *)lParam;
            if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
                fill_solid_rect(draw->nmcd.hdc, &draw->nmcd.rc,
                                app->palette.toolbarBackground);
                return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
            }
            if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                draw->clrText = (draw->nmcd.uItemState & CDIS_DISABLED)
                                    ? app->palette.toolbarDisabledText
                                    : app->palette.toolbarText;
                draw->clrMark = app->palette.toolbarHotBackground;
                draw->clrTextHighlight = app->palette.toolbarHotText;
                draw->clrBtnFace = app->palette.toolbarBackground;
                draw->clrBtnHighlight = app->palette.toolbarBackground;
                draw->clrHighlightHotTrack = app->palette.toolbarHotBackground;
                draw->nStringBkMode = TRANSPARENT;
                draw->nHLStringBkMode = TRANSPARENT;
                return TBCDRF_USECDCOLORS | TBCDRF_HILITEHOTTRACK;
            }
            if (draw->nmcd.dwDrawStage == CDDS_POSTPAINT) {
                draw_toolbar_brand_mark(app, draw->nmcd.hdc);
                return CDRF_DODEFAULT;
            }
        }
        if (((NMHDR *)lParam)->hwndFrom == app->editor &&
            ((NMHDR *)lParam)->code == EN_SELCHANGE) {
            if (!app->loading) {
                ribbon_set_active_style(app, -1);
            }
            app_update_command_ui(app);
            pageview_sync_to_caret(app, TRUE);
            if (!app->loading) {
                assist_selection_changed(app);
            }
            app_update_status(app, FALSE);
            if (!app->loading) {
                comments_selection_changed(app);
            }
            return 0;
        }
        if (((NMHDR *)lParam)->hwndFrom == app->editor &&
            ((NMHDR *)lParam)->code == EN_PAGECHANGE) {
            pageview_sync_to_caret(app, FALSE);
            app_update_status(app, FALSE);
            return 0;
        }
        break;
    case WM_INITMENUPOPUP:
        if (app != NULL) {
            app_update_command_ui(app);
        }
        return 0;
    case WM_TIMER:
        if (app != NULL) {
            if (wParam == STATUS_TIMER_ID) {
                if (pageview_is_scrolling(app)) {
                    SetTimer(hwnd, STATUS_TIMER_ID, 100, NULL);
                } else {
                    KillTimer(hwnd, STATUS_TIMER_ID);
                    app_update_status(app, app->wordCountDirty);
                }
            } else if (wParam == SPELL_TIMER_ID ||
                       wParam == COMPLETION_TIMER_ID) {
                assist_handle_timer(app, (UINT_PTR)wParam);
            } else if (wParam == LIVE_SHARE_TIMER_ID) {
                live_share_handle_timer(app, (UINT_PTR)wParam);
            }
        }
        return 0;
    case WM_DROPFILES:
        if (app != NULL) {
            HDROP drop = (HDROP)wParam;
            WCHAR path[PATH_CAPACITY];
            if (DragQueryFileW(drop, 0, path, ARRAYSIZE(path)) > 0) {
                if (document_open_path(app, path, TRUE) &&
                    live_share_query_state(app, WCQ_LIVE_ROLE) != 0) {
                    live_share_leave_for_document_replacement(app);
                }
            }
            DragFinish(drop);
        }
        return 0;
    case WM_QUERYENDSESSION:
        return app != NULL ? document_prompt_save(app) : TRUE;
    case WM_CLOSE:
        if (app == NULL || document_prompt_save(app)) {
            if (app != NULL) {
                live_share_shutdown(app);
                assist_request_stop(app);
                comments_shutdown(app);
                text_engine_shutdown(app);
                ribbon_free(app);
            }
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        live_share_shutdown(app);
        assist_request_stop(app);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previousInstance,
                    PWSTR commandLine, int showCommand)
{
    INITCOMMONCONTROLSEX controls;
    WNDCLASSEXW windowClass;
    AppState *app;
    HWND window;
    HACCEL accelerators;
    MSG message;
    HICON largeIcon;
    HICON smallIcon;
    WordcraftSplash *splash = NULL;
    HRESULT oleStatus;
    int exitCode = 1;
    int getMessageResult;
    (void)previousInstance;
    (void)commandLine;

    SetProcessDPIAware();
    if (showCommand != SW_HIDE) {
        splash = wordcraft_splash_show(instance, NULL,
                                       L"Starting WordCraft...");
    }
    oleStatus = OleInitialize(NULL);
    if (FAILED(oleStatus)) {
        if (splash != NULL) {
            wordcraft_splash_close(splash, FALSE);
        }
        MessageBoxW(NULL, L"Windows OLE services could not be initialized.",
                    APP_NAME, MB_OK | MB_ICONERROR);
        return 1;
    }
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_WIN95_CLASSES | ICC_BAR_CLASSES |
                     ICC_STANDARD_CLASSES | ICC_TAB_CLASSES;
    if (!InitCommonControlsEx(&controls)) {
        if (splash != NULL) {
            wordcraft_splash_close(splash, FALSE);
        }
        MessageBoxW(NULL, L"Windows common controls could not be initialized.",
                    APP_NAME, MB_OK | MB_ICONERROR);
        OleUninitialize();
        return 1;
    }

    app = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*app));
    if (app == NULL) {
        if (splash != NULL) {
            wordcraft_splash_close(splash, FALSE);
        }
        MessageBoxW(NULL, L"Not enough memory to start WordCraft.",
                    APP_NAME, MB_OK | MB_ICONERROR);
        OleUninitialize();
        return 1;
    }
    app->instance = instance;
    app->startupSplash = splash;
    app->wordWrap = TRUE;
    app->showStatusBar = TRUE;
    app->useBrandColors = query_brand_colors_enabled();
    app->darkMode = FALSE;
    app->spellCheckEnabled = TRUE;
    app->autoCompleteEnabled = TRUE;
    app->pageCount = 1;
    app->currentPage = 1;
    app->paginationDirty = TRUE;
    app->zoomPercent = 100;
    app->pageMargins.left = WORDCRAFT_DEFAULT_PAGE_MARGIN_THOUSANDTHS;
    app->pageMargins.top = WORDCRAFT_DEFAULT_PAGE_MARGIN_THOUSANDTHS;
    app->pageMargins.right = WORDCRAFT_DEFAULT_PAGE_MARGIN_THOUSANDTHS;
    app->pageMargins.bottom = WORDCRAFT_DEFAULT_PAGE_MARGIN_THOUSANDTHS;
    app->pageSize.x = WORDCRAFT_DEFAULT_PAGE_WIDTH_THOUSANDTHS;
    app->pageSize.y = WORDCRAFT_DEFAULT_PAGE_HEIGHT_THOUSANDTHS;
    app->paperSizeId = PAPER_SIZE_LETTER;
    configure_palette(app);
    startup_splash_set_status(app, L"Loading the text renderer...");
    app->findMessage = RegisterWindowMessageW(FINDMSGSTRINGW);
    app->richEditModule = LoadLibraryW(L"Msftedit.dll");
    if (app->richEditModule == NULL) {
        DWORD startupError = GetLastError();

        if (splash != NULL) {
            wordcraft_splash_close(splash, FALSE);
        }
        app_show_error(NULL, L"The Windows Rich Edit component (Msftedit.dll) could not be loaded.",
                       startupError);
        HeapFree(GetProcessHeap(), 0, app);
        OleUninitialize();
        return 1;
    }
    if (!render_editor_register(instance, app->richEditModule)) {
        DWORD startupError = GetLastError();

        if (splash != NULL) {
            wordcraft_splash_close(splash, FALSE);
        }
        app_show_error(NULL,
                       L"WordCraft could not initialize its text renderer.",
                       startupError);
        FreeLibrary(app->richEditModule);
        HeapFree(GetProcessHeap(), 0, app);
        OleUninitialize();
        return 1;
    }

    startup_splash_set_status(app, L"Preparing the WordCraft window...");
    ZeroMemory(&windowClass, sizeof(windowClass));
    largeIcon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_WORDCRAFT),
                                  IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                  GetSystemMetrics(SM_CYICON), LR_SHARED);
    smallIcon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_WORDCRAFT),
                                  IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    if (largeIcon == NULL) {
        largeIcon = LoadIconW(NULL, IDI_APPLICATION);
    }
    if (smallIcon == NULL) {
        smallIcon = LoadIconW(NULL, IDI_APPLICATION);
    }
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = main_window_proc;
    windowClass.hInstance = instance;
    windowClass.hIcon = largeIcon;
    windowClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    windowClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    windowClass.lpszMenuName = MAKEINTRESOURCEW(IDR_MAINMENU);
    windowClass.lpszClassName = APP_CLASS_NAME;
    windowClass.hIconSm = smallIcon;
    if (RegisterClassExW(&windowClass) == 0) {
        DWORD startupError = GetLastError();

        if (splash != NULL) {
            wordcraft_splash_close(splash, FALSE);
        }
        app_show_error(NULL, L"WordCraft could not register its window class.",
                       startupError);
        FreeLibrary(app->richEditModule);
        HeapFree(GetProcessHeap(), 0, app);
        OleUninitialize();
        return 1;
    }

    window = CreateWindowExW(
        0, APP_CLASS_NAME, APP_NAME,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1180, 760,
        NULL, NULL, instance, app);
    if (window == NULL) {
        DWORD startupError = GetLastError();

        if (splash != NULL) {
            wordcraft_splash_close(splash, FALSE);
        }
        app_show_error(NULL, L"WordCraft could not create its main window.",
                       startupError);
        if (app->uiFont != NULL &&
            app->uiFont != GetStockObject(DEFAULT_GUI_FONT)) {
            DeleteObject(app->uiFont);
        }
        FreeLibrary(app->richEditModule);
        HeapFree(GetProcessHeap(), 0, app);
        OleUninitialize();
        return 1;
    }
    accelerators = LoadAcceleratorsW(instance, MAKEINTRESOURCEW(IDR_ACCELERATORS));

    {
        int argumentCount = 0;
        LPWSTR *arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        if (arguments != NULL) {
            if (argumentCount > 1) {
                startup_splash_set_status(app, L"Opening your document...");
                document_open_path(app, arguments[1], FALSE);
            }
            LocalFree(arguments);
        }
    }

    startup_splash_set_status(app, L"Ready");
    startup_splash_test_hold(splash);
    ShowWindow(window, showCommand);
    UpdateWindow(window);
    if (showCommand != SW_HIDE && app->editor != NULL) {
        SetFocus(app->editor);
    }
    if (splash != NULL) {
        wordcraft_splash_close(splash, TRUE);
        splash = NULL;
    }
    app->startupSplash = NULL;

    while ((getMessageResult = GetMessageW(&message, NULL, 0, 0)) > 0) {
        if (app->findDialog != NULL && IsWindow(app->findDialog) &&
            IsDialogMessageW(app->findDialog, &message)) {
            continue;
        }
        if (message.hwnd == app->editor && message.message == WM_KEYDOWN &&
            message.wParam == VK_TAB &&
            (GetKeyState(VK_SHIFT) & 0x8000) == 0 &&
            (GetKeyState(VK_CONTROL) & 0x8000) == 0 &&
            (GetKeyState(VK_MENU) & 0x8000) == 0 &&
            assist_accept_completion(app)) {
            continue;
        }
        if (message.hwnd == app->editor && message.message == WM_KEYDOWN &&
            message.wParam == VK_ESCAPE && assist_has_completion(app)) {
            /* Consume the key before TranslateMessage can enqueue a stray
             * WM_CHAR after dismissing the inline suggestion. */
            assist_clear_completion(app);
            continue;
        }
        if (ribbon_handle_keyboard(app, &message)) {
            continue;
        }
        if (accelerators == NULL ||
            !((message.hwnd == window || IsChild(window, message.hwnd)) &&
              !(message.hwnd == app->sizeCombo ||
                IsChild(app->sizeCombo, message.hwnd)) &&
              TranslateAcceleratorW(window, accelerators, &message))) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (getMessageResult == 0) {
        exitCode = (int)message.wParam;
    }

    live_share_shutdown(app);
    assist_shutdown(app);
    comments_shutdown(app);
    text_engine_shutdown(app);
    ribbon_free(app);
    if (app->uiFont != NULL && app->uiFont != GetStockObject(DEFAULT_GUI_FONT)) {
        DeleteObject(app->uiFont);
    }
    if (app->printerDevMode != NULL) {
        GlobalFree(app->printerDevMode);
    }
    if (app->printerDevNames != NULL) {
        GlobalFree(app->printerDevNames);
    }
    pageview_free(app);
    FreeLibrary(app->richEditModule);
    HeapFree(GetProcessHeap(), 0, app);
    OleUninitialize();
    return exitCode;
}
