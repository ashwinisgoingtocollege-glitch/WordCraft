#ifndef COBJMACROS
#define COBJMACROS
#endif

#include "editor.h"
#include "rendereditor.h"

#include <limits.h>
#include <richole.h>
#include <stdlib.h>
#include <tom.h>

#define PAGEVIEW_CLASS_NAME L"WordCraftPageView"
#define PAGEVIEW_EDITOR_SUBCLASS_ID ((UINT_PTR)0x50414745)
#define PAGEVIEW_DEFAULT_WIDTH 8500
#define PAGEVIEW_DEFAULT_HEIGHT 11000
#define PAGEVIEW_MIN_PRINTABLE_TWIPS 144
#define PAGEVIEW_PREVIEW_CACHE_CAPACITY 16
#define PAGEVIEW_SCROLL_TIMER_ID 1
#define PAGEVIEW_SCROLL_FRAME_MS 16
#define PAGEVIEW_SCROLL_TIMER_INTERVAL_MS 15
#define PAGEVIEW_SCROLL_TIME_CONSTANT_MS 28
#define PAGEVIEW_PREVIEW_TIMER_ID 2
#define PAGEVIEW_PREVIEW_WARM_DELAY_MS 40
#define PAGEVIEW_MAX_SCROLL_PIXELS_PER_SECOND 2400

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
    LONGLONG verticalWheelRemainder;
    LONGLONG horizontalWheelRemainder;
    LARGE_INTEGER performanceFrequency;
    LARGE_INTEGER lastScrollFrame;
    int scrollTargetY;
    int scrollPixelsPerSecond;
    int shadowOffset;
    int shadowInflate;
    int previewInvalidatePadding;
    LONG scrollFrameCount;
    int lastScrollDirection;
    BOOL scrollAnimating;
    BOOL thumbTracking;
    BOOL previewRenderingDeferred;
    BOOL previewWarmScheduled;
    BOOL editorOnPage;
    BOOL layingOut;
    BOOL switchingPage;
    BOOL paintingPages;
    int pageWidth;
    int pageHeight;
    int pageGap;
    int pagePitch;
    int outerGutter;
    int commentMarginWidth;
    int commentMarginGap;
    BOOL commentsVisible;
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
static void pageview_cancel_scroll_animation(PageViewState *state,
                                             HWND hwnd);
static void pageview_schedule_preview_warm(PageViewState *state,
                                           HWND hwnd);

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

