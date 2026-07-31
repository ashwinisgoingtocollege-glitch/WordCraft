#include "editor.h"

#include <stdint.h>

/*
 * The document model already has one deliberately narrow path for persistent
 * static vector pictures.  Keep the drawing surface independent from the
 * editor, then use that path only after the user explicitly chooses Insert.
 */

#define DRAW_CANVAS_CLASS_NAME L"WordCraftDrawingCanvas"

#define DRAW_MAX_STROKES 256u
#define DRAW_MAX_POINTS 65536u
#define DRAW_MAX_POINTS_PER_STROKE 4096u
#define DRAW_INITIAL_POINT_CAPACITY 32u
#define DRAW_MAX_ACTIONS 512u
#define DRAW_DELETED_WORDS ((DRAW_MAX_STROKES + 31u) / 32u)
#define DRAW_EMF_MAX_BYTES ((SIZE_T)5u * 1024u * 1024u)

#define IDC_DRAW_CLEAR 0x7201
#define IDC_DRAW_INSERT 0x7202
#define IDC_DRAW_CANCEL 0x7203

typedef struct DrawStroke {
    POINT *points;
    UINT count;
    UINT capacity;
    UINT tool;
    COLORREF color;
    int width;
    BOOL highlighter;
    BOOL deleted;
} DrawStroke;

typedef enum DrawActionKind {
    DRAW_ACTION_ADD = 1,
    DRAW_ACTION_ERASE,
    DRAW_ACTION_CLEAR
} DrawActionKind;

typedef struct DrawAction {
    DrawActionKind kind;
    UINT stroke;
    BOOL previousDeleted;
    DWORD deleted[DRAW_DELETED_WORDS];
} DrawAction;

typedef struct DrawToolSpec {
    UINT command;
    const WCHAR *caption;
} DrawToolSpec;

typedef struct DrawCanvasState {
    AppState *app;
    HWND window;
    HWND toolButtons[8];
    HWND undoButton;
    HWND clearButton;
    HWND insertButton;
    HWND cancelButton;
    RECT canvasRect;
    UINT dpi;
    UINT tool;
    BOOL showRuler;
    BOOL ruledBackground;
    BOOL drawing;
    BOOL accepted;
    BOOL finished;
    UINT activeStroke;
    UINT lastErasedStroke;
    DrawStroke strokes[DRAW_MAX_STROKES];
    UINT strokeCount;
    UINT pointCount;
    DrawAction actions[DRAW_MAX_ACTIONS];
    UINT actionCount;
    WCHAR notice[160];
} DrawCanvasState;

static const DrawToolSpec drawToolSpecs[] = {
    {IDM_DRAW_PEN_BLACK, L"Black"},
    {IDM_DRAW_PEN_RED, L"Red"},
    {IDM_DRAW_PENCIL, L"Pencil"},
    {IDM_DRAW_HIGHLIGHTER, L"Highlight"},
    {IDM_DRAW_PEN_BLUE, L"Blue"},
    {IDM_DRAW_PEN_GREEN, L"Green"},
    {IDM_DRAW_ACTION_PEN, L"Action"},
    {IDM_DRAW_ERASER, L"Eraser"}
};

_Static_assert(ARRAYSIZE(drawToolSpecs) == 8,
               "drawing canvas tool button count must stay synchronized");

static LRESULT CALLBACK draw_canvas_window_proc(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam);

static int draw_clamp_int(int value, int minimum, int maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static UINT draw_query_dpi(HWND window)
{
    typedef UINT(WINAPI *GetDpiForWindowProc)(HWND);
    static GetDpiForWindowProc getDpiForWindow;
    static BOOL resolved;
    HDC dc;
    HWND dcWindow;
    int dpi;

    if (!resolved) {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32 != NULL) {
            getDpiForWindow = (GetDpiForWindowProc)(void *)
                GetProcAddress(user32, "GetDpiForWindow");
        }
        resolved = TRUE;
    }
    if (getDpiForWindow != NULL && window != NULL) {
        UINT value = getDpiForWindow(window);
        if (value != 0) {
            return value;
        }
    }
    dcWindow = window;
    dc = GetDC(dcWindow);
    if (dc == NULL) {
        dcWindow = NULL;
        dc = GetDC(dcWindow);
    }
    if (dc == NULL) {
        return 96u;
    }
    dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(dcWindow, dc);
    return dpi > 0 ? (UINT)dpi : 96u;
}

static BOOL draw_get_work_area(HWND window, RECT *workArea)
{
    MONITORINFO monitorInfo;
    HMONITOR monitor;

    if (workArea == NULL) {
        return FALSE;
    }
    monitor = MonitorFromWindow(
        window, MONITOR_DEFAULTTONEAREST);
    ZeroMemory(&monitorInfo, sizeof(monitorInfo));
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (monitor == NULL ||
        !GetMonitorInfoW(monitor, &monitorInfo)) {
        return FALSE;
    }
    *workArea = monitorInfo.rcWork;
    return TRUE;
}

static void draw_clamp_window_rect_to_work_area(
    HWND reference, RECT *windowRect)
{
    RECT workArea;
    int width;
    int height;

    if (windowRect == NULL ||
        !draw_get_work_area(reference, &workArea)) {
        return;
    }
    width = min(windowRect->right - windowRect->left,
                workArea.right - workArea.left);
    height = min(windowRect->bottom - windowRect->top,
                 workArea.bottom - workArea.top);
    width = max(1, width);
    height = max(1, height);
    windowRect->left = draw_clamp_int(
        windowRect->left, workArea.left, workArea.right - width);
    windowRect->top = draw_clamp_int(
        windowRect->top, workArea.top, workArea.bottom - height);
    windowRect->right = windowRect->left + width;
    windowRect->bottom = windowRect->top + height;
}

static int draw_scale(const DrawCanvasState *state, int value)
{
    UINT dpi = state != NULL && state->dpi != 0 ? state->dpi : 96u;
    return MulDiv(value, (int)dpi, 96);
}

static DrawCanvasState *draw_state(HWND window)
{
    return (DrawCanvasState *)GetWindowLongPtrW(window, GWLP_USERDATA);
}

