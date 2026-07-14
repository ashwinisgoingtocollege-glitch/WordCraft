#ifndef COBJMACROS
#define COBJMACROS
#endif

#include "editor.h"

#include <limits.h>
#include <richole.h>
#include <stdlib.h>
#include <tom.h>

#define PAGEVIEW_CLASS_NAME L"WordCraftPageView"
#define PAGEVIEW_CONTROL_ID 2020
#define PAGEVIEW_EDITOR_SUBCLASS_ID ((UINT_PTR)0x50414745)
#define PAGEVIEW_DEFAULT_WIDTH 8500
#define PAGEVIEW_DEFAULT_HEIGHT 11000
#define PAGEVIEW_MIN_PRINTABLE_TWIPS 144
#define PAGEVIEW_PREVIEW_CACHE_CAPACITY 16

static const IID wordcraftPageviewIidTextDocument = {
    0x8CC497C0, 0xA1DF, 0x11CE,
    {0x80, 0x98, 0x00, 0xAA, 0x00, 0x47, 0xBE, 0x5D}
};

typedef struct PagePreviewCacheEntry {
    LONG page;
    HENHMETAFILE metafile;
    ULONGLONG lastUse;
} PagePreviewCacheEntry;

typedef struct PageViewState {
    AppState *app;
    RECT pageRect;
    LONG visiblePage;
    int wheelRemainder;
    BOOL layingOut;
    BOOL switchingPage;
    BOOL paintingPages;
    int pageWidth;
    int pageHeight;
    int pageGap;
    int pagePitch;
    int outerGutter;
    int pageLeft;
    int firstPageTop;
    int canvasWidth;
    int canvasHeight;
    HWND configuredEditor;
    int configuredWidth;
    int configuredHeight;
    int configuredMarginLeft;
    int configuredMarginTop;
    int configuredMarginRight;
    int configuredMarginBottom;
    int configuredZoom;
    LONG configuredPrintableWidth;
    PagePreviewCacheEntry previews[PAGEVIEW_PREVIEW_CACHE_CAPACITY];
    ULONGLONG previewClock;
} PageViewState;

