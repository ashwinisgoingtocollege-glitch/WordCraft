#include "editor.h"
#include "rendereditor.h"

#include <richole.h>
#include <stdint.h>
#include <stdio.h>

#define RENDERER_PROBE_PARENT_CLASS L"WordCraftRendererProbeParent"
#define RENDERER_PROBE_EDITOR_ID 77

/* Values from textserv.h.  Keeping the probe C-only avoids pulling in the
 * C++ ITextHost declarations merely to inspect these public property bits. */
#define PROBE_TXTBIT_D2DDWRITE 0x01000000UL
#define PROBE_TXTBIT_D2DSIMPLETYPOGRAPHY 0x02000000UL
#define PROBE_TXTBIT_D2DPIXELSNAPPED 0x04000000UL
#define PROBE_TXTBIT_D2DSUBPIXELLINES 0x08000000UL
#define PROBE_TXTBIT_ADVANCEDINPUT 0x20000000UL

#define PROBE_CAPTURE_WIDTH 640
#define PROBE_CAPTURE_HEIGHT 240
#define PROBE_CAPTURE_SENTINEL 0x005A00A5UL

typedef struct ProbeNotifications {
    LONG changes;
    LONG selections;
    LONG pages;
    BOOL headersValid;
} ProbeNotifications;

typedef struct ProbeCapture {
    SIZE_T changedPixels;
    SIZE_T inkPixels;
    DWORD backgroundRgb;
    uint64_t checksum;
    LRESULT printResult;
} ProbeCapture;

