#ifndef COBJMACROS
#define COBJMACROS
#endif
#include "editor.h"

#include <limits.h>
#include <oleacc.h>

#define RIBBON_MAX_CONTROLS_PER_PAGE 48
#define RIBBON_MAX_OWNED_CONTROLS 96
#define HOME_MAX_CONTROLS 48

typedef struct RibbonPage {
    HWND controls[RIBBON_MAX_CONTROLS_PER_PAGE];
    size_t count;
} RibbonPage;

typedef struct HomeControlInfo {
    HWND window;
    UINT id;
    int group;
} HomeControlInfo;

struct RibbonContext {
    AppState *app;
    RibbonPage pages[RIBBON_TAB_COUNT];
    HWND ownedControls[RIBBON_MAX_OWNED_CONTROLS];
    size_t ownedCount;
    int activeTab;
    int lastWidth;
    int lastHeight;
    BOOL lastCompact;

    HomeControlInfo homeControls[HOME_MAX_CONTROLS];
    size_t homeControlCount;
    RECT homeGroupRects[HOME_GROUP_COUNT];
    HWND homeGroupLabels[HOME_GROUP_COUNT];
    UINT homeGroupPaintCount;
    UINT layoutGeneration;
    int homeLayoutMode;
    int activeStyle;
    BOOL styleGalleryVisible;

    HWND clipboardButtons[3];
    HWND growFontButton;
    HWND shrinkFontButton;
    HWND subscriptButton;
    HWND superscriptButton;
    HWND highlightButton;
    HWND clearFormattingButton;
    HWND numberingButton;
    HWND indentButtons[2];
    HWND lineSpacingButton;
    HWND styleButtons[WORDCRAFT_STYLE_COUNT];
    HWND styleCombo;
    HWND editingButtons[3];

    HWND commentEdit;
    HWND commentSummary;
    HWND addCommentButton;
    HWND previousCommentButton;
    HWND nextCommentButton;
    HWND deleteCommentButton;
    HWND spellCheckButton;
    HWND autoCompleteButton;
    HWND liveShareButton;
    HWND statusBarButton;
    HWND darkModeButton;
    HWND zoomButtons[6];
    HWND paperSizeCombo;
};

static const WCHAR *const ribbonTabNames[] = {
    L"File", L"Home", L"Insert", L"Draw", L"Design", L"Layout",
    L"References", L"Mailings", L"Review", L"View", L"Help"
};

static const CLSID wordcraftClsidAccPropServices = {
    0xB5F8350B, 0x0548, 0x48B1,
    {0xA6, 0xEE, 0x88, 0xBD, 0x00, 0xB4, 0xA5, 0xE7}
};

static const MSAAPROPID wordcraftPropidAccName = {
    0x608D3DF8, 0x8128, 0x4AA7,
    {0xA4, 0x28, 0xF5, 0x5E, 0x49, 0x26, 0x72, 0x91}
};

static const WCHAR *const homeGroupNames[] = {
    L"Clipboard", L"Font", L"Paragraph", L"Styles", L"Editing"
};

static const UINT homeGroupLabelIds[] = {
    IDC_HOME_GROUP_CLIPBOARD, IDC_HOME_GROUP_FONT,
    IDC_HOME_GROUP_PARAGRAPH, IDC_HOME_GROUP_STYLES,
    IDC_HOME_GROUP_EDITING
};

static const WCHAR *const styleNames[] = {
    L"Normal", L"No Spacing", L"Heading 1", L"Heading 2", L"Title"
};