static LRESULT CALLBACK pageview_window_proc(HWND hwnd, UINT message,
                                              WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK pageview_editor_subclass_proc(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR subclassId, DWORD_PTR referenceData);

static PageViewState *pageview_get_state(HWND hwnd)
{
    return (PageViewState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
}

static int pageview_clamp_int(int value, int minimum, int maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static LONG pageview_clamp_long(LONG value, LONG minimum, LONG maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static int pageview_saturate_int64(LONGLONG value)
{
    if (value < INT_MIN) {
        return INT_MIN;
    }
    if (value > INT_MAX) {
        return INT_MAX;
    }
    return (int)value;
}

static void pageview_clear_preview_cache(PageViewState *state)
{
    SIZE_T index;

    if (state == NULL) {
        return;
    }
    for (index = 0; index < ARRAYSIZE(state->previews); ++index) {
        if (state->previews[index].metafile != NULL) {
            DeleteEnhMetaFile(state->previews[index].metafile);
            state->previews[index].metafile = NULL;
        }
        state->previews[index].page = 0;
        state->previews[index].lastUse = 0;
    }
    state->previewClock = 0;
}

static BOOL pageview_page_rect(const PageViewState *state, LONG page,
                               RECT *rect)
{
    LONGLONG top;

    if (state == NULL || state->app == NULL || rect == NULL || page < 1 ||
        page > state->app->pageCount || state->pageWidth <= 0 ||
        state->pageHeight <= 0 || state->pagePitch <= 0) {
        return FALSE;
    }
    top = (LONGLONG)state->firstPageTop +
          (LONGLONG)(page - 1) * state->pagePitch;
    if (top < INT_MIN || top > INT_MAX - state->pageHeight) {
        return FALSE;
    }
    SetRect(rect, state->pageLeft, (int)top,
            state->pageLeft + state->pageWidth,
            (int)top + state->pageHeight);
    return TRUE;
}

static void pageview_visible_page_range(const PageViewState *state,
                                        HWND hwnd, LONG *firstPage,
                                        LONG *lastPage,
                                        LONG *fullyVisibleCount)
{
    RECT client;
    LONGLONG relative;
    LONGLONG startIndex;
    LONG page;
    LONG first = 0;
    LONG last = 0;
    LONG full = 0;

    if (firstPage != NULL) {
        *firstPage = 0;
    }
    if (lastPage != NULL) {
        *lastPage = 0;
    }
    if (fullyVisibleCount != NULL) {
        *fullyVisibleCount = 0;
    }
    if (state == NULL || state->app == NULL ||
        state->app->pageCount <= 0 || state->pagePitch <= 0) {
        return;
    }
    GetClientRect(hwnd, &client);
    relative = (LONGLONG)client.top - state->firstPageTop;
    startIndex = relative > 0 ? relative / state->pagePitch : 0;
    if (startIndex > 0) {
        --startIndex;
    }
    if (startIndex >= state->app->pageCount) {
        startIndex = state->app->pageCount - 1;
    }
    for (page = (LONG)startIndex + 1; page <= state->app->pageCount;
         ++page) {
        LONGLONG top = (LONGLONG)state->firstPageTop +
                       (LONGLONG)(page - 1) * state->pagePitch;
        LONGLONG bottom = top + state->pageHeight;

        if (top >= client.bottom) {
            break;
        }
        if (bottom <= client.top) {
            continue;
        }
        if (first == 0) {
            first = page;
        }
        last = page;
        if (top >= client.top && bottom <= client.bottom) {
            ++full;
        }
    }
    if (firstPage != NULL) {
        *firstPage = first;
    }
    if (lastPage != NULL) {
        *lastPage = last;
    }
    if (fullyVisibleCount != NULL) {
        *fullyVisibleCount = full;
    }
}

static void pageview_get_dpi(HWND hwnd, int *dpiX, int *dpiY)
{
    int dpi = app_scale(hwnd, 96);

    if (dpi <= 0) {
        dpi = 96;
    }
    *dpiX = dpi;
    *dpiY = dpi;
}

static int pageview_thousandths_to_pixels(LONG value, int dpi, int zoom)
{
    LONGLONG numerator;
    LONGLONG result;

    if (value <= 0 || dpi <= 0 || zoom <= 0) {
        return 0;
    }
    numerator = (LONGLONG)value * dpi * zoom;
    result = (numerator + 50000) / 100000;
    /* Leave headroom for gutters, scroll ranges, and RECT arithmetic. */
    if (result > INT_MAX / 4) {
        return INT_MAX / 4;
    }
    return (int)result;
}

static LONG pageview_thousandths_to_twips(LONG value)
{
    if (value <= 0) {
        return 0;
    }
    return MulDiv(value, 1440, 1000);
}

static void pageview_get_page_units(const AppState *app, LONG *width,
                                    LONG *height)
{
    *width = app->pageSize.x > 0 ? app->pageSize.x : PAGEVIEW_DEFAULT_WIDTH;
    *height = app->pageSize.y > 0 ? app->pageSize.y : PAGEVIEW_DEFAULT_HEIGHT;
}

static int pageview_scroll_position(HWND hwnd, int bar)
{
    SCROLLINFO info;

    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = SIF_POS;
    if (!GetScrollInfo(hwnd, bar, &info)) {
        return 0;
    }
    return info.nPos;
}

static int pageview_scroll_maximum(HWND hwnd, int bar)
{
    SCROLLINFO info;
    int maximum;

    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE;
    if (!GetScrollInfo(hwnd, bar, &info)) {
        return 0;
    }
    maximum = info.nMax - (int)info.nPage + 1;
    return maximum > info.nMin ? maximum : info.nMin;
}

static BOOL pageview_set_scroll_position(HWND hwnd, int bar, int position)
{
    SCROLLINFO info;
    int oldPosition;
    int maximum;

    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = SIF_ALL;
    if (!GetScrollInfo(hwnd, bar, &info)) {
        return FALSE;
    }
    oldPosition = info.nPos;
    maximum = info.nMax - (int)info.nPage + 1;
    if (maximum < info.nMin) {
        maximum = info.nMin;
    }
    info.fMask = SIF_POS;
    info.nPos = pageview_clamp_int(position, info.nMin, maximum);
    SetScrollInfo(hwnd, bar, &info, TRUE);
    return info.nPos != oldPosition;
}

static BOOL pageview_scroll_by(HWND hwnd, int bar, int distance)
{
    int position = pageview_scroll_position(hwnd, bar);
    return pageview_set_scroll_position(hwnd, bar, position + distance);
}

static void pageview_set_scroll_extent(HWND hwnd, int bar, int canvas,
                                       int viewport)
{
    SCROLLINFO info;
    int oldPosition;

    oldPosition = pageview_scroll_position(hwnd, bar);
    if (canvas < 1) {
        canvas = 1;
    }
    if (viewport < 1) {
        viewport = 1;
    }
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    info.nMin = 0;
    info.nMax = canvas - 1;
    info.nPage = (UINT)viewport;
    info.nPos = oldPosition;
    SetScrollInfo(hwnd, bar, &info, TRUE);
}

static void pageview_fill_rect(HDC dc, const RECT *rect, COLORREF color)
{
    COLORREF previous = SetDCBrushColor(dc, color);
    FillRect(dc, rect, (HBRUSH)GetStockObject(DC_BRUSH));
    if (previous != CLR_INVALID) {
        SetDCBrushColor(dc, previous);
    }
}

static LONG pageview_metafile_frame_extent(HDC dc, int logicalPixels,
                                            int millimeterCapability,
                                            int pixelCapability)
{
    int millimeters;
    int pixels;
    LONGLONG extent;

    if (dc == NULL || logicalPixels <= 0) {
        return 0;
    }
    millimeters = GetDeviceCaps(dc, millimeterCapability);
    pixels = GetDeviceCaps(dc, pixelCapability);
    if (millimeters <= 0 || pixels <= 0) {
        return 0;
    }
    /* Enhanced-metafile frames use physical .01 mm units. Convert the
     * logical-DPI page pixels through the reference device's physical size
     * so playback is 1:1 with RichEdit on high-DPI displays. */
    extent = ((LONGLONG)logicalPixels * millimeters * 100 + pixels / 2) /
             pixels;
    return extent > 0 && extent <= LONG_MAX ? (LONG)extent : 0;
}

static BOOL pageview_prepare_format_range(AppState *app, HDC output,
                                          HDC target, LONG page,
                                          FORMATRANGE *formatRange,
                                          LONG *rangeStart,
                                          LONG *rangeEnd)
{
    LONG pageWidthUnits;
    LONG pageHeightUnits;
    LONG pageWidth;
    LONG pageHeight;

    if (app == NULL || app->pageEnds == NULL || page < 1 ||
        page > app->pageCount || formatRange == NULL) {
        return FALSE;
    }
    pageview_get_page_units(app, &pageWidthUnits, &pageHeightUnits);
    pageWidth = pageview_thousandths_to_twips(pageWidthUnits);
    pageHeight = pageview_thousandths_to_twips(pageHeightUnits);
    if (pageWidth <= 0 || pageHeight <= 0) {
        return FALSE;
    }
    ZeroMemory(formatRange, sizeof(*formatRange));
    formatRange->hdc = output;
    formatRange->hdcTarget = target;
    formatRange->rcPage.left = 0;
    formatRange->rcPage.top = 0;
    formatRange->rcPage.right = pageWidth;
    formatRange->rcPage.bottom = pageHeight;
    formatRange->rc.left = pageview_thousandths_to_twips(
        app->pageMargins.left);
    formatRange->rc.top = pageview_thousandths_to_twips(
        app->pageMargins.top);
    formatRange->rc.right = pageWidth - pageview_thousandths_to_twips(
                                           app->pageMargins.right);
    formatRange->rc.bottom = pageHeight - pageview_thousandths_to_twips(
                                             app->pageMargins.bottom);
    formatRange->rc.left = max(0, formatRange->rc.left);
    formatRange->rc.top = max(0, formatRange->rc.top);
    formatRange->rc.right = min(pageWidth, formatRange->rc.right);
    formatRange->rc.bottom = min(pageHeight, formatRange->rc.bottom);
    if (formatRange->rc.right <=
            formatRange->rc.left + PAGEVIEW_MIN_PRINTABLE_TWIPS ||
        formatRange->rc.bottom <=
            formatRange->rc.top + PAGEVIEW_MIN_PRINTABLE_TWIPS) {
        return FALSE;
    }
    *rangeStart = pageview_page_start(app, page);
    *rangeEnd = app->pageEnds[page - 1];
    formatRange->chrg.cpMin = *rangeStart;
    formatRange->chrg.cpMax = *rangeEnd;
    return *rangeStart >= 0 && *rangeEnd >= *rangeStart;
}

static HENHMETAFILE pageview_render_preview(PageViewState *state,
                                            HDC referenceDc, LONG page)
{
    AppState *app = state->app;
    FORMATRANGE formatRange;
    RECT frame;
    LONG pageWidthUnits;
    LONG pageHeightUnits;
    int logicalPageWidth;
    int logicalPageHeight;
    LONG rangeStart = 0;
    LONG rangeEnd = 0;
    LONG renderedThrough;
    HDC metafileDc;
    HENHMETAFILE metafile;
    BOOL rendered = FALSE;

    if (app == NULL || app->editor == NULL || app->paginationDirty ||
        app->pageEnds == NULL) {
        return NULL;
    }
    pageview_get_page_units(app, &pageWidthUnits, &pageHeightUnits);
    logicalPageWidth = pageview_thousandths_to_pixels(
        pageWidthUnits, GetDeviceCaps(referenceDc, LOGPIXELSX), 100);
    logicalPageHeight = pageview_thousandths_to_pixels(
        pageHeightUnits, GetDeviceCaps(referenceDc, LOGPIXELSY), 100);
    SetRect(&frame, 0, 0,
            pageview_metafile_frame_extent(
                referenceDc, logicalPageWidth, HORZSIZE, HORZRES),
            pageview_metafile_frame_extent(
                referenceDc, logicalPageHeight, VERTSIZE, VERTRES));
    if (frame.right <= 0 || frame.bottom <= 0) {
        SetRect(&frame, 0, 0, MulDiv(pageWidthUnits, 2540, 1000),
                MulDiv(pageHeightUnits, 2540, 1000));
    }
    if (frame.right <= 0 || frame.bottom <= 0) {
        return NULL;
    }
    metafileDc = CreateEnhMetaFileW(
        referenceDc, NULL, &frame, L"WordCraft\0Page preview\0");
    if (metafileDc == NULL) {
        return NULL;
    }
    SetBkMode(metafileDc, TRANSPARENT);
    if (pageview_prepare_format_range(app, metafileDc, referenceDc, page,
                                      &formatRange, &rangeStart,
                                      &rangeEnd)) {
        if (rangeStart == rangeEnd) {
            rendered = TRUE;
        } else {
            renderedThrough = (LONG)SendMessageW(
                app->editor, EM_FORMATRANGE, TRUE, (LPARAM)&formatRange);
            rendered = renderedThrough >= rangeEnd;
        }
    }
    SendMessageW(app->editor, EM_FORMATRANGE, FALSE, 0);
    metafile = CloseEnhMetaFile(metafileDc);
    if (!rendered && metafile != NULL) {
        DeleteEnhMetaFile(metafile);
        metafile = NULL;
    }
    return metafile;
}

static HENHMETAFILE pageview_cached_preview(PageViewState *state,
                                            HDC referenceDc, LONG page)
{
    SIZE_T index;
    SIZE_T replacement = 0;
    ULONGLONG oldestUse = (ULONGLONG)-1;
    HENHMETAFILE metafile;

    for (index = 0; index < ARRAYSIZE(state->previews); ++index) {
        if (state->previews[index].metafile != NULL &&
            state->previews[index].page == page) {
            state->previews[index].lastUse = ++state->previewClock;
            return state->previews[index].metafile;
        }
        if (state->previews[index].metafile == NULL) {
            replacement = index;
            break;
        }
        if (state->previews[index].lastUse < oldestUse) {
            oldestUse = state->previews[index].lastUse;
            replacement = index;
        }
    }
    metafile = pageview_render_preview(state, referenceDc, page);
    if (metafile == NULL) {
        return NULL;
    }
    if (state->previews[replacement].metafile != NULL) {
        DeleteEnhMetaFile(state->previews[replacement].metafile);
    }
    state->previews[replacement].page = page;
    state->previews[replacement].metafile = metafile;
    state->previews[replacement].lastUse = ++state->previewClock;
    return metafile;
}

static void pageview_paint(PageViewState *state, HWND hwnd, HDC dc)
{
    RECT client;
    COLORREF workspace;
    COLORREF paper;
    COLORREF borderColor;
    COLORREF shadowColor;
    int shadowOffset;
    HBRUSH borderBrush;
    LONG firstPage;
    LONG lastPage;
    LONG pageNumber;

    GetClientRect(hwnd, &client);
    if (state == NULL || state->app == NULL) {
        pageview_fill_rect(dc, &client, GetSysColor(COLOR_APPWORKSPACE));
        return;
    }
    workspace = state->app->palette.workspaceBackground;
    paper = state->app->palette.pageBackground;
    borderColor = state->app->palette.pageBorder;
    shadowColor = state->app->palette.pageShadow;
    pageview_fill_rect(dc, &client, workspace);
    pageview_visible_page_range(state, hwnd, &firstPage, &lastPage, NULL);
    if (firstPage == 0 || lastPage == 0) {
        return;
    }
    shadowOffset = app_scale(hwnd, 6);
    borderBrush = CreateSolidBrush(borderColor);
    state->paintingPages = TRUE;
    for (pageNumber = firstPage; pageNumber <= lastPage; ++pageNumber) {
        RECT page;
        RECT shadow;
        RECT border;

        if (!pageview_page_rect(state, pageNumber, &page)) {
            continue;
        }
        shadow = page;
        OffsetRect(&shadow, shadowOffset, shadowOffset);
        InflateRect(&shadow, app_scale(hwnd, 2), app_scale(hwnd, 2));
        pageview_fill_rect(dc, &shadow, shadowColor);
        pageview_fill_rect(dc, &page, paper);
        if (pageNumber != state->visiblePage &&
            !state->app->paginationDirty) {
            HENHMETAFILE preview = pageview_cached_preview(
                state, dc, pageNumber);
            if (preview != NULL) {
                PlayEnhMetaFile(dc, preview, &page);
            }
        }
        if (borderBrush != NULL) {
            border = page;
            InflateRect(&border, 1, 1);
            FrameRect(dc, &border, borderBrush);
        }
    }
    state->paintingPages = FALSE;
    if (borderBrush != NULL) {
        DeleteObject(borderBrush);
    }
}

static BOOL pageview_register_class(HINSTANCE instance)
{
    WNDCLASSEXW windowClass;

    ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_DBLCLKS;
    windowClass.lpfnWndProc = pageview_window_proc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    windowClass.hbrBackground = NULL;
    windowClass.lpszClassName = PAGEVIEW_CLASS_NAME;
    if (RegisterClassExW(&windowClass) != 0) {
        return TRUE;
    }
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

static void pageview_prepare_editor(AppState *app)
{
    LONG_PTR extendedStyle;
    LONG_PTR style;
    LONG_PTR updatedStyle;
    BOOL frameChanged = FALSE;

    if (app == NULL || app->editor == NULL || app->pageView == NULL) {
        return;
    }
    if (GetParent(app->editor) != app->pageView) {
        SetParent(app->editor, app->pageView);
    }
    extendedStyle = GetWindowLongPtrW(app->editor, GWL_EXSTYLE);
    if ((extendedStyle & WS_EX_CLIENTEDGE) != 0) {
        SetWindowLongPtrW(app->editor, GWL_EXSTYLE,
                          extendedStyle & ~(LONG_PTR)WS_EX_CLIENTEDGE);
        frameChanged = TRUE;
    }
    style = GetWindowLongPtrW(app->editor, GWL_STYLE);
    updatedStyle = style &
                   ~(LONG_PTR)(WS_HSCROLL | WS_VSCROLL | ES_AUTOHSCROLL);
    if (updatedStyle != style) {
        SetWindowLongPtrW(app->editor, GWL_STYLE, updatedStyle);
        frameChanged = TRUE;
    }
    ShowScrollBar(app->editor, SB_BOTH, FALSE);
    if (frameChanged) {
        SetWindowPos(app->editor, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                         SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    SetWindowSubclass(app->editor, pageview_editor_subclass_proc,
                      PAGEVIEW_EDITOR_SUBCLASS_ID, (DWORD_PTR)app);
}

static LONG pageview_page_from_character(const AppState *app, LONG character)
{
    SIZE_T low = 0;
    SIZE_T high;

    if (app == NULL || app->pageCount <= 1 || app->pageEnds == NULL) {
        return 1;
    }
    high = (SIZE_T)app->pageCount;
    while (low < high) {
        SIZE_T middle = low + (high - low) / 2;
        if (character < app->pageEnds[middle]) {
            high = middle;
        } else {
            low = middle + 1;
        }
    }
    if (low >= (SIZE_T)app->pageCount) {
        return app->pageCount;
    }
    return (LONG)low + 1;
}

static BOOL pageview_get_active_selection_character(
    HWND editor, const CHARRANGE *range, LONG *character)
{
    IRichEditOle *richEditOle = NULL;
    ITextDocument *document = NULL;
    ITextSelection *selection = NULL;
    long flags = 0;
    BOOL success = FALSE;

    if (editor == NULL || range == NULL || character == NULL) {
        return FALSE;
    }
    if (range->cpMin == range->cpMax) {
        *character = range->cpMin;
        return TRUE;
    }
    if (!SendMessageW(editor, EM_GETOLEINTERFACE, 0,
                      (LPARAM)&richEditOle) || richEditOle == NULL) {
        return FALSE;
    }
    if (SUCCEEDED(richEditOle->lpVtbl->QueryInterface(
            richEditOle, &wordcraftPageviewIidTextDocument,
            (void **)&document)) &&
        document != NULL &&
        SUCCEEDED(ITextDocument_GetSelection(document, &selection)) &&
        selection != NULL &&
        SUCCEEDED(ITextSelection_GetFlags(selection, &flags))) {
        *character = (flags & tomSelStartActive) != 0
                         ? range->cpMin
                         : range->cpMax;
        success = TRUE;
    }
    if (selection != NULL) {
        ITextSelection_Release(selection);
    }
    if (document != NULL) {
        ITextDocument_Release(document);
    }
    richEditOle->lpVtbl->Release(richEditOle);
    return success;
}

static BOOL pageview_append_end(LONG **ends, SIZE_T *count,
                                SIZE_T *capacity, LONG value)
{
    LONG *resized;
    SIZE_T nextCapacity;

    if (*count == *capacity) {
        if (*capacity == 0) {
            nextCapacity = 16;
        } else {
            if (*capacity > ((SIZE_T)-1) / 2) {
                return FALSE;
            }
            nextCapacity = *capacity * 2;
        }
        if (nextCapacity > ((SIZE_T)-1) / sizeof(**ends)) {
            return FALSE;
        }
        if (*ends == NULL) {
            resized = HeapAlloc(GetProcessHeap(), 0,
                                nextCapacity * sizeof(**ends));
        } else {
            resized = HeapReAlloc(GetProcessHeap(), 0, *ends,
                                  nextCapacity * sizeof(**ends));
        }
        if (resized == NULL) {
            return FALSE;
        }
        *ends = resized;
        *capacity = nextCapacity;
    }
    (*ends)[*count] = value;
    ++*count;
    return TRUE;
}

static void pageview_navigate_display(AppState *app, LONG page)
{
    PageViewState *state;
    LONG requested;
    LONG actual;

    if (app == NULL || app->editor == NULL || app->pageView == NULL) {
        return;
    }
    state = pageview_get_state(app->pageView);
    requested = page > 0 ? page - 1 : 0;
    if (state != NULL) {
        state->switchingPage = TRUE;
    }
    SendMessageW(app->editor, EM_SETPAGE, (WPARAM)requested, 0);
    actual = (LONG)SendMessageW(app->editor, EM_GETPAGE, 0, 0);
    if (state != NULL) {
        state->visiblePage = actual >= 0 ? actual + 1 : page;
        state->switchingPage = FALSE;
    }
    InvalidateRect(app->editor, NULL, TRUE);
}

static void pageview_ensure_caret_visible_in_host(AppState *app, LONG character)
{
    PageViewState *state;
    POINT position;
    RECT client;
    LONGLONG logicalX;
    LONGLONG logicalY;
    LONGLONG clientX;
    LONGLONG clientY;
    int horizontal;
    int vertical;
    int inset;
    BOOL changed = FALSE;

    if (app == NULL || app->editor == NULL || app->pageView == NULL) {
        return;
    }
    state = pageview_get_state(app->pageView);
    if (state == NULL) {
        return;
    }
    if (state->pagePitch <= 0) {
        pageview_layout(app);
    }
    position.x = -1;
    position.y = -1;
    if (GetFocus() != app->editor || !GetCaretPos(&position)) {
        position.x = -1;
        position.y = -1;
        SendMessageW(app->editor, EM_POSFROMCHAR, (WPARAM)&position,
                     (LPARAM)character);
    }
    if (position.x < 0 || position.y < 0) {
        return;
    }
    GetClientRect(app->pageView, &client);
    inset = app_scale(app->pageView, 24);
    horizontal = pageview_scroll_position(app->pageView, SB_HORZ);
    vertical = pageview_scroll_position(app->pageView, SB_VERT);
    logicalX = (LONGLONG)state->outerGutter + position.x;
    logicalY = (LONGLONG)state->outerGutter +
               (LONGLONG)(max(1, app->currentPage) - 1) *
                   state->pagePitch +
               position.y;
    clientX = logicalX - horizontal;
    clientY = logicalY - vertical;
    if (state->canvasWidth > client.right - client.left) {
        if (clientX < client.left + inset) {
            changed |= pageview_set_scroll_position(
                app->pageView, SB_HORZ,
                pageview_saturate_int64(logicalX - client.left - inset));
        } else if (clientX > client.right - inset) {
            changed |= pageview_set_scroll_position(
                app->pageView, SB_HORZ,
                pageview_saturate_int64(logicalX - client.right + inset));
        }
    }
    if (state->canvasHeight > client.bottom - client.top) {
        if (clientY < client.top + inset) {
            changed |= pageview_set_scroll_position(
                app->pageView, SB_VERT,
                pageview_saturate_int64(logicalY - client.top - inset));
        } else if (clientY > client.bottom - inset) {
            changed |= pageview_set_scroll_position(
                app->pageView, SB_VERT,
                pageview_saturate_int64(logicalY - client.bottom + inset));
        }
    }
    if (changed) {
        pageview_layout(app);
    }
}

static BOOL pageview_handle_scroll(HWND hwnd, int bar, UINT command)
{
    RECT client;
    int position;
    int maximum;
    int lineStep;
    int pageStep;

    GetClientRect(hwnd, &client);
    position = pageview_scroll_position(hwnd, bar);
    maximum = pageview_scroll_maximum(hwnd, bar);
    lineStep = app_scale(hwnd, 32);
    pageStep = (bar == SB_HORZ ? client.right - client.left
                               : client.bottom - client.top) - lineStep;
    if (pageStep < lineStep) {
        pageStep = lineStep;
    }
    switch (command) {
    case SB_LINEUP:
        position -= lineStep;
        break;
    case SB_LINEDOWN:
        position += lineStep;
        break;
    case SB_PAGEUP:
        position -= pageStep;
        break;
    case SB_PAGEDOWN:
        position += pageStep;
        break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK: {
        SCROLLINFO track;
        ZeroMemory(&track, sizeof(track));
        track.cbSize = sizeof(track);
        track.fMask = SIF_TRACKPOS;
        if (GetScrollInfo(hwnd, bar, &track)) {
            position = track.nTrackPos;
        }
        break;
    }
    case SB_TOP:
        position = 0;
        break;
    case SB_BOTTOM:
        position = maximum;
        break;
    default:
        return FALSE;
    }
    return pageview_set_scroll_position(hwnd, bar, position);
}

static LRESULT pageview_handle_mouse_wheel(PageViewState *state, HWND hwnd,
                                           WPARAM wParam, BOOL horizontal)
{
    UINT lines = 3;
    int delta;
    int step;

    if (state == NULL || state->app == NULL) {
        return 0;
    }
    delta = GET_WHEEL_DELTA_WPARAM(wParam);
    if (horizontal) {
        int distance = app_scale(hwnd, 48);
        pageview_scroll_by(hwnd, SB_HORZ,
                           delta > 0 ? distance : -distance);
        pageview_layout(state->app);
        return 0;
    }
    state->wheelRemainder += delta;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    if (lines == WHEEL_PAGESCROLL) {
        RECT client;
        GetClientRect(hwnd, &client);
        step = client.bottom - client.top;
    } else {
        if (lines == 0) {
            lines = 1;
        }
        step = app_scale(hwnd, 16) * (int)lines;
    }
    if (step < 1) {
        step = 1;
    }
    while (state->wheelRemainder >= WHEEL_DELTA) {
        pageview_scroll_by(hwnd, SB_VERT, -step);
        state->wheelRemainder -= WHEEL_DELTA;
    }
    while (state->wheelRemainder <= -WHEEL_DELTA) {
        pageview_scroll_by(hwnd, SB_VERT, step);
        state->wheelRemainder += WHEEL_DELTA;
    }
    pageview_layout(state->app);
    return 0;
}

static LRESULT CALLBACK pageview_editor_subclass_proc(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR subclassId, DWORD_PTR referenceData)
{
    AppState *app = (AppState *)referenceData;
    (void)subclassId;

    if (message == WM_MOUSEWHEEL && app != NULL && app->pageView != NULL) {
        return SendMessageW(app->pageView, message, wParam, lParam);
    }
    if (message == WM_MOUSEHWHEEL && app != NULL && app->pageView != NULL) {
        return SendMessageW(app->pageView, message, wParam, lParam);
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, pageview_editor_subclass_proc,
                             PAGEVIEW_EDITOR_SUBCLASS_ID);
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

static LONG pageview_page_at_point(const PageViewState *state, POINT point)
{
    LONGLONG relative;
    LONGLONG pageIndex;
    RECT pageRect;
    LONG page;

    if (state == NULL || state->app == NULL || state->pagePitch <= 0) {
        return 0;
    }
    relative = (LONGLONG)point.y - state->firstPageTop;
    if (relative < 0) {
        return 0;
    }
    pageIndex = relative / state->pagePitch;
    if (pageIndex < 0 || pageIndex >= state->app->pageCount) {
        return 0;
    }
    page = (LONG)pageIndex + 1;
    if (!pageview_page_rect(state, page, &pageRect) ||
        !PtInRect(&pageRect, point)) {
        return 0;
    }
    return page;
}

static LRESULT pageview_forward_page_click(PageViewState *state, HWND hwnd,
                                           UINT message, WPARAM wParam,
                                           LPARAM lParam)
{
    POINT point;
    LONG page;

    if (state == NULL || state->app == NULL ||
        state->app->editor == NULL) {
        return 0;
    }
    point.x = (short)LOWORD(lParam);
    point.y = (short)HIWORD(lParam);
    page = pageview_page_at_point(state, point);
    if (page == 0) {
        SetFocus(state->app->editor);
        return 0;
    }
    if (page != state->visiblePage) {
        pageview_navigate_display(state->app, page);
        pageview_layout(state->app);
    }
    SetFocus(state->app->editor);
    MapWindowPoints(hwnd, state->app->editor, &point, 1);
    return SendMessageW(state->app->editor, message, wParam,
                        MAKELPARAM((WORD)point.x, (WORD)point.y));
}

static LRESULT CALLBACK pageview_window_proc(HWND hwnd, UINT message,
                                              WPARAM wParam, LPARAM lParam)
{
    PageViewState *state = pageview_get_state(hwnd);
    AppState *app = state != NULL ? state->app : NULL;

    if (message == WM_NCCREATE) {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lParam;
        state = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*state));
        if (state == NULL) {
            return FALSE;
        }
        state->app = (AppState *)create->lpCreateParams;
        state->visiblePage = state->app != NULL && state->app->currentPage > 0
                                 ? state->app->currentPage
                                 : 1;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
        app = state->app;
    }

    switch (message) {
    case WM_ERASEBKGND:
        return state != NULL ? 1 : DefWindowProcW(hwnd, message, wParam, lParam);
    case WM_PAINT:
        if (state != NULL) {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(hwnd, &paint);
            pageview_paint(state, hwnd, dc);
            EndPaint(hwnd, &paint);
            return 0;
        }
        break;
    case WM_SIZE:
        if (state != NULL && !state->layingOut && app != NULL) {
            pageview_layout(app);
        }
        return 0;
    case WM_HSCROLL:
    case WM_VSCROLL:
        if (state != NULL && app != NULL) {
            int bar = message == WM_HSCROLL ? SB_HORZ : SB_VERT;
            UINT command = LOWORD(wParam);
            if (pageview_handle_scroll(hwnd, bar, command)) {
                pageview_layout(app);
            }
            return 0;
        }
        break;
    case WM_MOUSEWHEEL:
        return pageview_handle_mouse_wheel(state, hwnd, wParam, FALSE);
    case WM_MOUSEHWHEEL:
        return pageview_handle_mouse_wheel(state, hwnd, wParam, TRUE);
    case WM_SETFOCUS:
        if (app != NULL && app->editor != NULL) {
            SetFocus(app->editor);
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
        return pageview_forward_page_click(state, hwnd, message, wParam,
                                           lParam);
    case WM_COMMAND:
    case WM_NOTIFY:
        if (app != NULL && app->mainWindow != NULL) {
            return SendMessageW(app->mainWindow, message, wParam, lParam);
        }
        break;
    case WM_NCDESTROY:
        if (state != NULL) {
            pageview_clear_preview_cache(state);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            HeapFree(GetProcessHeap(), 0, state);
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

BOOL pageview_create(AppState *app)
{
    if (app == NULL || app->mainWindow == NULL || app->instance == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (app->pageView != NULL) {
        pageview_prepare_editor(app);
        return TRUE;
    }
    if (!pageview_register_class(app->instance)) {
        return FALSE;
    }
    if (app->pageSize.x <= 0) {
        app->pageSize.x = PAGEVIEW_DEFAULT_WIDTH;
    }
    if (app->pageSize.y <= 0) {
        app->pageSize.y = PAGEVIEW_DEFAULT_HEIGHT;
    }
    app->wordWrap = TRUE;
    if (app->pageCount <= 0) {
        app->pageCount = 1;
    }
    if (app->currentPage <= 0) {
        app->currentPage = 1;
    }
    app->paginationDirty = TRUE;
    app->pageView = CreateWindowExW(
        WS_EX_CONTROLPARENT, PAGEVIEW_CLASS_NAME, NULL,
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
            WS_HSCROLL | WS_VSCROLL,
        0, 0, 0, 0, app->mainWindow,
        (HMENU)(INT_PTR)PAGEVIEW_CONTROL_ID, app->instance, app);
    if (app->pageView == NULL) {
        return FALSE;
    }
    pageview_prepare_editor(app);
    pageview_apply_theme(app);
    return TRUE;
}

void pageview_layout(AppState *app)
{
    PageViewState *state;
    RECT client;
    RECT formatRect;
    RECT activePageRect;
    RECT intersection;
    LONG pageWidthUnits;
    LONG pageHeightUnits;
    LONG printableWidthTwips;
    LONG actualPage;
    int dpiX;
    int dpiY;
    int zoom;
    int pageWidth;
    int pageHeight;
    int logicalPageWidth;
    int logicalPageHeight;
    int gutter;
    int pageGap;
    int pagePitch;
    int canvasWidth;
    int canvasHeight;
    int horizontalPosition;
    int verticalPosition;
    int pageLeft;
    int firstPageTop;
    int marginLeft;
    int marginTop;
    int marginRight;
    int marginBottom;
    int viewportWidth;
    int viewportHeight;
    LONGLONG stackHeight64;
    LONGLONG canvasHeight64;
    BOOL configureEditor;
    BOOL activePageVisible;
    HDC dc;

    if (app == NULL || app->pageView == NULL) {
        return;
    }
    state = pageview_get_state(app->pageView);
    if (state == NULL || state->layingOut || state->paintingPages) {
        return;
    }
    state->layingOut = TRUE;
    GetClientRect(app->pageView, &client);
    viewportWidth = max(1, client.right - client.left);
    viewportHeight = max(1, client.bottom - client.top);
    pageview_get_dpi(app->pageView, &dpiX, &dpiY);
    pageview_get_page_units(app, &pageWidthUnits, &pageHeightUnits);
    zoom = app->zoomPercent > 0 ? app->zoomPercent : 100;
    pageWidth = pageview_thousandths_to_pixels(pageWidthUnits, dpiX, zoom);
    pageHeight = pageview_thousandths_to_pixels(pageHeightUnits, dpiY, zoom);
    logicalPageWidth = pageview_thousandths_to_pixels(
        pageWidthUnits, dpiX, 100);
    logicalPageHeight = pageview_thousandths_to_pixels(
        pageHeightUnits, dpiY, 100);
    if (pageWidth < 1) {
        pageWidth = 1;
    }
    if (pageHeight < 1) {
        pageHeight = 1;
    }
    if (logicalPageWidth < 1) {
        logicalPageWidth = 1;
    }
    if (logicalPageHeight < 1) {
        logicalPageHeight = 1;
    }
    if (app->pageCount < 1) {
        app->pageCount = 1;
    }
    gutter = app_scale(app->pageView, 28);
    pageGap = app_scale(app->pageView, 24);
    if (gutter < 1) {
        gutter = 1;
    }
    if (pageGap < 1) {
        pageGap = 1;
    }
    pagePitch = pageHeight > INT_MAX - pageGap
                    ? INT_MAX
                    : pageHeight + pageGap;
    if (pageWidth > INT_MAX - gutter * 2) {
        canvasWidth = INT_MAX;
    } else {
        canvasWidth = pageWidth + gutter * 2;
    }
    stackHeight64 = (LONGLONG)app->pageCount * pageHeight +
                    (LONGLONG)(app->pageCount - 1) * pageGap;
    canvasHeight64 = stackHeight64 + (LONGLONG)gutter * 2;
    canvasHeight = pageview_saturate_int64(canvasHeight64);
    if (canvasHeight < 1) {
        canvasHeight = INT_MAX;
    }
    pageview_set_scroll_extent(app->pageView, SB_HORZ, canvasWidth,
                               viewportWidth);
    pageview_set_scroll_extent(app->pageView, SB_VERT, canvasHeight,
                               viewportHeight);
    horizontalPosition = pageview_scroll_position(app->pageView, SB_HORZ);
    verticalPosition = pageview_scroll_position(app->pageView, SB_VERT);
    if (canvasWidth <= viewportWidth) {
        pageLeft = client.left +
                   (viewportWidth - pageWidth) / 2;
    } else {
        pageLeft = gutter - horizontalPosition;
    }
    if (canvasHeight <= viewportHeight && stackHeight64 <= INT_MAX) {
        firstPageTop = client.top +
                       (viewportHeight - (int)stackHeight64) / 2;
    } else {
        firstPageTop = gutter - verticalPosition;
    }
    state->pageWidth = pageWidth;
    state->pageHeight = pageHeight;
    state->pageGap = pageGap;
    state->pagePitch = pagePitch;
    state->outerGutter = gutter;
    state->pageLeft = pageLeft;
    state->firstPageTop = firstPageTop;
    state->canvasWidth = canvasWidth;
    state->canvasHeight = canvasHeight;

    actualPage = app->editor != NULL
                     ? (LONG)SendMessageW(app->editor, EM_GETPAGE, 0, 0) + 1
                     : 0;
    if (actualPage < 1 || actualPage > app->pageCount) {
        actualPage = pageview_clamp_long(
            app->currentPage > 0 ? app->currentPage : state->visiblePage,
            1, app->pageCount);
    }
    state->visiblePage = actualPage;
    if (!pageview_page_rect(state, actualPage, &activePageRect)) {
        SetRectEmpty(&activePageRect);
    }
    state->pageRect = activePageRect;
    activePageVisible = !IsRectEmpty(&activePageRect) &&
                        IntersectRect(&intersection, &activePageRect,
                                      &client);

    if (app->editor != NULL) {
        pageview_prepare_editor(app);
        if (activePageVisible) {
            MoveWindow(app->editor, activePageRect.left,
                       activePageRect.top, pageWidth, pageHeight, TRUE);
        } else {
            MoveWindow(app->editor, client.left,
                       client.top - pageHeight - app_scale(app->pageView, 4),
                       pageWidth, pageHeight, TRUE);
        }
        ShowWindow(app->editor, SW_SHOWNOACTIVATE);

        marginLeft = pageview_thousandths_to_pixels(
            app->pageMargins.left, dpiX, 100);
        marginTop = pageview_thousandths_to_pixels(
            app->pageMargins.top, dpiY, 100);
        marginRight = pageview_thousandths_to_pixels(
            app->pageMargins.right, dpiX, 100);
        marginBottom = pageview_thousandths_to_pixels(
            app->pageMargins.bottom, dpiY, 100);
        marginLeft = pageview_clamp_int(marginLeft, 0,
                                        logicalPageWidth - 1);
        marginTop = pageview_clamp_int(marginTop, 0,
                                       logicalPageHeight - 1);
        marginRight = pageview_clamp_int(marginRight, 0,
                                         logicalPageWidth - 1);
        marginBottom = pageview_clamp_int(marginBottom, 0,
                                          logicalPageHeight - 1);
        printableWidthTwips = pageview_thousandths_to_twips(pageWidthUnits) -
                              pageview_thousandths_to_twips(
                                  app->pageMargins.left) -
                              pageview_thousandths_to_twips(
                                  app->pageMargins.right);
        if (printableWidthTwips < PAGEVIEW_MIN_PRINTABLE_TWIPS) {
            printableWidthTwips = PAGEVIEW_MIN_PRINTABLE_TWIPS;
        }
        configureEditor = state->configuredEditor != app->editor ||
                          state->configuredWidth != pageWidth ||
                          state->configuredHeight != pageHeight ||
                          state->configuredMarginLeft != marginLeft ||
                          state->configuredMarginTop != marginTop ||
                          state->configuredMarginRight != marginRight ||
                          state->configuredMarginBottom != marginBottom ||
                          state->configuredZoom != zoom ||
                          state->configuredPrintableWidth != printableWidthTwips;
        if (configureEditor) {
            pageview_clear_preview_cache(state);
            SetRect(&formatRect, marginLeft, marginTop,
                    logicalPageWidth - marginRight,
                    logicalPageHeight - marginBottom);
            if (formatRect.right <= formatRect.left + 1) {
                formatRect.right = formatRect.left + 1;
            }
            if (formatRect.bottom <= formatRect.top + 1) {
                formatRect.bottom = formatRect.top + 1;
            }
            state->switchingPage = TRUE;
            SendMessageW(app->editor, EM_SETVIEWKIND, VM_PAGE, 0);
            SendMessageW(app->editor, EM_SETZOOM, (WPARAM)zoom, 100);
            dc = GetDC(app->editor);
            if (dc != NULL) {
                SendMessageW(app->editor, EM_SETTARGETDEVICE, (WPARAM)dc,
                             (LPARAM)printableWidthTwips);
                ReleaseDC(app->editor, dc);
            }
            SendMessageW(app->editor, EM_SETRECTNP, 0,
                         (LPARAM)&formatRect);
            state->switchingPage = FALSE;
            state->configuredEditor = app->editor;
            state->configuredWidth = pageWidth;
            state->configuredHeight = pageHeight;
            state->configuredMarginLeft = marginLeft;
            state->configuredMarginTop = marginTop;
            state->configuredMarginRight = marginRight;
            state->configuredMarginBottom = marginBottom;
            state->configuredZoom = zoom;
            state->configuredPrintableWidth = printableWidthTwips;
        }
        InvalidateRect(app->editor, NULL, TRUE);
    }
    InvalidateRect(app->pageView, NULL, TRUE);
    state->layingOut = FALSE;
}

void pageview_apply_theme(AppState *app)
{
    PageViewState *state;

    if (app == NULL) {
        return;
    }
    state = app->pageView != NULL ? pageview_get_state(app->pageView) : NULL;
    pageview_clear_preview_cache(state);
    if (app->editor != NULL) {
        SendMessageW(app->editor, EM_SETBKGNDCOLOR, 0,
                     app->palette.pageBackground);
        InvalidateRect(app->editor, NULL, TRUE);
    }
    if (app->pageView != NULL) {
        RedrawWindow(app->pageView, NULL, NULL,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
}

void pageview_mark_dirty(AppState *app)
{
    if (app != NULL) {
        if (app->pageView != NULL) {
            pageview_clear_preview_cache(
                pageview_get_state(app->pageView));
            InvalidateRect(app->pageView, NULL, FALSE);
        }
        app->paginationDirty = TRUE;
        if (app->mainWindow != NULL && IsWindow(app->mainWindow)) {
            SetTimer(app->mainWindow, STATUS_TIMER_ID, 250, NULL);
        }
    }
}

BOOL pageview_paginate(AppState *app)
{
    FORMATRANGE formatRange;
    LONG *newEnds = NULL;
    SIZE_T newCount = 0;
    SIZE_T newCapacity = 0;
    SIZE_T textLengthSize = 0;
    LONG textLength;
    LONG pageWidth;
    LONG pageHeight;
    LONG nextCharacter;
    LONG previousCharacter = 0;
    DWORD error = ERROR_SUCCESS;
    HDC dc = NULL;
    BOOL success = FALSE;

    if (app == NULL || app->editor == NULL) {
        return FALSE;
    }
    if (app->pageLayoutBusy) {
        return FALSE;
    }
    if (!app->paginationDirty && app->pageCount > 0 &&
        app->pageEnds != NULL) {
        return TRUE;
    }
    app->pageLayoutBusy = TRUE;
    if (!editor_get_text_length(app->editor, FALSE, &textLengthSize, &error) ||
        textLengthSize > LONG_MAX) {
        goto cleanup;
    }
    textLength = (LONG)textLengthSize;
    pageview_get_page_units(app, &pageWidth, &pageHeight);
    pageWidth = pageview_thousandths_to_twips(pageWidth);
    pageHeight = pageview_thousandths_to_twips(pageHeight);
    if (pageWidth <= 0 || pageHeight <= 0) {
        goto cleanup;
    }
    dc = GetDC(app->editor);
    if (dc == NULL) {
        goto cleanup;
    }
    ZeroMemory(&formatRange, sizeof(formatRange));
    formatRange.hdc = dc;
    formatRange.hdcTarget = dc;
    formatRange.rcPage.left = 0;
    formatRange.rcPage.top = 0;
    formatRange.rcPage.right = pageWidth;
    formatRange.rcPage.bottom = pageHeight;
    formatRange.rc.left = pageview_thousandths_to_twips(
        app->pageMargins.left);
    formatRange.rc.top = pageview_thousandths_to_twips(
        app->pageMargins.top);
    formatRange.rc.right = pageWidth - pageview_thousandths_to_twips(
                                           app->pageMargins.right);
    formatRange.rc.bottom = pageHeight - pageview_thousandths_to_twips(
                                             app->pageMargins.bottom);
    if (formatRange.rc.left < 0) {
        formatRange.rc.left = 0;
    }
    if (formatRange.rc.top < 0) {
        formatRange.rc.top = 0;
    }
    if (formatRange.rc.right > pageWidth) {
        formatRange.rc.right = pageWidth;
    }
    if (formatRange.rc.bottom > pageHeight) {
        formatRange.rc.bottom = pageHeight;
    }
    if (formatRange.rc.right <=
            formatRange.rc.left + PAGEVIEW_MIN_PRINTABLE_TWIPS ||
        formatRange.rc.bottom <=
            formatRange.rc.top + PAGEVIEW_MIN_PRINTABLE_TWIPS) {
        goto cleanup;
    }

    if (textLength == 0) {
        if (!pageview_append_end(&newEnds, &newCount, &newCapacity, 0)) {
            goto cleanup;
        }
    } else {
        while (previousCharacter < textLength) {
            formatRange.chrg.cpMin = previousCharacter;
            formatRange.chrg.cpMax = -1;
            nextCharacter = (LONG)SendMessageW(
                app->editor, EM_FORMATRANGE, FALSE, (LPARAM)&formatRange);
            if (nextCharacter <= previousCharacter) {
                goto cleanup;
            }
            if (nextCharacter > textLength) {
                nextCharacter = textLength;
            }
            if (!pageview_append_end(&newEnds, &newCount, &newCapacity,
                                     nextCharacter)) {
                goto cleanup;
            }
            previousCharacter = nextCharacter;
            if (newCount > LONG_MAX) {
                goto cleanup;
            }
        }
    }
    if (newCount == 0 || newCount > LONG_MAX) {
        goto cleanup;
    }

    if (app->pageEnds != NULL) {
        HeapFree(GetProcessHeap(), 0, app->pageEnds);
    }
    app->pageEnds = newEnds;
    app->pageCapacity = newCapacity;
    app->pageCount = (LONG)newCount;
    app->paginationDirty = FALSE;
    newEnds = NULL;
    success = TRUE;

cleanup:
    SendMessageW(app->editor, EM_FORMATRANGE, FALSE, 0);
    if (dc != NULL) {
        ReleaseDC(app->editor, dc);
    }
    if (newEnds != NULL) {
        HeapFree(GetProcessHeap(), 0, newEnds);
    }
    app->pageLayoutBusy = FALSE;
    if (success) {
        if (app->pageView != NULL) {
            pageview_clear_preview_cache(
                pageview_get_state(app->pageView));
        }
        pageview_sync_to_caret(app, FALSE);
        pageview_layout(app);
    }
    return success;
}

void pageview_sync_to_caret(AppState *app, BOOL ensureVisible)
{
    PageViewState *state;
    CHARRANGE selection;
    LONG activeCharacter;
    LONG page;
    LONG displayedPage;
    BOOL selectionCollapsed;
    BOOL activeCharacterKnown;
    BOOL pageChanged = FALSE;

    if (app == NULL || app->editor == NULL) {
        return;
    }
    state = app->pageView != NULL ? pageview_get_state(app->pageView) : NULL;
    if (state != NULL && state->switchingPage) {
        return;
    }
    if ((app->pageCount <= 0 || app->pageEnds == NULL) &&
        !app->pageLayoutBusy) {
        pageview_paginate(app);
    }
    ZeroMemory(&selection, sizeof(selection));
    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
    selectionCollapsed = selection.cpMin == selection.cpMax;
    activeCharacter = selection.cpMin;
    activeCharacterKnown = pageview_get_active_selection_character(
        app->editor, &selection, &activeCharacter);
    displayedPage = app->pageView != NULL
                        ? (LONG)SendMessageW(app->editor, EM_GETPAGE, 0, 0) + 1
                        : 0;
    if (activeCharacterKnown) {
        page = pageview_page_from_character(app, activeCharacter);
    } else if (!selectionCollapsed && displayedPage >= 1 &&
               displayedPage <= app->pageCount) {
        /* Fall back to RichEdit's displayed page if TOM is unavailable. */
        page = displayedPage;
    } else {
        page = pageview_page_from_character(app, selection.cpMin);
    }
    app->currentPage = page;
    if (app->pageView != NULL) {
        if (displayedPage < 1 || displayedPage > app->pageCount ||
            (activeCharacterKnown && displayedPage != page)) {
            pageview_navigate_display(app, page);
            pageChanged = TRUE;
        } else if (state != NULL && state->visiblePage != displayedPage) {
            state->visiblePage = displayedPage;
            pageChanged = TRUE;
        }
        if (pageChanged) {
            pageview_layout(app);
        }
    }
    if (ensureVisible && app->pageView != NULL) {
        if (!activeCharacterKnown && !selectionCollapsed &&
            pageview_page_from_character(app, selection.cpMax) == page) {
            activeCharacter = selection.cpMax;
        }
        SendMessageW(app->editor, EM_SCROLLCARET, 0, 0);
        pageview_ensure_caret_visible_in_host(app, activeCharacter);
    }
}

LONG pageview_page_start(AppState *app, LONG page)
{
    if (app == NULL || page <= 1 || app->pageEnds == NULL ||
        app->pageCount <= 1) {
        return 0;
    }
    page = pageview_clamp_long(page, 1, app->pageCount);
    return app->pageEnds[page - 2];
}

LRESULT pageview_query_state(AppState *app, UINT query)
{
    PageViewState *state;
    LONG firstPage = 0;
    LONG lastPage = 0;
    LONG fullyVisible = 0;

    if (app == NULL || app->pageView == NULL) {
        return 0;
    }
    state = pageview_get_state(app->pageView);
    if (state == NULL) {
        return 0;
    }
    pageview_visible_page_range(state, app->pageView, &firstPage,
                                &lastPage, &fullyVisible);
    switch (query) {
    case WCQ_FIRST_VISIBLE_PAGE:
        return firstPage;
    case WCQ_LAST_VISIBLE_PAGE:
        return lastPage;
    case WCQ_VISIBLE_PAGE_COUNT:
        return firstPage > 0 && lastPage >= firstPage
                   ? lastPage - firstPage + 1
                   : 0;
    case WCQ_VIEW_SCROLL_Y:
        return pageview_scroll_position(app->pageView, SB_VERT);
    case WCQ_VIEW_SCROLL_MAX:
        return pageview_scroll_maximum(app->pageView, SB_VERT);
    case WCQ_FULLY_VISIBLE_PAGE_COUNT:
        return fullyVisible;
    default:
        return 0;
    }
}

void pageview_free(AppState *app)
{
    if (app == NULL) {
        return;
    }
    if (app->editor != NULL && IsWindow(app->editor)) {
        RemoveWindowSubclass(app->editor, pageview_editor_subclass_proc,
                             PAGEVIEW_EDITOR_SUBCLASS_ID);
    }
    if (app->pageEnds != NULL) {
        HeapFree(GetProcessHeap(), 0, app->pageEnds);
        app->pageEnds = NULL;
    }
    app->pageCapacity = 0;
    app->pageCount = 0;
    app->currentPage = 0;
    app->paginationDirty = TRUE;
}