static void draw_set_notice(DrawCanvasState *state, const WCHAR *notice)
{
    if (state == NULL) {
        return;
    }
    StringCchCopyW(state->notice, ARRAYSIZE(state->notice),
                   notice != NULL ? notice : L"");
    if (state->window != NULL) {
        InvalidateRect(state->window, NULL, FALSE);
    }
}

static BOOL draw_tool_supported(UINT command)
{
    size_t index;

    for (index = 0; index < ARRAYSIZE(drawToolSpecs); ++index) {
        if (drawToolSpecs[index].command == command) {
            return TRUE;
        }
    }
    return FALSE;
}

static int draw_ruler_band_height(const DrawCanvasState *state)
{
    return state != NULL && state->showRuler
               ? max(18, draw_scale(state, 24)) : 0;
}

static const WCHAR *draw_tool_name(UINT command)
{
    size_t index;

    for (index = 0; index < ARRAYSIZE(drawToolSpecs); ++index) {
        if (drawToolSpecs[index].command == command) {
            return drawToolSpecs[index].caption;
        }
    }
    return L"Black";
}

static void draw_style(UINT command, UINT dpi, COLORREF *color, int *width,
                       BOOL *highlighter)
{
    int baseWidth = 3;
    COLORREF value = RGB(25, 25, 25);
    BOOL highlight = FALSE;

    switch (command) {
    case IDM_DRAW_PEN_RED:
        value = RGB(214, 42, 55);
        baseWidth = 4;
        break;
    case IDM_DRAW_PENCIL:
        value = RGB(85, 91, 99);
        baseWidth = 2;
        break;
    case IDM_DRAW_HIGHLIGHTER:
        value = RGB(255, 239, 112);
        baseWidth = 14;
        highlight = TRUE;
        break;
    case IDM_DRAW_PEN_BLUE:
        value = RGB(38, 100, 190);
        baseWidth = 4;
        break;
    case IDM_DRAW_PEN_GREEN:
        value = RGB(22, 139, 87);
        baseWidth = 4;
        break;
    case IDM_DRAW_ACTION_PEN:
        value = RGB(67, 112, 211);
        baseWidth = 5;
        break;
    case IDM_DRAW_PEN_BLACK:
    default:
        break;
    }
    if (color != NULL) {
        *color = value;
    }
    if (width != NULL) {
        *width = max(1, MulDiv(baseWidth, (int)(dpi != 0 ? dpi : 96u), 96));
    }
    if (highlighter != NULL) {
        *highlighter = highlight;
    }
}

static BOOL draw_any_strokes(const DrawCanvasState *state)
{
    UINT index;

    if (state == NULL) {
        return FALSE;
    }
    for (index = 0; index < state->strokeCount; ++index) {
        if (!state->strokes[index].deleted &&
            state->strokes[index].count != 0) {
            return TRUE;
        }
    }
    return FALSE;
}

static void draw_refresh_command_state(DrawCanvasState *state)
{
    size_t index;

    if (state == NULL) {
        return;
    }
    for (index = 0; index < ARRAYSIZE(drawToolSpecs); ++index) {
        if (state->toolButtons[index] != NULL) {
            SendMessageW(
                state->toolButtons[index], BM_SETCHECK,
                state->tool == drawToolSpecs[index].command
                    ? BST_CHECKED : BST_UNCHECKED,
                0);
        }
    }
    if (state->undoButton != NULL) {
        EnableWindow(state->undoButton, state->actionCount != 0);
    }
    if (state->clearButton != NULL) {
        EnableWindow(state->clearButton, draw_any_strokes(state));
    }
    if (state->insertButton != NULL) {
        EnableWindow(state->insertButton, draw_any_strokes(state));
    }
}

static void draw_set_tool(DrawCanvasState *state, UINT command)
{
    WCHAR notice[160];

    if (state == NULL) {
        return;
    }
    if (!draw_tool_supported(command)) {
        command = IDM_DRAW_PEN_BLACK;
    }
    state->tool = command;
    StringCchPrintfW(
        notice, ARRAYSIZE(notice),
        command == IDM_DRAW_ERASER
            ? L"%s selected — drag across a stroke to remove it"
            : L"%s selected — drag on the page to draw",
        draw_tool_name(command));
    draw_set_notice(state, notice);
    draw_refresh_command_state(state);
}

static void draw_release_stroke(DrawCanvasState *state, DrawStroke *stroke)
{
    if (stroke == NULL) {
        return;
    }
    if (stroke->points != NULL) {
        HeapFree(GetProcessHeap(), 0, stroke->points);
    }
    if (state != NULL && state->pointCount >= stroke->count) {
        state->pointCount -= stroke->count;
    }
    ZeroMemory(stroke, sizeof(*stroke));
}

static void draw_release_all(DrawCanvasState *state)
{
    UINT index;

    if (state == NULL) {
        return;
    }
    for (index = 0; index < state->strokeCount; ++index) {
        draw_release_stroke(state, &state->strokes[index]);
    }
    state->strokeCount = 0;
    state->pointCount = 0;
    state->actionCount = 0;
}

static void draw_push_action(DrawCanvasState *state,
                             const DrawAction *action)
{
    if (state == NULL || action == NULL) {
        return;
    }
    if (state->actionCount == DRAW_MAX_ACTIONS) {
        MoveMemory(
            &state->actions[0], &state->actions[1],
            (DRAW_MAX_ACTIONS - 1u) * sizeof(state->actions[0]));
        --state->actionCount;
    }
    state->actions[state->actionCount++] = *action;
}

