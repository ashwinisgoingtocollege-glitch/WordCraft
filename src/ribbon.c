#include "editor.h"

#define RIBBON_MAX_CONTROLS_PER_PAGE 20
#define RIBBON_MAX_OWNED_CONTROLS 64

typedef struct RibbonPage {
    HWND controls[RIBBON_MAX_CONTROLS_PER_PAGE];
    size_t count;
} RibbonPage;

struct RibbonContext {
    AppState *app;
    RibbonPage pages[RIBBON_TAB_COUNT];
    HWND ownedControls[RIBBON_MAX_OWNED_CONTROLS];
    size_t ownedCount;
    int activeTab;
    int lastWidth;
    int lastHeight;
    BOOL lastCompact;

    HWND commentEdit;
    HWND commentSummary;
    HWND addCommentButton;
    HWND previousCommentButton;
    HWND nextCommentButton;
    HWND deleteCommentButton;
    HWND spellCheckButton;
    HWND autoCompleteButton;
    HWND statusBarButton;
    HWND darkModeButton;
    HWND zoomButtons[6];
};

static const WCHAR *const ribbonTabNames[] = {
    L"File", L"Home", L"Insert", L"Draw", L"Design", L"Layout",
    L"References", L"Mailings", L"Review", L"View", L"Help"
};

static const UINT ribbonZoomCommands[] = {
    IDM_VIEW_ZOOM_50, IDM_VIEW_ZOOM_75, IDM_VIEW_ZOOM_100,
    IDM_VIEW_ZOOM_125, IDM_VIEW_ZOOM_150, IDM_VIEW_ZOOM_200
};

static const int ribbonZoomPercents[] = {
    50, 75, 100, 125, 150, 200
};

static const WCHAR *const ribbonZoomLabels[] = {
    L"50%", L"75%", L"100%", L"125%", L"150%", L"200%"
};

_Static_assert(RIBBON_TAB_COUNT == 11,
               "WordCraft's ribbon contract requires exactly 11 tabs");
_Static_assert(ARRAYSIZE(ribbonTabNames) == RIBBON_TAB_COUNT,
               "ribbon tab names must match RIBBON_TAB_COUNT");
_Static_assert(ARRAYSIZE(ribbonZoomCommands) ==
                   ARRAYSIZE(ribbonZoomPercents),
               "zoom commands and percentages must match");
_Static_assert(ARRAYSIZE(ribbonZoomLabels) ==
                   ARRAYSIZE(ribbonZoomCommands),
               "zoom labels and commands must match");

static void ribbon_fill_rect(HDC dc, const RECT *rect, COLORREF color)
{
    COLORREF previous = SetDCBrushColor(dc, color);
    FillRect(dc, rect, (HBRUSH)GetStockObject(DC_BRUSH));
    if (previous != CLR_INVALID) {
        SetDCBrushColor(dc, previous);
    }
}