static LONG pageview_thousandths_to_himetric(LONG value)
{
    LONGLONG result;

    if (value <= 0) {
        return 0;
    }
    result = ((LONGLONG)value * 2540 + 500) / 1000;
    return result > LONG_MAX ? LONG_MAX : (LONG)result;
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

static void pageview_cancel_scroll_animation(PageViewState *state,
                                             HWND hwnd)
{
    BOOL wasAnimating;

    if (state == NULL) {
        return;
    }
    wasAnimating = state->scrollAnimating;
    if (hwnd != NULL) {
        KillTimer(hwnd, PAGEVIEW_SCROLL_TIMER_ID);
        state->scrollTargetY = pageview_scroll_position(hwnd, SB_VERT);
    }
    state->scrollAnimating = FALSE;
    state->thumbTracking = FALSE;
    state->previewRenderingDeferred = wasAnimating;
    state->verticalWheelRemainder = 0;
    state->horizontalWheelRemainder = 0;
    state->lastScrollFrame.QuadPart = 0;
    if (wasAnimating && hwnd != NULL) {
        InvalidateRect(hwnd, NULL, FALSE);
        pageview_schedule_preview_warm(state, hwnd);
    }
}

static void pageview_position_editor_for_viewport(PageViewState *state,
                                                  HWND hwnd)
{
    AppState *app;
    RECT client;
    RECT activePage;
    RECT intersection;
    BOOL visible;
    int parkY;

    if (state == NULL || state->app == NULL ||
        state->app->editor == NULL) {
        return;
    }
    app = state->app;
    GetClientRect(hwnd, &client);
    visible = pageview_page_rect(state, state->visiblePage, &activePage) &&
              IntersectRect(&intersection, &activePage, &client);
    if (visible) {
        SetWindowPos(app->editor, NULL, activePage.left, activePage.top,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (!state->editorOnPage) {
            InvalidateRect(app->editor, NULL, FALSE);
        }
        state->editorOnPage = TRUE;
        state->pageRect = activePage;
        return;
    }
    if (state->editorOnPage) {
        parkY = client.top - state->pageHeight - app_scale(hwnd, 4);
        SetWindowPos(app->editor, NULL, client.left, parkY, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    state->editorOnPage = FALSE;
    if (pageview_page_rect(state, state->visiblePage, &activePage)) {
        state->pageRect = activePage;
    } else {
        SetRectEmpty(&state->pageRect);
    }
}

static BOOL pageview_apply_scroll_position(PageViewState *state, HWND hwnd,
                                           int bar, int position)
{
    RECT client;
    int oldHorizontal;
    int oldVertical;
    int newHorizontal;
    int newVertical;
    int dx;
    int dy;

    if (state == NULL || state->app == NULL) {
        return FALSE;
    }
    oldHorizontal = pageview_scroll_position(hwnd, SB_HORZ);
    oldVertical = pageview_scroll_position(hwnd, SB_VERT);
    if (!pageview_set_scroll_position(hwnd, bar, position)) {
        return FALSE;
    }
    newHorizontal = pageview_scroll_position(hwnd, SB_HORZ);
    newVertical = pageview_scroll_position(hwnd, SB_VERT);
    dx = oldHorizontal - newHorizontal;
    dy = oldVertical - newVertical;
    if (dx == 0 && dy == 0) {
        return FALSE;
    }
    state->pageLeft += dx;
    state->firstPageTop += dy;
    GetClientRect(hwnd, &client);
    if (abs(dx) >= client.right - client.left ||
        abs(dy) >= client.bottom - client.top ||
        ScrollWindowEx(hwnd, dx, dy, NULL, NULL, NULL, NULL,
                       SW_INVALIDATE) == ERROR) {
        InvalidateRect(hwnd, NULL, FALSE);
    }
    pageview_position_editor_for_viewport(state, hwnd);
    return TRUE;
}

static void pageview_finish_scroll_animation(PageViewState *state,
                                             HWND hwnd)
{
    if (state == NULL) {
        return;
    }
    KillTimer(hwnd, PAGEVIEW_SCROLL_TIMER_ID);
    state->scrollAnimating = FALSE;
    state->lastScrollFrame.QuadPart = 0;
    pageview_schedule_preview_warm(state, hwnd);
}

static void pageview_scroll_animation_frame(PageViewState *state,
                                            HWND hwnd, BOOL forceFrame)
{
    LARGE_INTEGER now;
    LONGLONG elapsedTicks;
    LONGLONG elapsedMilliseconds;
    LONGLONG step;
    LONGLONG maximumStep;
    int current;
    int difference;
    int next;

    if (state == NULL || !state->scrollAnimating) {
        return;
    }
    QueryPerformanceCounter(&now);
    if (state->performanceFrequency.QuadPart <= 0) {
        QueryPerformanceFrequency(&state->performanceFrequency);
    }
    if (forceFrame || state->lastScrollFrame.QuadPart == 0 ||
        state->performanceFrequency.QuadPart <= 0) {
        elapsedMilliseconds = PAGEVIEW_SCROLL_FRAME_MS;
    } else {
        elapsedTicks = now.QuadPart - state->lastScrollFrame.QuadPart;
        elapsedMilliseconds = elapsedTicks > 0
                                  ? elapsedTicks * 1000 /
                                        state->performanceFrequency.QuadPart
                                  : 0;
        if (elapsedMilliseconds < PAGEVIEW_SCROLL_FRAME_MS - 2) {
            return;
        }
        if (elapsedMilliseconds > 50) {
            elapsedMilliseconds = 50;
        }
    }
    state->lastScrollFrame = now;
    current = pageview_scroll_position(hwnd, SB_VERT);
    difference = state->scrollTargetY - current;
    if (difference == 0) {
        pageview_finish_scroll_animation(state, hwnd);
        return;
    }
    if (difference >= -2 && difference <= 2) {
        next = state->scrollTargetY;
    } else {
        step = (LONGLONG)difference * elapsedMilliseconds /
               (PAGEVIEW_SCROLL_TIME_CONSTANT_MS + elapsedMilliseconds);
        if (step == 0) {
            step = difference > 0 ? 1 : -1;
        }
        maximumStep = ((LONGLONG)max(1, state->scrollPixelsPerSecond) *
                       elapsedMilliseconds + 999) / 1000;
        if (step > maximumStep) {
            step = maximumStep;
        } else if (step < -maximumStep) {
            step = -maximumStep;
        }
        next = pageview_saturate_int64((LONGLONG)current + step);
    }
    if ((difference > 0 && next > state->scrollTargetY) ||
        (difference < 0 && next < state->scrollTargetY)) {
        next = state->scrollTargetY;
    }
    if (pageview_apply_scroll_position(state, hwnd, SB_VERT, next)) {
        if (state->scrollFrameCount < LONG_MAX) {
            ++state->scrollFrameCount;
        }
    }
    if (pageview_scroll_position(hwnd, SB_VERT) == state->scrollTargetY) {
        pageview_finish_scroll_animation(state, hwnd);
    }
}

static void pageview_set_smooth_scroll_target(PageViewState *state,
                                              HWND hwnd, int target)
{
    int current;
    int maximum;
    BOOL starting;

    if (state == NULL) {
        return;
    }
    current = pageview_scroll_position(hwnd, SB_VERT);
    maximum = pageview_scroll_maximum(hwnd, SB_VERT);
    target = pageview_clamp_int(target, 0, maximum);
    if (target == current && !state->scrollAnimating) {
        state->scrollTargetY = current;
        return;
    }
    starting = !state->scrollAnimating;
    state->scrollTargetY = target;
    state->lastScrollDirection = target < current ? -1 : 1;
    state->previewRenderingDeferred = TRUE;
    state->scrollAnimating = TRUE;
    pageview_schedule_preview_warm(state, hwnd);
    if (starting &&
        SetTimer(hwnd, PAGEVIEW_SCROLL_TIMER_ID,
                 PAGEVIEW_SCROLL_TIMER_INTERVAL_MS, NULL) == 0) {
        pageview_apply_scroll_position(state, hwnd, SB_VERT, target);
        state->scrollAnimating = FALSE;
        state->previewRenderingDeferred = FALSE;
        return;
    }
    pageview_scroll_animation_frame(state, hwnd, starting);
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

static HENHMETAFILE pageview_lookup_preview(PageViewState *state,
                                            LONG page, BOOL touch)
{
    SIZE_T index;

    if (state == NULL || page < 1) {
        return NULL;
    }
    for (index = 0; index < ARRAYSIZE(state->previews); ++index) {
        if (state->previews[index].metafile != NULL &&
            state->previews[index].page == page) {
            if (touch) {
                state->previews[index].lastUse = ++state->previewClock;
            }
            return state->previews[index].metafile;
        }
    }
    return NULL;
}

static HENHMETAFILE pageview_cached_preview(PageViewState *state,
                                            HDC referenceDc, LONG page)
{
    SIZE_T index;
    SIZE_T replacement = 0;
    ULONGLONG oldestUse = (ULONGLONG)-1;
    HENHMETAFILE metafile;

    metafile = pageview_lookup_preview(state, page, TRUE);
    if (metafile != NULL) {
        return metafile;
    }
    for (index = 0; index < ARRAYSIZE(state->previews); ++index) {
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

static void pageview_paint(PageViewState *state, HWND hwnd, HDC dc,
                           const RECT *dirtyRect)
{
    RECT client;
    RECT dirty;
    COLORREF workspace;
    COLORREF paper;
    COLORREF borderColor;
    COLORREF shadowColor;
    int shadowOffset;
    int shadowInflate;
    HBRUSH borderBrush;
    LONG firstPage;
    LONG lastPage;
    LONG pageNumber;

    GetClientRect(hwnd, &client);
    if (dirtyRect == NULL || IsRectEmpty(dirtyRect) ||
        !IntersectRect(&dirty, &client, dirtyRect)) {
        dirty = client;
    }
    if (state == NULL || state->app == NULL) {
        pageview_fill_rect(dc, &dirty, GetSysColor(COLOR_APPWORKSPACE));
        return;
    }
    workspace = state->app->palette.workspaceBackground;
    paper = state->app->palette.pageBackground;
    borderColor = state->app->palette.pageBorder;
    shadowColor = state->app->palette.pageShadow;
    pageview_fill_rect(dc, &dirty, workspace);
    pageview_visible_page_range(state, hwnd, &firstPage, &lastPage, NULL);
    if (firstPage == 0 || lastPage == 0) {
        return;
    }
    shadowOffset = state->shadowOffset > 0
                       ? state->shadowOffset : app_scale(hwnd, 6);
    shadowInflate = state->shadowInflate > 0
                        ? state->shadowInflate : app_scale(hwnd, 2);
    borderBrush = CreateSolidBrush(borderColor);
    state->paintingPages = TRUE;
    for (pageNumber = firstPage; pageNumber <= lastPage; ++pageNumber) {
        RECT page;
        RECT shadow;
        RECT border;
        RECT bounds;
        RECT intersection;

        if (!pageview_page_rect(state, pageNumber, &page)) {
            continue;
        }
        shadow = page;
        OffsetRect(&shadow, shadowOffset, shadowOffset);
        InflateRect(&shadow, shadowInflate, shadowInflate);
        border = page;
        InflateRect(&border, 1, 1);
        UnionRect(&bounds, &shadow, &border);
        if (!IntersectRect(&intersection, &bounds, &dirty)) {
            continue;
        }
        pageview_fill_rect(dc, &shadow, shadowColor);
        pageview_fill_rect(dc, &page, paper);
        if (pageNumber != state->visiblePage &&
            !state->app->paginationDirty) {
            HENHMETAFILE preview = state->previewRenderingDeferred
                                        ? pageview_lookup_preview(
                                              state, pageNumber, TRUE)
                                        : pageview_cached_preview(
                                              state, dc, pageNumber);
            if (preview != NULL) {
                PlayEnhMetaFile(dc, preview, &page);
            }
        }
        if (borderBrush != NULL) {
            FrameRect(dc, &border, borderBrush);
        }
    }
    state->paintingPages = FALSE;
    if (borderBrush != NULL) {
        DeleteObject(borderBrush);
    }
    comments_paint_margin(state->app, dc, &dirty);
    if (!state->app->paginationDirty) {
        pageview_schedule_preview_warm(state, hwnd);
    }
}

static BOOL pageview_preview_needs_warm(PageViewState *state,
                                        LONG page)
{
    return state != NULL && state->app != NULL && page >= 1 &&
           page <= state->app->pageCount && page != state->visiblePage &&
           pageview_lookup_preview(state, page, FALSE) == NULL;
}

static LONG pageview_next_preview_to_warm(PageViewState *state, HWND hwnd)
{
    LONG firstPage = 0;
    LONG lastPage = 0;
    LONG warmFirst;
    LONG warmLast;
    LONG visibleCount;
    LONG remainingSlots;
    LONG page;
    LONG candidates[3];
    SIZE_T index;

    if (state == NULL || state->app == NULL ||
        state->app->paginationDirty || state->app->pageEnds == NULL) {
        return 0;
    }
    pageview_visible_page_range(state, hwnd, &firstPage, &lastPage, NULL);
    if (firstPage <= 0 || lastPage < firstPage) {
        return 0;
    }
    warmFirst = firstPage;
    warmLast = lastPage;
    visibleCount = lastPage - firstPage + 1;
    if (visibleCount > PAGEVIEW_PREVIEW_CACHE_CAPACITY) {
        if (state->lastScrollDirection < 0) {
            warmLast = firstPage + PAGEVIEW_PREVIEW_CACHE_CAPACITY - 1;
        } else {
            warmFirst = lastPage - PAGEVIEW_PREVIEW_CACHE_CAPACITY + 1;
        }
        visibleCount = PAGEVIEW_PREVIEW_CACHE_CAPACITY;
    }
    for (page = warmFirst; page <= warmLast; ++page) {
        if (pageview_preview_needs_warm(state, page)) {
            return page;
        }
    }
    if (state->lastScrollDirection < 0) {
        candidates[0] = warmFirst - 1;
        candidates[1] = warmFirst - 2;
        candidates[2] = warmLast + 1;
    } else {
        candidates[0] = warmLast + 1;
        candidates[1] = warmLast + 2;
        candidates[2] = warmFirst - 1;
    }
    remainingSlots = PAGEVIEW_PREVIEW_CACHE_CAPACITY - visibleCount;
    for (index = 0; index < ARRAYSIZE(candidates) &&
                    (LONG)index < remainingSlots; ++index) {
        if (pageview_preview_needs_warm(state, candidates[index])) {
            return candidates[index];
        }
    }
    return 0;
}

static void pageview_schedule_preview_warm(PageViewState *state, HWND hwnd)
{
    if (state == NULL || hwnd == NULL || state->previewWarmScheduled ||
        state->app == NULL ||
        state->app->paginationDirty) {
        return;
    }
    if (SetTimer(hwnd, PAGEVIEW_PREVIEW_TIMER_ID,
                 PAGEVIEW_PREVIEW_WARM_DELAY_MS, NULL) != 0) {
        state->previewWarmScheduled = TRUE;
    }
}

static void pageview_warm_one_preview(PageViewState *state, HWND hwnd)
{
    LONG page;
    HDC dc;
    HENHMETAFILE preview;
    RECT pageRect;
    BOOL wasDeferred;

    if (state == NULL) {
        return;
    }
    KillTimer(hwnd, PAGEVIEW_PREVIEW_TIMER_ID);
    state->previewWarmScheduled = FALSE;
    wasDeferred = state->previewRenderingDeferred;
    if (state->app == NULL || state->app->paginationDirty) {
        return;
    }
    page = pageview_next_preview_to_warm(state, hwnd);
    if (page == 0) {
        if (!state->scrollAnimating && !state->thumbTracking) {
            state->previewRenderingDeferred = FALSE;
            if (wasDeferred) {
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return;
    }
    dc = GetDC(hwnd);
    if (dc == NULL) {
        state->previewRenderingDeferred = FALSE;
        if (wasDeferred) {
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return;
    }
    preview = pageview_cached_preview(state, dc, page);
    ReleaseDC(hwnd, dc);
    if (preview == NULL) {
        state->previewRenderingDeferred = FALSE;
        if (wasDeferred) {
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return;
    }
    if (pageview_page_rect(state, page, &pageRect)) {
        int padding = state->previewInvalidatePadding > 0
                          ? state->previewInvalidatePadding
                          : app_scale(hwnd, 8);
        InflateRect(&pageRect, padding, padding);
        InvalidateRect(hwnd, &pageRect, FALSE);
    }
    pageview_schedule_preview_warm(state, hwnd);
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

static BOOL pageview_handle_scroll(PageViewState *state, HWND hwnd,
                                   int bar, UINT command)
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
    if (bar == SB_VERT && position !=
                              pageview_scroll_position(hwnd, SB_VERT)) {
        state->lastScrollDirection =
            position < pageview_scroll_position(hwnd, SB_VERT) ? -1 : 1;
    }
    {
        BOOL changed = pageview_apply_scroll_position(state, hwnd, bar,
                                                       position);
        if (bar == SB_VERT) {
            state->scrollTargetY = pageview_scroll_position(hwnd, SB_VERT);
        }
        return changed;
    }
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
        LONGLONG numerator = state->horizontalWheelRemainder +
                             (LONGLONG)delta * app_scale(hwnd, 48);
        int distance = pageview_saturate_int64(numerator / WHEEL_DELTA);
        state->horizontalWheelRemainder = numerator % WHEEL_DELTA;
        if (distance != 0) {
            state->previewRenderingDeferred = TRUE;
            pageview_apply_scroll_position(
                state, hwnd, SB_HORZ,
                pageview_saturate_int64(
                    (LONGLONG)pageview_scroll_position(hwnd, SB_HORZ) +
                    distance));
            pageview_schedule_preview_warm(state, hwnd);
        }
        return 0;
    }
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    if (lines == WHEEL_PAGESCROLL) {
        RECT client;
        GetClientRect(hwnd, &client);
        step = max(1, client.bottom - client.top - app_scale(hwnd, 32));
    } else {
        if (lines == 0) {
            return 0;
        }
        step = pageview_saturate_int64(
            (LONGLONG)app_scale(hwnd, 16) * lines);
    }
    if (step < 1) {
        step = 1;
    }
    {
        LONGLONG numerator = state->verticalWheelRemainder -
                             (LONGLONG)delta * step;
        int distance = pageview_saturate_int64(numerator / WHEEL_DELTA);
        int base = state->scrollAnimating
                       ? state->scrollTargetY
                       : pageview_scroll_position(hwnd, SB_VERT);
        state->verticalWheelRemainder = numerator % WHEEL_DELTA;
        if (distance != 0) {
            pageview_set_smooth_scroll_target(
                state, hwnd,
                pageview_saturate_int64((LONGLONG)base + distance));
        }
    }
    return 0;
}

static LRESULT CALLBACK pageview_editor_subclass_proc(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR subclassId, DWORD_PTR referenceData)
{
    AppState *app = (AppState *)referenceData;
    (void)subclassId;

    if ((message == EM_SETSEL || message == EM_EXSETSEL) &&
        app != NULL && app->pageView != NULL &&
        !app->paginationDirty && app->pageEnds != NULL) {
        LONG character = -1;
        if (message == EM_SETSEL) {
            character = (LONG)wParam;
        } else if (lParam != 0) {
            const CHARRANGE *range = (const CHARRANGE *)lParam;
            character = range->cpMin;
        }
        if (character >= 0) {
            LONG page = pageview_page_from_character(app, character);
            PageViewState *state = pageview_get_state(app->pageView);
            if (state != NULL && page >= 1 &&
                page <= app->pageCount && page != state->visiblePage) {
                /*
                 * A windowless RichEdit does not automatically unpark its
                 * VM_PAGE host when a selection targets another page. Move
                 * both the service page and its clipped HWND into view before
                 * forwarding either selection message.
                 */
                pageview_navigate_display(app, page);
                pageview_apply_scroll_position(
                    state, app->pageView, SB_VERT,
                    pageview_saturate_int64(
                        (LONGLONG)(page - 1) * state->pagePitch));
            }
        }
    }

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
        state->lastScrollDirection = 1;
        state->scrollPixelsPerSecond =
            PAGEVIEW_MAX_SCROLL_PIXELS_PER_SECOND;
        QueryPerformanceFrequency(&state->performanceFrequency);
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
            pageview_paint(state, hwnd, dc, &paint.rcPaint);
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
            pageview_cancel_scroll_animation(state, hwnd);
            if (command == SB_ENDSCROLL) {
                state->thumbTracking = FALSE;
                state->previewRenderingDeferred = TRUE;
                pageview_schedule_preview_warm(state, hwnd);
                return 0;
            }
            state->thumbTracking = command == SB_THUMBTRACK;
            state->previewRenderingDeferred = TRUE;
            pageview_handle_scroll(state, hwnd, bar, command);
            if (command != SB_THUMBTRACK) {
                pageview_schedule_preview_warm(state, hwnd);
            }
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        if (state != NULL && state->thumbTracking) {
            state->thumbTracking = FALSE;
            pageview_schedule_preview_warm(state, hwnd);
        }
        break;
    case WM_MOUSEWHEEL:
        return pageview_handle_mouse_wheel(state, hwnd, wParam, FALSE);
    case WM_MOUSEHWHEEL:
        return pageview_handle_mouse_wheel(state, hwnd, wParam, TRUE);
    case WM_TIMER:
        if (state != NULL && wParam == PAGEVIEW_SCROLL_TIMER_ID) {
            pageview_scroll_animation_frame(state, hwnd, FALSE);
            return 0;
        }
        if (state != NULL && wParam == PAGEVIEW_PREVIEW_TIMER_ID) {
            pageview_warm_one_preview(state, hwnd);
            return 0;
        }
        break;
    case WM_SETFOCUS:
        if (app != NULL && app->editor != NULL) {
            SetFocus(app->editor);
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (app != NULL) {
            POINT point;
            point.x = (short)LOWORD(lParam);
            point.y = (short)HIWORD(lParam);
            if (comments_handle_margin_click(app, point)) {
                return 0;
            }
        }
        return pageview_forward_page_click(state, hwnd, message, wParam,
                                           lParam);
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
            KillTimer(hwnd, PAGEVIEW_SCROLL_TIMER_ID);
            KillTimer(hwnd, PAGEVIEW_PREVIEW_TIMER_ID);
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
        (HMENU)(INT_PTR)IDC_PAGE_VIEW, app->instance, app);
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
    RECT viewInsets;
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
    int gutter;
    int pageGap;
    int pagePitch;
    int commentMarginWidth;
    int commentMarginGap;
    int commentExtraWidth;
    int groupWidth;
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
    pageview_cancel_scroll_animation(state, app->pageView);
    state->layingOut = TRUE;
    GetClientRect(app->pageView, &client);
    viewportWidth = max(1, client.right - client.left);
    viewportHeight = max(1, client.bottom - client.top);
    pageview_get_dpi(app->pageView, &dpiX, &dpiY);
    pageview_get_page_units(app, &pageWidthUnits, &pageHeightUnits);
    zoom = app->zoomPercent > 0 ? app->zoomPercent : 100;
    pageWidth = pageview_thousandths_to_pixels(pageWidthUnits, dpiX, zoom);
    pageHeight = pageview_thousandths_to_pixels(pageHeightUnits, dpiY, zoom);
    if (pageWidth < 1) {
        pageWidth = 1;
    }
    if (pageHeight < 1) {
        pageHeight = 1;
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
    state->commentsVisible = comments_count(app) > 0;
    commentMarginWidth = state->commentsVisible
                             ? app_scale(app->pageView, 286) : 0;
    commentMarginGap = state->commentsVisible
                           ? app_scale(app->pageView, 18) : 0;
    if (state->commentsVisible) {
        commentMarginWidth = max(app_scale(app->pageView, 220),
                                 commentMarginWidth);
        commentMarginGap = max(1, commentMarginGap);
    }
    commentExtraWidth = pageview_saturate_int64(
        (LONGLONG)commentMarginGap + commentMarginWidth);
    groupWidth = pageview_saturate_int64(
        (LONGLONG)pageWidth + commentExtraWidth);
    pagePitch = pageHeight > INT_MAX - pageGap
                    ? INT_MAX
                    : pageHeight + pageGap;
    canvasWidth = pageview_saturate_int64(
        (LONGLONG)groupWidth + (LONGLONG)gutter * 2);
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
    state->scrollTargetY = verticalPosition;
    if (canvasWidth <= viewportWidth) {
        pageLeft = client.left +
                   (viewportWidth - groupWidth) / 2;
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
    state->commentMarginWidth = commentMarginWidth;
    state->commentMarginGap = commentMarginGap;
    state->pageLeft = pageLeft;
    state->firstPageTop = firstPageTop;
    state->canvasWidth = canvasWidth;
    state->canvasHeight = canvasHeight;
    state->scrollPixelsPerSecond = max(
        1, MulDiv(PAGEVIEW_MAX_SCROLL_PIXELS_PER_SECOND, dpiY, 96));
    state->shadowOffset = max(1, MulDiv(6, dpiY, 96));
    state->shadowInflate = max(1, MulDiv(2, dpiY, 96));
    state->previewInvalidatePadding = max(1, MulDiv(8, dpiY, 96));

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
            state->editorOnPage = TRUE;
        } else {
            MoveWindow(app->editor, client.left,
                       client.top - pageHeight - app_scale(app->pageView, 4),
                       pageWidth, pageHeight, TRUE);
            state->editorOnPage = FALSE;
        }
        ShowWindow(app->editor, SW_SHOWNOACTIVATE);

        marginLeft = pageview_thousandths_to_himetric(
            app->pageMargins.left);
        marginTop = pageview_thousandths_to_himetric(
            app->pageMargins.top);
        marginRight = pageview_thousandths_to_himetric(
            app->pageMargins.right);
        marginBottom = pageview_thousandths_to_himetric(
            app->pageMargins.bottom);
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
            SetRect(&viewInsets, marginLeft, marginTop,
                    marginRight, marginBottom);
            state->switchingPage = TRUE;
            SendMessageW(app->editor, EM_SETVIEWKIND, VM_PAGE, 0);
            SendMessageW(app->editor, EM_SETZOOM, (WPARAM)zoom, 100);
            dc = GetDC(app->editor);
            if (dc != NULL) {
                SendMessageW(app->editor, EM_SETTARGETDEVICE, (WPARAM)dc,
                             (LPARAM)printableWidthTwips);
                ReleaseDC(app->editor, dc);
            }
            render_editor_set_view_insets(app->editor, &viewInsets);
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
    state->previewRenderingDeferred = FALSE;
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
    if (state != NULL) {
        pageview_cancel_scroll_animation(state, app->pageView);
        state->previewRenderingDeferred = FALSE;
    }
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
    if (state != NULL && ensureVisible) {
        pageview_cancel_scroll_animation(state, app->pageView);
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

LONG pageview_character_page(AppState *app, LONG character)
{
    if (character < 0) {
        character = 0;
    }
    return pageview_page_from_character(app, character);
}

BOOL pageview_get_comment_margin_rect(AppState *app, LONG page, RECT *rect)
{
    PageViewState *state;
    RECT pageRect;

    if (rect != NULL) {
        SetRectEmpty(rect);
    }
    if (app == NULL || app->pageView == NULL || rect == NULL) {
        return FALSE;
    }
    state = pageview_get_state(app->pageView);
    if (state == NULL || !state->commentsVisible ||
        state->commentMarginWidth <= 0 ||
        !pageview_page_rect(state, page, &pageRect)) {
        return FALSE;
    }
    rect->left = pageview_saturate_int64(
        (LONGLONG)pageRect.right + state->commentMarginGap);
    rect->top = pageRect.top;
    rect->right = pageview_saturate_int64(
        (LONGLONG)rect->left + state->commentMarginWidth);
    rect->bottom = pageRect.bottom;
    return rect->right > rect->left && rect->bottom > rect->top;
}

BOOL pageview_map_character_to_client(AppState *app, LONG character,
                                      LONG *page, POINT *point)
{
    PageViewState *state;
    RECT pageRect;
    POINT local;
    LONG mappedPage;
    LONG displayedPage;
    LONG pageStart;
    LONG pageEnd;
    LONG span;
    LONG offset;
    int dpiX;
    int dpiY;
    int zoom;
    int marginTop;
    int marginBottom;
    int printableHeight;

    if (page != NULL) {
        *page = 0;
    }
    if (point != NULL) {
        point->x = 0;
        point->y = 0;
    }
    if (app == NULL || app->pageView == NULL || app->editor == NULL ||
        point == NULL) {
        return FALSE;
    }
    state = pageview_get_state(app->pageView);
    if (state == NULL) {
        return FALSE;
    }
    if (state->pageWidth <= 0 || state->pageHeight <= 0) {
        pageview_layout(app);
    }
    character = max(0, character);
    mappedPage = pageview_page_from_character(app, character);
    if (!pageview_page_rect(state, mappedPage, &pageRect)) {
        return FALSE;
    }
    if (page != NULL) {
        *page = mappedPage;
    }

    displayedPage = (LONG)SendMessageW(app->editor, EM_GETPAGE, 0, 0) + 1;
    local.x = -1;
    local.y = -1;
    if (displayedPage == mappedPage) {
        SendMessageW(app->editor, EM_POSFROMCHAR, (WPARAM)&local,
                     (LPARAM)character);
        if (local.x >= 0 && local.y >= 0 &&
            local.y < pageRect.bottom - pageRect.top) {
            point->x = pageRect.right;
            point->y = pageRect.top + local.y;
            return TRUE;
        }
    }

    pageStart = pageview_page_start(app, mappedPage);
    if (mappedPage < app->pageCount && app->pageEnds != NULL) {
        pageEnd = app->pageEnds[mappedPage - 1];
    } else {
        pageEnd = (LONG)SendMessageW(app->editor, WM_GETTEXTLENGTH, 0, 0);
    }
    if (pageEnd < pageStart) {
        pageEnd = pageStart;
    }
    span = max(1, pageEnd - pageStart);
    offset = pageview_clamp_long(character - pageStart, 0, span);
    pageview_get_dpi(app->pageView, &dpiX, &dpiY);
    zoom = app->zoomPercent > 0 ? app->zoomPercent : 100;
    marginTop = pageview_thousandths_to_pixels(
        app->pageMargins.top, dpiY, zoom);
    marginBottom = pageview_thousandths_to_pixels(
        app->pageMargins.bottom, dpiY, zoom);
    marginTop = pageview_clamp_int(marginTop, 0, state->pageHeight - 1);
    marginBottom = pageview_clamp_int(marginBottom, 0,
                                      state->pageHeight - 1);
    printableHeight = max(1, state->pageHeight - marginTop - marginBottom);
    point->x = pageRect.right;
    point->y = pageRect.top + marginTop +
               pageview_saturate_int64(
                   ((LONGLONG)printableHeight * offset) / span);
    (void)dpiX;
    return TRUE;
}

LRESULT pageview_query_state(AppState *app, UINT query, LPARAM component)
{
    PageViewState *state;
    RECT rendererInsets;
    LONG firstPage = 0;
    LONG lastPage = 0;
    LONG fullyVisible = 0;
    LONG index = (LONG)component;
    int dpiX;
    int dpiY;
    int zoom;

    if (app == NULL || app->pageView == NULL) {
        return 0;
    }
    state = pageview_get_state(app->pageView);
    if (state == NULL) {
        return 0;
    }
    switch (query) {
    case WCQ_SCROLL_ANIMATING:
        return state->scrollAnimating;
    case WCQ_SCROLL_TARGET_Y:
        return state->scrollTargetY;
    case WCQ_SCROLL_FRAME_COUNT:
        return state->scrollFrameCount;
    case WCQ_SCROLL_FRAME_INTERVAL_MS:
        return PAGEVIEW_SCROLL_FRAME_MS;
    default:
        break;
    }
    if (query >= WCQ_PAGE_MARGIN_THOUSANDTHS &&
        query <= WCQ_PAGE_LAYOUT_SIZE_PIXELS) {
        switch (query) {
        case WCQ_PAGE_MARGIN_THOUSANDTHS:
            switch (index) {
            case 0: return app->pageMargins.left;
            case 1: return app->pageMargins.top;
            case 2: return app->pageMargins.right;
            case 3: return app->pageMargins.bottom;
            default: return -1;
            }
        case WCQ_PAGE_SIZE_THOUSANDTHS:
            if (index == 0) {
                return app->pageSize.x;
            }
            return index == 1 ? app->pageSize.y : -1;
        case WCQ_PAGE_LAYOUT_MARGIN_PIXELS:
            pageview_get_dpi(app->pageView, &dpiX, &dpiY);
            zoom = app->zoomPercent > 0 ? app->zoomPercent : 100;
            if (!render_editor_get_view_insets(app->editor,
                                                &rendererInsets)) {
                return -1;
            }
            switch (index) {
            case 0:
                return MulDiv(rendererInsets.left, dpiX * zoom,
                              2540 * 100);
            case 1:
                return MulDiv(rendererInsets.top, dpiY * zoom,
                              2540 * 100);
            case 2:
                return MulDiv(rendererInsets.right, dpiX * zoom,
                              2540 * 100);
            case 3:
                return MulDiv(rendererInsets.bottom, dpiY * zoom,
                              2540 * 100);
            default:
                return -1;
            }
        case WCQ_PAGE_LAYOUT_SIZE_PIXELS:
            if (index == 0) {
                return state->pageWidth;
            }
            return index == 1 ? state->pageHeight : -1;
        default:
            return 0;
        }
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

BOOL pageview_is_scrolling(AppState *app)
{
    PageViewState *state;

    if (app == NULL || app->pageView == NULL) {
        return FALSE;
    }
    state = pageview_get_state(app->pageView);
    return state != NULL &&
           (state->scrollAnimating || state->thumbTracking);
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