static BOOL draw_reserve_points(DrawStroke *stroke, UINT needed)
{
    POINT *points;
    UINT capacity;

    if (stroke == NULL || needed > DRAW_MAX_POINTS_PER_STROKE) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (needed <= stroke->capacity) {
        return TRUE;
    }
    capacity = stroke->capacity != 0
                   ? stroke->capacity : DRAW_INITIAL_POINT_CAPACITY;
    while (capacity < needed) {
        if (capacity >= DRAW_MAX_POINTS_PER_STROKE / 2u) {
            capacity = DRAW_MAX_POINTS_PER_STROKE;
            break;
        }
        capacity *= 2u;
    }
    if (stroke->points == NULL) {
        points = HeapAlloc(GetProcessHeap(), 0,
                           (SIZE_T)capacity * sizeof(*points));
    } else {
        points = HeapReAlloc(GetProcessHeap(), 0, stroke->points,
                             (SIZE_T)capacity * sizeof(*points));
    }
    if (points == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    stroke->points = points;
    stroke->capacity = capacity;
    return TRUE;
}

static BOOL draw_append_point(DrawCanvasState *state, DrawStroke *stroke,
                              POINT point, BOOL force)
{
    POINT previous;
    int minimum;

    if (state == NULL || stroke == NULL) {
        return FALSE;
    }
    if (stroke->count != 0) {
        previous = stroke->points[stroke->count - 1u];
        minimum = max(1, draw_scale(state, 2));
        if (!force &&
            abs(point.x - previous.x) < minimum &&
            abs(point.y - previous.y) < minimum) {
            return TRUE;
        }
        if (point.x == previous.x && point.y == previous.y) {
            return TRUE;
        }
    }
    if (state->pointCount >= DRAW_MAX_POINTS ||
        stroke->count >= DRAW_MAX_POINTS_PER_STROKE) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        draw_set_notice(
            state,
            L"The bounded drawing limit was reached; insert or clear the canvas.");
        return FALSE;
    }
    if (!draw_reserve_points(stroke, stroke->count + 1u)) {
        draw_set_notice(
            state, L"WordCraft could not allocate another drawing point.");
        return FALSE;
    }
    stroke->points[stroke->count++] = point;
    ++state->pointCount;
    return TRUE;
}

static POINT draw_clamp_to_canvas(const DrawCanvasState *state,
                                  POINT client)
{
    POINT result;
    int width = max(1, state->canvasRect.right - state->canvasRect.left);
    int height = max(1, state->canvasRect.bottom - state->canvasRect.top);
    int minimumY = min(height - 1, draw_ruler_band_height(state));

    result.x = draw_clamp_int(
        client.x - state->canvasRect.left, 0, width - 1);
    result.y = draw_clamp_int(
        client.y - state->canvasRect.top, minimumY, height - 1);
    return result;
}

static BOOL draw_point_in_canvas(const DrawCanvasState *state, POINT point)
{
    return state != NULL && PtInRect(&state->canvasRect, point) &&
           point.y >= state->canvasRect.top +
                          draw_ruler_band_height(state);
}

static BOOL draw_compact_deleted_strokes(DrawCanvasState *state)
{
    UINT readIndex;
    UINT writeIndex = 0;
    UINT retainedCount;
    UINT retainedPoints = 0;
    BOOL compacted = FALSE;

    if (state == NULL || state->drawing) {
        return FALSE;
    }
    for (readIndex = 0; readIndex < state->strokeCount; ++readIndex) {
        DrawStroke *source = &state->strokes[readIndex];

        if (source->deleted) {
            draw_release_stroke(state, source);
            compacted = TRUE;
            continue;
        }
        if (writeIndex != readIndex) {
            state->strokes[writeIndex] = *source;
            ZeroMemory(source, sizeof(*source));
            compacted = TRUE;
        }
        retainedPoints += state->strokes[writeIndex].count;
        ++writeIndex;
    }
    if (!compacted) {
        return FALSE;
    }
    retainedCount = writeIndex;
    while (writeIndex < state->strokeCount) {
        ZeroMemory(&state->strokes[writeIndex],
                   sizeof(state->strokes[writeIndex]));
        ++writeIndex;
    }
    state->strokeCount = retainedCount;
    state->pointCount = retainedPoints;
    state->actionCount = 0;
    state->lastErasedStroke = UINT_MAX;
    draw_set_notice(
        state,
        L"Erased strokes compacted; the older local Undo history was reset");
    draw_refresh_command_state(state);
    return TRUE;
}

static void draw_begin_stroke(DrawCanvasState *state, POINT point)
{
    DrawStroke *stroke;
    DrawAction action;

    if (state == NULL) {
        return;
    }
    if ((state->strokeCount >= DRAW_MAX_STROKES ||
         state->pointCount >= DRAW_MAX_POINTS) &&
        draw_compact_deleted_strokes(state)) {
        InvalidateRect(state->window, &state->canvasRect, FALSE);
    }
    if (state->strokeCount >= DRAW_MAX_STROKES ||
        state->pointCount >= DRAW_MAX_POINTS) {
        MessageBeep(MB_ICONWARNING);
        draw_set_notice(
            state,
            L"The bounded drawing limit was reached; insert or clear the canvas.");
        return;
    }
    stroke = &state->strokes[state->strokeCount];
    ZeroMemory(stroke, sizeof(*stroke));
    stroke->tool = state->tool;
    draw_style(state->tool, state->dpi, &stroke->color, &stroke->width,
               &stroke->highlighter);
    if (!draw_append_point(
            state, stroke, draw_clamp_to_canvas(state, point), TRUE)) {
        draw_release_stroke(state, stroke);
        return;
    }
    ZeroMemory(&action, sizeof(action));
    action.kind = DRAW_ACTION_ADD;
    action.stroke = state->strokeCount;
    draw_push_action(state, &action);
    state->activeStroke = state->strokeCount;
    ++state->strokeCount;
    state->drawing = TRUE;
    SetCapture(state->window);
    draw_refresh_command_state(state);
}

static void draw_continue_stroke(DrawCanvasState *state, POINT point,
                                 BOOL force)
{
    DrawStroke *stroke;

    if (state == NULL || !state->drawing ||
        state->activeStroke >= state->strokeCount) {
        return;
    }
    stroke = &state->strokes[state->activeStroke];
    (void)draw_append_point(
        state, stroke, draw_clamp_to_canvas(state, point), force);
    InvalidateRect(state->window, &state->canvasRect, FALSE);
}

static void draw_finish_stroke(DrawCanvasState *state, POINT point)
{
    if (state == NULL || !state->drawing) {
        return;
    }
    draw_continue_stroke(state, point, TRUE);
    state->drawing = FALSE;
    if (GetCapture() == state->window) {
        ReleaseCapture();
    }
    draw_refresh_command_state(state);
}