static const UINT styleCommands[] = {
    IDM_STYLE_NORMAL, IDM_STYLE_NO_SPACING, IDM_STYLE_HEADING_1,
    IDM_STYLE_HEADING_2, IDM_STYLE_TITLE
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
_Static_assert(ARRAYSIZE(homeGroupNames) == HOME_GROUP_COUNT,
               "Home group names must match HOME_GROUP_COUNT");
_Static_assert(ARRAYSIZE(homeGroupLabelIds) == HOME_GROUP_COUNT,
               "Home group labels must match HOME_GROUP_COUNT");
_Static_assert(ARRAYSIZE(styleNames) == WORDCRAFT_STYLE_COUNT,
               "style names must match WORDCRAFT_STYLE_COUNT");
_Static_assert(ARRAYSIZE(styleCommands) == WORDCRAFT_STYLE_COUNT,
               "style commands must match WORDCRAFT_STYLE_COUNT");
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

static COLORREF ribbon_blend_color(COLORREF base, COLORREF accent,
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

static void ribbon_set_accessible_name(HWND control, const WCHAR *name)
{
    IAccPropServices *services = NULL;

    if (control == NULL || name == NULL || name[0] == L'\0') {
        return;
    }
    if (SUCCEEDED(CoCreateInstance(
            &wordcraftClsidAccPropServices, NULL, CLSCTX_INPROC_SERVER,
            &IID_IAccPropServices, (void **)&services)) &&
        services != NULL) {
        IAccPropServices_SetHwndPropStr(
            services, control, OBJID_CLIENT, CHILDID_SELF,
            wordcraftPropidAccName, name);
        IAccPropServices_Release(services);
    }
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

static BOOL ribbon_home_track(RibbonContext *ribbon, HWND window,
                              UINT id, int group)
{
    HomeControlInfo *control;

    if (ribbon == NULL || window == NULL || group < 0 ||
        group >= HOME_GROUP_COUNT ||
        ribbon->homeControlCount >= ARRAYSIZE(ribbon->homeControls)) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    control = &ribbon->homeControls[ribbon->homeControlCount++];
    control->window = window;
    control->id = id;
    control->group = group;
    return TRUE;
}

static HWND ribbon_create_home_button(RibbonContext *ribbon, int group,
                                      UINT command, const WCHAR *caption,
                                      BOOL toggle)
{
    HWND button = ribbon_create_button(ribbon, RIBBON_TAB_HOME, command,
                                       caption, toggle);
    if (button == NULL ||
        !ribbon_home_track(ribbon, button, command, group)) {
        return NULL;
    }
    return button;
}

static HWND ribbon_create_home_label(RibbonContext *ribbon, int group)
{
    HWND label = ribbon_create_control(
        ribbon, RIBBON_TAB_HOME, 0, WC_STATICW, homeGroupNames[group],
        SS_CENTER | SS_CENTERIMAGE, homeGroupLabelIds[group]);
    if (label == NULL || !ribbon_home_track(
            ribbon, label, homeGroupLabelIds[group], group)) {
        return NULL;
    }
    return label;
}

static BOOL ribbon_register_existing_home(RibbonContext *ribbon,
                                          HWND window, UINT id, int group)
{
    return ribbon_page_add(ribbon, RIBBON_TAB_HOME, window) &&
           ribbon_home_track(ribbon, window, id, group);
}

static BOOL ribbon_create_home_page(RibbonContext *ribbon)
{
    AppState *app = ribbon->app;
    size_t index;

    ribbon->clipboardButtons[0] = ribbon_create_home_button(
        ribbon, HOME_GROUP_CLIPBOARD, IDM_EDIT_PASTE, L"Paste", FALSE);
    ribbon->clipboardButtons[1] = ribbon_create_home_button(
        ribbon, HOME_GROUP_CLIPBOARD, IDM_EDIT_CUT, L"Cut", FALSE);
    ribbon->clipboardButtons[2] = ribbon_create_home_button(
        ribbon, HOME_GROUP_CLIPBOARD, IDM_EDIT_COPY, L"Copy", FALSE);
    if (ribbon->clipboardButtons[0] == NULL ||
        ribbon->clipboardButtons[1] == NULL ||
        ribbon->clipboardButtons[2] == NULL ||
        !ribbon_page_add(ribbon, RIBBON_TAB_HOME, app->fontLabel) ||
        !ribbon_register_existing_home(ribbon, app->fontCombo,
                                       IDC_FONT_COMBO, HOME_GROUP_FONT) ||
        !ribbon_register_existing_home(ribbon, app->sizeCombo,
                                       IDC_SIZE_COMBO, HOME_GROUP_FONT)) {
        return FALSE;
    }

    ribbon->growFontButton = ribbon_create_home_button(
        ribbon, HOME_GROUP_FONT, IDM_FORMAT_GROW_FONT, L"Grow Font", FALSE);
    ribbon->shrinkFontButton = ribbon_create_home_button(
        ribbon, HOME_GROUP_FONT, IDM_FORMAT_SHRINK_FONT, L"Shrink Font", FALSE);
    ribbon->clearFormattingButton = ribbon_create_home_button(
        ribbon, HOME_GROUP_FONT, IDM_FORMAT_CLEAR, L"Clear", FALSE);
    if (ribbon->growFontButton == NULL || ribbon->shrinkFontButton == NULL ||
        ribbon->clearFormattingButton == NULL ||
        !ribbon_register_existing_home(ribbon, app->boldButton,
                                       IDC_FORMAT_BOLD, HOME_GROUP_FONT) ||
        !ribbon_register_existing_home(ribbon, app->italicButton,
                                       IDC_FORMAT_ITALIC, HOME_GROUP_FONT) ||
        !ribbon_register_existing_home(ribbon, app->underlineButton,
                                       IDC_FORMAT_UNDERLINE, HOME_GROUP_FONT) ||
        !ribbon_register_existing_home(ribbon, app->strikeButton,
                                       IDC_FORMAT_STRIKE, HOME_GROUP_FONT)) {
        return FALSE;
    }
    ribbon->subscriptButton = ribbon_create_home_button(
        ribbon, HOME_GROUP_FONT, IDM_FORMAT_SUBSCRIPT, L"Subscript", TRUE);
    ribbon->superscriptButton = ribbon_create_home_button(
        ribbon, HOME_GROUP_FONT, IDM_FORMAT_SUPERSCRIPT, L"Superscript", TRUE);
    ribbon->highlightButton = ribbon_create_home_button(
        ribbon, HOME_GROUP_FONT, IDM_FORMAT_HIGHLIGHT, L"Highlight", TRUE);
    if (ribbon->subscriptButton == NULL ||
        ribbon->superscriptButton == NULL ||
        ribbon->highlightButton == NULL ||
        !ribbon_register_existing_home(ribbon, app->colorButton,
                                       IDC_TEXT_COLOR, HOME_GROUP_FONT) ||
        !ribbon_register_existing_home(ribbon, app->bulletsButton,
                                       IDC_BULLETS,
                                       HOME_GROUP_PARAGRAPH)) {
        return FALSE;
    }

    ribbon->numberingButton = ribbon_create_home_button(
        ribbon, HOME_GROUP_PARAGRAPH, IDM_FORMAT_NUMBERING,
        L"Numbered List", TRUE);
    ribbon->indentButtons[0] = ribbon_create_home_button(
        ribbon, HOME_GROUP_PARAGRAPH, IDM_FORMAT_INDENT_DECREASE,
        L"Decrease Indent", FALSE);
    ribbon->indentButtons[1] = ribbon_create_home_button(
        ribbon, HOME_GROUP_PARAGRAPH, IDM_FORMAT_INDENT_INCREASE,
        L"Increase Indent", FALSE);
    if (ribbon->numberingButton == NULL || ribbon->indentButtons[0] == NULL ||
        ribbon->indentButtons[1] == NULL ||
        !ribbon_register_existing_home(ribbon, app->alignLeftButton,
                                       IDC_ALIGN_LEFT,
                                       HOME_GROUP_PARAGRAPH) ||
        !ribbon_register_existing_home(ribbon, app->alignCenterButton,
                                       IDC_ALIGN_CENTER,
                                       HOME_GROUP_PARAGRAPH) ||
        !ribbon_register_existing_home(ribbon, app->alignRightButton,
                                       IDC_ALIGN_RIGHT,
                                       HOME_GROUP_PARAGRAPH) ||
        !ribbon_register_existing_home(ribbon, app->alignJustifyButton,
                                       IDC_ALIGN_JUSTIFY,
                                       HOME_GROUP_PARAGRAPH)) {
        return FALSE;
    }
    ribbon->lineSpacingButton = ribbon_create_home_button(
        ribbon, HOME_GROUP_PARAGRAPH, IDM_FORMAT_LINE_SPACING,
        L"Line Spacing", FALSE);
    if (ribbon->lineSpacingButton == NULL) {
        return FALSE;
    }

    for (index = 0; index < ARRAYSIZE(ribbon->styleButtons); ++index) {
        ribbon->styleButtons[index] = ribbon_create_home_button(
            ribbon, HOME_GROUP_STYLES, styleCommands[index],
            styleNames[index], TRUE);
        if (ribbon->styleButtons[index] == NULL) {
            return FALSE;
        }
    }
    ribbon->styleCombo = ribbon_create_control(
        ribbon, RIBBON_TAB_HOME, 0, WC_COMBOBOXW, NULL,
        WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
        IDC_HOME_STYLE_COMBO);
    if (ribbon->styleCombo == NULL ||
        !ribbon_home_track(ribbon, ribbon->styleCombo,
                           IDC_HOME_STYLE_COMBO, HOME_GROUP_STYLES)) {
        return FALSE;
    }
    for (index = 0; index < ARRAYSIZE(styleNames); ++index) {
        LRESULT item = SendMessageW(ribbon->styleCombo, CB_ADDSTRING, 0,
                                    (LPARAM)styleNames[index]);
        if (item == CB_ERR || item == CB_ERRSPACE) {
            return FALSE;
        }
        SendMessageW(ribbon->styleCombo, CB_SETITEMDATA, (WPARAM)item,
                     (LPARAM)index);
    }
    SendMessageW(ribbon->styleCombo, CB_SETMINVISIBLE,
                 WORDCRAFT_STYLE_COUNT, 0);
    ribbon_set_accessible_name(app->fontCombo, L"Font family");
    ribbon_set_accessible_name(app->sizeCombo, L"Font size");
    ribbon_set_accessible_name(ribbon->styleCombo, L"Document style");

    ribbon->editingButtons[0] = ribbon_create_home_button(
        ribbon, HOME_GROUP_EDITING, IDM_EDIT_FIND, L"Find", FALSE);
    ribbon->editingButtons[1] = ribbon_create_home_button(
        ribbon, HOME_GROUP_EDITING, IDM_EDIT_REPLACE, L"Replace", FALSE);
    ribbon->editingButtons[2] = ribbon_create_home_button(
        ribbon, HOME_GROUP_EDITING, IDM_EDIT_SELECT_ALL, L"Select All",
        FALSE);
    if (ribbon->editingButtons[0] == NULL ||
        ribbon->editingButtons[1] == NULL ||
        ribbon->editingButtons[2] == NULL) {
        return FALSE;
    }

    for (index = 0; index < HOME_GROUP_COUNT; ++index) {
        ribbon->homeGroupLabels[index] = ribbon_create_home_label(
            ribbon, (int)index);
        if (ribbon->homeGroupLabels[index] == NULL) {
            return FALSE;
        }
    }
    ribbon_set_active_style(app, WORDCRAFT_STYLE_NORMAL);
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
    HWND paperLabel;
    HWND pageSetupButton;
    HWND decreaseButton;
    HWND increaseButton;
    size_t index;

    paperLabel = ribbon_create_label(ribbon, RIBBON_TAB_LAYOUT,
                                     L"Paper size:",
                                     IDC_PAPER_SIZE_LABEL);
    ribbon->paperSizeCombo = ribbon_create_control(
        ribbon, RIBBON_TAB_LAYOUT, WS_EX_CLIENTEDGE, WC_COMBOBOXW, NULL,
        WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
        IDC_PAPER_SIZE_COMBO);
    pageSetupButton = ribbon_create_button(
        ribbon, RIBBON_TAB_LAYOUT, IDM_FILE_PAGE_SETUP, L"Page Setup",
        FALSE);
    decreaseButton = ribbon_create_button(
        ribbon, RIBBON_TAB_LAYOUT, IDM_FORMAT_INDENT_DECREASE,
        L"Decrease Indent", FALSE);
    increaseButton = ribbon_create_button(
        ribbon, RIBBON_TAB_LAYOUT, IDM_FORMAT_INDENT_INCREASE,
        L"Increase Indent", FALSE);
    if (paperLabel == NULL || ribbon->paperSizeCombo == NULL ||
        pageSetupButton == NULL || decreaseButton == NULL ||
        increaseButton == NULL) {
        return FALSE;
    }

    for (index = 0; index < paper_size_count(); ++index) {
        const PaperSizePreset *preset = paper_size_at(index);
        LRESULT item;

        if (preset == NULL) {
            return FALSE;
        }
        item = SendMessageW(ribbon->paperSizeCombo, CB_ADDSTRING, 0,
                            (LPARAM)preset->displayName);
        if (item == CB_ERR || item == CB_ERRSPACE) {
            return FALSE;
        }
        SendMessageW(ribbon->paperSizeCombo, CB_SETITEMDATA,
                     (WPARAM)item, (LPARAM)index);
    }
    SendMessageW(ribbon->paperSizeCombo, CB_SETMINVISIBLE, 18, 0);
    SendMessageW(ribbon->paperSizeCombo, CB_SETDROPPEDWIDTH,
                 (WPARAM)app_scale(ribbon->app->mainWindow, 320), 0);
    return TRUE;
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
    ribbon->liveShareButton = ribbon_create_button(
        ribbon, RIBBON_TAB_REVIEW, IDM_REVIEW_LIVE_SHARE,
        L"Live Share...", TRUE);

    if (commentLabel == NULL || ribbon->commentEdit == NULL ||
        ribbon->addCommentButton == NULL || ribbon->commentSummary == NULL ||
        ribbon->previousCommentButton == NULL ||
        ribbon->nextCommentButton == NULL ||
        ribbon->deleteCommentButton == NULL ||
        ribbon->spellCheckButton == NULL ||
        ribbon->autoCompleteButton == NULL ||
        ribbon->liveShareButton == NULL) {
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
    return ribbon_create_home_page(ribbon) &&
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
    if (activeTab != RIBBON_TAB_REVIEW && ribbon->app != NULL) {
        comments_cancel_draft(ribbon->app);
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

static void ribbon_set_control_visible(RibbonContext *ribbon, HWND control,
                                       BOOL visible, HWND fallback)
{
    if (control != NULL) {
        HWND focus = GetFocus();
        if (!visible && focus != NULL &&
            (focus == control || IsChild(control, focus))) {
            if (fallback == NULL ||
                (GetWindowLongPtrW(fallback, GWL_STYLE) & WS_VISIBLE) == 0 ||
                !IsWindowEnabled(fallback)) {
                fallback = ribbon != NULL && ribbon->app != NULL
                               ? ribbon->app->editor
                               : NULL;
            }
            if (fallback != NULL && IsWindow(fallback)) {
                SetFocus(fallback);
            }
        }
        ShowWindow(control, visible ? SW_SHOWNA : SW_HIDE);
    }
}

static void ribbon_place_control(HWND control, int x, int y,
                                 int width, int height)
{
    if (control != NULL) {
        SetWindowPos(control, NULL, x, y, max(0, width), max(0, height),
                     SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOCOPYBITS);
    }
}

static void ribbon_layout_home(RibbonContext *ribbon, int width, int height)
{
    AppState *app = ribbon->app;
    int outer = app_scale(app->mainWindow, 8);
    int groupGap = app_scale(app->mainWindow, 4);
    int controlGap = app_scale(app->mainWindow, 4);
    int y1 = app_scale(app->mainWindow, 7);
    int y2 = app_scale(app->mainWindow, 39);
    int controlHeight = app_scale(app->mainWindow, 27);
    int labelY = app_scale(app->mainWindow, 74);
    int labelHeight = app_scale(app->mainWindow, 18);
    int available;
    int groupWidths[HOME_GROUP_COUNT];
    int x;
    int group;
    BOOL active = ribbon->activeTab == RIBBON_TAB_HOME;
    size_t index;

    ribbon->styleGalleryVisible = FALSE;
    if (width <= outer * 2 || height <= 0) {
        ribbon->homeLayoutMode = RIBBON_LAYOUT_COLLAPSED;
        for (group = 0; group < HOME_GROUP_COUNT; ++group) {
            SetRectEmpty(&ribbon->homeGroupRects[group]);
        }
        for (index = 0; index < ribbon->homeControlCount; ++index) {
            ribbon_set_control_visible(
                ribbon, ribbon->homeControls[index].window, FALSE,
                app->ribbonTabs);
        }
        ribbon_set_control_visible(ribbon, app->fontLabel, FALSE,
                                   app->ribbonTabs);
        goto finish;
    }

    available = max(0, width - outer * 2 - groupGap *
                    (HOME_GROUP_COUNT - 1));
    if (width >= app_scale(app->mainWindow, 1180)) {
        ribbon->homeLayoutMode = RIBBON_LAYOUT_FULL;
        groupWidths[HOME_GROUP_CLIPBOARD] = app_scale(app->mainWindow, 115);
        groupWidths[HOME_GROUP_FONT] = app_scale(app->mainWindow, 310);
        groupWidths[HOME_GROUP_PARAGRAPH] = app_scale(app->mainWindow, 285);
        groupWidths[HOME_GROUP_EDITING] = app_scale(app->mainWindow, 118);
        groupWidths[HOME_GROUP_STYLES] = max(
            app_scale(app->mainWindow, 260),
            available - groupWidths[HOME_GROUP_CLIPBOARD] -
                groupWidths[HOME_GROUP_FONT] -
                groupWidths[HOME_GROUP_PARAGRAPH] -
                groupWidths[HOME_GROUP_EDITING]);
    } else if (width >= app_scale(app->mainWindow, 800)) {
        ribbon->homeLayoutMode = RIBBON_LAYOUT_COMPACT;
        groupWidths[HOME_GROUP_CLIPBOARD] = app_scale(app->mainWindow, 88);
        groupWidths[HOME_GROUP_FONT] = app_scale(app->mainWindow, 258);
        groupWidths[HOME_GROUP_PARAGRAPH] = app_scale(app->mainWindow, 232);
        groupWidths[HOME_GROUP_STYLES] = app_scale(app->mainWindow, 100);
        groupWidths[HOME_GROUP_EDITING] = max(
            app_scale(app->mainWindow, 60),
            available - groupWidths[HOME_GROUP_CLIPBOARD] -
                groupWidths[HOME_GROUP_FONT] -
                groupWidths[HOME_GROUP_PARAGRAPH] -
                groupWidths[HOME_GROUP_STYLES]);
    } else {
        int flexible;
        ribbon->homeLayoutMode = RIBBON_LAYOUT_COLLAPSED;
        groupWidths[HOME_GROUP_CLIPBOARD] = max(
            app_scale(app->mainWindow, 58), available * 12 / 100);
        groupWidths[HOME_GROUP_STYLES] = max(
            app_scale(app->mainWindow, 82), available * 16 / 100);
        groupWidths[HOME_GROUP_EDITING] = max(
            app_scale(app->mainWindow, 56), available * 11 / 100);
        flexible = max(0, available -
                           groupWidths[HOME_GROUP_CLIPBOARD] -
                           groupWidths[HOME_GROUP_STYLES] -
                           groupWidths[HOME_GROUP_EDITING]);
        groupWidths[HOME_GROUP_FONT] = flexible * 52 / 100;
        groupWidths[HOME_GROUP_PARAGRAPH] =
            flexible - groupWidths[HOME_GROUP_FONT];
    }

    x = outer;
    for (group = 0; group < HOME_GROUP_COUNT; ++group) {
        RECT *rect = &ribbon->homeGroupRects[group];
        rect->left = x;
        rect->top = app_scale(app->mainWindow, 4);
        rect->right = min(width - outer, x + groupWidths[group]);
        rect->bottom = max(rect->top, height - app_scale(app->mainWindow, 4));
        ribbon_place_control(ribbon->homeGroupLabels[group],
                             rect->left + app_scale(app->mainWindow, 3),
                             labelY,
                             max(0, rect->right - rect->left -
                                        app_scale(app->mainWindow, 6)),
                             labelHeight);
        x = rect->right + groupGap;
    }

    for (index = 0; index < ribbon->homeControlCount; ++index) {
        ribbon_set_control_visible(ribbon,
                                   ribbon->homeControls[index].window,
                                   active, app->ribbonTabs);
    }
    ribbon_set_control_visible(ribbon, app->fontLabel, FALSE,
                               app->ribbonTabs);
    if (!active) {
        goto finish;
    }

    /* Clipboard: a large Paste action with Cut and Copy stacked beside it. */
    {
        RECT groupRect = ribbon->homeGroupRects[HOME_GROUP_CLIPBOARD];
        int left = groupRect.left + controlGap;
        int usable = max(0, groupRect.right - groupRect.left - controlGap * 2);
        if (ribbon->homeLayoutMode == RIBBON_LAYOUT_COLLAPSED) {
            int itemGap = max(1, app_scale(app->mainWindow, 1));
            int totalHeight = y2 + controlHeight - y1;
            int itemHeight = max(1, (totalHeight - itemGap * 2) / 3);
            for (index = 0; index < ARRAYSIZE(ribbon->clipboardButtons);
                 ++index) {
                int heightForItem =
                    index + 1 == ARRAYSIZE(ribbon->clipboardButtons)
                        ? totalHeight - (itemHeight + itemGap) * (int)index
                        : itemHeight;
                ribbon_place_control(ribbon->clipboardButtons[index], left,
                                     y1 + (itemHeight + itemGap) * (int)index,
                                     usable, heightForItem);
            }
        } else {
            int pasteWidth = max(1, usable * 48 / 100);
            int rightWidth = max(1, usable - pasteWidth - controlGap);
            ribbon_place_control(ribbon->clipboardButtons[0], left, y1,
                                 pasteWidth, y2 + controlHeight - y1);
            ribbon_place_control(ribbon->clipboardButtons[1],
                                 left + pasteWidth + controlGap, y1,
                                 rightWidth, controlHeight);
            ribbon_place_control(ribbon->clipboardButtons[2],
                                 left + pasteWidth + controlGap, y2,
                                 rightWidth, controlHeight);
        }
    }

    /* Font group: family and size above, character effects below. */
    {
        RECT groupRect = ribbon->homeGroupRects[HOME_GROUP_FONT];
        HWND top[] = {
            app->fontCombo, app->sizeCombo, ribbon->growFontButton,
            ribbon->shrinkFontButton, ribbon->clearFormattingButton
        };
        HWND bottom[] = {
            app->boldButton, app->italicButton, app->underlineButton,
            app->strikeButton, ribbon->subscriptButton,
            ribbon->superscriptButton, ribbon->highlightButton,
            app->colorButton
        };
        int left = groupRect.left + controlGap;
        int usable = max(0, groupRect.right - groupRect.left - controlGap * 2);
        int topWidths[5];
        int bottomWidths[8];
        int remaining;
        int cursor;

        if (ribbon->homeLayoutMode == RIBBON_LAYOUT_COLLAPSED) {
            ribbon_set_control_visible(ribbon, ribbon->growFontButton,
                                       FALSE, app->fontCombo);
            ribbon_set_control_visible(ribbon, ribbon->shrinkFontButton,
                                       FALSE, app->fontCombo);
            ribbon_set_control_visible(ribbon,
                                       ribbon->clearFormattingButton,
                                       FALSE, app->fontCombo);
            ribbon_set_control_visible(ribbon, app->strikeButton, FALSE,
                                       app->fontCombo);
            ribbon_set_control_visible(ribbon, ribbon->subscriptButton,
                                       FALSE, app->fontCombo);
            ribbon_set_control_visible(ribbon, ribbon->superscriptButton,
                                       FALSE, app->fontCombo);
            ribbon_set_control_visible(ribbon, ribbon->highlightButton,
                                       FALSE, app->fontCombo);
            topWidths[0] = max(1, usable * 70 / 100 - controlGap);
            topWidths[1] = max(1, usable - topWidths[0] - controlGap);
            ribbon_place_control(top[0], left, y1, topWidths[0],
                                 app_scale(app->mainWindow, 230));
            ribbon_place_control(top[1], left + topWidths[0] + controlGap,
                                 y1, topWidths[1],
                                 app_scale(app->mainWindow, 230));
            remaining = max(0, usable - controlGap * 3);
            cursor = left;
            for (index = 0; index < 4; ++index) {
                HWND control = index < 3 ? bottom[index] : app->colorButton;
                int itemWidth = (index == 3)
                                    ? left + usable - cursor
                                    : remaining / 4;
                ribbon_place_control(control, cursor, y2,
                                     itemWidth, controlHeight);
                cursor += itemWidth + controlGap;
            }
        } else {
            topWidths[0] = max(1, usable * 42 / 100);
            topWidths[1] = max(1, usable * 14 / 100);
            topWidths[2] = max(1, usable * 9 / 100);
            topWidths[3] = max(1, usable * 9 / 100);
            topWidths[4] = max(1, usable - controlGap * 4 -
                                      topWidths[0] - topWidths[1] -
                                      topWidths[2] - topWidths[3]);
            cursor = left;
            for (index = 0; index < ARRAYSIZE(top); ++index) {
                ribbon_place_control(top[index], cursor, y1,
                                     topWidths[index],
                                     index < 2
                                         ? app_scale(app->mainWindow, 230)
                                         : controlHeight);
                cursor += topWidths[index] + controlGap;
            }

            bottomWidths[4] = app_scale(
                app->mainWindow,
                ribbon->homeLayoutMode == RIBBON_LAYOUT_COMPACT ? 27 : 31);
            bottomWidths[5] = app_scale(
                app->mainWindow,
                ribbon->homeLayoutMode == RIBBON_LAYOUT_COMPACT ? 32 : 38);
            bottomWidths[6] = app_scale(
                app->mainWindow,
                ribbon->homeLayoutMode == RIBBON_LAYOUT_COMPACT ? 40 : 52);
            bottomWidths[7] = app_scale(
                app->mainWindow,
                ribbon->homeLayoutMode == RIBBON_LAYOUT_COMPACT ? 40 : 48);
            remaining = max(4, usable - controlGap * 7 -
                                   bottomWidths[4] - bottomWidths[5] -
                                   bottomWidths[6] - bottomWidths[7]);
            for (index = 0; index < 4; ++index) {
                bottomWidths[index] = remaining / 4;
            }
            bottomWidths[3] += remaining - bottomWidths[0] * 4;
            cursor = left;
            for (index = 0; index < ARRAYSIZE(bottom); ++index) {
                ribbon_place_control(bottom[index], cursor, y2,
                                     bottomWidths[index], controlHeight);
                cursor += bottomWidths[index] + controlGap;
            }
        }
    }

    /* Paragraph group: lists and indentation above, alignment below. */
    {
        RECT groupRect = ribbon->homeGroupRects[HOME_GROUP_PARAGRAPH];
        HWND top[] = {
            app->bulletsButton, ribbon->numberingButton,
            ribbon->indentButtons[0], ribbon->indentButtons[1],
            ribbon->lineSpacingButton
        };
        HWND bottom[] = {
            app->alignLeftButton, app->alignCenterButton,
            app->alignRightButton, app->alignJustifyButton
        };
        int left = groupRect.left + controlGap;
        int usable = max(0, groupRect.right - groupRect.left - controlGap * 2);
        int cursor = left;

        if (ribbon->homeLayoutMode == RIBBON_LAYOUT_COLLAPSED) {
            int half = max(1, (usable - controlGap) / 2);
            ribbon_set_control_visible(ribbon, ribbon->indentButtons[0],
                                       FALSE, app->bulletsButton);
            ribbon_set_control_visible(ribbon, ribbon->indentButtons[1],
                                       FALSE, app->bulletsButton);
            ribbon_set_control_visible(ribbon, ribbon->lineSpacingButton,
                                       FALSE, app->bulletsButton);
            ribbon_place_control(top[0], left, y1, half, controlHeight);
            ribbon_place_control(top[1], left + half + controlGap, y1,
                                 usable - half - controlGap, controlHeight);
        } else {
            int topWidths[5];
            topWidths[0] = max(1, usable * 18 / 100);
            topWidths[1] = max(1, usable * 20 / 100);
            topWidths[2] = max(1, usable * 16 / 100);
            topWidths[3] = max(1, usable * 16 / 100);
            topWidths[4] = max(1, usable - controlGap * 4 -
                                      topWidths[0] - topWidths[1] -
                                      topWidths[2] - topWidths[3]);
            for (index = 0; index < ARRAYSIZE(top); ++index) {
                ribbon_place_control(top[index], cursor, y1,
                                     topWidths[index], controlHeight);
                cursor += topWidths[index] + controlGap;
            }
        }
        cursor = left;
        for (index = 0; index < ARRAYSIZE(bottom); ++index) {
            int itemWidth = index + 1 == ARRAYSIZE(bottom)
                                ? left + usable - cursor
                                : max(1, (usable - controlGap * 3) / 4);
            ribbon_place_control(bottom[index], cursor, y2,
                                 itemWidth, controlHeight);
            cursor += itemWidth + controlGap;
        }
    }

    /* Styles expand into a gallery when there is room and collapse to a
     * standard keyboard-friendly combo at narrower widths. */
    {
        RECT groupRect = ribbon->homeGroupRects[HOME_GROUP_STYLES];
        int left = groupRect.left + controlGap;
        int usable = max(0, groupRect.right - groupRect.left - controlGap * 2);
        BOOL gallery =
            ribbon->homeLayoutMode == RIBBON_LAYOUT_FULL &&
            usable >= app_scale(app->mainWindow,
                                WORDCRAFT_STYLE_COUNT * 84 +
                                    (WORDCRAFT_STYLE_COUNT - 1) * 4);
        HWND styleFallback =
            ribbon->styleButtons[ribbon->activeStyle >= 0 &&
                                         ribbon->activeStyle <
                                             WORDCRAFT_STYLE_COUNT
                                     ? ribbon->activeStyle
                                     : WORDCRAFT_STYLE_NORMAL];
        int cursor = left;

        ribbon->styleGalleryVisible = gallery;
        ribbon_set_control_visible(ribbon, ribbon->styleCombo, !gallery,
                                   styleFallback);
        for (index = 0; index < ARRAYSIZE(ribbon->styleButtons); ++index) {
            ribbon_set_control_visible(ribbon, ribbon->styleButtons[index],
                                       gallery, ribbon->styleCombo);
        }
        if (gallery) {
            for (index = 0; index < ARRAYSIZE(ribbon->styleButtons); ++index) {
                int itemWidth = index + 1 == ARRAYSIZE(ribbon->styleButtons)
                                    ? left + usable - cursor
                                    : max(1, (usable - controlGap *
                                                   (WORDCRAFT_STYLE_COUNT - 1)) /
                                                 WORDCRAFT_STYLE_COUNT);
                ribbon_place_control(ribbon->styleButtons[index], cursor, y1,
                                     itemWidth, y2 + controlHeight - y1);
                cursor += itemWidth + controlGap;
            }
        } else {
            ribbon_place_control(ribbon->styleCombo, left, y1, usable,
                                 app_scale(app->mainWindow, 190));
        }
    }

    /* Editing remains available even in the narrow collapsed layout. */
    {
        RECT groupRect = ribbon->homeGroupRects[HOME_GROUP_EDITING];
        int left = groupRect.left + controlGap;
        int usable = max(0, groupRect.right - groupRect.left - controlGap * 2);
        if (ribbon->homeLayoutMode == RIBBON_LAYOUT_FULL) {
            int half = max(1, (usable - controlGap) / 2);
            ribbon_place_control(ribbon->editingButtons[0], left, y1,
                                 half, controlHeight);
            ribbon_place_control(ribbon->editingButtons[1],
                                 left + half + controlGap, y1,
                                 usable - half - controlGap, controlHeight);
            ribbon_place_control(ribbon->editingButtons[2], left, y2,
                                 usable, controlHeight);
        } else {
            int editHeight = max(1, app_scale(app->mainWindow, 19));
            int editGap = max(1, app_scale(app->mainWindow, 2));
            ribbon_place_control(ribbon->editingButtons[0], left, y1,
                                 usable, editHeight);
            ribbon_place_control(ribbon->editingButtons[1], left,
                                 y1 + editHeight + editGap,
                                 usable, editHeight);
            ribbon_place_control(ribbon->editingButtons[2], left,
                                 y1 + (editHeight + editGap) * 2,
                                 usable, editHeight);
        }
    }

finish:
    ribbon->layoutGeneration = ribbon->layoutGeneration == UINT_MAX
                                   ? 1
                                   : ribbon->layoutGeneration + 1;
    InvalidateRect(app->formatBar, NULL, TRUE);
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

static void ribbon_layout_layout_page(RibbonContext *ribbon)
{
    AppState *app = ribbon->app;
    RibbonPage *page = &ribbon->pages[RIBBON_TAB_LAYOUT];
    int gap = app_scale(app->mainWindow, 4);
    int x = app_scale(app->mainWindow, 8);
    int firstY = app_scale(app->mainWindow, 5);
    int secondY = app_scale(app->mainWindow, 39);
    int controlHeight = app_scale(app->mainWindow, 28);
    int labelWidth = app_scale(app->mainWindow, 72);
    int comboWidth = app_scale(app->mainWindow, 278);
    int buttonWidth = app_scale(app->mainWindow, 112);

    if (page->count < 5) {
        return;
    }
    MoveWindow(page->controls[0], x, firstY, labelWidth, controlHeight, TRUE);
    x += labelWidth + gap;
    MoveWindow(ribbon->paperSizeCombo, x, firstY, comboWidth,
               app_scale(app->mainWindow, 380), TRUE);
    x += comboWidth + gap;
    MoveWindow(page->controls[2], x, firstY, buttonWidth, controlHeight, TRUE);

    x = app_scale(app->mainWindow, 8);
    MoveWindow(page->controls[3], x, secondY,
               app_scale(app->mainWindow, 126), controlHeight, TRUE);
    x += app_scale(app->mainWindow, 130);
    MoveWindow(page->controls[4], x, secondY,
               app_scale(app->mainWindow, 126), controlHeight, TRUE);
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
        app_scale(app->mainWindow, 70), app_scale(app->mainWindow, 54),
        app_scale(app->mainWindow, 60), app_scale(app->mainWindow, 92),
        app_scale(app->mainWindow, 98), app_scale(app->mainWindow, 92)
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
    x += secondWidths[4] + gap;
    MoveWindow(ribbon->liveShareButton, x, secondY, secondWidths[5],
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

static void ribbon_tabs_paint_unused(HWND window, AppState *app, HDC dc)
{
    RECT client;
    RECT itemRect;
    RECT fill;
    HWND child;
    int count;
    int index;
    int usedRight = 0;
    int itemBottom = 0;
    int fillRight;

    if (window == NULL || app == NULL || dc == NULL ||
        !app->useBrandColors) {
        return;
    }
    GetClientRect(window, &client);
    fillRight = client.right;
    count = TabCtrl_GetItemCount(window);
    for (index = 0; index < count; ++index) {
        if (TabCtrl_GetItemRect(window, index, &itemRect)) {
            usedRight = max(usedRight, itemRect.right);
            itemBottom = max(itemBottom, itemRect.bottom);
        }
    }
    child = GetWindow(window, GW_CHILD);
    while (child != NULL) {
        if (IsWindowVisible(child)) {
            RECT childRect;
            GetWindowRect(child, &childRect);
            MapWindowPoints(NULL, window, (POINT *)&childRect, 2);
            fillRight = min(fillRight, childRect.left);
        }
        child = GetWindow(child, GW_HWNDNEXT);
    }
    if (usedRight < fillRight) {
        SetRect(&fill, max(client.left, usedRight), client.top,
                fillRight, client.bottom);
        ribbon_fill_rect(dc, &fill, app->palette.formatBackground);
    }
    if (itemBottom < client.bottom) {
        SetRect(&fill, client.left, max(client.top, itemBottom),
                fillRight, client.bottom);
        ribbon_fill_rect(dc, &fill, app->palette.formatBackground);
    }
}

static LRESULT CALLBACK ribbon_tabs_subclass_proc(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR subclassId, DWORD_PTR referenceData)
{
    AppState *app = (AppState *)referenceData;
    LRESULT result;
    (void)subclassId;

    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, ribbon_tabs_subclass_proc, 3);
        return DefSubclassProc(window, message, wParam, lParam);
    }
    if (app != NULL && app->useBrandColors && message == WM_ERASEBKGND) {
        RECT client;
        GetClientRect(window, &client);
        ribbon_fill_rect((HDC)wParam, &client,
                         app->palette.formatBackground);
        return 1;
    }
    result = DefSubclassProc(window, message, wParam, lParam);
    if (app != NULL && app->useBrandColors && message == WM_PAINT) {
        HDC dc = GetDC(window);
        if (dc != NULL) {
            ribbon_tabs_paint_unused(window, app, dc);
            ReleaseDC(window, dc);
        }
    } else if (app != NULL && app->useBrandColors &&
               message == WM_PRINTCLIENT && wParam != 0) {
        ribbon_tabs_paint_unused(window, app, (HDC)wParam);
    }
    return result;
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
    if (!SetWindowSubclass(app->ribbonTabs, ribbon_tabs_subclass_proc, 3,
                           (DWORD_PTR)app)) {
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
    SendMessageW(ribbon->styleCombo, CB_SETDROPPEDWIDTH,
                 (WPARAM)app_scale(app->mainWindow, 180), 0);
    SendMessageW(ribbon->paperSizeCombo, CB_SETDROPPEDWIDTH,
                 (WPARAM)app_scale(app->mainWindow, 320), 0);

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

    tabWidth = ribbon_clamp(
        (width - app_scale(app->mainWindow, 8)) / RIBBON_TAB_COUNT,
        app_scale(app->mainWindow, 58),
        app_scale(app->mainWindow, 92));
    TabCtrl_SetItemSize(app->ribbonTabs, tabWidth,
                        app_scale(app->mainWindow, 29));

    ribbon_show_active_page(ribbon, ribbon->activeTab);
    ribbon_layout_file(ribbon);
    ribbon_layout_home(ribbon, width, height);
    ribbon_layout_simple_page(ribbon, RIBBON_TAB_INSERT, 104);
    ribbon_layout_placeholder(ribbon, RIBBON_TAB_DRAW, width);
    ribbon_layout_placeholder(ribbon, RIBBON_TAB_DESIGN, width);
    ribbon_layout_layout_page(ribbon);
    ribbon_layout_placeholder(ribbon, RIBBON_TAB_REFERENCES, width);
    ribbon_layout_placeholder(ribbon, RIBBON_TAB_MAILINGS, width);
    ribbon_layout_review(ribbon, width);
    ribbon_layout_view(ribbon);
    ribbon_layout_simple_page(ribbon, RIBBON_TAB_HELP, 126);

    (void)height;
}

void ribbon_paint_home_groups(AppState *app, HDC dc)
{
    RibbonContext *ribbon;
    HPEN pen;
    HGDIOBJ previousPen;
    COLORREF divider;
    int group;
    UINT painted = 0;

    if (!ribbon_valid_context(app) || dc == NULL) {
        return;
    }
    ribbon = app->ribbon;
    if (ribbon->activeTab != RIBBON_TAB_HOME) {
        return;
    }
    divider = app->useBrandColors
                  ? ribbon_blend_color(app->palette.controlBorder,
                                       app->palette.controlBackground, 42)
                  : GetSysColor(COLOR_3DSHADOW);
    pen = CreatePen(PS_SOLID, max(1, app_scale(app->mainWindow, 1)),
                    divider);
    if (pen == NULL) {
        return;
    }
    previousPen = SelectObject(dc, pen);
    for (group = 0; group + 1 < HOME_GROUP_COUNT; ++group) {
        const RECT *left = &ribbon->homeGroupRects[group];
        const RECT *right = &ribbon->homeGroupRects[group + 1];
        int x;
        int top;
        int bottom;

        if (IsRectEmpty(left) || IsRectEmpty(right)) {
            continue;
        }
        x = left->right + (right->left - left->right) / 2;
        top = left->top + app_scale(app->mainWindow, 7);
        bottom = left->bottom - app_scale(app->mainWindow, 8);
        if (MoveToEx(dc, x, top, NULL) && LineTo(dc, x, bottom)) {
            ++painted;
        }
    }
    SelectObject(dc, previousPen);
    DeleteObject(pen);
    if (painted > 0) {
        ribbon->homeGroupPaintCount =
            ribbon->homeGroupPaintCount == UINT_MAX
                ? 1
                : ribbon->homeGroupPaintCount + 1;
    }
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
    SendMessageW(ribbon->styleCombo, CB_SETDROPPEDWIDTH,
                 (WPARAM)app_scale(app->mainWindow, 180), 0);
    SendMessageW(ribbon->paperSizeCombo, CB_SETDROPPEDWIDTH,
                 (WPARAM)app_scale(app->mainWindow, 320), 0);
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

BOOL ribbon_handle_command(AppState *app, WPARAM wParam, LPARAM lParam)
{
    RibbonContext *ribbon;
    LRESULT selection;
    LRESULT catalogIndex;

    if (!ribbon_valid_context(app)) {
        return FALSE;
    }
    ribbon = app->ribbon;
    if ((HWND)lParam == ribbon->styleCombo &&
        LOWORD(wParam) == IDC_HOME_STYLE_COMBO &&
        HIWORD(wParam) == CBN_SELENDOK) {
        selection = SendMessageW(ribbon->styleCombo, CB_GETCURSEL, 0, 0);
        if (selection != CB_ERR) {
            catalogIndex = SendMessageW(ribbon->styleCombo, CB_GETITEMDATA,
                                        (WPARAM)selection, 0);
            if (catalogIndex >= 0 &&
                catalogIndex < WORDCRAFT_STYLE_COUNT) {
                format_apply_style(app, (WordcraftStyle)catalogIndex);
            }
        }
        return TRUE;
    }
    if ((HWND)lParam != ribbon->paperSizeCombo ||
        LOWORD(wParam) != IDC_PAPER_SIZE_COMBO ||
        HIWORD(wParam) != CBN_SELENDOK) {
        return FALSE;
    }
    selection = SendMessageW(ribbon->paperSizeCombo, CB_GETCURSEL, 0, 0);
    if (selection == CB_ERR) {
        ribbon_sync_paper_size(app);
        return TRUE;
    }
    catalogIndex = SendMessageW(ribbon->paperSizeCombo, CB_GETITEMDATA,
                                (WPARAM)selection, 0);
    if (catalogIndex == CB_ERR || catalogIndex < 0 ||
        (size_t)catalogIndex >= paper_size_count() ||
        !paper_size_select_catalog_index(app, (size_t)catalogIndex)) {
        ribbon_sync_paper_size(app);
    }
    return TRUE;
}

BOOL ribbon_draw_item(AppState *app, const DRAWITEMSTRUCT *draw)
{
    RECT textRect;
    RECT shapeRect;
    COLORREF background;
    COLORREF textColor;
    COLORREF borderColor;
    HBRUSH brush;
    HPEN pen;
    HGDIOBJ previousBrush;
    HGDIOBJ previousPen;
    HFONT font;
    HGDIOBJ previousFont = NULL;
    BOOL selected;
    BOOL hot;
    int item;
    int radius;

    if (!ribbon_valid_context(app) || draw == NULL ||
        draw->CtlType != ODT_TAB || draw->hwndItem != app->ribbonTabs) {
        return FALSE;
    }
    item = (int)draw->itemID;
    if (item < 0 || item >= RIBBON_TAB_COUNT) {
        return FALSE;
    }
    selected = TabCtrl_GetCurSel(app->ribbonTabs) == item;
    hot = (draw->itemState & ODS_HOTLIGHT) != 0;
    if (app->useBrandColors) {
        background = selected
                         ? app->palette.controlBackground
                         : (hot
                                ? ribbon_blend_color(
                                      app->palette.toolbarBackground,
                                      app->palette.toolbarHotBackground, 22)
                                : app->palette.toolbarBackground);
        textColor = selected ? app->palette.formatText
                             : app->palette.toolbarText;
        borderColor = selected || hot
                          ? ribbon_blend_color(
                                app->palette.controlBorder,
                                app->palette.toolbarHotBackground,
                                selected ? 30 : 60)
                          : ribbon_blend_color(
                                app->palette.controlBorder,
                                app->palette.toolbarBackground, 55);
        ribbon_fill_rect(draw->hDC, &draw->rcItem,
                         app->palette.formatBackground);
        shapeRect = draw->rcItem;
        InflateRect(&shapeRect, -app_scale(app->mainWindow, 3),
                    -app_scale(app->mainWindow, 2));
        radius = max(2, app_scale(app->mainWindow, 8));
        brush = CreateSolidBrush(background);
        pen = CreatePen(PS_SOLID,
                        max(1, app_scale(app->mainWindow, 1)),
                        borderColor);
        if (brush != NULL && pen != NULL) {
            previousBrush = SelectObject(draw->hDC, brush);
            previousPen = SelectObject(draw->hDC, pen);
            if (RoundRect(draw->hDC, shapeRect.left, shapeRect.top,
                          shapeRect.right, shapeRect.bottom,
                          radius * 2, radius * 2)) {
                app->uiTabCornerRadius = radius;
                app->uiTabPaintCount = app->uiTabPaintCount == LONG_MAX
                                           ? 1
                                           : app->uiTabPaintCount + 1;
            }
            SelectObject(draw->hDC, previousPen);
            SelectObject(draw->hDC, previousBrush);
        }
        if (pen != NULL) {
            DeleteObject(pen);
        }
        if (brush != NULL) {
            DeleteObject(brush);
        }
        textRect = shapeRect;
    } else {
        background = GetSysColor(selected ? COLOR_WINDOW : COLOR_BTNFACE);
        textColor = GetSysColor(
            (draw->itemState & ODS_DISABLED) != 0
                ? COLOR_GRAYTEXT
                : (selected ? COLOR_WINDOWTEXT : COLOR_BTNTEXT));
        borderColor = GetSysColor(COLOR_3DSHADOW);
        ribbon_fill_rect(draw->hDC, &draw->rcItem, borderColor);
        textRect = draw->rcItem;
        InflateRect(&textRect, -1, -1);
        ribbon_fill_rect(draw->hDC, &textRect, background);
    }
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
    EnableWindow(ribbon->liveShareButton, app->editor != NULL);
    EnableWindow(ribbon->clipboardButtons[0], canPaste);
    EnableWindow(ribbon->clipboardButtons[1], hasSelection);
    EnableWindow(ribbon->clipboardButtons[2], hasSelection);
    EnableWindow(ribbon->editingButtons[0], app->editor != NULL);
    EnableWindow(ribbon->editingButtons[1], app->editor != NULL);
    EnableWindow(ribbon->editingButtons[2], app->editor != NULL);
    ribbon_sync_paper_size(app);

    (void)canUndo;
    (void)canRedo;
}

void ribbon_set_live_share_status(AppState *app, const WCHAR *caption,
                                  BOOL active)
{
    RibbonContext *ribbon;

    if (!ribbon_valid_context(app)) {
        return;
    }
    ribbon = app->ribbon;
    SetWindowTextW(ribbon->liveShareButton,
                   caption != NULL && caption[0] != L'\0'
                       ? caption : L"Live Share...");
    SendMessageW(ribbon->liveShareButton, BM_SETCHECK,
                 active ? BST_CHECKED : BST_UNCHECKED, 0);
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

void ribbon_sync_paper_size(AppState *app)
{
    RibbonContext *ribbon;
    size_t index;

    if (!ribbon_valid_context(app)) {
        return;
    }
    ribbon = app->ribbon;
    if (ribbon->paperSizeCombo == NULL) {
        return;
    }
    for (index = 0; index < paper_size_count(); ++index) {
        const PaperSizePreset *preset = paper_size_at(index);
        if (preset != NULL && preset->id == app->paperSizeId) {
            SendMessageW(ribbon->paperSizeCombo, CB_SETCURSEL,
                         (WPARAM)index, 0);
            return;
        }
    }
    SendMessageW(ribbon->paperSizeCombo, CB_SETCURSEL, CB_ERR, 0);
}

void ribbon_sync_home_formatting(AppState *app, BOOL subscript,
                                 BOOL superscript, BOOL numbering,
                                 BOOL highlighted, int lineSpacingPercent)
{
    RibbonContext *ribbon;
    WCHAR caption[24];
    WCHAR currentCaption[24];
    WCHAR accessibleName[64];

    if (!ribbon_valid_context(app)) {
        return;
    }
    ribbon = app->ribbon;
    SendMessageW(ribbon->subscriptButton, BM_SETCHECK,
                 subscript ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(ribbon->superscriptButton, BM_SETCHECK,
                 superscript ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(ribbon->numberingButton, BM_SETCHECK,
                 numbering ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(ribbon->highlightButton, BM_SETCHECK,
                 highlighted ? BST_CHECKED : BST_UNCHECKED, 0);
    if (lineSpacingPercent > 0) {
        if (lineSpacingPercent % 100 == 0) {
            StringCchPrintfW(caption, ARRAYSIZE(caption), L"%d.0x",
                             lineSpacingPercent / 100);
        } else if (lineSpacingPercent % 10 == 0) {
            StringCchPrintfW(caption, ARRAYSIZE(caption), L"%d.%dx",
                             lineSpacingPercent / 100,
                             (lineSpacingPercent % 100) / 10);
        } else {
            StringCchPrintfW(caption, ARRAYSIZE(caption), L"%d.%02dx",
                             lineSpacingPercent / 100,
                             lineSpacingPercent % 100);
        }
        StringCchPrintfW(accessibleName, ARRAYSIZE(accessibleName),
                         L"Line spacing: %s", caption);
    } else {
        StringCchCopyW(caption, ARRAYSIZE(caption), L"Spacing");
        StringCchCopyW(accessibleName, ARRAYSIZE(accessibleName),
                       L"Line spacing");
    }
    GetWindowTextW(ribbon->lineSpacingButton, currentCaption,
                   ARRAYSIZE(currentCaption));
    if (lstrcmpW(currentCaption, caption) != 0) {
        SetWindowTextW(ribbon->lineSpacingButton, caption);
        ribbon_set_accessible_name(ribbon->lineSpacingButton,
                                   accessibleName);
    }
}

void ribbon_set_active_style(AppState *app, int style)
{
    RibbonContext *ribbon;
    size_t index;

    if (!ribbon_valid_context(app)) {
        return;
    }
    ribbon = app->ribbon;
    ribbon->activeStyle = style >= 0 && style < WORDCRAFT_STYLE_COUNT
                              ? style
                              : -1;
    for (index = 0; index < ARRAYSIZE(ribbon->styleButtons); ++index) {
        SendMessageW(ribbon->styleButtons[index], BM_SETCHECK,
                     ribbon->activeStyle == (int)index
                         ? BST_CHECKED
                         : BST_UNCHECKED,
                     0);
    }
    SendMessageW(ribbon->styleCombo, CB_SETCURSEL,
                 ribbon->activeStyle >= 0 ? ribbon->activeStyle : CB_ERR, 0);
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
            (GetWindowLongPtrW(directChild, GWL_STYLE) & WS_VISIBLE) != 0 &&
            ribbon_control_is_on_page(ribbon, ribbon->activeTab,
                                      directChild)) {
            return RIBBON_FOCUS_PANEL;
        }
        return RIBBON_FOCUS_OTHER;
    case WCQ_HOME_GROUP_COUNT:
        return HOME_GROUP_COUNT;
    case WCQ_HOME_GROUP_NAME_HASH:
        if (index < 0 || index >= HOME_GROUP_COUNT) {
            return 0;
        }
        return (LRESULT)ribbon_name_hash(homeGroupNames[index]);
    case WCQ_HOME_GROUP_FLAGS:
        if (index < 0 || index >= HOME_GROUP_COUNT) {
            return 0;
        }
        return (ribbon->activeTab == RIBBON_TAB_HOME
                    ? HOME_GROUP_FLAG_VISIBLE
                    : 0) |
               ((GetWindowLongPtrW(ribbon->homeGroupLabels[index],
                                   GWL_STYLE) & WS_VISIBLE) != 0
                    ? HOME_GROUP_FLAG_LABEL_VISIBLE
                    : 0) |
               (index == HOME_GROUP_STYLES &&
                        ribbon->styleGalleryVisible
                    ? HOME_GROUP_FLAG_STYLE_GALLERY
                    : 0) |
               (ribbon->homeLayoutMode == RIBBON_LAYOUT_COLLAPSED
                    ? HOME_GROUP_FLAG_COLLAPSED
                    : 0);
    case WCQ_HOME_GROUP_RECT_COMPONENT: {
        int group = (int)(index / 4);
        int component = (int)(index % 4);
        const RECT *rect;
        if (index < 0 || group < 0 || group >= HOME_GROUP_COUNT) {
            return 0;
        }
        rect = &ribbon->homeGroupRects[group];
        switch (component) {
        case 0:
            return rect->left;
        case 1:
            return rect->top;
        case 2:
            return rect->right;
        case 3:
            return rect->bottom;
        default:
            return 0;
        }
    }
    case WCQ_HOME_GROUP_PAINT_COUNT:
        return ribbon->homeGroupPaintCount;
    case WCQ_HOME_CONTROL_COUNT:
        return (LRESULT)ribbon->homeControlCount;
    case WCQ_HOME_CONTROL_ID:
        if (index < 0 || (size_t)index >= ribbon->homeControlCount) {
            return 0;
        }
        return ribbon->homeControls[index].id;
    case WCQ_HOME_CONTROL_GROUP:
        if (index < 0 || (size_t)index >= ribbon->homeControlCount) {
            return -1;
        }
        return ribbon->homeControls[index].group;
    case WCQ_HOME_CONTROL_FLAGS:
        if (index < 0 || (size_t)index >= ribbon->homeControlCount) {
            return 0;
        } else {
            HWND control = ribbon->homeControls[index].window;
            LONG_PTR style = GetWindowLongPtrW(control, GWL_STYLE);
            return HOME_CONTROL_FLAG_CREATED |
                   ((style & WS_VISIBLE) != 0
                        ? HOME_CONTROL_FLAG_VISIBLE
                        : 0) |
                   (IsWindowEnabled(control)
                        ? HOME_CONTROL_FLAG_ENABLED
                        : 0) |
                   ((style & WS_TABSTOP) != 0
                        ? HOME_CONTROL_FLAG_TABSTOP
                        : 0);
        }
    case WCQ_RIBBON_LAYOUT_MODE:
        return ribbon->homeLayoutMode;
    case WCQ_RIBBON_LAYOUT_GENERATION:
        return ribbon->layoutGeneration;
    case WCQ_RIBBON_FOCUSED_CONTROL_ID:
        focus = GetFocus();
        directChild = ribbon_direct_panel_child(app, focus);
        return directChild != NULL &&
                       (GetWindowLongPtrW(directChild, GWL_STYLE) &
                        WS_VISIBLE) != 0
                   ? GetDlgCtrlID(directChild)
                   : 0;
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