static int ribbon_clamp(int value, int minimum, int maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static BOOL ribbon_valid_context(const AppState *app)
{
    return app != NULL && app->ribbon != NULL;
}

static BOOL ribbon_page_add(RibbonContext *ribbon, int tab, HWND control)
{
    RibbonPage *page;

    if (ribbon == NULL || tab < 0 || tab >= RIBBON_TAB_COUNT ||
        control == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    page = &ribbon->pages[tab];
    if (page->count >= ARRAYSIZE(page->controls)) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    page->controls[page->count++] = control;
    return TRUE;
}

static BOOL ribbon_remember_owned(RibbonContext *ribbon, HWND control)
{
    if (ribbon == NULL || control == NULL) {
        return FALSE;
    }
    if (ribbon->ownedCount >= ARRAYSIZE(ribbon->ownedControls)) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    ribbon->ownedControls[ribbon->ownedCount++] = control;
    return TRUE;
}

static HWND ribbon_create_control(RibbonContext *ribbon, int tab,
                                  DWORD extendedStyle,
                                  const WCHAR *className,
                                  const WCHAR *caption, DWORD style,
                                  UINT id)
{
    HWND control;
    AppState *app;

    if (ribbon == NULL || ribbon->app == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    app = ribbon->app;
    control = CreateWindowExW(
        extendedStyle, className, caption, WS_CHILD | style,
        0, 0, 0, 0, app->formatBar, (HMENU)(UINT_PTR)id, app->instance, NULL);
    if (control == NULL) {
        return NULL;
    }
    if (!ribbon_remember_owned(ribbon, control)) {
        DestroyWindow(control);
        return NULL;
    }
    if (!ribbon_page_add(ribbon, tab, control)) {
        ribbon->ownedControls[--ribbon->ownedCount] = NULL;
        DestroyWindow(control);
        return NULL;
    }
    if (app->uiFont != NULL) {
        SendMessageW(control, WM_SETFONT, (WPARAM)app->uiFont, FALSE);
    }
    return control;
}

static HWND ribbon_create_button(RibbonContext *ribbon, int tab, UINT command,
                                 const WCHAR *caption, BOOL toggle)
{
    DWORD style = WS_TABSTOP | (toggle ? BS_AUTOCHECKBOX | BS_PUSHLIKE
                                      : BS_PUSHBUTTON);
    return ribbon_create_control(ribbon, tab, 0, WC_BUTTONW, caption, style,
                                 command);
}

static HWND ribbon_create_label(RibbonContext *ribbon, int tab,
                                const WCHAR *caption, UINT id)
{
    return ribbon_create_control(ribbon, tab, 0, WC_STATICW, caption,
                                 SS_LEFT | SS_CENTERIMAGE, id);
}

static BOOL ribbon_register_home_controls(RibbonContext *ribbon)
{
    AppState *app = ribbon->app;
    HWND controls[] = {
        app->fontLabel, app->fontCombo, app->sizeCombo, app->boldButton,
        app->italicButton, app->underlineButton, app->strikeButton,
        app->colorButton, app->alignLeftButton, app->alignCenterButton,
        app->alignRightButton, app->alignJustifyButton, app->bulletsButton
    };
    size_t index;

    for (index = 0; index < ARRAYSIZE(controls); ++index) {
        if (!ribbon_page_add(ribbon, RIBBON_TAB_HOME, controls[index])) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL ribbon_create_file_page(RibbonContext *ribbon)
{
    return ribbon_create_button(ribbon, RIBBON_TAB_FILE, IDM_FILE_NEW,
                                L"New", FALSE) != NULL &&
           ribbon_create_button(ribbon, RIBBON_TAB_FILE, IDM_FILE_OPEN,
                                L"Open", FALSE) != NULL &&
           ribbon_create_button(ribbon, RIBBON_TAB_FILE, IDM_FILE_SAVE,
                                L"Save", FALSE) != NULL &&
           ribbon_create_button(ribbon, RIBBON_TAB_FILE, IDM_FILE_SAVE_AS,
                                L"Save As", FALSE) != NULL &&
           ribbon_create_button(ribbon, RIBBON_TAB_FILE,
                                IDM_FILE_PAGE_SETUP, L"Page Setup",
                                FALSE) != NULL &&
           ribbon_create_button(ribbon, RIBBON_TAB_FILE, IDM_FILE_PRINT,
                                L"Print", FALSE) != NULL &&
           ribbon_create_button(ribbon, RIBBON_TAB_FILE, IDM_FILE_EXIT,
                                L"Exit", FALSE) != NULL;
}

static BOOL ribbon_create_insert_page(RibbonContext *ribbon)
{
    return ribbon_create_button(ribbon, RIBBON_TAB_INSERT,
                                IDM_INSERT_DATETIME, L"Date / Time",
                                FALSE) != NULL;
}

static BOOL ribbon_create_placeholder_pages(RibbonContext *ribbon)
{
    return ribbon_create_label(
               ribbon, RIBBON_TAB_DRAW,
               L"Drawing tools are not implemented yet.", 0) != NULL &&
           ribbon_create_label(
               ribbon, RIBBON_TAB_DESIGN,
               L"Document themes and style sets are not implemented yet.",
               0) != NULL &&
           ribbon_create_label(
               ribbon, RIBBON_TAB_REFERENCES,
               L"Citations and table-of-contents tools are not implemented yet.",
               0) != NULL &&
           ribbon_create_label(
               ribbon, RIBBON_TAB_MAILINGS,
               L"Mail merge is not implemented yet.", 0) != NULL;
}

static BOOL ribbon_create_layout_page(RibbonContext *ribbon)
{
    return ribbon_create_button(ribbon, RIBBON_TAB_LAYOUT,
                                IDM_FILE_PAGE_SETUP, L"Page Setup",
                                FALSE) != NULL &&
           ribbon_create_button(ribbon, RIBBON_TAB_LAYOUT,
                                IDM_FORMAT_INDENT_DECREASE,
                                L"Decrease Indent", FALSE) != NULL &&
           ribbon_create_button(ribbon, RIBBON_TAB_LAYOUT,
                                IDM_FORMAT_INDENT_INCREASE,
                                L"Increase Indent", FALSE) != NULL;
}

static BOOL ribbon_create_review_page(RibbonContext *ribbon)
{
    HWND commentLabel;

    commentLabel = ribbon_create_label(ribbon, RIBBON_TAB_REVIEW,
                                       L"Comment:", 0);
    ribbon->commentEdit = ribbon_create_control(
        ribbon, RIBBON_TAB_REVIEW, WS_EX_CLIENTEDGE, WC_EDITW, NULL,
        WS_TABSTOP | ES_AUTOHSCROLL, IDC_COMMENT_EDIT);
    ribbon->addCommentButton = ribbon_create_button(
        ribbon, RIBBON_TAB_REVIEW, IDM_REVIEW_ADD_COMMENT,
        L"Add Comment", FALSE);
    ribbon->commentSummary = ribbon_create_label(
        ribbon, RIBBON_TAB_REVIEW, L"No comments", IDC_COMMENT_SUMMARY);
    ribbon->previousCommentButton = ribbon_create_button(
        ribbon, RIBBON_TAB_REVIEW, IDM_REVIEW_PREVIOUS_COMMENT,
        L"Previous", FALSE);
    ribbon->nextCommentButton = ribbon_create_button(
        ribbon, RIBBON_TAB_REVIEW, IDM_REVIEW_NEXT_COMMENT,
        L"Next", FALSE);
    ribbon->deleteCommentButton = ribbon_create_button(
        ribbon, RIBBON_TAB_REVIEW, IDM_REVIEW_DELETE_COMMENT,
        L"Delete", FALSE);
    ribbon->spellCheckButton = ribbon_create_button(
        ribbon, RIBBON_TAB_REVIEW, IDM_TOOLS_SPELL_CHECK,
        L"Spell Check", TRUE);
    ribbon->autoCompleteButton = ribbon_create_button(
        ribbon, RIBBON_TAB_REVIEW, IDM_TOOLS_AUTOCOMPLETE,
        L"Autocomplete", TRUE);

    if (commentLabel == NULL || ribbon->commentEdit == NULL ||
        ribbon->addCommentButton == NULL || ribbon->commentSummary == NULL ||
        ribbon->previousCommentButton == NULL ||
        ribbon->nextCommentButton == NULL ||
        ribbon->deleteCommentButton == NULL ||
        ribbon->spellCheckButton == NULL ||
        ribbon->autoCompleteButton == NULL) {
        return FALSE;
    }
    SendMessageW(ribbon->commentEdit, EM_SETLIMITTEXT,
                 COMMENT_TEXT_CAPACITY, 0);
#ifdef EM_SETCUEBANNER
    SendMessageW(ribbon->commentEdit, EM_SETCUEBANNER, TRUE,
                 (LPARAM)L"Type a comment");
#endif
    return TRUE;
}

static BOOL ribbon_create_view_page(RibbonContext *ribbon)
{
    size_t index;

    ribbon->statusBarButton = ribbon_create_button(
        ribbon, RIBBON_TAB_VIEW, IDM_VIEW_STATUS_BAR, L"Status Bar", TRUE);
    ribbon->darkModeButton = ribbon_create_button(
        ribbon, RIBBON_TAB_VIEW, IDM_VIEW_DARK_MODE, L"Dark Mode", TRUE);
    if (ribbon->statusBarButton == NULL || ribbon->darkModeButton == NULL ||
        ribbon_create_label(ribbon, RIBBON_TAB_VIEW, L"Zoom:", 0) == NULL) {
        return FALSE;
    }
    for (index = 0; index < ARRAYSIZE(ribbon->zoomButtons); ++index) {
        ribbon->zoomButtons[index] = ribbon_create_button(
            ribbon, RIBBON_TAB_VIEW, ribbonZoomCommands[index],
            ribbonZoomLabels[index], TRUE);
        if (ribbon->zoomButtons[index] == NULL) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL ribbon_create_help_page(RibbonContext *ribbon)
{
    return ribbon_create_button(ribbon, RIBBON_TAB_HELP, IDM_HELP_ABOUT,
                                L"About WordCraft", FALSE) != NULL;
}

static BOOL ribbon_create_pages(RibbonContext *ribbon)
{
    return ribbon_register_home_controls(ribbon) &&
           ribbon_create_file_page(ribbon) &&
           ribbon_create_insert_page(ribbon) &&
           ribbon_create_placeholder_pages(ribbon) &&
           ribbon_create_layout_page(ribbon) &&
           ribbon_create_review_page(ribbon) &&
           ribbon_create_view_page(ribbon) &&
           ribbon_create_help_page(ribbon);
}

static void ribbon_show_active_page(RibbonContext *ribbon, int activeTab)
{
    int tab;
    size_t index;

    if (ribbon == NULL || activeTab < 0 || activeTab >= RIBBON_TAB_COUNT) {
        return;
    }
    ribbon->activeTab = activeTab;
    for (tab = 0; tab < RIBBON_TAB_COUNT; ++tab) {
        int command = tab == activeTab ? SW_SHOWNA : SW_HIDE;
        for (index = 0; index < ribbon->pages[tab].count; ++index) {
            ShowWindow(ribbon->pages[tab].controls[index], command);
        }
    }
    if (ribbon->app != NULL && ribbon->app->formatBar != NULL) {
        InvalidateRect(ribbon->app->formatBar, NULL, TRUE);
    }
}

static void ribbon_move_page_row(const RibbonPage *page, int x, int y,
                                 int height, const int *widths, int gap)
{
    size_t index;

    if (page == NULL || widths == NULL) {
        return;
    }
    for (index = 0; index < page->count; ++index) {
        MoveWindow(page->controls[index], x, y, widths[index], height, TRUE);
        x += widths[index] + gap;
    }
}

static void ribbon_layout_file(RibbonContext *ribbon)
{
    AppState *app = ribbon->app;
    int widths[] = {
        app_scale(app->mainWindow, 60), app_scale(app->mainWindow, 64),
        app_scale(app->mainWindow, 64), app_scale(app->mainWindow, 78),
        app_scale(app->mainWindow, 98), app_scale(app->mainWindow, 64),
        app_scale(app->mainWindow, 60)
    };
    ribbon_move_page_row(&ribbon->pages[RIBBON_TAB_FILE],
                         app_scale(app->mainWindow, 8),
                         app_scale(app->mainWindow, 7),
                         app_scale(app->mainWindow, 28), widths,
                         app_scale(app->mainWindow, 4));
}

static void ribbon_layout_home(RibbonContext *ribbon, BOOL compact)
{
    AppState *app = ribbon->app;
    int x = app_scale(app->mainWindow, 8);
    int gap = app_scale(app->mainWindow, 4);
    int controlHeight = app_scale(app->mainWindow, 26);
    int buttonWidth = app_scale(app->mainWindow, 34);
    int wideButton = app_scale(app->mainWindow, 57);
    int alignmentY;

    MoveWindow(app->fontLabel, x, app_scale(app->mainWindow, 6),
               app_scale(app->mainWindow, 34), controlHeight, TRUE);
    x += app_scale(app->mainWindow, 38);
    MoveWindow(app->fontCombo, x, app_scale(app->mainWindow, 5),
               app_scale(app->mainWindow, 174),
               app_scale(app->mainWindow, 260), TRUE);
    x += app_scale(app->mainWindow, 174) + gap;
    MoveWindow(app->sizeCombo, x, app_scale(app->mainWindow, 5),
               app_scale(app->mainWindow, 58),
               app_scale(app->mainWindow, 260), TRUE);
    x += app_scale(app->mainWindow, 66);

    MoveWindow(app->boldButton, x, app_scale(app->mainWindow, 5),
               buttonWidth, controlHeight, TRUE);
    x += buttonWidth;
    MoveWindow(app->italicButton, x, app_scale(app->mainWindow, 5),
               buttonWidth, controlHeight, TRUE);
    x += buttonWidth;
    MoveWindow(app->underlineButton, x, app_scale(app->mainWindow, 5),
               buttonWidth, controlHeight, TRUE);
    x += buttonWidth;
    MoveWindow(app->strikeButton, x, app_scale(app->mainWindow, 5),
               buttonWidth, controlHeight, TRUE);
    x += buttonWidth + gap;
    MoveWindow(app->colorButton, x, app_scale(app->mainWindow, 5),
               wideButton, controlHeight, TRUE);
    x += wideButton;

    if (compact) {
        x = app_scale(app->mainWindow, 8);
        alignmentY = app_scale(app->mainWindow, 39);
    } else {
        x += app_scale(app->mainWindow, 8);
        alignmentY = app_scale(app->mainWindow, 5);
    }
    MoveWindow(app->alignLeftButton, x, alignmentY, wideButton,
               controlHeight, TRUE);
    x += wideButton;
    MoveWindow(app->alignCenterButton, x, alignmentY, wideButton,
               controlHeight, TRUE);
    x += wideButton;
    MoveWindow(app->alignRightButton, x, alignmentY, wideButton,
               controlHeight, TRUE);
    x += wideButton;
    MoveWindow(app->alignJustifyButton, x, alignmentY, wideButton,
               controlHeight, TRUE);
    x += wideButton + gap;
    MoveWindow(app->bulletsButton, x, alignmentY, wideButton,
               controlHeight, TRUE);
}

static void ribbon_layout_simple_page(RibbonContext *ribbon, int tab,
                                      int buttonWidth)
{
    AppState *app = ribbon->app;
    RibbonPage *page = &ribbon->pages[tab];
    int widths[RIBBON_MAX_CONTROLS_PER_PAGE];
    size_t index;

    for (index = 0; index < page->count; ++index) {
        widths[index] = app_scale(app->mainWindow, buttonWidth);
    }
    ribbon_move_page_row(page, app_scale(app->mainWindow, 8),
                         app_scale(app->mainWindow, 7),
                         app_scale(app->mainWindow, 28), widths,
                         app_scale(app->mainWindow, 4));
}

static void ribbon_layout_placeholder(RibbonContext *ribbon, int tab,
                                      int width)
{
    AppState *app = ribbon->app;
    HWND label = ribbon->pages[tab].controls[0];
    MoveWindow(label, app_scale(app->mainWindow, 12),
               app_scale(app->mainWindow, 8),
               ribbon_clamp(width - app_scale(app->mainWindow, 24), 0,
                            app_scale(app->mainWindow, 620)),
               app_scale(app->mainWindow, 28), TRUE);
}

static void ribbon_layout_review(RibbonContext *ribbon, int width)
{
    AppState *app = ribbon->app;
    RibbonPage *page = &ribbon->pages[RIBBON_TAB_REVIEW];
    int gap = app_scale(app->mainWindow, 4);
    int x = app_scale(app->mainWindow, 8);
    int firstY = app_scale(app->mainWindow, 5);
    int secondY = app_scale(app->mainWindow, 38);
    int controlHeight = app_scale(app->mainWindow, 27);
    int labelWidth = app_scale(app->mainWindow, 62);
    int addWidth = app_scale(app->mainWindow, 94);
    int summaryMinimum = app_scale(app->mainWindow, 110);
    int available = width - x - labelWidth - addWidth - summaryMinimum -
                    gap * 3 - app_scale(app->mainWindow, 8);
    int editWidth = ribbon_clamp(available, app_scale(app->mainWindow, 140),
                                 app_scale(app->mainWindow, 360));
    int summaryWidth;
    int secondWidths[] = {
        app_scale(app->mainWindow, 78), app_scale(app->mainWindow, 62),
        app_scale(app->mainWindow, 68), app_scale(app->mainWindow, 118),
        app_scale(app->mainWindow, 126)
    };

    MoveWindow(page->controls[0], x, firstY, labelWidth, controlHeight, TRUE);
    x += labelWidth + gap;
    MoveWindow(ribbon->commentEdit, x, firstY, editWidth, controlHeight, TRUE);
    x += editWidth + gap;
    MoveWindow(ribbon->addCommentButton, x, firstY, addWidth, controlHeight,
               TRUE);
    x += addWidth + gap;
    summaryWidth = width - x - app_scale(app->mainWindow, 8);
    if (summaryWidth < 0) {
        summaryWidth = 0;
    }
    MoveWindow(ribbon->commentSummary, x, firstY, summaryWidth,
               controlHeight, TRUE);

    x = app_scale(app->mainWindow, 8);
    MoveWindow(ribbon->previousCommentButton, x, secondY, secondWidths[0],
               controlHeight, TRUE);
    x += secondWidths[0] + gap;
    MoveWindow(ribbon->nextCommentButton, x, secondY, secondWidths[1],
               controlHeight, TRUE);
    x += secondWidths[1] + gap;
    MoveWindow(ribbon->deleteCommentButton, x, secondY, secondWidths[2],
               controlHeight, TRUE);
    x += secondWidths[2] + app_scale(app->mainWindow, 12);
    MoveWindow(ribbon->spellCheckButton, x, secondY, secondWidths[3],
               controlHeight, TRUE);
    x += secondWidths[3] + gap;
    MoveWindow(ribbon->autoCompleteButton, x, secondY, secondWidths[4],
               controlHeight, TRUE);
}

static void ribbon_layout_view(RibbonContext *ribbon)
{
    AppState *app = ribbon->app;
    RibbonPage *page = &ribbon->pages[RIBBON_TAB_VIEW];
    int widths[] = {
        app_scale(app->mainWindow, 88), app_scale(app->mainWindow, 88),
        app_scale(app->mainWindow, 48), app_scale(app->mainWindow, 52),
        app_scale(app->mainWindow, 52), app_scale(app->mainWindow, 58),
        app_scale(app->mainWindow, 58), app_scale(app->mainWindow, 58),
        app_scale(app->mainWindow, 58)
    };
    ribbon_move_page_row(page, app_scale(app->mainWindow, 8),
                         app_scale(app->mainWindow, 7),
                         app_scale(app->mainWindow, 28), widths,
                         app_scale(app->mainWindow, 4));
}

static BOOL ribbon_control_is_on_page(const RibbonContext *ribbon, int tab,
                                      HWND control)
{
    const RibbonPage *page;
    size_t index;

    if (ribbon == NULL || tab < 0 || tab >= RIBBON_TAB_COUNT ||
        control == NULL) {
        return FALSE;
    }
    page = &ribbon->pages[tab];
    for (index = 0; index < page->count; ++index) {
        if (page->controls[index] == control) {
            return TRUE;
        }
    }
    return FALSE;
}

static HWND ribbon_direct_panel_child(AppState *app, HWND window)
{
    HWND parent;

    if (app == NULL || app->formatBar == NULL || window == NULL ||
        !IsChild(app->formatBar, window)) {
        return NULL;
    }
    while ((parent = GetParent(window)) != NULL && parent != app->formatBar) {
        window = parent;
    }
    return GetParent(window) == app->formatBar ? window : NULL;
}

static HWND ribbon_first_panel_control(RibbonContext *ribbon, BOOL previous)
{
    const RibbonPage *page;
    size_t offset;

    if (ribbon == NULL || ribbon->app == NULL ||
        ribbon->app->formatBar == NULL) {
        return NULL;
    }
    page = &ribbon->pages[ribbon->activeTab];
    for (offset = 0; offset < page->count; ++offset) {
        size_t index = previous ? page->count - offset - 1 : offset;
        HWND candidate = page->controls[index];
        LONG_PTR style = GetWindowLongPtrW(candidate, GWL_STYLE);

        if ((style & (WS_TABSTOP | WS_VISIBLE)) ==
                (WS_TABSTOP | WS_VISIBLE) &&
            IsWindowEnabled(candidate)) {
            return candidate;
        }
    }
    return NULL;
}

static HWND ribbon_next_panel_control(RibbonContext *ribbon, HWND current,
                                      BOOL previous)
{
    const RibbonPage *page;
    size_t currentIndex;
    size_t offset;

    if (ribbon == NULL || current == NULL) {
        return NULL;
    }
    page = &ribbon->pages[ribbon->activeTab];
    for (currentIndex = 0; currentIndex < page->count; ++currentIndex) {
        if (page->controls[currentIndex] == current) {
            break;
        }
    }
    if (currentIndex == page->count) {
        return NULL;
    }
    for (offset = 1; offset <= page->count; ++offset) {
        size_t index;
        HWND candidate;
        LONG_PTR style;

        if (previous) {
            index = (currentIndex + page->count -
                     (offset % page->count)) % page->count;
        } else {
            index = (currentIndex + offset) % page->count;
        }
        candidate = page->controls[index];
        style = GetWindowLongPtrW(candidate, GWL_STYLE);
        if ((style & (WS_TABSTOP | WS_VISIBLE)) ==
                (WS_TABSTOP | WS_VISIBLE) &&
            IsWindowEnabled(candidate)) {
            return candidate;
        }
    }
    return NULL;
}

static UINT ribbon_name_hash(const WCHAR *text)
{
    UINT hash = 2166136261u;

    while (text != NULL && *text != L'\0') {
        hash ^= (UINT)*text++;
        hash *= 16777619u;
    }
    return hash;
}

BOOL ribbon_create(AppState *app)
{
    RibbonContext *ribbon;
    TCITEMW item;
    int index;

    if (app == NULL || app->mainWindow == NULL || app->formatBar == NULL ||
        app->ribbon != NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ribbon = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*ribbon));
    if (ribbon == NULL) {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    ribbon->app = app;
    ribbon->activeTab = RIBBON_TAB_HOME;
    app->ribbon = ribbon;

    app->ribbonTabs = CreateWindowExW(
        WS_EX_CONTROLPARENT, WC_TABCONTROLW, NULL,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS |
            TCS_SINGLELINE | TCS_FIXEDWIDTH | TCS_OWNERDRAWFIXED |
            TCS_FOCUSONBUTTONDOWN,
        0, 0, 0, 0, app->mainWindow, (HMENU)(INT_PTR)IDC_RIBBON_TABS,
        app->instance, NULL);
    if (app->ribbonTabs == NULL) {
        ribbon_free(app);
        return FALSE;
    }
    if (app->uiFont != NULL) {
        SendMessageW(app->ribbonTabs, WM_SETFONT, (WPARAM)app->uiFont, FALSE);
    }

    ZeroMemory(&item, sizeof(item));
    item.mask = TCIF_TEXT | TCIF_PARAM;
    for (index = 0; index < RIBBON_TAB_COUNT; ++index) {
        item.pszText = (LPWSTR)ribbonTabNames[index];
        item.lParam = index;
        if (TabCtrl_InsertItem(app->ribbonTabs, index, &item) == -1) {
            ribbon_free(app);
            return FALSE;
        }
    }
    if (!ribbon_create_pages(ribbon)) {
        ribbon_free(app);
        return FALSE;
    }

    TabCtrl_SetItemSize(app->ribbonTabs, app_scale(app->mainWindow, 92),
                        app_scale(app->mainWindow, 29));
    TabCtrl_SetCurSel(app->ribbonTabs, RIBBON_TAB_HOME);
    ribbon_show_active_page(ribbon, RIBBON_TAB_HOME);
    ribbon_update_command_ui(app, FALSE, FALSE, FALSE, FALSE);
    return TRUE;
}

void ribbon_layout(AppState *app, int width, int height, BOOL compact)
{
    RibbonContext *ribbon;
    int tabWidth;

    if (!ribbon_valid_context(app)) {
        return;
    }
    ribbon = app->ribbon;
    ribbon->lastWidth = width;
    ribbon->lastHeight = height;
    ribbon->lastCompact = compact;

    tabWidth = app_scale(app->mainWindow, 92);
    TabCtrl_SetItemSize(app->ribbonTabs, tabWidth,
                        app_scale(app->mainWindow, 29));

    ribbon_layout_file(ribbon);
    ribbon_layout_home(ribbon, compact);
    ribbon_layout_simple_page(ribbon, RIBBON_TAB_INSERT, 104);
    ribbon_layout_placeholder(ribbon, RIBBON_TAB_DRAW, width);
    ribbon_layout_placeholder(ribbon, RIBBON_TAB_DESIGN, width);
    ribbon_layout_simple_page(ribbon, RIBBON_TAB_LAYOUT, 116);
    ribbon_layout_placeholder(ribbon, RIBBON_TAB_REFERENCES, width);
    ribbon_layout_placeholder(ribbon, RIBBON_TAB_MAILINGS, width);
    ribbon_layout_review(ribbon, width);
    ribbon_layout_view(ribbon);
    ribbon_layout_simple_page(ribbon, RIBBON_TAB_HELP, 126);
    ribbon_show_active_page(ribbon, ribbon->activeTab);

    (void)height;
}

void ribbon_apply_theme(AppState *app)
{
    RibbonContext *ribbon;
    size_t index;

    if (!ribbon_valid_context(app)) {
        return;
    }
    ribbon = app->ribbon;
    if (app->uiFont != NULL) {
        SendMessageW(app->ribbonTabs, WM_SETFONT, (WPARAM)app->uiFont, FALSE);
        for (index = 0; index < ribbon->ownedCount; ++index) {
            SendMessageW(ribbon->ownedControls[index], WM_SETFONT,
                         (WPARAM)app->uiFont, FALSE);
        }
    }
    RedrawWindow(app->ribbonTabs, NULL, NULL,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    RedrawWindow(app->formatBar, NULL, NULL,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

BOOL ribbon_handle_notify(AppState *app, const NMHDR *notification)
{
    int selected;

    if (!ribbon_valid_context(app) || notification == NULL ||
        notification->hwndFrom != app->ribbonTabs ||
        notification->code != TCN_SELCHANGE) {
        return FALSE;
    }
    selected = TabCtrl_GetCurSel(app->ribbonTabs);
    if (selected < 0 || selected >= RIBBON_TAB_COUNT) {
        return FALSE;
    }
    ribbon_show_active_page(app->ribbon, selected);
    if (app->ribbon->lastWidth > 0) {
        ribbon_layout(app, app->ribbon->lastWidth, app->ribbon->lastHeight,
                      app->ribbon->lastCompact);
    }
    InvalidateRect(app->ribbonTabs, NULL, TRUE);
    return TRUE;
}

BOOL ribbon_draw_item(AppState *app, const DRAWITEMSTRUCT *draw)
{
    RECT textRect;
    COLORREF background;
    COLORREF textColor;
    COLORREF borderColor;
    HFONT font;
    HGDIOBJ previousFont = NULL;
    BOOL selected;
    int item;

    if (!ribbon_valid_context(app) || draw == NULL ||
        draw->CtlType != ODT_TAB || draw->hwndItem != app->ribbonTabs) {
        return FALSE;
    }
    item = (int)draw->itemID;
    if (item < 0 || item >= RIBBON_TAB_COUNT) {
        return FALSE;
    }
    selected = TabCtrl_GetCurSel(app->ribbonTabs) == item;
    if (app->useBrandColors) {
        background = selected ? app->palette.formatBackground
                              : app->palette.toolbarBackground;
        textColor = selected ? app->palette.formatText
                             : app->palette.toolbarText;
        borderColor = app->palette.controlBorder;
    } else {
        background = GetSysColor(selected ? COLOR_WINDOW : COLOR_BTNFACE);
        textColor = GetSysColor((draw->itemState & ODS_DISABLED) != 0
                                    ? COLOR_GRAYTEXT
                                    : COLOR_BTNTEXT);
        borderColor = GetSysColor(COLOR_3DSHADOW);
    }

    ribbon_fill_rect(draw->hDC, &draw->rcItem, borderColor);
    textRect = draw->rcItem;
    InflateRect(&textRect, -1, -1);
    ribbon_fill_rect(draw->hDC, &textRect, background);
    textRect.left += app_scale(app->mainWindow, 4);
    textRect.right -= app_scale(app->mainWindow, 4);
    SetBkMode(draw->hDC, TRANSPARENT);
    SetTextColor(draw->hDC, textColor);
    font = (HFONT)SendMessageW(app->ribbonTabs, WM_GETFONT, 0, 0);
    if (font != NULL) {
        previousFont = SelectObject(draw->hDC, font);
    }
    DrawTextW(draw->hDC, ribbonTabNames[item], -1, &textRect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                  DT_NOPREFIX);
    if (previousFont != NULL) {
        SelectObject(draw->hDC, previousFont);
    }
    if ((draw->itemState & ODS_FOCUS) != 0) {
        InflateRect(&textRect, -2, -2);
        DrawFocusRect(draw->hDC, &textRect);
    }
    return TRUE;
}

void ribbon_update_command_ui(AppState *app, BOOL hasSelection,
                              BOOL canUndo, BOOL canRedo, BOOL canPaste)
{
    RibbonContext *ribbon;
    SIZE_T commentCount;
    size_t index;

    if (!ribbon_valid_context(app)) {
        return;
    }
    ribbon = app->ribbon;
    commentCount = comments_count(app);

    SendMessageW(ribbon->spellCheckButton, BM_SETCHECK,
                 app->spellCheckEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(ribbon->autoCompleteButton, BM_SETCHECK,
                 app->autoCompleteEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(ribbon->statusBarButton, BM_SETCHECK,
                 app->showStatusBar ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(ribbon->darkModeButton, BM_SETCHECK,
                 app->darkMode ? BST_CHECKED : BST_UNCHECKED, 0);
    for (index = 0; index < ARRAYSIZE(ribbon->zoomButtons); ++index) {
        SendMessageW(ribbon->zoomButtons[index], BM_SETCHECK,
                     app->zoomPercent == ribbonZoomPercents[index]
                         ? BST_CHECKED
                         : BST_UNCHECKED,
                     0);
    }
    EnableWindow(ribbon->addCommentButton, app->editor != NULL);
    EnableWindow(ribbon->previousCommentButton, commentCount > 0);
    EnableWindow(ribbon->nextCommentButton, commentCount > 0);
    EnableWindow(ribbon->deleteCommentButton, commentCount > 0);

    (void)hasSelection;
    (void)canUndo;
    (void)canRedo;
    (void)canPaste;
}

void ribbon_focus(AppState *app)
{
    RibbonContext *ribbon;
    HWND focus;
    HWND panelChild;
    HWND target;

    if (!ribbon_valid_context(app)) {
        return;
    }
    ribbon = app->ribbon;
    focus = GetFocus();
    panelChild = ribbon_direct_panel_child(app, focus);
    if (focus == app->ribbonTabs) {
        target = ribbon_first_panel_control(ribbon, FALSE);
        SetFocus(target != NULL ? target : app->editor);
    } else if (panelChild != NULL &&
               ribbon_control_is_on_page(ribbon, ribbon->activeTab,
                                         panelChild)) {
        SetFocus(app->editor);
    } else {
        SetFocus(app->ribbonTabs);
    }
}

void ribbon_focus_comment_editor(AppState *app)
{
    RibbonContext *ribbon;
    int textLength;

    if (!ribbon_valid_context(app)) {
        return;
    }
    ribbon = app->ribbon;
    TabCtrl_SetCurSel(app->ribbonTabs, RIBBON_TAB_REVIEW);
    ribbon_show_active_page(ribbon, RIBBON_TAB_REVIEW);
    if (ribbon->lastWidth > 0) {
        ribbon_layout(app, ribbon->lastWidth, ribbon->lastHeight,
                      ribbon->lastCompact);
    }
    InvalidateRect(app->ribbonTabs, NULL, TRUE);
    SetFocus(ribbon->commentEdit);
    textLength = GetWindowTextLengthW(ribbon->commentEdit);
    SendMessageW(ribbon->commentEdit, EM_SETSEL, textLength, textLength);
}

BOOL ribbon_handle_keyboard(AppState *app, const MSG *message)
{
    RibbonContext *ribbon;
    HWND focus;
    HWND directChild;
    HWND next;
    BOOL previous;

    if (!ribbon_valid_context(app) || message == NULL ||
        message->message != WM_KEYDOWN || message->wParam != VK_TAB ||
        (GetKeyState(VK_CONTROL) & 0x8000) != 0 ||
        (GetKeyState(VK_MENU) & 0x8000) != 0) {
        return FALSE;
    }
    ribbon = app->ribbon;
    focus = GetFocus();
    directChild = ribbon_direct_panel_child(app, focus);
    if (focus != app->ribbonTabs &&
        (directChild == NULL ||
         !ribbon_control_is_on_page(ribbon, ribbon->activeTab,
                                    directChild))) {
        return FALSE;
    }

    previous = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (focus == app->ribbonTabs) {
        next = ribbon_first_panel_control(ribbon, previous);
    } else {
        if (directChild != NULL) {
            SendMessageW(directChild, CB_SHOWDROPDOWN, FALSE, 0);
        }
        next = ribbon_next_panel_control(ribbon, directChild, previous);
        if (next == NULL) {
            next = ribbon_first_panel_control(ribbon, previous);
        }
    }
    if (next == NULL) {
        return FALSE;
    }
    SetFocus(next);
    return TRUE;
}

BOOL ribbon_get_comment_text(AppState *app, WCHAR *text, size_t textCount)
{
    int length;

    if (!ribbon_valid_context(app) || text == NULL || textCount == 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    text[0] = L'\0';
    length = GetWindowTextLengthW(app->ribbon->commentEdit);
    if ((size_t)length >= textCount) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (length == 0) {
        return FALSE;
    }
    return GetWindowTextW(app->ribbon->commentEdit, text,
                          (int)textCount) == length;
}

void ribbon_clear_comment_text(AppState *app)
{
    if (ribbon_valid_context(app)) {
        SetWindowTextW(app->ribbon->commentEdit, L"");
    }
}

void ribbon_set_comment_summary(AppState *app, const WCHAR *summary)
{
    if (ribbon_valid_context(app)) {
        SetWindowTextW(app->ribbon->commentSummary,
                       summary != NULL && summary[0] != L'\0'
                           ? summary
                           : L"No comments");
    }
}

LRESULT ribbon_query_state(AppState *app, UINT query, LPARAM index)
{
    RibbonContext *ribbon;
    HWND focus;
    HWND directChild;

    if (!ribbon_valid_context(app)) {
        return 0;
    }
    ribbon = app->ribbon;
    switch (query) {
    case WCQ_RIBBON_TAB_COUNT:
        return RIBBON_TAB_COUNT;
    case WCQ_RIBBON_ACTIVE_TAB:
        return TabCtrl_GetCurSel(app->ribbonTabs);
    case WCQ_RIBBON_TAB_NAME_HASH:
        if (index < 0 || index >= RIBBON_TAB_COUNT) {
            return 0;
        }
        return (LRESULT)ribbon_name_hash(ribbonTabNames[index]);
    case WCQ_RIBBON_VISIBLE_PANEL:
        return ribbon->activeTab;
    case WCQ_RIBBON_PANEL_VISIBLE:
        return index >= 0 && index < RIBBON_TAB_COUNT &&
               ribbon->activeTab == (int)index &&
               (GetWindowLongPtrW(app->formatBar, GWL_STYLE) & WS_VISIBLE) != 0;
    case WCQ_RIBBON_FOCUS_AREA:
        focus = GetFocus();
        if (focus == app->editor) {
            return RIBBON_FOCUS_EDITOR;
        }
        if (focus == app->ribbonTabs) {
            return RIBBON_FOCUS_TABS;
        }
        directChild = ribbon_direct_panel_child(app, focus);
        if (directChild != NULL &&
            ribbon_control_is_on_page(ribbon, ribbon->activeTab,
                                      directChild)) {
            return RIBBON_FOCUS_PANEL;
        }
        return RIBBON_FOCUS_OTHER;
    default:
        return 0;
    }
}

void ribbon_free(AppState *app)
{
    RibbonContext *ribbon;
    size_t index;

    if (app == NULL || app->ribbon == NULL) {
        return;
    }
    ribbon = app->ribbon;
    for (index = ribbon->ownedCount; index > 0; --index) {
        HWND control = ribbon->ownedControls[index - 1];
        if (IsWindow(control)) {
            DestroyWindow(control);
        }
    }
    if (IsWindow(app->ribbonTabs)) {
        DestroyWindow(app->ribbonTabs);
    }
    app->ribbonTabs = NULL;
    app->ribbon = NULL;
    HeapFree(GetProcessHeap(), 0, ribbon);
}