static LRESULT CALLBACK probe_parent_proc(HWND window, UINT message,
                                          WPARAM wParam, LPARAM lParam)
{
    ProbeNotifications *notifications =
        (ProbeNotifications *)GetWindowLongPtrW(window, GWLP_USERDATA);

    if (message == WM_NCCREATE) {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lParam;
        notifications = (ProbeNotifications *)create->lpCreateParams;
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          (LONG_PTR)notifications);
    }
    if (notifications != NULL && message == WM_COMMAND &&
        LOWORD(wParam) == RENDERER_PROBE_EDITOR_ID &&
        HIWORD(wParam) == EN_CHANGE) {
        ++notifications->changes;
        if ((HWND)lParam == NULL ||
            GetDlgCtrlID((HWND)lParam) != RENDERER_PROBE_EDITOR_ID) {
            notifications->headersValid = FALSE;
        }
        return 0;
    }
    if (notifications != NULL && message == WM_NOTIFY && lParam != 0) {
        NMHDR *header = (NMHDR *)lParam;
        if ((UINT_PTR)wParam != RENDERER_PROBE_EDITOR_ID ||
            header->hwndFrom == NULL ||
            header->idFrom != RENDERER_PROBE_EDITOR_ID) {
            notifications->headersValid = FALSE;
        }
        if (header->code == EN_SELCHANGE) {
            ++notifications->selections;
        } else if (header->code == EN_PAGECHANGE) {
            ++notifications->pages;
        }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

static void pump_messages(void)
{
    MSG message;
    DWORD end = GetTickCount() + 100;

    do {
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(1);
    } while ((LONG)(GetTickCount() - end) < 0);
}

static BOOL get_editor_text(HWND editor, WCHAR *text, int textCount)
{
    int copied;

    if (text == NULL || textCount <= 0) {
        return FALSE;
    }
    text[0] = L'\0';
    copied = GetWindowTextW(editor, text, textCount);
    return copied >= 0 && copied < textCount;
}

static void get_editor_selection(HWND editor, CHARRANGE *selection)
{
    ZeroMemory(selection, sizeof(*selection));
    SendMessageW(editor, EM_EXGETSEL, 0, (LPARAM)selection);
}

static BOOL environment_forces_gdi(void)
{
    WCHAR value[16];
    DWORD length = GetEnvironmentVariableW(
        L"WORDCRAFT_DISABLE_D2D", value, ARRAYSIZE(value));

    if (length == 0 || length >= ARRAYSIZE(value)) {
        return FALSE;
    }
    return lstrcmpiW(value, L"1") == 0 ||
           lstrcmpiW(value, L"true") == 0 ||
           lstrcmpiW(value, L"yes") == 0 ||
           lstrcmpiW(value, L"on") == 0;
}

static BOOL capture_editor(HWND editor, int width, int height,
                           ProbeCapture *capture)
{
    BITMAPINFO bitmapInfo;
    HDC memoryDc = NULL;
    HBITMAP bitmap = NULL;
    HGDIOBJ oldBitmap = NULL;
    DWORD *pixels = NULL;
    SIZE_T pixelCount;
    SIZE_T index;
    DWORD background;
    uint64_t checksum = UINT64_C(1469598103934665603);
    BOOL success = FALSE;

    if (capture == NULL || width <= 0 || height <= 0) {
        return FALSE;
    }
    ZeroMemory(capture, sizeof(*capture));
    ZeroMemory(&bitmapInfo, sizeof(bitmapInfo));
    bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    memoryDc = CreateCompatibleDC(NULL);
    if (memoryDc == NULL) {
        goto cleanup;
    }
    bitmap = CreateDIBSection(memoryDc, &bitmapInfo, DIB_RGB_COLORS,
                              (void **)&pixels, NULL, 0);
    if (bitmap == NULL || pixels == NULL) {
        goto cleanup;
    }
    oldBitmap = SelectObject(memoryDc, bitmap);
    if (oldBitmap == NULL || oldBitmap == HGDI_ERROR) {
        oldBitmap = NULL;
        goto cleanup;
    }

    pixelCount = (SIZE_T)width * (SIZE_T)height;
    for (index = 0; index < pixelCount; ++index) {
        pixels[index] = PROBE_CAPTURE_SENTINEL;
    }
    capture->printResult = SendMessageW(
        editor, WM_PRINTCLIENT, (WPARAM)memoryDc, PRF_CLIENT);
    GdiFlush();

    background = pixels[pixelCount - 1] & 0x00FFFFFFUL;
    capture->backgroundRgb = background;
    for (index = 0; index < pixelCount; ++index) {
        DWORD rgb = pixels[index] & 0x00FFFFFFUL;
        if (rgb != (PROBE_CAPTURE_SENTINEL & 0x00FFFFFFUL)) {
            ++capture->changedPixels;
        }
        if (rgb != background) {
            ++capture->inkPixels;
        }
        checksum ^= (uint64_t)rgb;
        checksum *= UINT64_C(1099511628211);
    }
    capture->checksum = checksum;
    success = TRUE;

cleanup:
    if (oldBitmap != NULL) {
        SelectObject(memoryDc, oldBitmap);
    }
    if (bitmap != NULL) {
        DeleteObject(bitmap);
    }
    if (memoryDc != NULL) {
        DeleteDC(memoryDc);
    }
    return success;
}

static BOOL probe_default_font(HWND editor)
{
    CHARFORMAT2W format;
    LRESULT mask;

    ZeroMemory(&format, sizeof(format));
    format.cbSize = sizeof(format);
    mask = SendMessageW(editor, EM_GETCHARFORMAT, SCF_DEFAULT,
                        (LPARAM)&format);
    if ((mask & CFM_FACE) == 0 || (mask & CFM_SIZE) == 0 ||
        lstrcmpiW(format.szFaceName, WORDCRAFT_DEFAULT_FONT_FACE) != 0 ||
        format.yHeight != WORDCRAFT_DEFAULT_FONT_SIZE_TWIPS) {
        fwprintf(stderr,
                 L"default font mismatch face='%ls' height=%ld mask=0x%llX\n",
                 format.szFaceName, format.yHeight,
                 (unsigned long long)mask);
        return FALSE;
    }
    return TRUE;
}

static BOOL probe_hit_testing(HWND editor)
{
    POINTL characterPosition;
    POINTL hitPosition;
    LRESULT hitResult;
    LONG hitCharacter;
    const LONG targetCharacter = 3;

    ZeroMemory(&characterPosition, sizeof(characterPosition));
    SendMessageW(editor, EM_POSFROMCHAR, (WPARAM)&characterPosition,
                 targetCharacter);
    hitPosition.x = characterPosition.x + 1;
    hitPosition.y = characterPosition.y + 2;
    hitResult = SendMessageW(editor, EM_CHARFROMPOS, 0,
                             (LPARAM)&hitPosition);
    hitCharacter = (LONG)hitResult;
    if (hitCharacter < targetCharacter - 1 ||
        hitCharacter > targetCharacter + 1) {
        fwprintf(stderr,
                 L"hit testing failed target=%ld position=%ld,%ld hit=%ld\n",
                 targetCharacter, characterPosition.x,
                 characterPosition.y, hitCharacter);
        return FALSE;
    }
    return TRUE;
}

static BOOL probe_typing_and_history(HWND editor, const WCHAR *sample,
                                     ProbeNotifications *notifications)
{
    WCHAR expected[256];
    WCHAR text[256];
    LONG length = (LONG)lstrlenW(sample);
    LONG changesBefore = notifications->changes;

    if (FAILED(StringCchCopyW(expected, ARRAYSIZE(expected), sample)) ||
        FAILED(StringCchCatW(expected, ARRAYSIZE(expected), L"!"))) {
        return FALSE;
    }
    SendMessageW(editor, EM_SETSEL, length, length);
    SendMessageW(editor, WM_SETFOCUS, 0, 0);
    SendMessageW(editor, WM_CHAR, L'!', 1);
    SendMessageW(editor, WM_KILLFOCUS, 0, 0);
    pump_messages();
    if (!get_editor_text(editor, text, ARRAYSIZE(text)) ||
        wcscmp(text, expected) != 0 ||
        SendMessageW(editor, EM_CANUNDO, 0, 0) == 0 ||
        notifications->changes <= changesBefore) {
        fwprintf(stderr,
                 L"typing failed text='%ls' canUndo=%lld changes=%ld:%ld\n",
                 text,
                 (long long)SendMessageW(editor, EM_CANUNDO, 0, 0),
                 changesBefore, notifications->changes);
        return FALSE;
    }

    if (SendMessageW(editor, EM_UNDO, 0, 0) == 0 ||
        !get_editor_text(editor, text, ARRAYSIZE(text)) ||
        wcscmp(text, sample) != 0 ||
        SendMessageW(editor, EM_CANREDO, 0, 0) == 0) {
        fwprintf(stderr,
                 L"undo failed text='%ls' canRedo=%lld\n", text,
                 (long long)SendMessageW(editor, EM_CANREDO, 0, 0));
        return FALSE;
    }
    if (SendMessageW(editor, EM_REDO, 0, 0) == 0 ||
        !get_editor_text(editor, text, ARRAYSIZE(text)) ||
        wcscmp(text, expected) != 0) {
        fwprintf(stderr, L"redo failed text='%ls'\n", text);
        return FALSE;
    }
    return TRUE;
}

static BOOL probe_rendering(HWND editor, int backend,
                            ProbeNotifications *notifications,
                            ProbeCapture *lastCapture)
{
    ProbeCapture first;
    ProbeCapture second;
    WCHAR beforeText[256];
    WCHAR afterText[256];
    CHARRANGE beforeSelection;
    CHARRANGE afterSelection;
    LRESULT beforeD2d;
    LRESULT beforeGdi;
    LRESULT afterD2d;
    LRESULT afterGdi;
    LRESULT lastPath;
    BOOL beforeModified;
    LRESULT beforeCanUndo;
    LRESULT beforeCanRedo;
    LONG changesBefore;
    LONG selectionsBefore;
    const SIZE_T pixelCount =
        (SIZE_T)PROBE_CAPTURE_WIDTH * PROBE_CAPTURE_HEIGHT;

    SetWindowPos(editor, NULL, 0, 0, PROBE_CAPTURE_WIDTH,
                 PROBE_CAPTURE_HEIGHT,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    SendMessageW(editor, EM_SETBKGNDCOLOR, FALSE, RGB(250, 250, 250));
    pump_messages();

    if (!get_editor_text(editor, beforeText, ARRAYSIZE(beforeText))) {
        return FALSE;
    }
    get_editor_selection(editor, &beforeSelection);
    beforeModified = (BOOL)SendMessageW(editor, EM_GETMODIFY, 0, 0);
    beforeCanUndo = SendMessageW(editor, EM_CANUNDO, 0, 0);
    beforeCanRedo = SendMessageW(editor, EM_CANREDO, 0, 0);
    changesBefore = notifications->changes;
    selectionsBefore = notifications->selections;
    beforeD2d = render_editor_query_state(
        editor, WCQ_RENDER_ENGINE_D2D_DRAW_COUNT);
    beforeGdi = render_editor_query_state(
        editor, WCQ_RENDER_ENGINE_GDI_DRAW_COUNT);

    if (!capture_editor(editor, PROBE_CAPTURE_WIDTH,
                        PROBE_CAPTURE_HEIGHT, &first) ||
        !capture_editor(editor, PROBE_CAPTURE_WIDTH,
                        PROBE_CAPTURE_HEIGHT, &second)) {
        fwprintf(stderr, L"could not capture WM_PRINTCLIENT output\n");
        return FALSE;
    }
    afterD2d = render_editor_query_state(
        editor, WCQ_RENDER_ENGINE_D2D_DRAW_COUNT);
    afterGdi = render_editor_query_state(
        editor, WCQ_RENDER_ENGINE_GDI_DRAW_COUNT);
    lastPath = render_editor_query_state(
        editor, WCQ_RENDER_ENGINE_LAST_DRAW_PATH);

    if (first.printResult == 0 || second.printResult == 0 ||
        first.changedPixels < pixelCount * 9 / 10 ||
        second.changedPixels < pixelCount * 9 / 10 ||
        first.inkPixels < 20 || second.inkPixels < 20 ||
        FAILED((HRESULT)render_editor_query_state(
            editor, WCQ_RENDER_ENGINE_LAST_DRAW_RESULT))) {
        fwprintf(stderr,
                 L"rendered bitmap was blank print=%lld:%lld changed=%zu:%zu ink=%zu:%zu bg=0x%06lX:0x%06lX hr=0x%08lX\n",
                 (long long)first.printResult,
                 (long long)second.printResult,
                 first.changedPixels, second.changedPixels,
                 first.inkPixels, second.inkPixels,
                 first.backgroundRgb, second.backgroundRgb,
                 (unsigned long)(HRESULT)render_editor_query_state(
                     editor, WCQ_RENDER_ENGINE_LAST_DRAW_RESULT));
        return FALSE;
    }
    if (backend == RENDER_ENGINE_BACKEND_DIRECTWRITE) {
        if (afterD2d < beforeD2d + 2 || afterGdi != beforeGdi ||
            lastPath != RENDER_DRAW_PATH_DIRECTWRITE) {
            fwprintf(stderr,
                     L"DirectWrite draw path was not used d2d=%lld:%lld gdi=%lld:%lld path=%lld\n",
                     (long long)beforeD2d, (long long)afterD2d,
                     (long long)beforeGdi, (long long)afterGdi,
                     (long long)lastPath);
            return FALSE;
        }
    } else if (backend == RENDER_ENGINE_BACKEND_GDI) {
        if (afterGdi < beforeGdi + 2 || afterD2d != beforeD2d ||
            lastPath != RENDER_DRAW_PATH_GDI) {
            fwprintf(stderr,
                     L"GDI draw path was not used d2d=%lld:%lld gdi=%lld:%lld path=%lld\n",
                     (long long)beforeD2d, (long long)afterD2d,
                     (long long)beforeGdi, (long long)afterGdi,
                     (long long)lastPath);
            return FALSE;
        }
    }

    if (!get_editor_text(editor, afterText, ARRAYSIZE(afterText))) {
        return FALSE;
    }
    get_editor_selection(editor, &afterSelection);
    if (wcscmp(beforeText, afterText) != 0 ||
        beforeSelection.cpMin != afterSelection.cpMin ||
        beforeSelection.cpMax != afterSelection.cpMax ||
        beforeModified != (BOOL)SendMessageW(editor, EM_GETMODIFY, 0, 0) ||
        beforeCanUndo != SendMessageW(editor, EM_CANUNDO, 0, 0) ||
        beforeCanRedo != SendMessageW(editor, EM_CANREDO, 0, 0) ||
        changesBefore != notifications->changes ||
        selectionsBefore != notifications->selections) {
        fwprintf(stderr,
                 L"painting changed editor state text=%d sel=%ld:%ld->%ld:%ld modify=%d:%d undo=%lld:%lld redo=%lld:%lld notifications=%ld:%ld/%ld:%ld\n",
                 wcscmp(beforeText, afterText),
                 beforeSelection.cpMin, beforeSelection.cpMax,
                 afterSelection.cpMin, afterSelection.cpMax,
                 beforeModified,
                 (BOOL)SendMessageW(editor, EM_GETMODIFY, 0, 0),
                 (long long)beforeCanUndo,
                 (long long)SendMessageW(editor, EM_CANUNDO, 0, 0),
                 (long long)beforeCanRedo,
                 (long long)SendMessageW(editor, EM_CANREDO, 0, 0),
                 changesBefore, notifications->changes,
                 selectionsBefore, notifications->selections);
        return FALSE;
    }
    *lastCapture = second;
    return TRUE;
}

static BOOL probe_wrapping(HWND editor, LONG *narrowLinesOut,
                           LONG *wideLinesOut)
{
    static const WCHAR wrappingText[] =
        L"The quick brown fox jumps over the lazy dog while WordCraft "
        L"shapes and wraps every word into readable lines. The quick brown "
        L"fox jumps over the lazy dog while WordCraft shapes and wraps "
        L"every word into readable lines.";
    LONG narrowLines;
    LONG wideLines;
    POINTL forceLayout;

    SendMessageW(editor, EM_SETVIEWKIND, VM_NORMAL, 0);
    SendMessageW(editor, EM_SETTARGETDEVICE, 0, 0);
    SetWindowTextW(editor, wrappingText);
    SetWindowPos(editor, NULL, 0, 0, 170, 220,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    ZeroMemory(&forceLayout, sizeof(forceLayout));
    SendMessageW(editor, EM_POSFROMCHAR, (WPARAM)&forceLayout,
                 lstrlenW(wrappingText) - 1);
    narrowLines = (LONG)SendMessageW(editor, EM_GETLINECOUNT, 0, 0);

    SetWindowPos(editor, NULL, 0, 0, 640, 220,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    ZeroMemory(&forceLayout, sizeof(forceLayout));
    SendMessageW(editor, EM_POSFROMCHAR, (WPARAM)&forceLayout,
                 lstrlenW(wrappingText) - 1);
    wideLines = (LONG)SendMessageW(editor, EM_GETLINECOUNT, 0, 0);

    if (narrowLines < 3 || wideLines < 1 || narrowLines <= wideLines) {
        fwprintf(stderr,
                 L"word wrapping failed narrow=%ld wide=%ld\n",
                 narrowLines, wideLines);
        return FALSE;
    }
    *narrowLinesOut = narrowLines;
    *wideLinesOut = wideLines;
    return TRUE;
}

static BOOL probe_format_range(HWND editor, LONG textLength,
                               LONG *formattedThroughOut)
{
    FORMATRANGE range;
    HDC dc = GetDC(NULL);
    LRESULT formattedThrough;

    if (dc == NULL) {
        fwprintf(stderr, L"could not acquire the format-range DC\n");
        return FALSE;
    }
    ZeroMemory(&range, sizeof(range));
    range.hdc = dc;
    range.hdcTarget = dc;
    range.rcPage.left = 0;
    range.rcPage.top = 0;
    range.rcPage.right = 12240;
    range.rcPage.bottom = 15840;
    range.rc.left = 1440;
    range.rc.top = 1440;
    range.rc.right = 10800;
    range.rc.bottom = 14400;
    range.chrg.cpMin = 0;
    range.chrg.cpMax = -1;
    formattedThrough = SendMessageW(
        editor, EM_FORMATRANGE, FALSE, (LPARAM)&range);
    SendMessageW(editor, EM_FORMATRANGE, FALSE, 0);
    ReleaseDC(NULL, dc);

    if (formattedThrough <= 0 || formattedThrough >= textLength) {
        fwprintf(stderr,
                 L"EM_FORMATRANGE failed result=%lld length=%ld\n",
                 (long long)formattedThrough, textLength);
        return FALSE;
    }
    *formattedThroughOut = (LONG)formattedThrough;
    return TRUE;
}

int wmain(void)
{
    static const WCHAR sample[] =
        L"office caf\x00E9 \xD83D\xDE00 "
        L"\x0627\x0644\x0639\x0631\x0628\x064A\x0629";
    WNDCLASSEXW windowClass;
    ProbeNotifications notifications;
    ProbeCapture capture;
    HINSTANCE instance = GetModuleHandleW(NULL);
    HMODULE richEditModule = NULL;
    HWND parent = NULL;
    HWND editor = NULL;
    IRichEditOle *richEditOle = NULL;
    WCHAR text[256];
    CHARRANGE selection;
    LRESULT mask;
    LRESULT backend;
    LRESULT d2dCapable;
    DWORD properties;
    LRESULT fallbackReason;
    LONG narrowLines = 0;
    LONG wideLines = 0;
    LONG formattedThrough = 0;
    HRESULT oleStatus;
    BOOL oleInitialized = FALSE;
    int result = 1;

    ZeroMemory(&notifications, sizeof(notifications));
    notifications.headersValid = TRUE;
    ZeroMemory(&capture, sizeof(capture));
    oleStatus = OleInitialize(NULL);
    if (FAILED(oleStatus)) {
        fwprintf(stderr, L"could not initialize OLE for the focus probe\n");
        goto cleanup;
    }
    oleInitialized = TRUE;
    richEditModule = LoadLibraryW(L"Msftedit.dll");
    if (richEditModule == NULL ||
        !render_editor_register(instance, richEditModule)) {
        fwprintf(stderr, L"could not initialize the renderer host\n");
        goto cleanup;
    }

    ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = probe_parent_proc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = RENDERER_PROBE_PARENT_CLASS;
    if (RegisterClassExW(&windowClass) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        fwprintf(stderr, L"could not register the renderer probe parent\n");
        goto cleanup;
    }
    parent = CreateWindowExW(0, RENDERER_PROBE_PARENT_CLASS, L"",
                             WS_OVERLAPPED, 0, 0, 680, 320, NULL, NULL,
                             instance, &notifications);
    if (parent == NULL) {
        fwprintf(stderr, L"could not create the renderer probe parent\n");
        goto cleanup;
    }
    editor = CreateWindowExW(
        0, WORDCRAFT_RENDER_EDITOR_CLASS, L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL |
            ES_WANTRETURN | ES_NOHIDESEL | ES_SAVESEL,
        0, 0, PROBE_CAPTURE_WIDTH, PROBE_CAPTURE_HEIGHT, parent,
        (HMENU)(INT_PTR)RENDERER_PROBE_EDITOR_ID, instance, NULL);
    if (editor == NULL || !render_editor_is_window(editor)) {
        fwprintf(stderr,
                 L"could not create the windowless renderer control (%lu)\n",
                 GetLastError());
        goto cleanup;
    }

    mask = SendMessageW(editor, EM_GETEVENTMASK, 0, 0);
    SendMessageW(editor, EM_SETEVENTMASK, 0,
                 mask | ENM_CHANGE | ENM_SELCHANGE | ENM_PAGECHANGE);
    backend = render_editor_query_state(
        editor, WCQ_RENDER_ENGINE_BACKEND);
    d2dCapable = render_editor_query_state(
        editor, WCQ_RENDER_ENGINE_D2D_CAPABLE);
    properties = (DWORD)render_editor_query_state(
        editor, WCQ_RENDER_ENGINE_PROPERTY_BITS);
    fallbackReason = render_editor_query_state(
        editor, WCQ_RENDER_ENGINE_FALLBACK_REASON);
    if ((properties & PROBE_TXTBIT_ADVANCEDINPUT) != 0 ||
        render_editor_query_state(
            editor, WCQ_RENDER_ENGINE_WINDOWLESS) == 0 ||
        (backend != RENDER_ENGINE_BACKEND_GDI &&
         backend != RENDER_ENGINE_BACKEND_DIRECTWRITE)) {
        fwprintf(stderr,
                 L"invalid renderer state windowless=%lld backend=%lld\n",
                 (long long)render_editor_query_state(
                     editor, WCQ_RENDER_ENGINE_WINDOWLESS),
                 (long long)backend);
        goto cleanup;
    }

    /* Exercise the real focus transition with OLE/TSF initialized.  Merely
     * sending WM_SETFOCUS misses CoreText registration failures that occur
     * when Windows changes the focused HWND. */
    SetFocus(editor);
    if (GetFocus() != editor) {
        /* A hidden console probe is not always eligible for system focus, so
         * drive the identical windowless text-services transition directly. */
        SendMessageW(editor, WM_SETFOCUS, 0, 0);
    }
    pump_messages();
    if (backend == RENDER_ENGINE_BACKEND_DIRECTWRITE) {
        if (d2dCapable == 0 ||
            (properties & PROBE_TXTBIT_D2DDWRITE) == 0 ||
            (properties & PROBE_TXTBIT_D2DSUBPIXELLINES) == 0 ||
            (properties & PROBE_TXTBIT_D2DSIMPLETYPOGRAPHY) != 0 ||
            (properties & PROBE_TXTBIT_D2DPIXELSNAPPED) != 0 ||
            fallbackReason != RENDER_FALLBACK_NONE) {
            fwprintf(stderr,
                     L"invalid DirectWrite state capable=%lld properties=0x%08lX fallback=%lld\n",
                     (long long)d2dCapable,
                     (unsigned long)properties,
                     (long long)fallbackReason);
            goto cleanup;
        }
    } else {
        if (d2dCapable != 0 ||
            (properties & (PROBE_TXTBIT_D2DDWRITE |
                           PROBE_TXTBIT_D2DSIMPLETYPOGRAPHY |
                           PROBE_TXTBIT_D2DPIXELSNAPPED |
                           PROBE_TXTBIT_D2DSUBPIXELLINES)) != 0 ||
            fallbackReason == RENDER_FALLBACK_NONE ||
            (environment_forces_gdi() &&
             fallbackReason != RENDER_FALLBACK_FORCED)) {
            fwprintf(stderr,
                     L"invalid GDI fallback state capable=%lld properties=0x%08lX fallback=%lld forced=%d\n",
                     (long long)d2dCapable,
                     (unsigned long)properties,
                     (long long)fallbackReason,
                     environment_forces_gdi());
            goto cleanup;
        }
    }
    if (!probe_default_font(editor)) {
        goto cleanup;
    }

    SendMessageW(editor, EM_SETVIEWKIND, VM_NORMAL, 0);
    if (!SetWindowTextW(editor, sample)) {
        fwprintf(stderr, L"WM_SETTEXT failed\n");
        goto cleanup;
    }
    SendMessageW(editor, EM_SETSEL, 2, 8);
    pump_messages();

    ZeroMemory(text, sizeof(text));
    get_editor_selection(editor, &selection);
    GetWindowTextW(editor, text, ARRAYSIZE(text));
    SendMessageW(editor, EM_GETOLEINTERFACE, 0, (LPARAM)&richEditOle);
    if (wcscmp(text, sample) != 0 || selection.cpMin != 2 ||
        selection.cpMax != 8 || notifications.changes < 1 ||
        notifications.selections < 1 || !notifications.headersValid ||
        richEditOle == NULL) {
        fwprintf(stderr,
                 L"renderer message parity failed text='%ls' sel=%ld:%ld change=%ld selection=%ld ole=%d\n",
                 text, selection.cpMin, selection.cpMax,
                 notifications.changes, notifications.selections,
                 richEditOle != NULL);
        goto cleanup;
    }
    if (!probe_hit_testing(editor) ||
        !probe_typing_and_history(editor, sample, &notifications) ||
        !probe_rendering(editor, (int)backend, &notifications, &capture) ||
        !probe_wrapping(editor, &narrowLines, &wideLines)) {
        goto cleanup;
    }

    {
        const LONG largeLength = 30000;
        WCHAR *largeText = (WCHAR *)HeapAlloc(
            GetProcessHeap(), 0,
            ((SIZE_T)largeLength + 1) * sizeof(*largeText));
        LONG index;
        LONG actualLength;
        if (largeText == NULL) {
            fwprintf(stderr, L"could not allocate pagination text\n");
            goto cleanup;
        }
        for (index = 0; index < largeLength; ++index) {
            largeText[index] = index % 80 == 79 ? L'\r'
                                                  : L'a' + index % 20;
        }
        largeText[largeLength] = L'\0';
        SetWindowTextW(editor, largeText);
        HeapFree(GetProcessHeap(), 0, largeText);
        actualLength = GetWindowTextLengthW(editor);
        if (actualLength < 25000 ||
            !probe_format_range(editor, actualLength,
                                &formattedThrough)) {
            goto cleanup;
        }

        SendMessageW(editor, EM_SETVIEWKIND, VM_PAGE, 0);
        if (SendMessageW(editor, EM_GETVIEWKIND, 0, 0) != VM_PAGE) {
            fwprintf(stderr,
                     L"windowless RichEdit did not enter page view\n");
            goto cleanup;
        }
        SendMessageW(editor, EM_SETSEL, 21672, 21672);
        get_editor_selection(editor, &selection);
        if (selection.cpMin != 21672 || selection.cpMax != 21672) {
            fwprintf(stderr,
                     L"page-view selection was clamped (%ld..%ld)\n",
                     selection.cpMin, selection.cpMax);
            goto cleanup;
        }
    }

    wprintf(L"windowless=ok backend=%lld d2d_capable=%lld properties=0x%08lX draw_path=%lld pixels=%zu ink=%zu unicode=ok font=Times_New_Roman_12pt typing=ok undo_redo=ok hit_test=ok wrap=%ld:%ld format_range=%ld notifications=ok ole=ok nondestructive_paint=ok\n",
            (long long)backend, (long long)d2dCapable,
            (unsigned long)properties,
            (long long)render_editor_query_state(
                editor, WCQ_RENDER_ENGINE_LAST_DRAW_PATH),
            capture.changedPixels, capture.inkPixels,
            narrowLines, wideLines, formattedThrough);
    result = 0;

cleanup:
    if (richEditOle != NULL) {
        richEditOle->lpVtbl->Release(richEditOle);
    }
    if (editor != NULL) {
        DestroyWindow(editor);
    }
    if (parent != NULL) {
        DestroyWindow(parent);
    }
    if (richEditModule != NULL) {
        FreeLibrary(richEditModule);
    }
    if (oleInitialized) {
        OleUninitialize();
    }
    return result;
}