static double draw_segment_distance_squared(POINT point, POINT a, POINT b)
{
    double dx = (double)b.x - a.x;
    double dy = (double)b.y - a.y;
    double lengthSquared = dx * dx + dy * dy;
    double t;
    double nearestX;
    double nearestY;
    double offsetX;
    double offsetY;

    if (lengthSquared <= 0.0) {
        offsetX = (double)point.x - a.x;
        offsetY = (double)point.y - a.y;
        return offsetX * offsetX + offsetY * offsetY;
    }
    t = (((double)point.x - a.x) * dx +
         ((double)point.y - a.y) * dy) / lengthSquared;
    if (t < 0.0) {
        t = 0.0;
    } else if (t > 1.0) {
        t = 1.0;
    }
    nearestX = a.x + t * dx;
    nearestY = a.y + t * dy;
    offsetX = point.x - nearestX;
    offsetY = point.y - nearestY;
    return offsetX * offsetX + offsetY * offsetY;
}

static BOOL draw_stroke_hit(const DrawStroke *stroke, POINT point,
                            int radius)
{
    UINT index;
    double radiusSquared;

    if (stroke == NULL || stroke->deleted || stroke->count == 0) {
        return FALSE;
    }
    radius += max(1, stroke->width / 2);
    radiusSquared = (double)radius * radius;
    if (stroke->count == 1) {
        return draw_segment_distance_squared(
                   point, stroke->points[0], stroke->points[0]) <=
               radiusSquared;
    }
    for (index = 1; index < stroke->count; ++index) {
        if (draw_segment_distance_squared(
                point, stroke->points[index - 1u],
                stroke->points[index]) <= radiusSquared) {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL draw_erase_at(DrawCanvasState *state, POINT client)
{
    POINT point;
    UINT index;
    int radius;
    DrawAction action;

    if (state == NULL) {
        return FALSE;
    }
    point = draw_clamp_to_canvas(state, client);
    radius = max(4, draw_scale(state, 10));
    for (index = state->strokeCount; index != 0; --index) {
        DrawStroke *stroke = &state->strokes[index - 1u];
        if (index - 1u == state->lastErasedStroke ||
            !draw_stroke_hit(stroke, point, radius)) {
            continue;
        }
        ZeroMemory(&action, sizeof(action));
        action.kind = DRAW_ACTION_ERASE;
        action.stroke = index - 1u;
        action.previousDeleted = stroke->deleted;
        draw_push_action(state, &action);
        stroke->deleted = TRUE;
        state->lastErasedStroke = index - 1u;
        InvalidateRect(state->window, &state->canvasRect, FALSE);
        draw_refresh_command_state(state);
        return TRUE;
    }
    return FALSE;
}

static void draw_clear(DrawCanvasState *state)
{
    DrawAction action;
    UINT index;

    if (state == NULL || !draw_any_strokes(state)) {
        return;
    }
    ZeroMemory(&action, sizeof(action));
    action.kind = DRAW_ACTION_CLEAR;
    for (index = 0; index < state->strokeCount; ++index) {
        if (state->strokes[index].deleted) {
            action.deleted[index / 32u] |=
                (DWORD)1u << (index % 32u);
        }
        state->strokes[index].deleted = TRUE;
    }
    draw_push_action(state, &action);
    draw_set_notice(state, L"Canvas cleared — choose Undo to restore it");
    InvalidateRect(state->window, &state->canvasRect, FALSE);
    draw_refresh_command_state(state);
}

static void draw_undo(DrawCanvasState *state)
{
    DrawAction action;
    UINT index;

    if (state == NULL || state->actionCount == 0) {
        return;
    }
    action = state->actions[--state->actionCount];
    switch (action.kind) {
    case DRAW_ACTION_ADD:
        if (action.stroke < state->strokeCount) {
            if (action.stroke + 1u == state->strokeCount) {
                draw_release_stroke(
                    state, &state->strokes[action.stroke]);
                --state->strokeCount;
            } else {
                state->strokes[action.stroke].deleted = TRUE;
            }
        }
        break;
    case DRAW_ACTION_ERASE:
        if (action.stroke < state->strokeCount) {
            state->strokes[action.stroke].deleted =
                action.previousDeleted;
        }
        break;
    case DRAW_ACTION_CLEAR:
        for (index = 0; index < state->strokeCount; ++index) {
            state->strokes[index].deleted =
                (action.deleted[index / 32u] &
                 ((DWORD)1u << (index % 32u))) != 0;
        }
        break;
    default:
        break;
    }
    draw_set_notice(state, L"Last canvas action undone");
    InvalidateRect(state->window, &state->canvasRect, FALSE);
    draw_refresh_command_state(state);
}

static HPEN draw_create_stroke_pen(const DrawStroke *stroke)
{
    LOGBRUSH brush;
    HPEN pen;

    ZeroMemory(&brush, sizeof(brush));
    brush.lbStyle = BS_SOLID;
    brush.lbColor = stroke->color;
    pen = ExtCreatePen(
        PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND,
        (DWORD)max(1, stroke->width), &brush, 0, NULL);
    if (pen == NULL) {
        pen = CreatePen(PS_SOLID, max(1, stroke->width),
                        stroke->color);
    }
    return pen;
}

static void draw_render_stroke(HDC dc, const DrawStroke *stroke)
{
    HPEN pen;
    HGDIOBJ previousPen;

    if (dc == NULL || stroke == NULL || stroke->deleted ||
        stroke->count == 0) {
        return;
    }
    pen = draw_create_stroke_pen(stroke);
    if (pen == NULL) {
        return;
    }
    previousPen = SelectObject(dc, pen);
    if (stroke->count == 1) {
        HBRUSH brush = CreateSolidBrush(stroke->color);
        HGDIOBJ previousBrush = NULL;
        int radius = max(1, stroke->width / 2);

        if (brush != NULL) {
            previousBrush = SelectObject(dc, brush);
        }
        Ellipse(dc, stroke->points[0].x - radius,
                stroke->points[0].y - radius,
                stroke->points[0].x + radius + 1,
                stroke->points[0].y + radius + 1);
        if (previousBrush != NULL) {
            SelectObject(dc, previousBrush);
        }
        if (brush != NULL) {
            DeleteObject(brush);
        }
    } else {
        Polyline(dc, stroke->points, (int)stroke->count);
    }
    SelectObject(dc, previousPen);
    DeleteObject(pen);
}

static void draw_fill(HDC dc, const RECT *rect, COLORREF color)
{
    COLORREF previous;

    previous = SetDCBrushColor(dc, color);
    FillRect(dc, rect, (HBRUSH)GetStockObject(DC_BRUSH));
    if (previous != CLR_INVALID) {
        SetDCBrushColor(dc, previous);
    }
}

static void draw_render_background(HDC dc, const DrawCanvasState *state,
                                   int width, int height)
{
    RECT page = {0, 0, width, height};

    draw_fill(dc, &page, RGB(255, 255, 255));
    if (state->ruledBackground) {
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(220, 229, 241));
        HGDIOBJ previous = NULL;
        int spacing = max(12, draw_scale(state, 28));
        int y;

        if (pen != NULL) {
            previous = SelectObject(dc, pen);
            for (y = spacing; y < height; y += spacing) {
                MoveToEx(dc, 0, y, NULL);
                LineTo(dc, width, y);
            }
            SelectObject(dc, previous);
            DeleteObject(pen);
        }
    }
}

static void draw_render_strokes(HDC dc, const DrawCanvasState *state)
{
    UINT index;

    for (index = 0; index < state->strokeCount; ++index) {
        draw_render_stroke(dc, &state->strokes[index]);
    }
}

static void draw_render_ruler(HDC dc, const DrawCanvasState *state,
                              int width)
{
    RECT band;
    HPEN pen;
    HGDIOBJ previousPen;
    int height = draw_ruler_band_height(state);
    int step = max(5, draw_scale(state, 10));
    int x;
    int tick = 0;

    if (!state->showRuler) {
        return;
    }
    SetRect(&band, 0, 0, width, height);
    draw_fill(dc, &band, RGB(242, 244, 247));
    pen = CreatePen(PS_SOLID, 1, RGB(115, 122, 132));
    if (pen == NULL) {
        return;
    }
    previousPen = SelectObject(dc, pen);
    MoveToEx(dc, 0, height - 1, NULL);
    LineTo(dc, width, height - 1);
    for (x = 0; x < width; x += step, ++tick) {
        int length = tick % 10 == 0
                         ? height * 2 / 3
                         : tick % 5 == 0 ? height / 2 : height / 3;
        MoveToEx(dc, x, 0, NULL);
        LineTo(dc, x, length);
    }
    SelectObject(dc, previousPen);
    DeleteObject(pen);
}

static void draw_paint(DrawCanvasState *state, HDC dc)
{
    RECT client;
    RECT shadow;
    RECT border;
    RECT footer;
    COLORREF chrome;
    COLORREF text;
    HFONT font;
    HGDIOBJ previousFont = NULL;
    int saved;
    int width;
    int height;
    WCHAR footerText[256];

    if (state == NULL || dc == NULL) {
        return;
    }
    GetClientRect(state->window, &client);
    chrome = state->app->useBrandColors
                 ? state->app->palette.formatBackground
                 : GetSysColor(COLOR_BTNFACE);
    text = state->app->useBrandColors
               ? state->app->palette.formatText
               : GetSysColor(COLOR_BTNTEXT);
    draw_fill(dc, &client, chrome);

    shadow = state->canvasRect;
    OffsetRect(&shadow, draw_scale(state, 4), draw_scale(state, 4));
    draw_fill(dc, &shadow,
              state->app->useBrandColors
                  ? state->app->palette.pageShadow
                  : GetSysColor(COLOR_3DSHADOW));
    border = state->canvasRect;
    FrameRect(dc, &border, (HBRUSH)GetStockObject(GRAY_BRUSH));

    width = max(1, state->canvasRect.right - state->canvasRect.left);
    height = max(1, state->canvasRect.bottom - state->canvasRect.top);
    saved = SaveDC(dc);
    IntersectClipRect(dc, state->canvasRect.left, state->canvasRect.top,
                     state->canvasRect.right, state->canvasRect.bottom);
    SetViewportOrgEx(dc, state->canvasRect.left,
                     state->canvasRect.top, NULL);
    draw_render_background(dc, state, width, height);
    draw_render_strokes(dc, state);
    draw_render_ruler(dc, state, width);
    RestoreDC(dc, saved);

    footer = client;
    footer.top = state->canvasRect.bottom + draw_scale(state, 7);
    footer.left += draw_scale(state, 14);
    footer.right = state->insertButton != NULL
                       ? state->canvasRect.right -
                             draw_scale(state, 190)
                       : state->canvasRect.right;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, text);
    font = state->app->uiFont;
    if (font != NULL) {
        previousFont = SelectObject(dc, font);
    }
    StringCchPrintfW(
        footerText, ARRAYSIZE(footerText), L"%s  •  %u stroke%s",
        state->notice,
        (unsigned int)state->strokeCount,
        state->strokeCount == 1u ? L"" : L"s");
    DrawTextW(dc, footerText, -1, &footer,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                  DT_END_ELLIPSIS | DT_NOPREFIX);
    if (previousFont != NULL) {
        SelectObject(dc, previousFont);
    }
}

static HWND draw_create_button(DrawCanvasState *state, UINT id,
                               const WCHAR *caption, DWORD style)
{
    HWND button = CreateWindowExW(
        0, WC_BUTTONW, caption,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
        0, 0, 0, 0, state->window, (HMENU)(UINT_PTR)id,
        state->app->instance, NULL);

    if (button != NULL && state->app->uiFont != NULL) {
        SendMessageW(button, WM_SETFONT,
                     (WPARAM)state->app->uiFont, FALSE);
    }
    return button;
}

static BOOL draw_create_children(DrawCanvasState *state)
{
    size_t index;

    for (index = 0; index < ARRAYSIZE(drawToolSpecs); ++index) {
        DWORD style = BS_AUTORADIOBUTTON | BS_PUSHLIKE;
        if (index == 0) {
            style |= WS_GROUP;
        }
        state->toolButtons[index] = draw_create_button(
            state, drawToolSpecs[index].command,
            drawToolSpecs[index].caption, style);
        if (state->toolButtons[index] == NULL) {
            return FALSE;
        }
    }
    state->undoButton = draw_create_button(
        state, IDM_EDIT_UNDO, L"Undo", BS_PUSHBUTTON | WS_GROUP);
    state->clearButton = draw_create_button(
        state, IDC_DRAW_CLEAR, L"Clear", BS_PUSHBUTTON);
    state->insertButton = draw_create_button(
        state, IDC_DRAW_INSERT, L"Insert", BS_DEFPUSHBUTTON | WS_GROUP);
    state->cancelButton = draw_create_button(
        state, IDC_DRAW_CANCEL, L"Cancel", BS_PUSHBUTTON);
    return state->undoButton != NULL && state->clearButton != NULL &&
           state->insertButton != NULL && state->cancelButton != NULL;
}

static void draw_layout(DrawCanvasState *state)
{
    RECT client;
    int margin;
    int gap;
    int top;
    int toolHeight;
    int footerHeight;
    int actionWidth;
    int actionLeft;
    int toolAreaWidth;
    int toolWidth;
    int toolRows;
    int toolColumns;
    int toolbarHeight;
    int x;
    size_t index;

    if (state == NULL || state->window == NULL) {
        return;
    }
    GetClientRect(state->window, &client);
    margin = draw_scale(state, 12);
    gap = draw_scale(state, 5);
    top = draw_scale(state, 9);
    toolHeight = draw_scale(state, 32);
    footerHeight = draw_scale(state, 50);
    actionWidth = draw_scale(state, 68);
    actionLeft = max(
        margin, client.right - margin - actionWidth * 2 - gap);
    toolAreaWidth = max(
        draw_scale(state, 160),
        actionLeft - margin - draw_scale(state, 12));
    toolRows = toolAreaWidth <
                       draw_scale(state, 8 * 50) + gap * 7
                   ? 2 : 1;
    toolColumns = toolRows == 1
                      ? (int)ARRAYSIZE(drawToolSpecs) : 4;
    toolWidth = max(
        draw_scale(state, 20),
        (toolAreaWidth - gap * (toolColumns - 1)) / toolColumns);
    toolbarHeight = toolRows * toolHeight +
                    (toolRows - 1) * gap;
    for (index = 0; index < ARRAYSIZE(drawToolSpecs); ++index) {
        int row = (int)index / toolColumns;
        int column = (int)index % toolColumns;

        x = margin + column * (toolWidth + gap);
        MoveWindow(
            state->toolButtons[index], x,
            top + row * (toolHeight + gap),
            toolWidth, toolHeight, TRUE);
    }
    MoveWindow(state->undoButton, actionLeft, top, actionWidth,
               toolHeight, TRUE);
    MoveWindow(state->clearButton, actionLeft + actionWidth + gap, top,
               actionWidth, toolHeight, TRUE);

    SetRect(
        &state->canvasRect, margin, top + toolbarHeight + margin,
        max(margin + 1, client.right - margin - draw_scale(state, 4)),
        max(top + toolbarHeight + margin + 1,
            client.bottom - footerHeight));

    MoveWindow(
        state->cancelButton,
        max(margin, client.right - margin - actionWidth),
        max(0, client.bottom - margin - toolHeight),
        actionWidth, toolHeight, TRUE);
    MoveWindow(
        state->insertButton,
        max(margin, client.right - margin - actionWidth * 2 - gap),
        max(0, client.bottom - margin - toolHeight),
        actionWidth, toolHeight, TRUE);
    InvalidateRect(state->window, NULL, TRUE);
}

static void draw_rescale_strokes(DrawCanvasState *state, UINT oldDpi,
                                 UINT newDpi)
{
    UINT strokeIndex;

    if (state == NULL || oldDpi == 0 || newDpi == 0 ||
        oldDpi == newDpi) {
        return;
    }
    for (strokeIndex = 0; strokeIndex < state->strokeCount;
         ++strokeIndex) {
        DrawStroke *stroke = &state->strokes[strokeIndex];
        UINT pointIndex;
        for (pointIndex = 0; pointIndex < stroke->count; ++pointIndex) {
            stroke->points[pointIndex].x =
                MulDiv(stroke->points[pointIndex].x,
                       (int)newDpi, (int)oldDpi);
            stroke->points[pointIndex].y =
                MulDiv(stroke->points[pointIndex].y,
                       (int)newDpi, (int)oldDpi);
        }
        stroke->width = max(
            1, MulDiv(stroke->width, (int)newDpi, (int)oldDpi));
    }
}

static BOOL draw_register_class(HINSTANCE instance)
{
    WNDCLASSEXW windowClass;

    ZeroMemory(&windowClass, sizeof(windowClass));
    if (GetClassInfoExW(
            instance, DRAW_CANVAS_CLASS_NAME, &windowClass)) {
        return TRUE;
    }
    ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    windowClass.lpfnWndProc = draw_canvas_window_proc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_WORDCRAFT));
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.lpszClassName = DRAW_CANVAS_CLASS_NAME;
    if (RegisterClassExW(&windowClass) == 0) {
        return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    return TRUE;
}

static BOOL draw_build_emf(DrawCanvasState *state, BYTE **data,
                           SIZE_T *size, UINT *width, UINT *height)
{
    RECT frame;
    HDC reference;
    HDC metafile;
    HENHMETAFILE handle;
    UINT canvasWidth;
    UINT canvasHeight;
    UINT outputWidth;
    UINT outputHeight;
    UINT naturalWidth;
    UINT naturalHeight;
    UINT byteCount;
    BYTE *bytes;
    UINT dpi;
    int saved;

    if (state == NULL || data == NULL || size == NULL ||
        width == NULL || height == NULL || !draw_any_strokes(state)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *data = NULL;
    *size = 0;
    *width = 0;
    *height = 0;
    canvasWidth = (UINT)max(
        1, state->canvasRect.right - state->canvasRect.left);
    canvasHeight = (UINT)max(
        1, state->canvasRect.bottom - state->canvasRect.top);
    dpi = state->dpi != 0 ? state->dpi : 96u;
    naturalWidth = (UINT)max(
        1, MulDiv((int)canvasWidth, 96, (int)dpi));
    naturalHeight = (UINT)max(
        1, MulDiv((int)canvasHeight, 96, (int)dpi));
    outputWidth = naturalWidth;
    outputHeight = naturalHeight;
    if (naturalWidth > 2048u || naturalHeight > 2048u) {
        if (naturalWidth >= naturalHeight) {
            outputWidth = 2048u;
            outputHeight = (UINT)max(
                1, (int)(((uint64_t)naturalHeight * 2048u) /
                         naturalWidth));
        } else {
            outputHeight = 2048u;
            outputWidth = (UINT)max(
                1, (int)(((uint64_t)naturalWidth * 2048u) /
                         naturalHeight));
        }
    }
    SetRect(&frame, 0, 0,
            MulDiv((int)outputWidth, 2540, 96),
            MulDiv((int)outputHeight, 2540, 96));

    reference = GetDC(
        state->app->mainWindow != NULL
            ? state->app->mainWindow : NULL);
    if (reference == NULL) {
        return FALSE;
    }
    metafile = CreateEnhMetaFileW(
        reference, NULL, &frame,
        L"WordCraft\0Drawing canvas\0");
    ReleaseDC(
        state->app->mainWindow != NULL
            ? state->app->mainWindow : NULL,
        reference);
    if (metafile == NULL) {
        return FALSE;
    }
    SetMapMode(metafile, MM_ANISOTROPIC);
    SetWindowExtEx(metafile, (int)canvasWidth,
                   (int)canvasHeight, NULL);
    SetViewportExtEx(metafile, (int)outputWidth,
                     (int)outputHeight, NULL);
    saved = SaveDC(metafile);
    IntersectClipRect(metafile, 0, 0, (int)canvasWidth,
                      (int)canvasHeight);
    draw_render_background(
        metafile, state, (int)canvasWidth, (int)canvasHeight);
    draw_render_strokes(metafile, state);
    RestoreDC(metafile, saved);
    handle = CloseEnhMetaFile(metafile);
    if (handle == NULL) {
        return FALSE;
    }
    byteCount = GetEnhMetaFileBits(handle, 0, NULL);
    if (byteCount == 0 || (SIZE_T)byteCount > DRAW_EMF_MAX_BYTES) {
        DeleteEnhMetaFile(handle);
        SetLastError(byteCount == 0 ? ERROR_INVALID_DATA
                                    : ERROR_FILE_TOO_LARGE);
        return FALSE;
    }
    bytes = HeapAlloc(GetProcessHeap(), 0, byteCount);
    if (bytes == NULL) {
        DeleteEnhMetaFile(handle);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    if (GetEnhMetaFileBits(handle, byteCount, bytes) != byteCount) {
        DWORD error = GetLastError();
        HeapFree(GetProcessHeap(), 0, bytes);
        DeleteEnhMetaFile(handle);
        SetLastError(error != ERROR_SUCCESS ? error : ERROR_INVALID_DATA);
        return FALSE;
    }
    DeleteEnhMetaFile(handle);
    *data = bytes;
    *size = byteCount;
    *width = outputWidth;
    *height = outputHeight;
    return TRUE;
}

static LRESULT CALLBACK draw_canvas_window_proc(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    DrawCanvasState *state = draw_state(window);

    if (message == WM_NCCREATE) {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lParam;
        state = (DrawCanvasState *)create->lpCreateParams;
        if (state == NULL) {
            return FALSE;
        }
        state->window = window;
        state->dpi = draw_query_dpi(window);
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)state);
        return TRUE;
    }
    if (state == NULL) {
        return DefWindowProcW(window, message, wParam, lParam);
    }
    switch (message) {
    case WM_CREATE:
        if (!draw_create_children(state)) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return -1;
        }
        draw_set_tool(state, state->tool);
        draw_layout(state);
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *limits = (MINMAXINFO *)lParam;
        RECT workArea;
        int minimumWidth = draw_scale(state, 520);
        int minimumHeight = draw_scale(state, 400);

        if (draw_get_work_area(window, &workArea)) {
            minimumWidth = min(
                minimumWidth, workArea.right - workArea.left);
            minimumHeight = min(
                minimumHeight, workArea.bottom - workArea.top);
        }
        limits->ptMinTrackSize.x = max(320, minimumWidth);
        limits->ptMinTrackSize.y = max(280, minimumHeight);
        return 0;
    }
    case WM_SIZE:
        draw_layout(state);
        return 0;
    case WM_DPICHANGED: {
        UINT oldDpi = state->dpi;
        UINT newDpi = HIWORD(wParam);
        RECT *suggested = (RECT *)lParam;
        if (newDpi == 0) {
            newDpi = LOWORD(wParam);
        }
        if (newDpi != 0) {
            draw_rescale_strokes(state, oldDpi, newDpi);
            state->dpi = newDpi;
        }
        if (suggested != NULL) {
            RECT adjusted = *suggested;
            draw_clamp_window_rect_to_work_area(window, &adjusted);
            SetWindowPos(
                window, NULL, adjusted.left, adjusted.top,
                adjusted.right - adjusted.left,
                adjusted.bottom - adjusted.top,
                SWP_NOACTIVATE | SWP_NOZORDER);
        }
        draw_layout(state);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        draw_paint(state, dc);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_PRINTCLIENT:
        draw_paint(state, (HDC)wParam);
        return 0;
    case WM_SETCURSOR: {
        POINT cursor;
        if (LOWORD(lParam) == HTCLIENT && GetCursorPos(&cursor)) {
            ScreenToClient(window, &cursor);
            if (draw_point_in_canvas(state, cursor)) {
                SetCursor(LoadCursorW(
                    NULL, state->tool == IDM_DRAW_ERASER
                              ? IDC_SIZEALL : IDC_CROSS));
                return TRUE;
            }
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        POINT point = {
            (short)LOWORD(lParam), (short)HIWORD(lParam)
        };
        SetFocus(window);
        if (!draw_point_in_canvas(state, point)) {
            break;
        }
        state->lastErasedStroke = UINT_MAX;
        if (state->tool == IDM_DRAW_ERASER) {
            state->drawing = TRUE;
            SetCapture(window);
            (void)draw_erase_at(state, point);
        } else {
            draw_begin_stroke(state, point);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (state->drawing) {
            POINT point = {
                (short)LOWORD(lParam), (short)HIWORD(lParam)
            };
            if (state->tool == IDM_DRAW_ERASER) {
                (void)draw_erase_at(state, point);
            } else {
                draw_continue_stroke(state, point, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (state->drawing) {
            POINT point = {
                (short)LOWORD(lParam), (short)HIWORD(lParam)
            };
            if (state->tool != IDM_DRAW_ERASER) {
                draw_finish_stroke(state, point);
            } else {
                state->drawing = FALSE;
                if (GetCapture() == window) {
                    ReleaseCapture();
                }
            }
        }
        return 0;
    case WM_CAPTURECHANGED:
        state->drawing = FALSE;
        return 0;
    case WM_CANCELMODE:
        state->drawing = FALSE;
        if (GetCapture() == window) {
            ReleaseCapture();
        }
        return 0;
    case WM_COMMAND: {
        UINT command = LOWORD(wParam);
        if (draw_tool_supported(command)) {
            draw_set_tool(state, command);
            return 0;
        }
        switch (command) {
        case IDM_EDIT_UNDO:
            draw_undo(state);
            return 0;
        case IDC_DRAW_CLEAR:
            draw_clear(state);
            return 0;
        case IDC_DRAW_INSERT: {
            BYTE *emf = NULL;
            SIZE_T emfSize = 0;
            UINT emfWidth = 0;
            UINT emfHeight = 0;

            if (!draw_any_strokes(state)) {
                MessageBeep(MB_ICONWARNING);
                draw_set_notice(
                    state, L"Draw at least one stroke before inserting.");
                return 0;
            }
            if (!draw_build_emf(
                    state, &emf, &emfSize, &emfWidth, &emfHeight) ||
                !insert_emf_picture(
                    state->app, emf, emfSize, emfWidth, emfHeight,
                    L"Drawing canvas inserted")) {
                DWORD error = GetLastError();

                if (emf != NULL) {
                    HeapFree(GetProcessHeap(), 0, emf);
                }
                draw_set_notice(
                    state,
                    L"The drawing was not inserted; your sketch is still here");
                app_show_error(
                    state->window,
                    L"The drawing could not be inserted.",
                    error != ERROR_SUCCESS
                        ? error : ERROR_CAN_NOT_COMPLETE);
                return 0;
            }
            HeapFree(GetProcessHeap(), 0, emf);
            state->accepted = TRUE;
            state->finished = TRUE;
            DestroyWindow(window);
            return 0;
        }
        case IDC_DRAW_CANCEL:
            state->accepted = FALSE;
            state->finished = TRUE;
            DestroyWindow(window);
            return 0;
        default:
            break;
        }
        break;
    }
    case WM_CLOSE:
        state->accepted = FALSE;
        state->finished = TRUE;
        DestroyWindow(window);
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        state->window = NULL;
        state->drawing = FALSE;
        state->finished = TRUE;
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

BOOL draw_show_canvas(AppState *app, UINT initialTool, BOOL showRuler,
                      BOOL ruledBackground)
{
    DrawCanvasState state;
    HWND owner;
    BOOL ownerWasEnabled;
    int width;
    int height;
    RECT ownerRect;
    RECT windowRect;
    RECT workArea;
    int x;
    int y;
    int getMessageResult = 1;
    DWORD messageError = ERROR_SUCCESS;
    BOOL result = TRUE;

    if (app == NULL || app->instance == NULL || app->mainWindow == NULL ||
        app->editor == NULL || !IsWindow(app->mainWindow) ||
        !IsWindow(app->editor)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!draw_register_class(app->instance)) {
        return FALSE;
    }
    ZeroMemory(&state, sizeof(state));
    state.app = app;
    state.dpi = draw_query_dpi(app->mainWindow);
    state.tool = draw_tool_supported(initialTool)
                     ? initialTool : IDM_DRAW_PEN_BLACK;
    state.showRuler = showRuler;
    state.ruledBackground = ruledBackground;
    state.lastErasedStroke = UINT_MAX;

    owner = app->mainWindow;
    ownerWasEnabled = IsWindowEnabled(owner);
    width = MulDiv(900, (int)state.dpi, 96);
    height = MulDiv(640, (int)state.dpi, 96);
    if (GetWindowRect(owner, &ownerRect)) {
        x = ownerRect.left +
            max(0, (ownerRect.right - ownerRect.left - width) / 2);
        y = ownerRect.top +
            max(0, (ownerRect.bottom - ownerRect.top - height) / 2);
    } else {
        x = CW_USEDEFAULT;
        y = CW_USEDEFAULT;
    }
    if (x != CW_USEDEFAULT && y != CW_USEDEFAULT) {
        SetRect(&windowRect, x, y, x + width, y + height);
        draw_clamp_window_rect_to_work_area(owner, &windowRect);
        x = windowRect.left;
        y = windowRect.top;
        width = windowRect.right - windowRect.left;
        height = windowRect.bottom - windowRect.top;
    } else if (draw_get_work_area(owner, &workArea)) {
        width = min(width, workArea.right - workArea.left);
        height = min(height, workArea.bottom - workArea.top);
    }
    if (ownerWasEnabled) {
        EnableWindow(owner, FALSE);
    }
    state.window = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        DRAW_CANVAS_CLASS_NAME, L"Drawing Canvas",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
            WS_CLIPCHILDREN,
        x, y, width, height, owner, NULL, app->instance, &state);
    if (state.window == NULL) {
        DWORD error = GetLastError();
        if (ownerWasEnabled) {
            EnableWindow(owner, TRUE);
        }
        SetLastError(error);
        return FALSE;
    }
    ShowWindow(state.window, SW_SHOW);
    UpdateWindow(state.window);
    SetFocus(state.toolButtons[0]);

    while (!state.finished) {
        MSG message;
        getMessageResult = (int)GetMessageW(&message, NULL, 0, 0);
        if (getMessageResult <= 0) {
            if (getMessageResult < 0) {
                messageError = GetLastError();
            } else {
                PostQuitMessage((int)message.wParam);
            }
            break;
        }
        if ((message.hwnd == state.window ||
             IsChild(state.window, message.hwnd)) &&
            message.message == WM_KEYDOWN) {
            if (message.wParam == VK_ESCAPE) {
                SendMessageW(state.window, WM_COMMAND,
                             IDC_DRAW_CANCEL, 0);
                continue;
            }
            if (message.wParam == 'Z' &&
                (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                SendMessageW(state.window, WM_COMMAND,
                             IDM_EDIT_UNDO, 0);
                continue;
            }
        }
        if (!IsDialogMessageW(state.window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (state.window != NULL && IsWindow(state.window)) {
        DestroyWindow(state.window);
    }
    if (ownerWasEnabled && IsWindow(owner)) {
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
    }
    if (getMessageResult < 0) {
        result = FALSE;
        SetLastError(messageError != ERROR_SUCCESS
                         ? messageError : ERROR_CAN_NOT_COMPLETE);
    }
    draw_release_all(&state);
    if (result && IsWindow(app->editor)) {
        SetFocus(app->editor);
    }
    return result;
}
