#include "editor.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define PROBE_MESSAGE_TIMEOUT_MS 5000

static const WCHAR *const expectedRibbonTabNames[RIBBON_TAB_COUNT] = {
    L"File", L"Home", L"Insert", L"Draw", L"Design", L"Layout",
    L"References", L"Mailings", L"Review", L"View", L"Help"
};

static const WCHAR *const expectedHomeGroupNames[HOME_GROUP_COUNT] = {
    L"Clipboard", L"Font", L"Paragraph", L"Styles", L"Editing"
};

typedef struct ExpectedHomeControl {
    UINT id;
    int group;
} ExpectedHomeControl;

static const ExpectedHomeControl expectedHomeControls[] = {
    {IDM_EDIT_PASTE, HOME_GROUP_CLIPBOARD},
    {IDM_EDIT_CUT, HOME_GROUP_CLIPBOARD},
    {IDM_EDIT_COPY, HOME_GROUP_CLIPBOARD},
    {IDC_FONT_COMBO, HOME_GROUP_FONT},
    {IDC_SIZE_COMBO, HOME_GROUP_FONT},
    {IDM_FORMAT_GROW_FONT, HOME_GROUP_FONT},
    {IDM_FORMAT_SHRINK_FONT, HOME_GROUP_FONT},
    {IDM_FORMAT_CLEAR, HOME_GROUP_FONT},
    {IDC_FORMAT_BOLD, HOME_GROUP_FONT},
    {IDC_FORMAT_ITALIC, HOME_GROUP_FONT},
    {IDC_FORMAT_UNDERLINE, HOME_GROUP_FONT},
    {IDC_FORMAT_STRIKE, HOME_GROUP_FONT},
    {IDM_FORMAT_SUBSCRIPT, HOME_GROUP_FONT},
    {IDM_FORMAT_SUPERSCRIPT, HOME_GROUP_FONT},
    {IDM_FORMAT_HIGHLIGHT, HOME_GROUP_FONT},
    {IDC_TEXT_COLOR, HOME_GROUP_FONT},
    {IDC_BULLETS, HOME_GROUP_PARAGRAPH},
    {IDM_FORMAT_NUMBERING, HOME_GROUP_PARAGRAPH},
    {IDM_FORMAT_INDENT_DECREASE, HOME_GROUP_PARAGRAPH},
    {IDM_FORMAT_INDENT_INCREASE, HOME_GROUP_PARAGRAPH},
    {IDC_ALIGN_LEFT, HOME_GROUP_PARAGRAPH},
    {IDC_ALIGN_CENTER, HOME_GROUP_PARAGRAPH},
    {IDC_ALIGN_RIGHT, HOME_GROUP_PARAGRAPH},
    {IDC_ALIGN_JUSTIFY, HOME_GROUP_PARAGRAPH},
    {IDM_FORMAT_LINE_SPACING, HOME_GROUP_PARAGRAPH},
    {IDM_STYLE_NORMAL, HOME_GROUP_STYLES},
    {IDM_STYLE_NO_SPACING, HOME_GROUP_STYLES},
    {IDM_STYLE_HEADING_1, HOME_GROUP_STYLES},
    {IDM_STYLE_HEADING_2, HOME_GROUP_STYLES},
    {IDM_STYLE_TITLE, HOME_GROUP_STYLES},
    {IDC_HOME_STYLE_COMBO, HOME_GROUP_STYLES},
    {IDM_EDIT_FIND, HOME_GROUP_EDITING},
    {IDM_EDIT_REPLACE, HOME_GROUP_EDITING},
    {IDM_EDIT_SELECT_ALL, HOME_GROUP_EDITING},
    {IDC_HOME_GROUP_CLIPBOARD, HOME_GROUP_CLIPBOARD},
    {IDC_HOME_GROUP_FONT, HOME_GROUP_FONT},
    {IDC_HOME_GROUP_PARAGRAPH, HOME_GROUP_PARAGRAPH},
    {IDC_HOME_GROUP_STYLES, HOME_GROUP_STYLES},
    {IDC_HOME_GROUP_EDITING, HOME_GROUP_EDITING}
};

static const WCHAR *const expectedInsertGroupNames[INSERT_GROUP_COUNT] = {
    L"Pages", L"Tables", L"Illustrations", L"Media", L"Links",
    L"Comments", L"Header & Footer", L"Text", L"Symbols", L"eSignature"
};

typedef struct ExpectedInsertControl {
    UINT id;
    int group;
    RibbonInsertIcon icon;
    const WCHAR *caption;
} ExpectedInsertControl;

static const ExpectedInsertControl expectedInsertControls[] = {
    {IDM_INSERT_COVER_PAGE, INSERT_GROUP_PAGES,
     RIBBON_INSERT_ICON_COVER_PAGE, L"Cover Page"},
    {IDM_INSERT_BLANK_PAGE, INSERT_GROUP_PAGES,
     RIBBON_INSERT_ICON_BLANK_PAGE, L"Blank Page"},
    {IDM_INSERT_PAGE_BREAK, INSERT_GROUP_PAGES,
     RIBBON_INSERT_ICON_PAGE_BREAK, L"Page Break"},
    {IDM_INSERT_TABLE, INSERT_GROUP_TABLES,
     RIBBON_INSERT_ICON_TABLE, L"Table"},
    {IDM_INSERT_PICTURES, INSERT_GROUP_ILLUSTRATIONS,
     RIBBON_INSERT_ICON_PICTURES, L"Pictures"},
    {IDM_INSERT_SHAPES, INSERT_GROUP_ILLUSTRATIONS,
     RIBBON_INSERT_ICON_SHAPES, L"Shapes"},
    {IDM_INSERT_ICONS, INSERT_GROUP_ILLUSTRATIONS,
     RIBBON_INSERT_ICON_ICONS, L"Icons"},
    {IDM_INSERT_3D_MODELS, INSERT_GROUP_ILLUSTRATIONS,
     RIBBON_INSERT_ICON_3D_MODELS, L"3D Models"},
    {IDM_INSERT_SMARTART, INSERT_GROUP_ILLUSTRATIONS,
     RIBBON_INSERT_ICON_SMARTART, L"SmartArt"},
    {IDM_INSERT_CHART, INSERT_GROUP_ILLUSTRATIONS,
     RIBBON_INSERT_ICON_CHART, L"Chart"},
    {IDM_INSERT_SCREENSHOT, INSERT_GROUP_ILLUSTRATIONS,
     RIBBON_INSERT_ICON_SCREENSHOT, L"Screenshot"},
    {IDM_INSERT_ONLINE_VIDEO, INSERT_GROUP_MEDIA,
     RIBBON_INSERT_ICON_ONLINE_VIDEO, L"Online Videos"},
    {IDM_INSERT_LINK, INSERT_GROUP_LINKS,
     RIBBON_INSERT_ICON_LINK, L"Link"},
    {IDM_INSERT_BOOKMARK, INSERT_GROUP_LINKS,
     RIBBON_INSERT_ICON_BOOKMARK, L"Bookmark"},
    {IDM_INSERT_CROSS_REFERENCE, INSERT_GROUP_LINKS,
     RIBBON_INSERT_ICON_CROSS_REFERENCE, L"Cross-reference"},
    {IDM_INSERT_COMMENT, INSERT_GROUP_COMMENTS,
     RIBBON_INSERT_ICON_COMMENT, L"Comment"},
    {IDM_INSERT_HEADER, INSERT_GROUP_HEADER_FOOTER,
     RIBBON_INSERT_ICON_HEADER, L"Header"},
    {IDM_INSERT_FOOTER, INSERT_GROUP_HEADER_FOOTER,
     RIBBON_INSERT_ICON_FOOTER, L"Footer"},
    {IDM_INSERT_PAGE_NUMBER, INSERT_GROUP_HEADER_FOOTER,
     RIBBON_INSERT_ICON_PAGE_NUMBER, L"Page Number"},
    {IDM_INSERT_TEXT_BOX, INSERT_GROUP_TEXT,
     RIBBON_INSERT_ICON_TEXT_BOX, L"Text Box"},
    {IDM_INSERT_QUICK_PARTS, INSERT_GROUP_TEXT,
     RIBBON_INSERT_ICON_QUICK_PARTS, L"Quick Parts"},
    {IDM_INSERT_WORDART, INSERT_GROUP_TEXT,
     RIBBON_INSERT_ICON_WORDART, L"WordArt"},
    {IDM_INSERT_DROP_CAP, INSERT_GROUP_TEXT,
     RIBBON_INSERT_ICON_DROP_CAP, L"Drop Cap"},
    {IDM_INSERT_SIGNATURE_LINE, INSERT_GROUP_TEXT,
     RIBBON_INSERT_ICON_SIGNATURE_LINE, L"Signature Line"},
    {IDM_INSERT_DATETIME, INSERT_GROUP_TEXT,
     RIBBON_INSERT_ICON_DATETIME, L"Date & Time"},
    {IDM_INSERT_OBJECT, INSERT_GROUP_TEXT,
     RIBBON_INSERT_ICON_OBJECT, L"Object"},
    {IDM_INSERT_EQUATION, INSERT_GROUP_SYMBOLS,
     RIBBON_INSERT_ICON_EQUATION, L"Equation"},
    {IDM_INSERT_SYMBOL, INSERT_GROUP_SYMBOLS,
     RIBBON_INSERT_ICON_SYMBOL, L"Symbol"},
    {IDM_INSERT_ESIGNATURE_FIELDS, INSERT_GROUP_ESIGNATURE,
     RIBBON_INSERT_ICON_ESIGNATURE_FIELDS, L"eSignature fields"}
};

_Static_assert(ARRAYSIZE(expectedInsertControls) == 29,
               "the Insert ribbon probe must cover every command");

static const WCHAR *const expectedDrawGroupNames[DRAW_GROUP_COUNT] = {
    L"Input Mode", L"Undo", L"Drawing Tools", L"Stencils", L"Edit",
    L"Convert", L"Insert", L"Replay", L"Help"
};

typedef struct ExpectedDrawControl {
    UINT id;
    int group;
    RibbonDrawIcon icon;
    const WCHAR *caption;
} ExpectedDrawControl;

static const ExpectedDrawControl expectedDrawControls[] = {
    {IDM_DRAW_MODE, DRAW_GROUP_INPUT_MODE,
     RIBBON_DRAW_ICON_DRAW, L"Draw"},
    {IDM_EDIT_UNDO, DRAW_GROUP_UNDO,
     RIBBON_DRAW_ICON_UNDO, L"Undo"},
    {IDM_EDIT_REDO, DRAW_GROUP_UNDO,
     RIBBON_DRAW_ICON_REDO, L"Redo"},
    {IDM_DRAW_SELECT, DRAW_GROUP_DRAWING_TOOLS,
     RIBBON_DRAW_ICON_SELECT, L"Select"},
    {IDM_DRAW_LASSO_SELECT, DRAW_GROUP_DRAWING_TOOLS,
     RIBBON_DRAW_ICON_LASSO_SELECT, L"Lasso Select"},
    {IDM_DRAW_ERASER, DRAW_GROUP_DRAWING_TOOLS,
     RIBBON_DRAW_ICON_ERASER, L"Eraser"},
    {IDM_DRAW_PEN_BLACK, DRAW_GROUP_DRAWING_TOOLS,
     RIBBON_DRAW_ICON_PEN_BLACK, L"Black Pen"},
    {IDM_DRAW_PEN_RED, DRAW_GROUP_DRAWING_TOOLS,
     RIBBON_DRAW_ICON_PEN_RED, L"Red Pen"},
    {IDM_DRAW_PENCIL, DRAW_GROUP_DRAWING_TOOLS,
     RIBBON_DRAW_ICON_PENCIL, L"Pencil"},
    {IDM_DRAW_HIGHLIGHTER, DRAW_GROUP_DRAWING_TOOLS,
     RIBBON_DRAW_ICON_HIGHLIGHTER, L"Yellow Highlighter"},
    {IDM_DRAW_PEN_BLUE, DRAW_GROUP_DRAWING_TOOLS,
     RIBBON_DRAW_ICON_PEN_BLUE, L"Blue Pen"},
    {IDM_DRAW_PEN_GREEN, DRAW_GROUP_DRAWING_TOOLS,
     RIBBON_DRAW_ICON_PEN_GREEN, L"Green Pen"},
    {IDM_DRAW_ACTION_PEN, DRAW_GROUP_DRAWING_TOOLS,
     RIBBON_DRAW_ICON_ACTION_PEN, L"Action Pen"},
    {IDM_DRAW_ADD_PEN, DRAW_GROUP_DRAWING_TOOLS,
     RIBBON_DRAW_ICON_ADD_PEN, L"Add Pen"},
    {IDM_DRAW_RULER, DRAW_GROUP_STENCILS,
     RIBBON_DRAW_ICON_RULER, L"Ruler"},
    {IDM_DRAW_FORMAT_BACKGROUND, DRAW_GROUP_EDIT,
     RIBBON_DRAW_ICON_FORMAT_BACKGROUND, L"Format Background"},
    {IDM_DRAW_INK_TO_SHAPE, DRAW_GROUP_CONVERT,
     RIBBON_DRAW_ICON_INK_TO_SHAPE, L"Ink to Shape"},
    {IDM_DRAW_INK_TO_MATH, DRAW_GROUP_CONVERT,
     RIBBON_DRAW_ICON_INK_TO_MATH, L"Ink to Math"},
    {IDM_DRAW_CANVAS, DRAW_GROUP_INSERT,
     RIBBON_DRAW_ICON_CANVAS, L"Drawing Canvas"},
    {IDM_DRAW_INK_REPLAY, DRAW_GROUP_REPLAY,
     RIBBON_DRAW_ICON_REPLAY, L"Ink Replay"},
    {IDM_DRAW_INK_HELP, DRAW_GROUP_HELP,
     RIBBON_DRAW_ICON_HELP, L"Ink Help"}
};

_Static_assert(ARRAYSIZE(expectedDrawControls) == 21,
               "the Draw ribbon probe must cover every command");

static const WCHAR *const expectedDesignGroupNames[DESIGN_GROUP_COUNT] = {
    L"Document Formatting", L"Page Background"
};

typedef struct ExpectedDesignControl {
    UINT id;
    int group;
    RibbonDesignIcon icon;
    const WCHAR *caption;
    BOOL stylePreview;
} ExpectedDesignControl;

static const ExpectedDesignControl expectedDesignControls[] = {
    {IDM_DESIGN_THEMES, DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_THEMES, L"Themes", FALSE},
    {IDM_DESIGN_STYLE_OFFICE, DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_STYLE_PREVIEW, L"Office", TRUE},
    {IDM_DESIGN_STYLE_BASIC_ELEGANT, DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_STYLE_PREVIEW, L"Basic (Elegant)", TRUE},
    {IDM_DESIGN_STYLE_BASIC_SIMPLE, DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_STYLE_PREVIEW, L"Basic (Simple)", TRUE},
    {IDM_DESIGN_STYLE_BASIC_STYLISH, DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_STYLE_PREVIEW, L"Basic (Stylish)", TRUE},
    {IDM_DESIGN_STYLE_CENTERED, DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_STYLE_PREVIEW, L"Centered", TRUE},
    {IDM_DESIGN_STYLE_CASUAL, DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_STYLE_PREVIEW, L"Casual", TRUE},
    {IDM_DESIGN_STYLE_COMPACT, DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_STYLE_PREVIEW, L"Compact", TRUE},
    {IDM_DESIGN_STYLE_LINES_DISTINCT, DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_STYLE_PREVIEW, L"Lines (Distinctive)", TRUE},
    {IDM_DESIGN_STYLE_LINES_ELEGANT, DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_STYLE_PREVIEW, L"Lines (Elegant)", TRUE},
    {IDM_DESIGN_STYLE_LINES_SIMPLE, DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_STYLE_PREVIEW, L"Lines (Simple)", TRUE},
    {IDM_DESIGN_STYLE_GALLERY_MORE, DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_MORE, L"More Style Sets", FALSE},
    {IDM_DESIGN_COLORS, DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_COLORS, L"Colors", FALSE},
    {IDM_DESIGN_FONTS, DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_FONTS, L"Fonts", FALSE},
    {IDM_DESIGN_PARAGRAPH_SPACING, DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_PARAGRAPH_SPACING, L"Paragraph Spacing", FALSE},
    {IDM_DESIGN_EFFECTS, DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_EFFECTS, L"Effects", FALSE},
    {IDM_DESIGN_SET_AS_DEFAULT, DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_SET_AS_DEFAULT, L"Set as Default", FALSE},
    {IDM_DESIGN_WATERMARK, DESIGN_GROUP_PAGE_BACKGROUND,
     RIBBON_DESIGN_ICON_WATERMARK, L"Watermark", FALSE},
    {IDM_DESIGN_PAGE_COLOR, DESIGN_GROUP_PAGE_BACKGROUND,
     RIBBON_DESIGN_ICON_PAGE_COLOR, L"Page Color", FALSE},
    {IDM_DESIGN_PAGE_BORDERS, DESIGN_GROUP_PAGE_BACKGROUND,
     RIBBON_DESIGN_ICON_PAGE_BORDERS, L"Page Borders", FALSE}
};

_Static_assert(ARRAYSIZE(expectedDesignControls) == 20,
               "the Design ribbon probe must cover every main control");

static const WCHAR *const expectedViewGroupNames[VIEW_GROUP_COUNT] = {
    L"Views", L"Immersive", L"Dark Mode", L"Page Movement", L"Show",
    L"Zoom", L"Window", L"Macros", L"SharePoint"
};

typedef struct ExpectedViewControl {
    UINT id;
    int group;
    int icon;
    const WCHAR *caption;
    BOOL enabledByDefault;
} ExpectedViewControl;

static const ExpectedViewControl expectedViewControls[] = {
    {IDM_VIEW_READ_MODE, VIEW_GROUP_VIEWS, 1, L"Read Mode", TRUE},
    {IDM_VIEW_PRINT_LAYOUT, VIEW_GROUP_VIEWS, 2, L"Print Layout", TRUE},
    {IDM_VIEW_WEB_LAYOUT, VIEW_GROUP_VIEWS, 3, L"Web Layout", TRUE},
    {IDM_VIEW_OUTLINE, VIEW_GROUP_VIEWS, 4, L"Outline", TRUE},
    {IDM_VIEW_DRAFT, VIEW_GROUP_VIEWS, 5, L"Draft", TRUE},
    {IDM_VIEW_FOCUS, VIEW_GROUP_IMMERSIVE, 6, L"Focus", TRUE},
    {IDM_VIEW_IMMERSIVE_READER, VIEW_GROUP_IMMERSIVE, 7,
     L"Immersive Reader", TRUE},
    {IDM_VIEW_DARK_MODE, VIEW_GROUP_DARK_MODE, 8,
     L"Switch Modes", TRUE},
    {IDM_VIEW_VERTICAL, VIEW_GROUP_PAGE_MOVEMENT, 9,
     L"Vertical", TRUE},
    {IDM_VIEW_SIDE_TO_SIDE, VIEW_GROUP_PAGE_MOVEMENT, 10,
     L"Side to Side", TRUE},
    {IDM_VIEW_RULER, VIEW_GROUP_SHOW, 11, L"Ruler", TRUE},
    {IDM_VIEW_GRIDLINES, VIEW_GROUP_SHOW, 12, L"Gridlines", TRUE},
    {IDM_VIEW_NAVIGATION_PANE, VIEW_GROUP_SHOW, 13,
     L"Navigation Pane", TRUE},
    {IDM_VIEW_ZOOM_DIALOG, VIEW_GROUP_ZOOM, 14, L"Zoom", TRUE},
    {IDM_VIEW_ZOOM_100, VIEW_GROUP_ZOOM, 15, L"100%", TRUE},
    {IDM_VIEW_ONE_PAGE, VIEW_GROUP_ZOOM, 16, L"One Page", TRUE},
    {IDM_VIEW_MULTIPLE_PAGES, VIEW_GROUP_ZOOM, 17,
     L"Multiple Pages", TRUE},
    {IDM_VIEW_PAGE_WIDTH, VIEW_GROUP_ZOOM, 18, L"Page Width", TRUE},
    {IDM_VIEW_NEW_WINDOW, VIEW_GROUP_WINDOW, 19, L"New Window", TRUE},
    {IDM_VIEW_ARRANGE_ALL, VIEW_GROUP_WINDOW, 20, L"Arrange All", TRUE},
    {IDM_VIEW_SPLIT, VIEW_GROUP_WINDOW, 21, L"Split", TRUE},
    {IDM_VIEW_SIDE_BY_SIDE, VIEW_GROUP_WINDOW, 22,
     L"View Side by Side", TRUE},
    {IDM_VIEW_SYNCHRONOUS_SCROLLING, VIEW_GROUP_WINDOW, 23,
     L"Synchronous Scrolling", FALSE},
    {IDM_VIEW_RESET_WINDOW_POSITION, VIEW_GROUP_WINDOW, 24,
     L"Reset Window Position", FALSE},
    {IDM_VIEW_SWITCH_WINDOWS, VIEW_GROUP_WINDOW, 25,
     L"Switch Windows", TRUE},
    {IDM_VIEW_MACROS, VIEW_GROUP_MACROS, 26, L"Macros", TRUE},
    {IDM_VIEW_PROPERTIES, VIEW_GROUP_SHAREPOINT, 27,
     L"Properties", TRUE}
};

_Static_assert(ARRAYSIZE(expectedViewControls) == 27,
               "the View ribbon probe must cover every command");

static const WCHAR firstCommentText[] = L"Opening note: caf\x00E9";
static const WCHAR secondCommentText[] = L"Second-page review note";

typedef struct WindowSearch {
    DWORD processId;
    HWND window;
} WindowSearch;

typedef struct ControlSearch {
    int controlId;
    HWND window;
} ControlSearch;

typedef struct TextEngineSnapshot {
    LRESULT enabled;
    LRESULT backend;
    LRESULT typographyOptions;
    LRESULT lineSpacingRule;
    LRESULT lineSpacing;
    LRESULT paragraphSpaceAfter;
    LRESULT layoutGeneration;
} TextEngineSnapshot;

static BOOL CALLBACK find_main_window(HWND window, LPARAM data)
{
    WindowSearch *search = (WindowSearch *)data;
    DWORD processId = 0;
    WCHAR className[128];
    GetWindowThreadProcessId(window, &processId);
    if (processId != search->processId) {
        return TRUE;
    }
    if (GetClassNameW(window, className, ARRAYSIZE(className)) > 0 &&
        lstrcmpW(className, APP_CLASS_NAME) == 0) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

static BOOL CALLBACK find_dialog_window(HWND window, LPARAM data)
{
    WindowSearch *search = (WindowSearch *)data;
    DWORD processId = 0;
    WCHAR className[32];

    GetWindowThreadProcessId(window, &processId);
    if (processId != search->processId) {
        return TRUE;
    }
    if (GetClassNameW(window, className, ARRAYSIZE(className)) > 0 &&
        lstrcmpW(className, L"#32770") == 0) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

static BOOL CALLBACK find_design_gallery_window(HWND window, LPARAM data)
{
    WindowSearch *search = (WindowSearch *)data;
    DWORD processId = 0;
    WCHAR className[64];

    GetWindowThreadProcessId(window, &processId);
    if (processId != search->processId || !IsWindowVisible(window)) {
        return TRUE;
    }
    if (GetClassNameW(window, className, ARRAYSIZE(className)) > 0 &&
        lstrcmpW(className, L"WordCraftDesignGallery") == 0) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

static HWND find_open_design_gallery(HWND mainWindow)
{
    WindowSearch search;

    if (mainWindow == NULL) {
        return NULL;
    }
    GetWindowThreadProcessId(mainWindow, &search.processId);
    search.window = NULL;
    EnumWindows(find_design_gallery_window, (LPARAM)&search);
    return search.window;
}

static BOOL accept_next_message_box(DWORD processId)
{
    WindowSearch search;
    int attempt;

    search.processId = processId;
    search.window = NULL;
    for (attempt = 0; attempt < 50 && search.window == NULL; ++attempt) {
        EnumWindows(find_dialog_window, (LPARAM)&search);
        if (search.window == NULL) {
            Sleep(100);
        }
    }
    if (search.window == NULL) {
        return FALSE;
    }
    return SendMessageTimeoutW(GetDlgItem(search.window, IDOK), BM_CLICK,
                               0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK,
                               PROBE_MESSAGE_TIMEOUT_MS, NULL) != 0;
}

static BOOL CALLBACK find_descendant_control(HWND window, LPARAM data)
{
    ControlSearch *search = (ControlSearch *)data;
    if (GetDlgCtrlID(window) == search->controlId) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

static HWND find_control(HWND parent, int controlId)
{
    ControlSearch search;
    search.controlId = controlId;
    search.window = NULL;
    EnumChildWindows(parent, find_descendant_control, (LPARAM)&search);
    return search.window;
}

static BOOL send_message_bounded(HWND window, UINT message,
                                 WPARAM wParam, LPARAM lParam,
                                 LRESULT *result)
{
    DWORD_PTR rawResult = 0;
    if (SendMessageTimeoutW(window, message, wParam, lParam,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK,
                            PROBE_MESSAGE_TIMEOUT_MS, &rawResult) == 0) {
        return FALSE;
    }
    if (result != NULL) {
        *result = (LRESULT)rawResult;
    }
    return TRUE;
}

static BOOL get_window_text_bounded(HWND window, WCHAR *text, size_t capacity)
{
    LRESULT copied = 0;
    if (text == NULL || capacity == 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    text[0] = L'\0';
    return send_message_bounded(window, WM_GETTEXT, (WPARAM)capacity,
                                (LPARAM)text, &copied);
}

static BOOL write_probe_file(const WCHAR *path)
{
    static const BYTE content[] = {
        0xEF, 0xBB, 0xBF,
        'G', 'U', 'I', ' ', 'p', 'r', 'o', 'b', 'e', ' ',
        'c', 'a', 'f', 0xC3, 0xA9
    };
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    BOOL success;
    if (file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    success = WriteFile(file, content, sizeof(content), &written, NULL) &&
              written == sizeof(content);
    CloseHandle(file);
    return success;
}

static BOOL write_long_probe_file(const WCHAR *path)
{
    static const CHAR header[] =
        "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}}\\f0\\fs22\r\n";
    static const CHAR footer[] = "}\r\n";
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    int lineNumber;
    BOOL success = FALSE;

    if (file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    if (!WriteFile(file, header, sizeof(header) - 1, &written, NULL) ||
        written != sizeof(header) - 1) {
        goto cleanup;
    }
    for (lineNumber = 1; lineNumber <= 700; ++lineNumber) {
        CHAR line[160];
        size_t length = 0;
        if (FAILED(StringCchPrintfA(
                line, ARRAYSIZE(line),
                "Pagination probe line %04d: homework pages stay deterministic.\\par\r\n",
                lineNumber)) ||
            FAILED(StringCchLengthA(line, ARRAYSIZE(line), &length)) ||
            length > MAXDWORD ||
            !WriteFile(file, line, (DWORD)length, &written, NULL) ||
            written != (DWORD)length) {
            goto cleanup;
        }
    }
    if (!WriteFile(file, footer, sizeof(footer) - 1, &written, NULL) ||
        written != sizeof(footer) - 1) {
        goto cleanup;
    }
    success = TRUE;

cleanup:
    CloseHandle(file);
    return success;
}

static BOOL verify_saved_file(const WCHAR *path)
{
    static const BYTE expected[] = {
        0xEF, 0xBB, 0xBF,
        'G', 'U', 'I', ' ', 'p', 'r', 'o', 'b', 'e', ' ',
        'c', 'a', 'f', 0xC3, 0xA9, 'x'
    };
    BYTE actual[sizeof(expected)];
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD bytesRead = 0;
    DWORD attributes;
    BOOL success;
    if (file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    success = ReadFile(file, actual, sizeof(actual), &bytesRead, NULL) &&
              bytesRead == sizeof(actual) &&
              memcmp(actual, expected, sizeof(expected)) == 0 &&
              GetFileSize(file, NULL) == sizeof(expected);
    CloseHandle(file);
    attributes = GetFileAttributesW(path);
    return success && attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_TEMPORARY) == 0;
}

static BOOL launch_hidden_wordcraft(const WCHAR *executable, const WCHAR *path,
                                    PROCESS_INFORMATION *process, HWND *window)
{
    WCHAR commandLine[PATH_CAPACITY * 2];
    STARTUPINFOW startup;
    WindowSearch search;
    int attempt;

    if (FAILED(StringCchPrintfW(commandLine, ARRAYSIZE(commandLine),
                                L"\"%s\" \"%s\"", executable, path))) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    ZeroMemory(&startup, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    ZeroMemory(process, sizeof(*process));
    if (!CreateProcessW(executable, commandLine, NULL, NULL, FALSE, 0,
                        NULL, NULL, &startup, process)) {
        return FALSE;
    }
    CloseHandle(process->hThread);
    process->hThread = NULL;
    WaitForInputIdle(process->hProcess, 5000);

    search.processId = process->dwProcessId;
    search.window = NULL;
    for (attempt = 0; attempt < 50 && search.window == NULL; ++attempt) {
        EnumWindows(find_main_window, (LPARAM)&search);
        if (search.window == NULL) {
            Sleep(100);
        }
    }
    *window = search.window;
    return search.window != NULL;
}

static BOOL query_wordcraft_state(HWND window, WPARAM query, LPARAM argument,
                                  LRESULT *value)
{
    return send_message_bounded(window, WCM_QUERY_STATE, query, argument,
                                value);
}

static BOOL scalar_within_tolerance(LRESULT actual, LONG expected,
                                    LONG tolerance)
{
    LONGLONG difference = (LONGLONG)actual - expected;

    if (difference < 0) {
        difference = -difference;
    }
    return difference <= tolerance;
}

static BOOL query_text_engine_snapshot(HWND window,
                                       TextEngineSnapshot *snapshot)
{
    if (snapshot == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ZeroMemory(snapshot, sizeof(*snapshot));
    return query_wordcraft_state(window, WCQ_TEXT_ENGINE_ENABLED, 0,
                                 &snapshot->enabled) &&
           query_wordcraft_state(window, WCQ_TEXT_ENGINE_BACKEND, 0,
                                 &snapshot->backend) &&
           query_wordcraft_state(window,
                                 WCQ_TEXT_ENGINE_TYPOGRAPHY_OPTIONS, 0,
                                 &snapshot->typographyOptions) &&
           query_wordcraft_state(window,
                                 WCQ_TEXT_ENGINE_LINE_SPACING_RULE, 0,
                                 &snapshot->lineSpacingRule) &&
           query_wordcraft_state(window, WCQ_TEXT_ENGINE_LINE_SPACING, 0,
                                 &snapshot->lineSpacing) &&
           query_wordcraft_state(
               window, WCQ_TEXT_ENGINE_PARAGRAPH_SPACE_AFTER, 0,
               &snapshot->paragraphSpaceAfter) &&
           query_wordcraft_state(window,
                                 WCQ_TEXT_ENGINE_LAYOUT_GENERATION, 0,
                                 &snapshot->layoutGeneration);
}

static BOOL text_engine_snapshot_has_defaults(
    const TextEngineSnapshot *snapshot)
{
    UINT options;

    if (snapshot == NULL) {
        return FALSE;
    }
    options = (UINT)(DWORD_PTR)snapshot->typographyOptions;
    return snapshot->enabled == 1 &&
           snapshot->backend == TEXT_ENGINE_BACKEND_RICHEDIT_ADVANCED &&
           (options & TO_ADVANCEDTYPOGRAPHY) != 0 &&
           (options & TO_SIMPLELINEBREAK) == 0 &&
           snapshot->lineSpacingRule ==
               WORDCRAFT_DEFAULT_LINE_SPACING_RULE &&
           snapshot->lineSpacing == WORDCRAFT_DEFAULT_LINE_SPACING &&
           snapshot->paragraphSpaceAfter ==
               WORDCRAFT_DEFAULT_PARAGRAPH_SPACE_AFTER_TWIPS &&
           snapshot->layoutGeneration >= 1;
}

static BOOL text_engine_snapshots_equal(const TextEngineSnapshot *left,
                                        const TextEngineSnapshot *right)
{
    return left != NULL && right != NULL &&
           left->enabled == right->enabled &&
           left->backend == right->backend &&
           left->typographyOptions == right->typographyOptions &&
           left->lineSpacingRule == right->lineSpacingRule &&
           left->lineSpacing == right->lineSpacing &&
           left->paragraphSpaceAfter == right->paragraphSpaceAfter &&
           left->layoutGeneration == right->layoutGeneration;
}

static BOOL get_paragraph_format_bounded(HWND editor, HANDLE process,
                                         PARAFORMAT2 *paragraph)
{
    PARAFORMAT2 request;
    void *remoteParagraph;
    SIZE_T transferred = 0;
    BOOL success = FALSE;

    if (editor == NULL || process == NULL || paragraph == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ZeroMemory(&request, sizeof(request));
    request.cbSize = sizeof(request);
    remoteParagraph = VirtualAllocEx(process, NULL, sizeof(request),
                                     MEM_COMMIT | MEM_RESERVE,
                                     PAGE_READWRITE);
    if (remoteParagraph == NULL) {
        return FALSE;
    }
    if (WriteProcessMemory(process, remoteParagraph, &request,
                           sizeof(request), &transferred) &&
        transferred == sizeof(request) &&
        send_message_bounded(editor, EM_GETPARAFORMAT, 0,
                             (LPARAM)remoteParagraph, NULL)) {
        transferred = 0;
        if (ReadProcessMemory(process, remoteParagraph, paragraph,
                              sizeof(*paragraph), &transferred) &&
            transferred == sizeof(*paragraph) &&
            paragraph->cbSize == sizeof(*paragraph)) {
            success = TRUE;
        }
    }
    VirtualFreeEx(process, remoteParagraph, 0, MEM_RELEASE);
    return success;
}

static BOOL get_character_format_bounded(HWND editor, HANDLE process,
                                         CHARFORMAT2W *character)
{
    CHARFORMAT2W request;
    void *remoteCharacter;
    SIZE_T transferred = 0;
    BOOL success = FALSE;

    if (editor == NULL || process == NULL || character == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ZeroMemory(&request, sizeof(request));
    request.cbSize = sizeof(request);
    remoteCharacter = VirtualAllocEx(process, NULL, sizeof(request),
                                     MEM_COMMIT | MEM_RESERVE,
                                     PAGE_READWRITE);
    if (remoteCharacter == NULL) {
        return FALSE;
    }
    if (WriteProcessMemory(process, remoteCharacter, &request,
                           sizeof(request), &transferred) &&
        transferred == sizeof(request) &&
        send_message_bounded(editor, EM_GETCHARFORMAT, SCF_SELECTION,
                             (LPARAM)remoteCharacter, NULL)) {
        transferred = 0;
        if (ReadProcessMemory(process, remoteCharacter, character,
                              sizeof(*character), &transferred) &&
            transferred == sizeof(*character) &&
            character->cbSize == sizeof(*character)) {
            success = TRUE;
        }
    }
    VirtualFreeEx(process, remoteCharacter, 0, MEM_RELEASE);
    return success;
}

static BOOL find_text_bounded(HWND editor, HANDLE process,
                              const WCHAR *needle, LONG *position)
{
    FINDTEXTEXW request;
    FINDTEXTEXW result;
    void *remoteNeedle = NULL;
    void *remoteRequest = NULL;
    SIZE_T needleBytes;
    SIZE_T transferred = 0;
    LRESULT found = -1;
    BOOL success = FALSE;

    if (editor == NULL || process == NULL || needle == NULL ||
        needle[0] == L'\0' || position == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    needleBytes = (wcslen(needle) + 1) * sizeof(*needle);
    remoteNeedle = VirtualAllocEx(process, NULL, needleBytes,
                                  MEM_COMMIT | MEM_RESERVE,
                                  PAGE_READWRITE);
    remoteRequest = VirtualAllocEx(process, NULL, sizeof(request),
                                   MEM_COMMIT | MEM_RESERVE,
                                   PAGE_READWRITE);
    if (remoteNeedle == NULL || remoteRequest == NULL) {
        goto cleanup;
    }
    ZeroMemory(&request, sizeof(request));
    request.chrg.cpMin = 0;
    request.chrg.cpMax = -1;
    request.lpstrText = (LPCWSTR)remoteNeedle;
    if (!WriteProcessMemory(process, remoteNeedle, needle, needleBytes,
                            &transferred) ||
        transferred != needleBytes) {
        goto cleanup;
    }
    transferred = 0;
    if (!WriteProcessMemory(process, remoteRequest, &request,
                            sizeof(request), &transferred) ||
        transferred != sizeof(request) ||
        !send_message_bounded(editor, EM_FINDTEXTEXW, FR_DOWN,
                              (LPARAM)remoteRequest, &found) ||
        found < 0) {
        goto cleanup;
    }
    transferred = 0;
    if (!ReadProcessMemory(process, remoteRequest, &result,
                           sizeof(result), &transferred) ||
        transferred != sizeof(result) || result.chrgText.cpMin != found ||
        result.chrgText.cpMax <= result.chrgText.cpMin) {
        goto cleanup;
    }
    *position = result.chrgText.cpMin;
    success = TRUE;

cleanup:
    if (remoteRequest != NULL) {
        VirtualFreeEx(process, remoteRequest, 0, MEM_RELEASE);
    }
    if (remoteNeedle != NULL) {
        VirtualFreeEx(process, remoteNeedle, 0, MEM_RELEASE);
    }
    return success;
}

static BOOL character_formats_match(const CHARFORMAT2W *left,
                                    const CHARFORMAT2W *right)
{
    const DWORD effects =
        CFE_BOLD | CFE_ITALIC | CFE_UNDERLINE | CFE_STRIKEOUT |
        CFE_SUBSCRIPT | CFE_SUPERSCRIPT | CFE_AUTOCOLOR |
        CFE_AUTOBACKCOLOR;

    return left != NULL && right != NULL &&
           left->yHeight == right->yHeight &&
           left->yOffset == right->yOffset &&
           left->crTextColor == right->crTextColor &&
           left->crBackColor == right->crBackColor &&
           (left->dwEffects & effects) == (right->dwEffects & effects) &&
           left->bCharSet == right->bCharSet &&
           lstrcmpW(left->szFaceName, right->szFaceName) == 0;
}

static BOOL paragraph_formats_match(const PARAFORMAT2 *left,
                                    const PARAFORMAT2 *right)
{
    return left != NULL && right != NULL &&
           left->wAlignment == right->wAlignment &&
           left->wNumbering == right->wNumbering &&
           left->wNumberingStyle == right->wNumberingStyle &&
           left->wNumberingStart == right->wNumberingStart &&
           left->wNumberingTab == right->wNumberingTab &&
           left->dxStartIndent == right->dxStartIndent &&
           left->dxRightIndent == right->dxRightIndent &&
           left->dxOffset == right->dxOffset &&
           left->dySpaceBefore == right->dySpaceBefore &&
           left->dySpaceAfter == right->dySpaceAfter &&
           left->bLineSpacingRule == right->bLineSpacingRule &&
           left->dyLineSpacing == right->dyLineSpacing;
}

static BOOL paragraph_has_text_engine_defaults(const PARAFORMAT2 *paragraph)
{
    DWORD requiredMask = PFM_LINESPACING | PFM_SPACEAFTER;

    return paragraph != NULL &&
           (paragraph->dwMask & requiredMask) == requiredMask &&
           paragraph->bLineSpacingRule ==
               WORDCRAFT_DEFAULT_LINE_SPACING_RULE &&
           paragraph->dyLineSpacing == WORDCRAFT_DEFAULT_LINE_SPACING &&
           paragraph->dySpaceAfter ==
               WORDCRAFT_DEFAULT_PARAGRAPH_SPACE_AFTER_TWIPS;
}

static UINT probe_text_hash(const WCHAR *text)
{
    UINT hash = 2166136261u;

    while (text != NULL && *text != L'\0') {
        hash ^= (UINT)*text++;
        hash *= 16777619u;
    }
    return hash;
}

static BOOL query_home_group_rect(HWND window, int group, RECT *rect)
{
    LRESULT values[4];
    int component;

    if (rect == NULL || group < 0 || group >= HOME_GROUP_COUNT) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (component = 0; component < 4; ++component) {
        if (!query_wordcraft_state(
                window, WCQ_HOME_GROUP_RECT_COMPONENT,
                group * 4 + component, &values[component]) ||
            values[component] < LONG_MIN ||
            values[component] > LONG_MAX) {
            return FALSE;
        }
    }
    rect->left = (LONG)values[0];
    rect->top = (LONG)values[1];
    rect->right = (LONG)values[2];
    rect->bottom = (LONG)values[3];
    return TRUE;
}

static BOOL query_insert_group_rect(HWND window, int group, RECT *rect)
{
    LRESULT values[4];
    int component;

    if (rect == NULL || group < 0 || group >= INSERT_GROUP_COUNT) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (component = 0; component < 4; ++component) {
        if (!query_wordcraft_state(
                window, WCQ_INSERT_GROUP_RECT_COMPONENT,
                group * 4 + component, &values[component]) ||
            values[component] < LONG_MIN ||
            values[component] > LONG_MAX) {
            return FALSE;
        }
    }
    rect->left = (LONG)values[0];
    rect->top = (LONG)values[1];
    rect->right = (LONG)values[2];
    rect->bottom = (LONG)values[3];
    return TRUE;
}

static BOOL query_draw_group_rect(HWND window, int group, RECT *rect)
{
    LRESULT values[4];
    int component;

    if (rect == NULL || group < 0 || group >= DRAW_GROUP_COUNT) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (component = 0; component < 4; ++component) {
        if (!query_wordcraft_state(
                window, WCQ_DRAW_GROUP_RECT_COMPONENT,
                group * 4 + component, &values[component]) ||
            values[component] < LONG_MIN ||
            values[component] > LONG_MAX) {
            return FALSE;
        }
    }
    rect->left = (LONG)values[0];
    rect->top = (LONG)values[1];
    rect->right = (LONG)values[2];
    rect->bottom = (LONG)values[3];
    return TRUE;
}

static BOOL query_design_group_rect(HWND window, int group, RECT *rect)
{
    LRESULT values[4];
    int component;

    if (rect == NULL || group < 0 || group >= DESIGN_GROUP_COUNT) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (component = 0; component < 4; ++component) {
        if (!query_wordcraft_state(
                window, WCQ_DESIGN_GROUP_RECT_COMPONENT,
                group * 4 + component, &values[component]) ||
            values[component] < LONG_MIN ||
            values[component] > LONG_MAX) {
            return FALSE;
        }
    }
    rect->left = (LONG)values[0];
    rect->top = (LONG)values[1];
    rect->right = (LONG)values[2];
    rect->bottom = (LONG)values[3];
    return TRUE;
}

static BOOL query_design_gallery_item_rect(HWND window, int item, RECT *rect)
{
    LRESULT values[4];
    int component;

    if (rect == NULL || item < 0 ||
        item >= DESIGN_STYLE_SET_COUNT + 2) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (component = 0; component < 4; ++component) {
        if (!query_wordcraft_state(
                window, WCQ_DESIGN_GALLERY_ITEM_RECT_COMPONENT,
                item * 4 + component, &values[component]) ||
            values[component] < LONG_MIN ||
            values[component] > LONG_MAX) {
            return FALSE;
        }
    }
    rect->left = (LONG)values[0];
    rect->top = (LONG)values[1];
    rect->right = (LONG)values[2];
    rect->bottom = (LONG)values[3];
    return TRUE;
}

static BOOL query_view_group_rect(HWND window, int group, RECT *rect)
{
    LRESULT values[4];
    int component;

    if (rect == NULL || group < 0 || group >= VIEW_GROUP_COUNT) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (component = 0; component < 4; ++component) {
        if (!query_wordcraft_state(
                window, WCQ_VIEW_GROUP_RECT_COMPONENT,
                group * 4 + component, &values[component]) ||
            values[component] < LONG_MIN ||
            values[component] > LONG_MAX) {
            return FALSE;
        }
    }
    rect->left = (LONG)values[0];
    rect->top = (LONG)values[1];
    rect->right = (LONG)values[2];
    rect->bottom = (LONG)values[3];
    return TRUE;
}

static BOOL home_control_is_style_button(UINT id)
{
    return id >= IDM_STYLE_NORMAL && id <= IDM_STYLE_TITLE;
}

static BOOL home_control_is_group_label(UINT id)
{
    return id >= IDC_HOME_GROUP_CLIPBOARD &&
           id <= IDC_HOME_GROUP_EDITING;
}

static BOOL home_control_hidden_in_collapsed(UINT id)
{
    switch (id) {
    case IDM_FORMAT_GROW_FONT:
    case IDM_FORMAT_SHRINK_FONT:
    case IDM_FORMAT_CLEAR:
    case IDC_FORMAT_STRIKE:
    case IDM_FORMAT_SUBSCRIPT:
    case IDM_FORMAT_SUPERSCRIPT:
    case IDM_FORMAT_HIGHLIGHT:
    case IDM_FORMAT_INDENT_DECREASE:
    case IDM_FORMAT_INDENT_INCREASE:
    case IDM_FORMAT_LINE_SPACING:
        return TRUE;
    default:
        return FALSE;
    }
}

static BOOL validate_home_layout_contract(HWND window, HWND formatBar,
                                          int expectedMode)
{
    RECT client;
    RECT previous = {0};
    LRESULT groupCount = 0;
    LRESULT controlCount = 0;
    LRESULT paintCount = 0;
    LRESULT mode = 0;
    size_t index;

    if (window == NULL || formatBar == NULL ||
        (expectedMode != RIBBON_LAYOUT_FULL &&
         expectedMode != RIBBON_LAYOUT_COMPACT &&
         expectedMode != RIBBON_LAYOUT_COLLAPSED) ||
        !GetClientRect(formatBar, &client) ||
        !query_wordcraft_state(window, WCQ_HOME_GROUP_COUNT, 0,
                               &groupCount) ||
        !query_wordcraft_state(window, WCQ_HOME_CONTROL_COUNT, 0,
                               &controlCount) ||
        !query_wordcraft_state(window, WCQ_HOME_GROUP_PAINT_COUNT, 0,
                               &paintCount) ||
        !query_wordcraft_state(window, WCQ_RIBBON_LAYOUT_MODE, 0, &mode) ||
        groupCount != HOME_GROUP_COUNT ||
        controlCount != (LRESULT)ARRAYSIZE(expectedHomeControls) ||
        mode != expectedMode || client.right <= client.left ||
        client.bottom <= client.top || paintCount < 0) {
        return FALSE;
    }

    for (index = 0; index < HOME_GROUP_COUNT; ++index) {
        RECT rect;
        LRESULT hash = 0;
        LRESULT flags = 0;
        UINT required = HOME_GROUP_FLAG_VISIBLE |
                        HOME_GROUP_FLAG_LABEL_VISIBLE;
        BOOL gallery = index == HOME_GROUP_STYLES &&
                       expectedMode == RIBBON_LAYOUT_FULL;
        BOOL collapsed = expectedMode == RIBBON_LAYOUT_COLLAPSED;

        if (!query_wordcraft_state(window, WCQ_HOME_GROUP_NAME_HASH,
                                   (LPARAM)index, &hash) ||
            !query_wordcraft_state(window, WCQ_HOME_GROUP_FLAGS,
                                   (LPARAM)index, &flags) ||
            !query_home_group_rect(window, (int)index, &rect) ||
            (UINT)(DWORD_PTR)hash !=
                probe_text_hash(expectedHomeGroupNames[index]) ||
            ((UINT)flags & required) != required ||
            (((UINT)flags & HOME_GROUP_FLAG_STYLE_GALLERY) != 0) !=
                gallery ||
            ((((UINT)flags & HOME_GROUP_FLAG_COLLAPSED) != 0) !=
             collapsed) ||
            rect.left < client.left || rect.top < client.top ||
            rect.right > client.right || rect.bottom > client.bottom ||
            rect.right <= rect.left || rect.bottom <= rect.top ||
            (index > 0 && rect.left < previous.right)) {
            return FALSE;
        }
        previous = rect;
    }

    for (index = 0; index < ARRAYSIZE(expectedHomeControls); ++index) {
        LRESULT id = 0;
        LRESULT group = -1;
        LRESULT flags = 0;
        UINT expectedId = expectedHomeControls[index].id;
        BOOL expectedVisible;
        BOOL expectedTabstop = !home_control_is_group_label(expectedId);

        if (expectedMode == RIBBON_LAYOUT_FULL) {
            expectedVisible = expectedId != IDC_HOME_STYLE_COMBO;
        } else {
            expectedVisible = !home_control_is_style_button(expectedId);
            if (expectedMode == RIBBON_LAYOUT_COLLAPSED &&
                home_control_hidden_in_collapsed(expectedId)) {
                expectedVisible = FALSE;
            }
        }

        if (!query_wordcraft_state(window, WCQ_HOME_CONTROL_ID,
                                   (LPARAM)index, &id) ||
            !query_wordcraft_state(window, WCQ_HOME_CONTROL_GROUP,
                                   (LPARAM)index, &group) ||
            !query_wordcraft_state(window, WCQ_HOME_CONTROL_FLAGS,
                                   (LPARAM)index, &flags) ||
            id != expectedId || group != expectedHomeControls[index].group ||
            ((UINT)flags & HOME_CONTROL_FLAG_CREATED) == 0 ||
            ((((UINT)flags & HOME_CONTROL_FLAG_VISIBLE) != 0) !=
             expectedVisible) ||
            ((((UINT)flags & HOME_CONTROL_FLAG_TABSTOP) != 0) !=
             expectedTabstop)) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL validate_insert_layout_contract(HWND window, HWND formatBar,
                                            int expectedMode)
{
    RECT client;
    RECT previous = {0};
    LRESULT groupCount = 0;
    LRESULT controlCount = 0;
    LRESULT groupPaintCount = 0;
    LRESULT mode = 0;
    size_t index;

    if (window == NULL || formatBar == NULL ||
        expectedMode < RIBBON_LAYOUT_FULL ||
        expectedMode > RIBBON_LAYOUT_COLLAPSED ||
        !GetClientRect(formatBar, &client) ||
        !query_wordcraft_state(window, WCQ_INSERT_GROUP_COUNT, 0,
                               &groupCount) ||
        !query_wordcraft_state(window, WCQ_INSERT_CONTROL_COUNT, 0,
                               &controlCount) ||
        !query_wordcraft_state(window, WCQ_INSERT_GROUP_PAINT_COUNT, 0,
                               &groupPaintCount) ||
        !query_wordcraft_state(window, WCQ_INSERT_LAYOUT_MODE, 0, &mode) ||
        groupCount != INSERT_GROUP_COUNT ||
        controlCount != (LRESULT)ARRAYSIZE(expectedInsertControls) ||
        mode != expectedMode || client.right <= client.left ||
        client.bottom <= client.top || groupPaintCount < 0) {
        return FALSE;
    }

    for (index = 0; index < INSERT_GROUP_COUNT; ++index) {
        RECT rect;
        LRESULT hash = 0;
        LRESULT flags = 0;
        UINT required = INSERT_GROUP_FLAG_VISIBLE;

        if (expectedMode == RIBBON_LAYOUT_COLLAPSED) {
            required |= INSERT_GROUP_FLAG_COLLAPSED;
        } else {
            required |= INSERT_GROUP_FLAG_LABEL_VISIBLE;
        }

        if (!query_wordcraft_state(window, WCQ_INSERT_GROUP_NAME_HASH,
                                   (LPARAM)index, &hash) ||
            !query_wordcraft_state(window, WCQ_INSERT_GROUP_FLAGS,
                                   (LPARAM)index, &flags) ||
            !query_insert_group_rect(window, (int)index, &rect) ||
            (UINT)(DWORD_PTR)hash !=
                probe_text_hash(expectedInsertGroupNames[index]) ||
            ((UINT)flags & required) != required ||
            (expectedMode == RIBBON_LAYOUT_COLLAPSED
                 ? ((UINT)flags & INSERT_GROUP_FLAG_LABEL_VISIBLE) != 0
                 : ((UINT)flags & INSERT_GROUP_FLAG_COLLAPSED) != 0) ||
            rect.left < client.left || rect.top < client.top ||
            rect.right > client.right || rect.bottom > client.bottom ||
            rect.right <= rect.left || rect.bottom <= rect.top ||
            (index > 0 && rect.left < previous.right)) {
            return FALSE;
        }
        previous = rect;
    }

    for (index = 0; index < ARRAYSIZE(expectedInsertControls); ++index) {
        const ExpectedInsertControl *expected =
            &expectedInsertControls[index];
        RECT controlRect;
        RECT groupRect;
        HWND control;
        WCHAR caption[64] = L"";
        LRESULT id = 0;
        LRESULT group = -1;
        LRESULT flags = 0;
        LRESULT icon = RIBBON_INSERT_ICON_NONE;
        UINT required = INSERT_CONTROL_FLAG_CREATED |
                        INSERT_CONTROL_FLAG_VISIBLE |
                        INSERT_CONTROL_FLAG_ENABLED |
                        INSERT_CONTROL_FLAG_TABSTOP |
                        INSERT_CONTROL_FLAG_HAS_ICON;

        control = find_control(formatBar, (int)expected->id);
        if (control == NULL ||
            !query_wordcraft_state(window, WCQ_INSERT_CONTROL_ID,
                                   (LPARAM)index, &id) ||
            !query_wordcraft_state(window, WCQ_INSERT_CONTROL_GROUP,
                                   (LPARAM)index, &group) ||
            !query_wordcraft_state(window, WCQ_INSERT_CONTROL_FLAGS,
                                   (LPARAM)index, &flags) ||
            !query_wordcraft_state(window, WCQ_INSERT_CONTROL_ICON,
                                   (LPARAM)index, &icon) ||
            id != expected->id || group != expected->group ||
            icon != expected->icon ||
            ((UINT)flags & required) != required ||
            !get_window_text_bounded(control, caption,
                                     ARRAYSIZE(caption)) ||
            wcscmp(caption, expected->caption) != 0 ||
            !GetWindowRect(control, &controlRect) ||
            !query_insert_group_rect(window, expected->group,
                                     &groupRect)) {
            return FALSE;
        }
        MapWindowPoints(HWND_DESKTOP, formatBar,
                        (POINT *)&controlRect, 2);
        if (controlRect.right <= controlRect.left ||
            controlRect.bottom <= controlRect.top ||
            controlRect.left < groupRect.left ||
            controlRect.top < groupRect.top ||
            controlRect.right > groupRect.right ||
            controlRect.bottom > groupRect.bottom) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL force_insert_ribbon_paint(HWND window, HWND formatBar)
{
    LRESULT groupBefore = 0;
    LRESULT groupAfter = 0;
    LRESULT iconBefore[ARRAYSIZE(expectedInsertControls)];
    HDC referenceDc = NULL;
    HDC memoryDc = NULL;
    HBITMAP bitmap = NULL;
    HGDIOBJ previousBitmap = NULL;
    int attempt;
    size_t index;

    if (window == NULL || formatBar == NULL ||
        !query_wordcraft_state(window, WCQ_INSERT_GROUP_PAINT_COUNT, 0,
                               &groupBefore)) {
        return FALSE;
    }
    for (index = 0; index < ARRAYSIZE(expectedInsertControls); ++index) {
        if (!query_wordcraft_state(window, WCQ_INSERT_ICON_PAINT_COUNT,
                                   (LPARAM)index, &iconBefore[index])) {
            return FALSE;
        }
    }

    if (!RedrawWindow(formatBar, NULL, NULL,
                      RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN |
                          RDW_UPDATENOW) ||
        !send_message_bounded(formatBar, WM_PAINT, 0, 0, NULL)) {
        return FALSE;
    }
    for (index = 0; index < ARRAYSIZE(expectedInsertControls); ++index) {
        HWND control =
            find_control(formatBar, (int)expectedInsertControls[index].id);
        if (control == NULL ||
            !RedrawWindow(control, NULL, NULL,
                          RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW) ||
            !send_message_bounded(control, WM_PAINT, 0, 0, NULL)) {
            return FALSE;
        }
    }
    /*
     * The probe launches the top-level window hidden. WM_PRINTCLIENT against
     * a real compatible bitmap still exercises each native button's
     * NM_CUSTOMDRAW path without flashing a test window on the desktop.
     */
    referenceDc = GetDC(formatBar);
    if (referenceDc != NULL) {
        memoryDc = CreateCompatibleDC(referenceDc);
        bitmap = CreateCompatibleBitmap(referenceDc, 256, 128);
        if (memoryDc != NULL && bitmap != NULL) {
            previousBitmap = SelectObject(memoryDc, bitmap);
            for (index = 0; index < ARRAYSIZE(expectedInsertControls);
                 ++index) {
                HWND control = find_control(
                    formatBar, (int)expectedInsertControls[index].id);
                if (control != NULL) {
                    send_message_bounded(
                        control, WM_PRINTCLIENT, (WPARAM)memoryDc,
                        PRF_CLIENT | PRF_ERASEBKGND, NULL);
                }
            }
            SelectObject(memoryDc, previousBitmap);
        }
        if (bitmap != NULL) {
            DeleteObject(bitmap);
        }
        if (memoryDc != NULL) {
            DeleteDC(memoryDc);
        }
        ReleaseDC(formatBar, referenceDc);
    }

    for (attempt = 0; attempt < 100; ++attempt) {
        BOOL allIconsPainted = TRUE;

        if (!query_wordcraft_state(window, WCQ_INSERT_GROUP_PAINT_COUNT, 0,
                                   &groupAfter)) {
            return FALSE;
        }
        for (index = 0; index < ARRAYSIZE(expectedInsertControls); ++index) {
            LRESULT current = 0;
            if (!query_wordcraft_state(window,
                                       WCQ_INSERT_ICON_PAINT_COUNT,
                                       (LPARAM)index, &current)) {
                return FALSE;
            }
            if (current <= iconBefore[index]) {
                allIconsPainted = FALSE;
            }
        }
        if (groupAfter > groupBefore && allIconsPainted) {
            return TRUE;
        }
        Sleep(20);
    }
    fwprintf(stderr,
             L"Insert paint telemetry stalled (groups %ld -> %ld)",
             (long)groupBefore, (long)groupAfter);
    for (index = 0; index < ARRAYSIZE(expectedInsertControls); ++index) {
        LRESULT current = 0;
        if (query_wordcraft_state(window, WCQ_INSERT_ICON_PAINT_COUNT,
                                  (LPARAM)index, &current) &&
            current <= iconBefore[index]) {
            fwprintf(stderr, L" %s:%ld->%ld",
                     expectedInsertControls[index].caption,
                     (long)iconBefore[index], (long)current);
        }
    }
    fwprintf(stderr, L"\n");
    return FALSE;
}

static BOOL insert_controls_are_hidden(HWND window, HWND formatBar)
{
    size_t index;

    if (window == NULL || formatBar == NULL) {
        return FALSE;
    }
    for (index = 0; index < INSERT_GROUP_COUNT; ++index) {
        LRESULT flags = 0;
        if (!query_wordcraft_state(window, WCQ_INSERT_GROUP_FLAGS,
                                   (LPARAM)index, &flags) ||
            ((UINT)flags & (INSERT_GROUP_FLAG_VISIBLE |
                            INSERT_GROUP_FLAG_LABEL_VISIBLE)) != 0) {
            fwprintf(stderr,
                     L"Insert group %lu remained visible (flags=0x%lx)\n",
                     (unsigned long)index, (unsigned long)flags);
            return FALSE;
        }
    }
    for (index = 0; index < ARRAYSIZE(expectedInsertControls); ++index) {
        HWND control =
            find_control(formatBar, (int)expectedInsertControls[index].id);
        LRESULT flags = 0;
        if (control == NULL ||
            !query_wordcraft_state(window, WCQ_INSERT_CONTROL_FLAGS,
                                   (LPARAM)index, &flags) ||
            ((UINT)flags & INSERT_CONTROL_FLAG_VISIBLE) != 0 ||
            (GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE) != 0) {
            fwprintf(stderr,
                     L"Insert control %s remained visible (flags=0x%lx style=0x%lx)\n",
                     expectedInsertControls[index].caption,
                     (unsigned long)flags,
                     (unsigned long)GetWindowLongPtrW(control, GWL_STYLE));
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL draw_control_is_tool(UINT id)
{
    return id >= IDM_DRAW_SELECT && id <= IDM_DRAW_ACTION_PEN;
}

static BOOL validate_draw_layout_contract(HWND window, HWND editor,
                                          HWND formatBar,
                                          int expectedMode)
{
    RECT client;
    RECT previous = {0};
    LRESULT groupCount = 0;
    LRESULT controlCount = 0;
    LRESULT groupPaintCount = 0;
    LRESULT mode = 0;
    LRESULT activeTool = 0;
    LRESULT rulerVisible = 0;
    LRESULT backgroundRuled = 0;
    LRESULT canUndo = 0;
    LRESULT canRedo = 0;
    size_t index;

    if (window == NULL || editor == NULL || formatBar == NULL ||
        expectedMode < RIBBON_LAYOUT_FULL ||
        expectedMode > RIBBON_LAYOUT_COLLAPSED ||
        !GetClientRect(formatBar, &client) ||
        !query_wordcraft_state(window, WCQ_DRAW_GROUP_COUNT, 0,
                               &groupCount) ||
        !query_wordcraft_state(window, WCQ_DRAW_CONTROL_COUNT, 0,
                               &controlCount) ||
        !query_wordcraft_state(window, WCQ_DRAW_GROUP_PAINT_COUNT, 0,
                               &groupPaintCount) ||
        !query_wordcraft_state(window, WCQ_DRAW_LAYOUT_MODE, 0, &mode) ||
        !query_wordcraft_state(window, WCQ_DRAW_ACTIVE_TOOL, 0,
                               &activeTool) ||
        !query_wordcraft_state(window, WCQ_DRAW_RULER_VISIBLE, 0,
                               &rulerVisible) ||
        !query_wordcraft_state(window, WCQ_DRAW_BACKGROUND_RULED, 0,
                               &backgroundRuled) ||
        !send_message_bounded(editor, EM_CANUNDO, 0, 0, &canUndo) ||
        !send_message_bounded(editor, EM_CANREDO, 0, 0, &canRedo) ||
        groupCount != DRAW_GROUP_COUNT ||
        controlCount != (LRESULT)ARRAYSIZE(expectedDrawControls) ||
        mode != expectedMode || client.right <= client.left ||
        client.bottom <= client.top || groupPaintCount < 0 ||
        !draw_control_is_tool((UINT)activeTool) ||
        (rulerVisible != 0 && rulerVisible != 1) ||
        (backgroundRuled != 0 && backgroundRuled != 1)) {
        fwprintf(
            stderr,
            L"Draw layout precondition mismatch mode=%ld/%d groups=%ld/%d controls=%ld/%lu active=%ld ruler=%ld background=%ld client=%ldx%ld paint=%ld undo=%ld redo=%ld\n",
            (long)mode, expectedMode, (long)groupCount,
            DRAW_GROUP_COUNT, (long)controlCount,
            (unsigned long)ARRAYSIZE(expectedDrawControls),
            (long)activeTool, (long)rulerVisible,
            (long)backgroundRuled,
            client.right - client.left, client.bottom - client.top,
            (long)groupPaintCount, (long)canUndo, (long)canRedo);
        return FALSE;
    }

    for (index = 0; index < DRAW_GROUP_COUNT; ++index) {
        RECT rect;
        LRESULT hash = 0;
        LRESULT flags = 0;
        UINT expectedFlags = DRAW_GROUP_FLAG_VISIBLE;
        const UINT knownFlags = DRAW_GROUP_FLAG_VISIBLE |
                                DRAW_GROUP_FLAG_LABEL_VISIBLE |
                                DRAW_GROUP_FLAG_COLLAPSED;

        if (expectedMode == RIBBON_LAYOUT_COLLAPSED) {
            expectedFlags |= DRAW_GROUP_FLAG_COLLAPSED;
        } else {
            expectedFlags |= DRAW_GROUP_FLAG_LABEL_VISIBLE;
        }
        if (!query_wordcraft_state(window, WCQ_DRAW_GROUP_NAME_HASH,
                                   (LPARAM)index, &hash) ||
            !query_wordcraft_state(window, WCQ_DRAW_GROUP_FLAGS,
                                   (LPARAM)index, &flags) ||
            !query_draw_group_rect(window, (int)index, &rect) ||
            (UINT)(DWORD_PTR)hash !=
                probe_text_hash(expectedDrawGroupNames[index]) ||
            ((UINT)flags & knownFlags) != expectedFlags ||
            rect.left < client.left || rect.top < client.top ||
            rect.right > client.right || rect.bottom > client.bottom ||
            rect.right <= rect.left || rect.bottom <= rect.top ||
            (index > 0 && rect.left < previous.right)) {
            fwprintf(
                stderr,
                L"Draw group %lu contract mismatch hash=0x%lx expected=0x%lx flags=0x%lx expected_flags=0x%x rect=%ld,%ld,%ld,%ld client=%ld,%ld,%ld,%ld previous_right=%ld\n",
                (unsigned long)index, (unsigned long)hash,
                (unsigned long)probe_text_hash(
                    expectedDrawGroupNames[index]),
                (unsigned long)flags, expectedFlags,
                rect.left, rect.top, rect.right, rect.bottom,
                client.left, client.top, client.right, client.bottom,
                previous.right);
            return FALSE;
        }
        previous = rect;
    }

    for (index = 0; index < ARRAYSIZE(expectedDrawControls); ++index) {
        const ExpectedDrawControl *expected =
            &expectedDrawControls[index];
        RECT controlRect;
        RECT groupRect;
        HWND control;
        WCHAR caption[64] = L"";
        LRESULT id = 0;
        LRESULT group = -1;
        LRESULT flags = 0;
        LRESULT icon = RIBBON_DRAW_ICON_NONE;
        BOOL enabled = expected->id == IDM_EDIT_UNDO
                           ? canUndo != 0
                           : expected->id == IDM_EDIT_REDO
                                 ? canRedo != 0
                                 : TRUE;
        BOOL checked =
            expected->id == (UINT)activeTool ||
            (expected->id == IDM_DRAW_RULER && rulerVisible != 0) ||
            (expected->id == IDM_DRAW_FORMAT_BACKGROUND &&
             backgroundRuled != 0);
        UINT expectedFlags = DRAW_CONTROL_FLAG_CREATED |
                             DRAW_CONTROL_FLAG_VISIBLE |
                             DRAW_CONTROL_FLAG_TABSTOP |
                             DRAW_CONTROL_FLAG_HAS_ICON |
                             (enabled ? DRAW_CONTROL_FLAG_ENABLED : 0) |
                             (checked ? DRAW_CONTROL_FLAG_CHECKED : 0);
        const UINT knownFlags = DRAW_CONTROL_FLAG_CREATED |
                                DRAW_CONTROL_FLAG_VISIBLE |
                                DRAW_CONTROL_FLAG_ENABLED |
                                DRAW_CONTROL_FLAG_TABSTOP |
                                DRAW_CONTROL_FLAG_HAS_ICON |
                                DRAW_CONTROL_FLAG_CHECKED;

        control = find_control(formatBar, (int)expected->id);
        if (control == NULL ||
            !query_wordcraft_state(window, WCQ_DRAW_CONTROL_ID,
                                   (LPARAM)index, &id) ||
            !query_wordcraft_state(window, WCQ_DRAW_CONTROL_GROUP,
                                   (LPARAM)index, &group) ||
            !query_wordcraft_state(window, WCQ_DRAW_CONTROL_FLAGS,
                                   (LPARAM)index, &flags) ||
            !query_wordcraft_state(window, WCQ_DRAW_CONTROL_ICON,
                                   (LPARAM)index, &icon) ||
            id != expected->id || group != expected->group ||
            icon != expected->icon ||
            ((UINT)flags & knownFlags) != expectedFlags ||
            !get_window_text_bounded(control, caption,
                                     ARRAYSIZE(caption)) ||
            wcscmp(caption, expected->caption) != 0 ||
            !GetWindowRect(control, &controlRect) ||
            !query_draw_group_rect(window, expected->group,
                                   &groupRect)) {
            fwprintf(
                stderr,
                L"Draw control %lu (%s) contract mismatch hwnd=%p id=%ld/%u group=%ld/%d flags=0x%lx/0x%x icon=%ld/%d caption='%s'\n",
                (unsigned long)index, expected->caption, (void *)control,
                (long)id, expected->id, (long)group, expected->group,
                (unsigned long)flags, expectedFlags, (long)icon,
                expected->icon, caption);
            return FALSE;
        }
        MapWindowPoints(HWND_DESKTOP, formatBar,
                        (POINT *)&controlRect, 2);
        if (controlRect.right <= controlRect.left ||
            controlRect.bottom <= controlRect.top ||
            controlRect.left < groupRect.left ||
            controlRect.top < groupRect.top ||
            controlRect.right > groupRect.right ||
            controlRect.bottom > groupRect.bottom) {
            fwprintf(
                stderr,
                L"Draw control %lu (%s) geometry mismatch control=%ld,%ld,%ld,%ld group=%ld,%ld,%ld,%ld\n",
                (unsigned long)index, expected->caption,
                controlRect.left, controlRect.top,
                controlRect.right, controlRect.bottom,
                groupRect.left, groupRect.top,
                groupRect.right, groupRect.bottom);
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL force_draw_ribbon_offscreen_paint(HWND window,
                                               HWND formatBar)
{
    RECT client;
    LRESULT groupBefore = 0;
    LRESULT groupAfter = 0;
    LRESULT iconBefore[ARRAYSIZE(expectedDrawControls)];
    HDC referenceDc = NULL;
    HDC memoryDc = NULL;
    HBITMAP bitmap = NULL;
    HGDIOBJ previousBitmap = NULL;
    BOOL success = FALSE;
    size_t index;

    if (window == NULL || formatBar == NULL ||
        !GetClientRect(formatBar, &client) ||
        client.right <= client.left || client.bottom <= client.top ||
        !query_wordcraft_state(window, WCQ_DRAW_GROUP_PAINT_COUNT, 0,
                               &groupBefore)) {
        return FALSE;
    }
    for (index = 0; index < ARRAYSIZE(expectedDrawControls); ++index) {
        if (!query_wordcraft_state(window, WCQ_DRAW_ICON_PAINT_COUNT,
                                   (LPARAM)index, &iconBefore[index])) {
            return FALSE;
        }
    }

    referenceDc = GetDC(formatBar);
    if (referenceDc == NULL) {
        goto cleanup;
    }
    memoryDc = CreateCompatibleDC(referenceDc);
    bitmap = CreateCompatibleBitmap(
        referenceDc, client.right - client.left,
        client.bottom - client.top);
    if (memoryDc == NULL || bitmap == NULL) {
        goto cleanup;
    }
    previousBitmap = SelectObject(memoryDc, bitmap);
    if (previousBitmap == NULL ||
        previousBitmap == (HGDIOBJ)HGDI_ERROR ||
        !send_message_bounded(formatBar, WM_PRINTCLIENT,
                              (WPARAM)memoryDc,
                              PRF_CLIENT | PRF_ERASEBKGND, NULL)) {
        goto cleanup;
    }
    for (index = 0; index < ARRAYSIZE(expectedDrawControls); ++index) {
        HWND control =
            find_control(formatBar, (int)expectedDrawControls[index].id);
        if (control == NULL ||
            !send_message_bounded(control, WM_PRINTCLIENT,
                                  (WPARAM)memoryDc,
                                  PRF_CLIENT | PRF_ERASEBKGND, NULL)) {
            goto cleanup;
        }
    }
    GdiFlush();
    if (!query_wordcraft_state(window, WCQ_DRAW_GROUP_PAINT_COUNT, 0,
                               &groupAfter) ||
        groupAfter <= groupBefore) {
        goto cleanup;
    }
    for (index = 0; index < ARRAYSIZE(expectedDrawControls); ++index) {
        LRESULT iconAfter = 0;
        if (!query_wordcraft_state(window, WCQ_DRAW_ICON_PAINT_COUNT,
                                   (LPARAM)index, &iconAfter) ||
            iconAfter <= iconBefore[index]) {
            fwprintf(stderr,
                     L"Draw offscreen icon paint stalled for %s (%ld -> %ld)\n",
                     expectedDrawControls[index].caption,
                     (long)iconBefore[index], (long)iconAfter);
            goto cleanup;
        }
    }
    success = TRUE;

cleanup:
    if (memoryDc != NULL && previousBitmap != NULL &&
        previousBitmap != (HGDIOBJ)HGDI_ERROR) {
        SelectObject(memoryDc, previousBitmap);
    }
    if (bitmap != NULL) {
        DeleteObject(bitmap);
    }
    if (memoryDc != NULL) {
        DeleteDC(memoryDc);
    }
    if (referenceDc != NULL) {
        ReleaseDC(formatBar, referenceDc);
    }
    if (!success && groupAfter <= groupBefore) {
        fwprintf(stderr,
                 L"Draw offscreen group paint stalled (%ld -> %ld)\n",
                 (long)groupBefore, (long)groupAfter);
    }
    return success;
}

static BOOL draw_controls_are_hidden(HWND window, HWND formatBar)
{
    size_t index;

    if (window == NULL || formatBar == NULL) {
        return FALSE;
    }
    for (index = 0; index < DRAW_GROUP_COUNT; ++index) {
        LRESULT flags = 0;
        if (!query_wordcraft_state(window, WCQ_DRAW_GROUP_FLAGS,
                                   (LPARAM)index, &flags) ||
            ((UINT)flags & (DRAW_GROUP_FLAG_VISIBLE |
                            DRAW_GROUP_FLAG_LABEL_VISIBLE)) != 0) {
            fwprintf(stderr,
                     L"Draw group %lu remained visible (flags=0x%lx)\n",
                     (unsigned long)index, (unsigned long)flags);
            return FALSE;
        }
    }
    for (index = 0; index < ARRAYSIZE(expectedDrawControls); ++index) {
        HWND control =
            find_control(formatBar, (int)expectedDrawControls[index].id);
        LRESULT flags = 0;
        if (control == NULL ||
            !query_wordcraft_state(window, WCQ_DRAW_CONTROL_FLAGS,
                                   (LPARAM)index, &flags) ||
            ((UINT)flags & DRAW_CONTROL_FLAG_VISIBLE) != 0 ||
            (GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE) != 0) {
            fwprintf(stderr,
                     L"Draw control %s remained visible (flags=0x%lx style=0x%lx)\n",
                     expectedDrawControls[index].caption,
                     (unsigned long)flags,
                     (unsigned long)GetWindowLongPtrW(control, GWL_STYLE));
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL design_preview_visible_in_mode(size_t controlIndex,
                                           int layoutMode)
{
    size_t previewIndex;

    if (controlIndex == 0 ||
        controlIndex > 10 ||
        !expectedDesignControls[controlIndex].stylePreview) {
        return TRUE;
    }
    previewIndex = controlIndex - 1;
    if (layoutMode == RIBBON_LAYOUT_FULL) {
        return TRUE;
    }
    if (layoutMode == RIBBON_LAYOUT_COMPACT) {
        return previewIndex < 4;
    }
    return previewIndex == 0;
}

static BOOL validate_design_layout_contract(HWND window, HWND formatBar,
                                             int expectedMode)
{
    RECT client;
    RECT previous = {0};
    LRESULT groupCount = 0;
    LRESULT controlCount = 0;
    LRESULT groupPaintCount = 0;
    LRESULT mode = 0;
    LRESULT activeStyleSet = -1;
    LRESULT galleryVisible = -1;
    LRESULT colorScheme = -1;
    LRESULT fontScheme = -1;
    LRESULT paragraphSpacing = -1;
    LRESULT effect = -1;
    size_t index;

    if (window == NULL || formatBar == NULL ||
        expectedMode < RIBBON_LAYOUT_FULL ||
        expectedMode > RIBBON_LAYOUT_COLLAPSED ||
        !GetClientRect(formatBar, &client) ||
        !query_wordcraft_state(window, WCQ_DESIGN_GROUP_COUNT, 0,
                               &groupCount) ||
        !query_wordcraft_state(window, WCQ_DESIGN_CONTROL_COUNT, 0,
                               &controlCount) ||
        !query_wordcraft_state(window, WCQ_DESIGN_GROUP_PAINT_COUNT, 0,
                               &groupPaintCount) ||
        !query_wordcraft_state(window, WCQ_DESIGN_LAYOUT_MODE, 0, &mode) ||
        !query_wordcraft_state(window, WCQ_DESIGN_ACTIVE_STYLE_SET, 0,
                               &activeStyleSet) ||
        !query_wordcraft_state(window, WCQ_DESIGN_GALLERY_VISIBLE, 0,
                               &galleryVisible) ||
        !query_wordcraft_state(window, WCQ_DESIGN_COLOR_SCHEME, 0,
                               &colorScheme) ||
        !query_wordcraft_state(window, WCQ_DESIGN_FONT_SCHEME, 0,
                               &fontScheme) ||
        !query_wordcraft_state(window, WCQ_DESIGN_PARAGRAPH_SPACING, 0,
                               &paragraphSpacing) ||
        !query_wordcraft_state(window, WCQ_DESIGN_EFFECT, 0, &effect) ||
        groupCount != DESIGN_GROUP_COUNT ||
        controlCount != (LRESULT)ARRAYSIZE(expectedDesignControls) ||
        mode != expectedMode || client.right <= client.left ||
        client.bottom <= client.top || groupPaintCount < 0 ||
        activeStyleSet != DESIGN_STYLE_SET_OFFICE ||
        galleryVisible != 0 ||
        colorScheme != DESIGN_COLOR_SCHEME_OFFICE ||
        fontScheme != DESIGN_FONT_SCHEME_OFFICE ||
        paragraphSpacing != DESIGN_PARAGRAPH_SPACING_OPEN ||
        effect != DESIGN_EFFECT_OFFICE) {
        fwprintf(
            stderr,
            L"Design layout precondition mismatch mode=%ld/%d groups=%ld/%d controls=%ld/%lu active=%ld gallery=%ld color=%ld font=%ld spacing=%ld effect=%ld client=%ldx%ld paint=%ld\n",
            (long)mode, expectedMode, (long)groupCount,
            DESIGN_GROUP_COUNT, (long)controlCount,
            (unsigned long)ARRAYSIZE(expectedDesignControls),
            (long)activeStyleSet, (long)galleryVisible,
            (long)colorScheme, (long)fontScheme,
            (long)paragraphSpacing, (long)effect,
            client.right - client.left, client.bottom - client.top,
            (long)groupPaintCount);
        return FALSE;
    }

    for (index = 0; index < DESIGN_GROUP_COUNT; ++index) {
        RECT rect;
        LRESULT hash = 0;
        LRESULT flags = 0;
        UINT expectedFlags = DESIGN_GROUP_FLAG_VISIBLE;
        const UINT knownFlags = DESIGN_GROUP_FLAG_VISIBLE |
                                DESIGN_GROUP_FLAG_LABEL_VISIBLE |
                                DESIGN_GROUP_FLAG_COLLAPSED |
                                DESIGN_GROUP_FLAG_GALLERY_OPEN;

        if (expectedMode == RIBBON_LAYOUT_COLLAPSED) {
            expectedFlags |= DESIGN_GROUP_FLAG_COLLAPSED;
        } else {
            expectedFlags |= DESIGN_GROUP_FLAG_LABEL_VISIBLE;
        }
        if (!query_wordcraft_state(window, WCQ_DESIGN_GROUP_NAME_HASH,
                                   (LPARAM)index, &hash) ||
            !query_wordcraft_state(window, WCQ_DESIGN_GROUP_FLAGS,
                                   (LPARAM)index, &flags) ||
            !query_design_group_rect(window, (int)index, &rect) ||
            (UINT)(DWORD_PTR)hash !=
                probe_text_hash(expectedDesignGroupNames[index]) ||
            ((UINT)flags & knownFlags) != expectedFlags ||
            rect.left < client.left || rect.top < client.top ||
            rect.right > client.right || rect.bottom > client.bottom ||
            rect.right <= rect.left || rect.bottom <= rect.top ||
            (index > 0 && rect.left < previous.right)) {
            fwprintf(
                stderr,
                L"Design group %lu contract mismatch hash=0x%lx expected=0x%lx flags=0x%lx expected_flags=0x%x rect=%ld,%ld,%ld,%ld client=%ld,%ld,%ld,%ld previous_right=%ld\n",
                (unsigned long)index, (unsigned long)hash,
                (unsigned long)probe_text_hash(
                    expectedDesignGroupNames[index]),
                (unsigned long)flags, expectedFlags,
                rect.left, rect.top, rect.right, rect.bottom,
                client.left, client.top, client.right, client.bottom,
                previous.right);
            return FALSE;
        }
        previous = rect;
    }

    for (index = 0; index < ARRAYSIZE(expectedDesignControls); ++index) {
        const ExpectedDesignControl *expected =
            &expectedDesignControls[index];
        RECT controlRect;
        RECT groupRect;
        HWND control;
        WCHAR caption[64] = L"";
        LRESULT id = 0;
        LRESULT group = -1;
        LRESULT flags = 0;
        LRESULT icon = RIBBON_DESIGN_ICON_NONE;
        BOOL visible =
            !expected->stylePreview ||
            design_preview_visible_in_mode(index, expectedMode);
        BOOL checked =
            expected->id == IDM_DESIGN_STYLE_OFFICE;
        UINT expectedFlags = DESIGN_CONTROL_FLAG_CREATED |
                             DESIGN_CONTROL_FLAG_ENABLED |
                             DESIGN_CONTROL_FLAG_TABSTOP |
                             DESIGN_CONTROL_FLAG_HAS_ICON |
                             (visible
                                  ? DESIGN_CONTROL_FLAG_VISIBLE
                                  : 0) |
                             (checked
                                  ? DESIGN_CONTROL_FLAG_CHECKED
                                  : 0) |
                             (expected->stylePreview
                                  ? DESIGN_CONTROL_FLAG_STYLE_PREVIEW
                                  : 0);
        const UINT knownFlags = DESIGN_CONTROL_FLAG_CREATED |
                                DESIGN_CONTROL_FLAG_VISIBLE |
                                DESIGN_CONTROL_FLAG_ENABLED |
                                DESIGN_CONTROL_FLAG_TABSTOP |
                                DESIGN_CONTROL_FLAG_HAS_ICON |
                                DESIGN_CONTROL_FLAG_CHECKED |
                                DESIGN_CONTROL_FLAG_STYLE_PREVIEW;

        control = find_control(formatBar, (int)expected->id);
        if (control == NULL ||
            !query_wordcraft_state(window, WCQ_DESIGN_CONTROL_ID,
                                   (LPARAM)index, &id) ||
            !query_wordcraft_state(window, WCQ_DESIGN_CONTROL_GROUP,
                                   (LPARAM)index, &group) ||
            !query_wordcraft_state(window, WCQ_DESIGN_CONTROL_FLAGS,
                                   (LPARAM)index, &flags) ||
            !query_wordcraft_state(window, WCQ_DESIGN_CONTROL_ICON,
                                   (LPARAM)index, &icon) ||
            id != expected->id || group != expected->group ||
            icon != expected->icon ||
            ((UINT)flags & knownFlags) != expectedFlags ||
            !get_window_text_bounded(control, caption,
                                     ARRAYSIZE(caption)) ||
            wcscmp(caption, expected->caption) != 0 ||
            !query_design_group_rect(window, expected->group,
                                     &groupRect)) {
            fwprintf(
                stderr,
                L"Design control %lu (%s) contract mismatch hwnd=%p id=%ld/%u group=%ld/%d flags=0x%lx/0x%x icon=%ld/%d caption='%s'\n",
                (unsigned long)index, expected->caption, (void *)control,
                (long)id, expected->id, (long)group, expected->group,
                (unsigned long)flags, expectedFlags, (long)icon,
                expected->icon, caption);
            return FALSE;
        }
        if (!visible) {
            if ((GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE) != 0) {
                return FALSE;
            }
            continue;
        }
        if (!GetWindowRect(control, &controlRect)) {
            return FALSE;
        }
        MapWindowPoints(HWND_DESKTOP, formatBar,
                        (POINT *)&controlRect, 2);
        if (controlRect.right <= controlRect.left ||
            controlRect.bottom <= controlRect.top ||
            controlRect.left < groupRect.left ||
            controlRect.top < groupRect.top ||
            controlRect.right > groupRect.right ||
            controlRect.bottom > groupRect.bottom) {
            fwprintf(
                stderr,
                L"Design control %lu (%s) geometry mismatch control=%ld,%ld,%ld,%ld group=%ld,%ld,%ld,%ld\n",
                (unsigned long)index, expected->caption,
                controlRect.left, controlRect.top,
                controlRect.right, controlRect.bottom,
                groupRect.left, groupRect.top,
                groupRect.right, groupRect.bottom);
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL force_design_ribbon_offscreen_paint(HWND window,
                                                 HWND formatBar)
{
    RECT client;
    LRESULT groupBefore = 0;
    LRESULT groupAfter = 0;
    LRESULT iconBefore[ARRAYSIZE(expectedDesignControls)];
    HDC referenceDc = NULL;
    HDC memoryDc = NULL;
    HBITMAP bitmap = NULL;
    HGDIOBJ previousBitmap = NULL;
    BOOL success = FALSE;
    size_t index;

    if (window == NULL || formatBar == NULL ||
        !GetClientRect(formatBar, &client) ||
        client.right <= client.left || client.bottom <= client.top ||
        !query_wordcraft_state(window, WCQ_DESIGN_GROUP_PAINT_COUNT, 0,
                               &groupBefore)) {
        return FALSE;
    }
    for (index = 0; index < ARRAYSIZE(expectedDesignControls); ++index) {
        if (!query_wordcraft_state(window, WCQ_DESIGN_ICON_PAINT_COUNT,
                                   (LPARAM)index, &iconBefore[index])) {
            return FALSE;
        }
    }

    referenceDc = GetDC(formatBar);
    if (referenceDc == NULL) {
        goto cleanup;
    }
    memoryDc = CreateCompatibleDC(referenceDc);
    bitmap = CreateCompatibleBitmap(
        referenceDc, client.right - client.left,
        client.bottom - client.top);
    if (memoryDc == NULL || bitmap == NULL) {
        goto cleanup;
    }
    previousBitmap = SelectObject(memoryDc, bitmap);
    if (previousBitmap == NULL ||
        previousBitmap == (HGDIOBJ)HGDI_ERROR ||
        !send_message_bounded(formatBar, WM_PRINTCLIENT,
                              (WPARAM)memoryDc,
                              PRF_CLIENT | PRF_ERASEBKGND, NULL)) {
        goto cleanup;
    }
    for (index = 0; index < ARRAYSIZE(expectedDesignControls); ++index) {
        HWND control =
            find_control(formatBar, (int)expectedDesignControls[index].id);
        if (control == NULL ||
            !send_message_bounded(control, WM_PRINTCLIENT,
                                  (WPARAM)memoryDc,
                                  PRF_CLIENT | PRF_ERASEBKGND, NULL)) {
            goto cleanup;
        }
    }
    GdiFlush();
    if (!query_wordcraft_state(window, WCQ_DESIGN_GROUP_PAINT_COUNT, 0,
                               &groupAfter) ||
        groupAfter <= groupBefore) {
        goto cleanup;
    }
    for (index = 0; index < ARRAYSIZE(expectedDesignControls); ++index) {
        LRESULT iconAfter = 0;
        if (!query_wordcraft_state(window, WCQ_DESIGN_ICON_PAINT_COUNT,
                                   (LPARAM)index, &iconAfter) ||
            iconAfter <= iconBefore[index]) {
            fwprintf(stderr,
                     L"Design offscreen icon paint stalled for %s (%ld -> %ld)\n",
                     expectedDesignControls[index].caption,
                     (long)iconBefore[index], (long)iconAfter);
            goto cleanup;
        }
    }
    success = TRUE;

cleanup:
    if (memoryDc != NULL && previousBitmap != NULL &&
        previousBitmap != (HGDIOBJ)HGDI_ERROR) {
        SelectObject(memoryDc, previousBitmap);
    }
    if (bitmap != NULL) {
        DeleteObject(bitmap);
    }
    if (memoryDc != NULL) {
        DeleteDC(memoryDc);
    }
    if (referenceDc != NULL) {
        ReleaseDC(formatBar, referenceDc);
    }
    if (!success && groupAfter <= groupBefore) {
        fwprintf(stderr,
                 L"Design offscreen group paint stalled (%ld -> %ld)\n",
                 (long)groupBefore, (long)groupAfter);
    }
    return success;
}

static BOOL design_controls_are_hidden(HWND window, HWND formatBar)
{
    size_t index;

    if (window == NULL || formatBar == NULL) {
        return FALSE;
    }
    for (index = 0; index < ARRAYSIZE(expectedDesignControls); ++index) {
        HWND control =
            find_control(formatBar, (int)expectedDesignControls[index].id);
        LRESULT flags = 0;
        if (control == NULL ||
            !query_wordcraft_state(window, WCQ_DESIGN_CONTROL_FLAGS,
                                   (LPARAM)index, &flags) ||
            ((UINT)flags & DESIGN_CONTROL_FLAG_VISIBLE) != 0 ||
            (GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE) != 0) {
            fwprintf(
                stderr,
                L"Design control %s remained visible (flags=0x%lx style=0x%lx)\n",
                expectedDesignControls[index].caption,
                (unsigned long)flags,
                (unsigned long)GetWindowLongPtrW(control, GWL_STYLE));
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL wait_for_ribbon_control_focus(HWND window,
                                          LRESULT expectedControlId);

static BOOL wait_for_design_gallery_state(HWND window, BOOL expectedVisible,
                                          LRESULT minimumPaintCount,
                                          int expectedFocusedIndex)
{
    int attempt;

    for (attempt = 0; attempt < 100; ++attempt) {
        LRESULT visible = -1;
        LRESULT paintCount = -1;
        LRESULT itemCount = -1;
        LRESULT focusedIndex = -1;
        LRESULT groupFlags = 0;
        BOOL openFlag;

        if (!query_wordcraft_state(window, WCQ_DESIGN_GALLERY_VISIBLE, 0,
                                   &visible) ||
            !query_wordcraft_state(window,
                                   WCQ_DESIGN_GALLERY_PAINT_COUNT, 0,
                                   &paintCount) ||
            !query_wordcraft_state(window,
                                   WCQ_DESIGN_GALLERY_ITEM_COUNT, 0,
                                   &itemCount) ||
            !query_wordcraft_state(window,
                                   WCQ_DESIGN_GALLERY_FOCUSED_INDEX, 0,
                                   &focusedIndex) ||
            !query_wordcraft_state(window, WCQ_DESIGN_GROUP_FLAGS,
                                   DESIGN_GROUP_DOCUMENT_FORMATTING,
                                   &groupFlags)) {
            return FALSE;
        }
        openFlag =
            ((UINT)groupFlags & DESIGN_GROUP_FLAG_GALLERY_OPEN) != 0;
        if ((visible != 0) == expectedVisible &&
            openFlag == expectedVisible &&
            itemCount == DESIGN_STYLE_SET_COUNT + 2 &&
            (!expectedVisible ||
             (paintCount > minimumPaintCount &&
              focusedIndex == expectedFocusedIndex))) {
            return TRUE;
        }
        Sleep(20);
    }
    return FALSE;
}

static BOOL validate_design_gallery_contract(HWND window, HWND formatBar)
{
    HWND more;
    HWND gallery;
    LRESULT paintBefore = 0;
    LRESULT focusedIndex = -1;
    RECT previousFooter = {0};
    int item;
    int attempt;

    if (window == NULL || formatBar == NULL ||
        (more = find_control(formatBar,
                             IDM_DESIGN_STYLE_GALLERY_MORE)) == NULL ||
        !query_wordcraft_state(window, WCQ_DESIGN_GALLERY_PAINT_COUNT, 0,
                               &paintBefore) ||
        !send_message_bounded(
            window, WM_COMMAND,
            MAKEWPARAM(IDM_DESIGN_STYLE_GALLERY_MORE, BN_CLICKED),
            (LPARAM)more, NULL) ||
        !wait_for_design_gallery_state(window, TRUE, paintBefore, 0)) {
        return FALSE;
    }

    gallery = NULL;
    for (attempt = 0; attempt < 100 && gallery == NULL; ++attempt) {
        gallery = find_open_design_gallery(window);
        if (gallery == NULL) {
            Sleep(20);
        }
    }
    if (gallery == NULL) {
        return FALSE;
    }
    for (item = 0; item < DESIGN_STYLE_SET_COUNT + 2; ++item) {
        RECT rect;
        if (!query_design_gallery_item_rect(window, item, &rect) ||
            rect.left < 0 || rect.top < 0 ||
            rect.right <= rect.left || rect.bottom <= rect.top) {
            fwprintf(stderr,
                     L"Design gallery item %d had an invalid rect\n",
                     item);
            return FALSE;
        }
        if (item == DESIGN_STYLE_SET_COUNT) {
            previousFooter = rect;
        } else if (item == DESIGN_STYLE_SET_COUNT + 1 &&
                   rect.top < previousFooter.bottom) {
            fwprintf(stderr,
                     L"Design gallery footer items overlapped\n");
            return FALSE;
        }
    }

    if (!send_message_bounded(gallery, WM_KEYDOWN, VK_RIGHT, 0, NULL)) {
        return FALSE;
    }
    for (attempt = 0; attempt < 100; ++attempt) {
        if (!query_wordcraft_state(
                window, WCQ_DESIGN_GALLERY_FOCUSED_INDEX, 0,
                &focusedIndex)) {
            return FALSE;
        }
        if (focusedIndex == 1) {
            break;
        }
        Sleep(20);
    }
    if (focusedIndex != 1 ||
        !send_message_bounded(gallery, WM_KEYDOWN, VK_ESCAPE, 0, NULL) ||
        !wait_for_design_gallery_state(window, FALSE, 0, -1) ||
        !wait_for_ribbon_control_focus(
            window, IDM_DESIGN_STYLE_GALLERY_MORE)) {
        return FALSE;
    }
    return TRUE;
}

static BOOL validate_view_layout_contract(HWND window, HWND formatBar,
                                          int expectedMode)
{
    RECT client;
    RECT previous = {0};
    LRESULT groupCount = 0;
    LRESULT controlCount = 0;
    LRESULT groupPaintCount = 0;
    LRESULT mode = 0;
    LRESULT activeMode = 0;
    LRESULT vertical = 0;
    LRESULT rulerVisible = 0;
    LRESULT gridlinesVisible = 0;
    LRESULT navigationVisible = 0;
    LRESULT sideBySide = 0;
    LRESULT focusMode = 0;
    LRESULT darkMode = 0;
    size_t index;

    if (window == NULL || formatBar == NULL ||
        expectedMode < RIBBON_LAYOUT_FULL ||
        expectedMode > RIBBON_LAYOUT_COLLAPSED ||
        !GetClientRect(formatBar, &client) ||
        !query_wordcraft_state(window, WCQ_VIEW_GROUP_COUNT, 0,
                               &groupCount) ||
        !query_wordcraft_state(window, WCQ_VIEW_CONTROL_COUNT, 0,
                               &controlCount) ||
        !query_wordcraft_state(window, WCQ_VIEW_GROUP_PAINT_COUNT, 0,
                               &groupPaintCount) ||
        !query_wordcraft_state(window, WCQ_VIEW_LAYOUT_MODE, 0, &mode) ||
        !query_wordcraft_state(window, WCQ_VIEW_ACTIVE_MODE, 0,
                               &activeMode) ||
        !query_wordcraft_state(window, WCQ_VIEW_MOVEMENT_VERTICAL, 0,
                               &vertical) ||
        !query_wordcraft_state(window, WCQ_VIEW_RULER_VISIBLE, 0,
                               &rulerVisible) ||
        !query_wordcraft_state(window, WCQ_VIEW_GRIDLINES_VISIBLE, 0,
                               &gridlinesVisible) ||
        !query_wordcraft_state(window, WCQ_VIEW_NAVIGATION_VISIBLE, 0,
                               &navigationVisible) ||
        !query_wordcraft_state(window, WCQ_VIEW_SIDE_BY_SIDE, 0,
                               &sideBySide) ||
        !query_wordcraft_state(window, WCQ_VIEW_FOCUS_MODE, 0,
                               &focusMode) ||
        !query_wordcraft_state(window, WCQ_DARK_MODE, 0, &darkMode) ||
        groupCount != VIEW_GROUP_COUNT ||
        controlCount != (LRESULT)ARRAYSIZE(expectedViewControls) ||
        mode != expectedMode || client.right <= client.left ||
        client.bottom <= client.top || groupPaintCount < 0 ||
        activeMode != VIEW_MODE_PRINT_LAYOUT || vertical != 1 ||
        rulerVisible != 0 || gridlinesVisible != 0 ||
        navigationVisible != 0 || sideBySide != 0 || focusMode != 0 ||
        darkMode != 0) {
        fwprintf(
            stderr,
            L"View layout precondition mismatch mode=%ld/%d groups=%ld/%d controls=%ld/%lu active=%ld/%d vertical=%ld ruler=%ld grid=%ld navigation=%ld side=%ld focus=%ld dark=%ld client=%ldx%ld paint=%ld\n",
            (long)mode, expectedMode, (long)groupCount, VIEW_GROUP_COUNT,
            (long)controlCount,
            (unsigned long)ARRAYSIZE(expectedViewControls),
            (long)activeMode, VIEW_MODE_PRINT_LAYOUT, (long)vertical,
            (long)rulerVisible, (long)gridlinesVisible,
            (long)navigationVisible, (long)sideBySide, (long)focusMode,
            (long)darkMode, client.right - client.left,
            client.bottom - client.top, (long)groupPaintCount);
        return FALSE;
    }

    for (index = 0; index < VIEW_GROUP_COUNT; ++index) {
        RECT rect;
        LRESULT hash = 0;
        LRESULT flags = 0;
        UINT expectedFlags = VIEW_GROUP_FLAG_VISIBLE;
        const UINT knownFlags = VIEW_GROUP_FLAG_VISIBLE |
                                VIEW_GROUP_FLAG_LABEL_VISIBLE |
                                VIEW_GROUP_FLAG_COLLAPSED;

        if (expectedMode == RIBBON_LAYOUT_COLLAPSED) {
            expectedFlags |= VIEW_GROUP_FLAG_COLLAPSED;
        } else {
            expectedFlags |= VIEW_GROUP_FLAG_LABEL_VISIBLE;
        }
        if (!query_wordcraft_state(window, WCQ_VIEW_GROUP_NAME_HASH,
                                   (LPARAM)index, &hash) ||
            !query_wordcraft_state(window, WCQ_VIEW_GROUP_FLAGS,
                                   (LPARAM)index, &flags) ||
            !query_view_group_rect(window, (int)index, &rect) ||
            (UINT)(DWORD_PTR)hash !=
                probe_text_hash(expectedViewGroupNames[index]) ||
            ((UINT)flags & knownFlags) != expectedFlags ||
            rect.left < client.left || rect.top < client.top ||
            rect.right > client.right || rect.bottom > client.bottom ||
            rect.right <= rect.left || rect.bottom <= rect.top ||
            (index > 0 && rect.left < previous.right)) {
            fwprintf(
                stderr,
                L"View group %lu contract mismatch hash=0x%lx expected=0x%lx flags=0x%lx expected_flags=0x%x rect=%ld,%ld,%ld,%ld client=%ld,%ld,%ld,%ld previous_right=%ld\n",
                (unsigned long)index, (unsigned long)hash,
                (unsigned long)probe_text_hash(
                    expectedViewGroupNames[index]),
                (unsigned long)flags, expectedFlags,
                rect.left, rect.top, rect.right, rect.bottom,
                client.left, client.top, client.right, client.bottom,
                previous.right);
            return FALSE;
        }
        previous = rect;
    }

    for (index = 0; index < ARRAYSIZE(expectedViewControls); ++index) {
        const ExpectedViewControl *expected = &expectedViewControls[index];
        RECT controlRect;
        RECT groupRect;
        HWND control;
        WCHAR caption[64] = L"";
        LRESULT id = 0;
        LRESULT group = -1;
        LRESULT flags = 0;
        LRESULT icon = 0;
        BOOL checked = expected->id == IDM_VIEW_PRINT_LAYOUT ||
                       expected->id == IDM_VIEW_VERTICAL;
        UINT expectedFlags = VIEW_CONTROL_FLAG_CREATED |
                             VIEW_CONTROL_FLAG_VISIBLE |
                             VIEW_CONTROL_FLAG_TABSTOP |
                             VIEW_CONTROL_FLAG_HAS_ICON |
                             (expected->enabledByDefault
                                  ? VIEW_CONTROL_FLAG_ENABLED
                                  : 0) |
                             (checked ? VIEW_CONTROL_FLAG_CHECKED : 0);
        const UINT knownFlags = VIEW_CONTROL_FLAG_CREATED |
                                VIEW_CONTROL_FLAG_VISIBLE |
                                VIEW_CONTROL_FLAG_ENABLED |
                                VIEW_CONTROL_FLAG_TABSTOP |
                                VIEW_CONTROL_FLAG_HAS_ICON |
                                VIEW_CONTROL_FLAG_CHECKED;

        control = find_control(formatBar, (int)expected->id);
        if (control == NULL ||
            !query_wordcraft_state(window, WCQ_VIEW_CONTROL_ID,
                                   (LPARAM)index, &id) ||
            !query_wordcraft_state(window, WCQ_VIEW_CONTROL_GROUP,
                                   (LPARAM)index, &group) ||
            !query_wordcraft_state(window, WCQ_VIEW_CONTROL_FLAGS,
                                   (LPARAM)index, &flags) ||
            !query_wordcraft_state(window, WCQ_VIEW_CONTROL_ICON,
                                   (LPARAM)index, &icon) ||
            id != expected->id || group != expected->group ||
            icon != expected->icon ||
            ((UINT)flags & knownFlags) != expectedFlags ||
            !get_window_text_bounded(control, caption,
                                     ARRAYSIZE(caption)) ||
            wcscmp(caption, expected->caption) != 0 ||
            !GetWindowRect(control, &controlRect) ||
            !query_view_group_rect(window, expected->group, &groupRect)) {
            fwprintf(
                stderr,
                L"View control %lu (%s) contract mismatch hwnd=%p id=%ld/%u group=%ld/%d flags=0x%lx/0x%x icon=%ld/%d caption='%s'\n",
                (unsigned long)index, expected->caption, (void *)control,
                (long)id, expected->id, (long)group, expected->group,
                (unsigned long)flags, expectedFlags, (long)icon,
                expected->icon, caption);
            return FALSE;
        }
        MapWindowPoints(HWND_DESKTOP, formatBar,
                        (POINT *)&controlRect, 2);
        if (controlRect.right <= controlRect.left ||
            controlRect.bottom <= controlRect.top ||
            controlRect.left < groupRect.left ||
            controlRect.top < groupRect.top ||
            controlRect.right > groupRect.right ||
            controlRect.bottom > groupRect.bottom) {
            fwprintf(
                stderr,
                L"View control %lu (%s) geometry mismatch control=%ld,%ld,%ld,%ld group=%ld,%ld,%ld,%ld\n",
                (unsigned long)index, expected->caption,
                controlRect.left, controlRect.top,
                controlRect.right, controlRect.bottom,
                groupRect.left, groupRect.top,
                groupRect.right, groupRect.bottom);
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL force_view_ribbon_offscreen_paint(HWND window,
                                               HWND formatBar)
{
    RECT client;
    LRESULT groupBefore = 0;
    LRESULT groupAfter = 0;
    LRESULT iconBefore[ARRAYSIZE(expectedViewControls)];
    HDC referenceDc = NULL;
    HDC memoryDc = NULL;
    HBITMAP bitmap = NULL;
    HGDIOBJ previousBitmap = NULL;
    BOOL success = FALSE;
    size_t index;

    if (window == NULL || formatBar == NULL ||
        !GetClientRect(formatBar, &client) ||
        client.right <= client.left || client.bottom <= client.top ||
        !query_wordcraft_state(window, WCQ_VIEW_GROUP_PAINT_COUNT, 0,
                               &groupBefore)) {
        return FALSE;
    }
    for (index = 0; index < ARRAYSIZE(expectedViewControls); ++index) {
        if (!query_wordcraft_state(window, WCQ_VIEW_ICON_PAINT_COUNT,
                                   (LPARAM)index, &iconBefore[index])) {
            return FALSE;
        }
    }

    referenceDc = GetDC(formatBar);
    if (referenceDc == NULL) {
        goto cleanup;
    }
    memoryDc = CreateCompatibleDC(referenceDc);
    bitmap = CreateCompatibleBitmap(
        referenceDc, client.right - client.left,
        client.bottom - client.top);
    if (memoryDc == NULL || bitmap == NULL) {
        goto cleanup;
    }
    previousBitmap = SelectObject(memoryDc, bitmap);
    if (previousBitmap == NULL ||
        previousBitmap == (HGDIOBJ)HGDI_ERROR ||
        !send_message_bounded(formatBar, WM_PRINTCLIENT,
                              (WPARAM)memoryDc,
                              PRF_CLIENT | PRF_ERASEBKGND, NULL)) {
        goto cleanup;
    }
    for (index = 0; index < ARRAYSIZE(expectedViewControls); ++index) {
        HWND control =
            find_control(formatBar, (int)expectedViewControls[index].id);
        if (control == NULL ||
            !send_message_bounded(control, WM_PRINTCLIENT,
                                  (WPARAM)memoryDc,
                                  PRF_CLIENT | PRF_ERASEBKGND, NULL)) {
            goto cleanup;
        }
    }
    GdiFlush();
    if (!query_wordcraft_state(window, WCQ_VIEW_GROUP_PAINT_COUNT, 0,
                               &groupAfter) ||
        groupAfter <= groupBefore) {
        goto cleanup;
    }
    for (index = 0; index < ARRAYSIZE(expectedViewControls); ++index) {
        LRESULT iconAfter = 0;
        if (!query_wordcraft_state(window, WCQ_VIEW_ICON_PAINT_COUNT,
                                   (LPARAM)index, &iconAfter) ||
            iconAfter <= iconBefore[index]) {
            fwprintf(stderr,
                     L"View offscreen icon paint stalled for %s (%ld -> %ld)\n",
                     expectedViewControls[index].caption,
                     (long)iconBefore[index], (long)iconAfter);
            goto cleanup;
        }
    }
    success = TRUE;

cleanup:
    if (memoryDc != NULL && previousBitmap != NULL &&
        previousBitmap != (HGDIOBJ)HGDI_ERROR) {
        SelectObject(memoryDc, previousBitmap);
    }
    if (bitmap != NULL) {
        DeleteObject(bitmap);
    }
    if (memoryDc != NULL) {
        DeleteDC(memoryDc);
    }
    if (referenceDc != NULL) {
        ReleaseDC(formatBar, referenceDc);
    }
    if (!success && groupAfter <= groupBefore) {
        fwprintf(stderr,
                 L"View offscreen group paint stalled (%ld -> %ld)\n",
                 (long)groupBefore, (long)groupAfter);
    }
    return success;
}

static BOOL view_controls_are_hidden(HWND window, HWND formatBar)
{
    size_t index;

    if (window == NULL || formatBar == NULL) {
        return FALSE;
    }
    for (index = 0; index < VIEW_GROUP_COUNT; ++index) {
        LRESULT flags = 0;
        if (!query_wordcraft_state(window, WCQ_VIEW_GROUP_FLAGS,
                                   (LPARAM)index, &flags) ||
            ((UINT)flags & (VIEW_GROUP_FLAG_VISIBLE |
                            VIEW_GROUP_FLAG_LABEL_VISIBLE)) != 0) {
            fwprintf(stderr,
                     L"View group %lu remained visible (flags=0x%lx)\n",
                     (unsigned long)index, (unsigned long)flags);
            return FALSE;
        }
    }
    for (index = 0; index < ARRAYSIZE(expectedViewControls); ++index) {
        HWND control =
            find_control(formatBar, (int)expectedViewControls[index].id);
        LRESULT flags = 0;
        if (control == NULL ||
            !query_wordcraft_state(window, WCQ_VIEW_CONTROL_FLAGS,
                                   (LPARAM)index, &flags) ||
            ((UINT)flags & VIEW_CONTROL_FLAG_VISIBLE) != 0 ||
            (GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE) != 0) {
            fwprintf(stderr,
                     L"View control %s remained visible (flags=0x%lx style=0x%lx)\n",
                     expectedViewControls[index].caption,
                     (unsigned long)flags,
                     (unsigned long)GetWindowLongPtrW(control, GWL_STYLE));
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL get_selection_bounded(HWND editor, DWORD *start, DWORD *end);

typedef struct DrawDocumentSnapshot {
    WCHAR text[512];
    LRESULT textLength;
    LRESULT modified;
    LRESULT canUndo;
    LRESULT canRedo;
    LRESULT pageCount;
    DWORD selectionStart;
    DWORD selectionEnd;
} DrawDocumentSnapshot;

static BOOL capture_draw_document_snapshot(
    HWND window, HWND editor, DrawDocumentSnapshot *snapshot)
{
    if (window == NULL || editor == NULL || snapshot == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ZeroMemory(snapshot, sizeof(*snapshot));
    return send_message_bounded(editor, WM_GETTEXTLENGTH, 0, 0,
                                &snapshot->textLength) &&
           snapshot->textLength >= 0 &&
           snapshot->textLength <
               (LRESULT)ARRAYSIZE(snapshot->text) &&
           get_window_text_bounded(editor, snapshot->text,
                                   ARRAYSIZE(snapshot->text)) &&
           (LRESULT)wcslen(snapshot->text) == snapshot->textLength &&
           send_message_bounded(editor, EM_GETMODIFY, 0, 0,
                                &snapshot->modified) &&
           send_message_bounded(editor, EM_CANUNDO, 0, 0,
                                &snapshot->canUndo) &&
           send_message_bounded(editor, EM_CANREDO, 0, 0,
                                &snapshot->canRedo) &&
           query_wordcraft_state(window, WCQ_PAGE_COUNT, 0,
                                 &snapshot->pageCount) &&
           get_selection_bounded(editor, &snapshot->selectionStart,
                                 &snapshot->selectionEnd);
}

static BOOL draw_document_matches_snapshot(
    HWND window, HWND editor, const DrawDocumentSnapshot *expected)
{
    DrawDocumentSnapshot actual;

    return expected != NULL &&
           capture_draw_document_snapshot(window, editor, &actual) &&
           actual.textLength == expected->textLength &&
           wcscmp(actual.text, expected->text) == 0 &&
           actual.modified == expected->modified &&
           actual.canUndo == expected->canUndo &&
           actual.canRedo == expected->canRedo &&
           actual.pageCount == expected->pageCount &&
           actual.selectionStart == expected->selectionStart &&
           actual.selectionEnd == expected->selectionEnd;
}

static BOOL draw_state_matches(HWND window, UINT expectedTool,
                               BOOL expectedRuler,
                               BOOL expectedBackground)
{
    LRESULT activeTool = 0;
    LRESULT rulerVisible = 0;
    LRESULT backgroundRuled = 0;
    size_t index;

    if (!draw_control_is_tool(expectedTool) ||
        !query_wordcraft_state(window, WCQ_DRAW_ACTIVE_TOOL, 0,
                               &activeTool) ||
        !query_wordcraft_state(window, WCQ_DRAW_RULER_VISIBLE, 0,
                               &rulerVisible) ||
        !query_wordcraft_state(window, WCQ_DRAW_BACKGROUND_RULED, 0,
                               &backgroundRuled) ||
        activeTool != expectedTool ||
        (rulerVisible != 0) != expectedRuler ||
        (backgroundRuled != 0) != expectedBackground) {
        return FALSE;
    }
    for (index = 0; index < ARRAYSIZE(expectedDrawControls); ++index) {
        UINT id = expectedDrawControls[index].id;
        BOOL expectedChecked =
            id == expectedTool ||
            (id == IDM_DRAW_RULER && expectedRuler) ||
            (id == IDM_DRAW_FORMAT_BACKGROUND &&
             expectedBackground);
        LRESULT flags = 0;

        if (!query_wordcraft_state(window, WCQ_DRAW_CONTROL_FLAGS,
                                   (LPARAM)index, &flags) ||
            ((((UINT)flags & DRAW_CONTROL_FLAG_CHECKED) != 0) !=
             expectedChecked)) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL validate_draw_nonmutating_commands(HWND window, HWND editor)
{
    static const struct {
        UINT command;
        UINT expectedTool;
        BOOL expectedRuler;
        BOOL expectedBackground;
    } steps[] = {
        {IDM_DRAW_PEN_RED, IDM_DRAW_PEN_RED, FALSE, FALSE},
        {IDM_DRAW_RULER, IDM_DRAW_PEN_RED, TRUE, FALSE},
        {IDM_DRAW_FORMAT_BACKGROUND, IDM_DRAW_PEN_RED, TRUE, TRUE},
        {IDM_DRAW_RULER, IDM_DRAW_PEN_RED, FALSE, TRUE},
        {IDM_DRAW_FORMAT_BACKGROUND, IDM_DRAW_PEN_RED, FALSE, FALSE},
        {IDM_DRAW_SELECT, IDM_DRAW_SELECT, FALSE, FALSE}
    };
    DrawDocumentSnapshot document;
    size_t index;

    if (!draw_state_matches(window, IDM_DRAW_SELECT, FALSE, FALSE) ||
        !capture_draw_document_snapshot(window, editor, &document)) {
        return FALSE;
    }
    for (index = 0; index < ARRAYSIZE(steps); ++index) {
        if (!send_message_bounded(
                window, WM_COMMAND,
                MAKEWPARAM(steps[index].command, 0), 0, NULL) ||
            !draw_state_matches(window, steps[index].expectedTool,
                                steps[index].expectedRuler,
                                steps[index].expectedBackground) ||
            !draw_document_matches_snapshot(window, editor,
                                            &document)) {
            return FALSE;
        }
    }
    return TRUE;
}

typedef struct DesignStateSnapshot {
    LRESULT activeStyleSet;
    LRESULT colorScheme;
    LRESULT fontScheme;
    LRESULT paragraphSpacing;
    LRESULT effect;
    LRESULT galleryVisible;
} DesignStateSnapshot;

static BOOL capture_design_state(HWND window, DesignStateSnapshot *state)
{
    if (window == NULL || state == NULL) {
        return FALSE;
    }
    return query_wordcraft_state(window, WCQ_DESIGN_ACTIVE_STYLE_SET, 0,
                                 &state->activeStyleSet) &&
           query_wordcraft_state(window, WCQ_DESIGN_COLOR_SCHEME, 0,
                                 &state->colorScheme) &&
           query_wordcraft_state(window, WCQ_DESIGN_FONT_SCHEME, 0,
                                 &state->fontScheme) &&
           query_wordcraft_state(window, WCQ_DESIGN_PARAGRAPH_SPACING, 0,
                                 &state->paragraphSpacing) &&
           query_wordcraft_state(window, WCQ_DESIGN_EFFECT, 0,
                                 &state->effect) &&
           query_wordcraft_state(window, WCQ_DESIGN_GALLERY_VISIBLE, 0,
                                 &state->galleryVisible);
}

static BOOL design_state_matches(HWND window,
                                 const DesignStateSnapshot *expected)
{
    DesignStateSnapshot actual;

    return expected != NULL &&
           capture_design_state(window, &actual) &&
           actual.activeStyleSet == expected->activeStyleSet &&
           actual.colorScheme == expected->colorScheme &&
           actual.fontScheme == expected->fontScheme &&
           actual.paragraphSpacing == expected->paragraphSpacing &&
           actual.effect == expected->effect &&
           actual.galleryVisible == expected->galleryVisible;
}

static BOOL design_checkmark_matches(HWND window, UINT command,
                                     BOOL expectedChecked)
{
    size_t index;

    for (index = 0; index < ARRAYSIZE(expectedDesignControls); ++index) {
        LRESULT flags = 0;
        if (expectedDesignControls[index].id != command) {
            continue;
        }
        return query_wordcraft_state(window, WCQ_DESIGN_CONTROL_FLAGS,
                                     (LPARAM)index, &flags) &&
               ((((UINT)flags & DESIGN_CONTROL_FLAG_CHECKED) != 0) ==
                expectedChecked);
    }
    return FALSE;
}

static BOOL validate_design_active_selection(HWND window, HWND formatBar)
{
    HWND elegant;
    HWND office;
    LRESULT activeStyleSet = -1;

    if (window == NULL || formatBar == NULL ||
        (elegant = find_control(
             formatBar, IDM_DESIGN_STYLE_BASIC_ELEGANT)) == NULL ||
        (office = find_control(formatBar,
                               IDM_DESIGN_STYLE_OFFICE)) == NULL ||
        !send_message_bounded(
            window, WM_COMMAND,
            MAKEWPARAM(IDM_DESIGN_STYLE_BASIC_ELEGANT, BN_CLICKED),
            (LPARAM)elegant, NULL) ||
        !query_wordcraft_state(window, WCQ_DESIGN_ACTIVE_STYLE_SET, 0,
                               &activeStyleSet) ||
        activeStyleSet != DESIGN_STYLE_SET_BASIC_ELEGANT ||
        !design_checkmark_matches(
            window, IDM_DESIGN_STYLE_BASIC_ELEGANT, TRUE) ||
        !design_checkmark_matches(window, IDM_DESIGN_STYLE_OFFICE,
                                  FALSE) ||
        !send_message_bounded(
            window, WM_COMMAND,
            MAKEWPARAM(IDM_DESIGN_STYLE_OFFICE, BN_CLICKED),
            (LPARAM)office, NULL) ||
        !query_wordcraft_state(window, WCQ_DESIGN_ACTIVE_STYLE_SET, 0,
                               &activeStyleSet) ||
        activeStyleSet != DESIGN_STYLE_SET_OFFICE ||
        !design_checkmark_matches(window, IDM_DESIGN_STYLE_OFFICE,
                                  TRUE) ||
        !design_checkmark_matches(
            window, IDM_DESIGN_STYLE_BASIC_ELEGANT, FALSE)) {
        return FALSE;
    }
    return TRUE;
}

static BOOL validate_design_unsupported_nonmutating(
    HWND window, HWND editor, HWND formatBar)
{
    static const UINT commands[] = {
        IDM_DESIGN_SET_AS_DEFAULT,
        IDM_DESIGN_WATERMARK,
        IDM_DESIGN_PAGE_COLOR,
        IDM_DESIGN_PAGE_BORDERS
    };
    DrawDocumentSnapshot document;
    DesignStateSnapshot design;
    size_t index;

    if (window == NULL || editor == NULL || formatBar == NULL ||
        !capture_draw_document_snapshot(window, editor, &document) ||
        !capture_design_state(window, &design)) {
        return FALSE;
    }
    for (index = 0; index < ARRAYSIZE(commands); ++index) {
        HWND control = find_control(formatBar, (int)commands[index]);
        if (control == NULL ||
            !send_message_bounded(
                window, WM_COMMAND,
                MAKEWPARAM(commands[index], BN_CLICKED),
                (LPARAM)control, NULL) ||
            !draw_document_matches_snapshot(window, editor, &document) ||
            !design_state_matches(window, &design)) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL view_mode_is_valid(int mode)
{
    return mode >= VIEW_MODE_READ && mode <= VIEW_MODE_DRAFT;
}

static UINT view_command_for_mode(int mode)
{
    switch (mode) {
    case VIEW_MODE_READ:
        return IDM_VIEW_READ_MODE;
    case VIEW_MODE_PRINT_LAYOUT:
        return IDM_VIEW_PRINT_LAYOUT;
    case VIEW_MODE_WEB_LAYOUT:
        return IDM_VIEW_WEB_LAYOUT;
    case VIEW_MODE_OUTLINE:
        return IDM_VIEW_OUTLINE;
    case VIEW_MODE_DRAFT:
        return IDM_VIEW_DRAFT;
    default:
        return 0;
    }
}

static BOOL view_state_matches(HWND window, int expectedMode,
                               BOOL expectedVertical,
                               BOOL expectedRuler,
                               BOOL expectedGridlines,
                               BOOL expectedNavigation,
                               BOOL expectedSideBySide,
                               BOOL expectedFocus)
{
    LRESULT activeMode = 0;
    LRESULT vertical = 0;
    LRESULT rulerVisible = 0;
    LRESULT gridlinesVisible = 0;
    LRESULT navigationVisible = 0;
    LRESULT sideBySide = 0;
    LRESULT focusMode = 0;
    LRESULT darkMode = 0;
    size_t index;

    if (!view_mode_is_valid(expectedMode) ||
        !query_wordcraft_state(window, WCQ_VIEW_ACTIVE_MODE, 0,
                               &activeMode) ||
        !query_wordcraft_state(window, WCQ_VIEW_MOVEMENT_VERTICAL, 0,
                               &vertical) ||
        !query_wordcraft_state(window, WCQ_VIEW_RULER_VISIBLE, 0,
                               &rulerVisible) ||
        !query_wordcraft_state(window, WCQ_VIEW_GRIDLINES_VISIBLE, 0,
                               &gridlinesVisible) ||
        !query_wordcraft_state(window, WCQ_VIEW_NAVIGATION_VISIBLE, 0,
                               &navigationVisible) ||
        !query_wordcraft_state(window, WCQ_VIEW_SIDE_BY_SIDE, 0,
                               &sideBySide) ||
        !query_wordcraft_state(window, WCQ_VIEW_FOCUS_MODE, 0,
                               &focusMode) ||
        !query_wordcraft_state(window, WCQ_DARK_MODE, 0, &darkMode) ||
        activeMode != expectedMode ||
        (vertical != 0) != expectedVertical ||
        (rulerVisible != 0) != expectedRuler ||
        (gridlinesVisible != 0) != expectedGridlines ||
        (navigationVisible != 0) != expectedNavigation ||
        (sideBySide != 0) != expectedSideBySide ||
        (focusMode != 0) != expectedFocus || darkMode != 0) {
        return FALSE;
    }

    for (index = 0; index < ARRAYSIZE(expectedViewControls); ++index) {
        UINT id = expectedViewControls[index].id;
        BOOL expectedChecked =
            id == view_command_for_mode(expectedMode) ||
            (id == IDM_VIEW_VERTICAL && expectedVertical) ||
            (id == IDM_VIEW_SIDE_TO_SIDE && !expectedVertical) ||
            (id == IDM_VIEW_RULER && expectedRuler) ||
            (id == IDM_VIEW_GRIDLINES && expectedGridlines) ||
            (id == IDM_VIEW_NAVIGATION_PANE && expectedNavigation) ||
            (id == IDM_VIEW_SIDE_BY_SIDE && expectedSideBySide) ||
            (id == IDM_VIEW_FOCUS && expectedFocus);
        LRESULT flags = 0;

        if (!query_wordcraft_state(window, WCQ_VIEW_CONTROL_FLAGS,
                                   (LPARAM)index, &flags) ||
            ((((UINT)flags & VIEW_CONTROL_FLAG_CHECKED) != 0) !=
             expectedChecked)) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL validate_view_nonmutating_commands(HWND window, HWND editor)
{
    static const struct {
        UINT command;
        int expectedMode;
        BOOL vertical;
        BOOL ruler;
        BOOL gridlines;
        BOOL navigation;
        BOOL sideBySide;
        BOOL focus;
    } steps[] = {
        {IDM_VIEW_READ_MODE, VIEW_MODE_READ,
         TRUE, FALSE, FALSE, FALSE, FALSE, FALSE},
        {IDM_VIEW_PRINT_LAYOUT, VIEW_MODE_PRINT_LAYOUT,
         TRUE, FALSE, FALSE, FALSE, FALSE, FALSE},
        {IDM_VIEW_WEB_LAYOUT, VIEW_MODE_PRINT_LAYOUT,
         TRUE, FALSE, FALSE, FALSE, FALSE, FALSE},
        {IDM_VIEW_OUTLINE, VIEW_MODE_PRINT_LAYOUT,
         TRUE, FALSE, FALSE, FALSE, FALSE, FALSE},
        {IDM_VIEW_DRAFT, VIEW_MODE_PRINT_LAYOUT,
         TRUE, FALSE, FALSE, FALSE, FALSE, FALSE},
        {IDM_VIEW_SIDE_TO_SIDE, VIEW_MODE_PRINT_LAYOUT,
         TRUE, FALSE, FALSE, FALSE, FALSE, FALSE},
        {IDM_VIEW_VERTICAL, VIEW_MODE_PRINT_LAYOUT,
         TRUE, FALSE, FALSE, FALSE, FALSE, FALSE},
        {IDM_VIEW_RULER, VIEW_MODE_PRINT_LAYOUT,
         TRUE, TRUE, FALSE, FALSE, FALSE, FALSE},
        {IDM_VIEW_GRIDLINES, VIEW_MODE_PRINT_LAYOUT,
         TRUE, TRUE, TRUE, FALSE, FALSE, FALSE},
        {IDM_VIEW_RULER, VIEW_MODE_PRINT_LAYOUT,
         TRUE, FALSE, TRUE, FALSE, FALSE, FALSE},
        {IDM_VIEW_GRIDLINES, VIEW_MODE_PRINT_LAYOUT,
         TRUE, FALSE, FALSE, FALSE, FALSE, FALSE},
        {IDM_VIEW_FOCUS, VIEW_MODE_PRINT_LAYOUT,
         TRUE, FALSE, FALSE, FALSE, FALSE, TRUE}
    };
    static const UINT actionCommands[] = {
        IDM_VIEW_ONE_PAGE, IDM_VIEW_MULTIPLE_PAGES,
        IDM_VIEW_PAGE_WIDTH, IDM_VIEW_ZOOM_100
    };
    DrawDocumentSnapshot document;
    size_t index;

    if (!view_state_matches(window, VIEW_MODE_PRINT_LAYOUT, TRUE,
                            FALSE, FALSE, FALSE, FALSE, FALSE) ||
        !capture_draw_document_snapshot(window, editor, &document)) {
        return FALSE;
    }
    for (index = 0; index < ARRAYSIZE(steps); ++index) {
        if (!send_message_bounded(
                window, WM_COMMAND,
                MAKEWPARAM(steps[index].command, 0), 0, NULL) ||
            !view_state_matches(
                window, steps[index].expectedMode, steps[index].vertical,
                steps[index].ruler, steps[index].gridlines,
                steps[index].navigation, steps[index].sideBySide,
                steps[index].focus) ||
            !draw_document_matches_snapshot(window, editor, &document)) {
            return FALSE;
        }
    }
    if (!send_message_bounded(editor, WM_KEYDOWN, VK_ESCAPE, 0, NULL) ||
        !view_state_matches(window, VIEW_MODE_PRINT_LAYOUT, TRUE,
                            FALSE, FALSE, FALSE, FALSE, FALSE) ||
        !draw_document_matches_snapshot(window, editor, &document)) {
        return FALSE;
    }
    for (index = 0; index < ARRAYSIZE(actionCommands); ++index) {
        if (!send_message_bounded(
                window, WM_COMMAND,
                MAKEWPARAM(actionCommands[index], 0), 0, NULL) ||
            !view_state_matches(window, VIEW_MODE_PRINT_LAYOUT, TRUE,
                                FALSE, FALSE, FALSE, FALSE, FALSE) ||
            !draw_document_matches_snapshot(window, editor, &document)) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL wait_for_home_layout(HWND window, LRESULT previousGeneration,
                                 int expectedMode,
                                 LRESULT *currentGeneration)
{
    int attempt;

    for (attempt = 0; attempt < 100; ++attempt) {
        LRESULT generation = 0;
        LRESULT mode = 0;
        if (!query_wordcraft_state(window, WCQ_RIBBON_LAYOUT_GENERATION, 0,
                                   &generation) ||
            !query_wordcraft_state(window, WCQ_RIBBON_LAYOUT_MODE, 0,
                                   &mode)) {
            return FALSE;
        }
        if (generation != previousGeneration &&
            (expectedMode == 0 || mode == expectedMode)) {
            if (currentGeneration != NULL) {
                *currentGeneration = generation;
            }
            return TRUE;
        }
        Sleep(20);
    }
    return FALSE;
}

static BOOL resize_window_for_home_layout(HWND window, int logicalWidth,
                                          int expectedMode,
                                          LRESULT *currentGeneration)
{
    RECT client;
    RECT frame;
    HDC dc;
    int dpiX = 96;
    int dpiY = 96;
    int frameWidth;
    int frameHeight;
    int targetWidth;
    int targetHeight;
    LRESULT generation = 0;

    if (window == NULL || logicalWidth <= 0 ||
        !GetClientRect(window, &client) || !GetWindowRect(window, &frame) ||
        !query_wordcraft_state(window, WCQ_RIBBON_LAYOUT_GENERATION, 0,
                               &generation)) {
        return FALSE;
    }
    dc = GetDC(window);
    if (dc != NULL) {
        dpiX = max(1, GetDeviceCaps(dc, LOGPIXELSX));
        dpiY = max(1, GetDeviceCaps(dc, LOGPIXELSY));
        ReleaseDC(window, dc);
    }
    frameWidth = (frame.right - frame.left) - (client.right - client.left);
    frameHeight = (frame.bottom - frame.top) - (client.bottom - client.top);
    targetWidth = MulDiv(logicalWidth, dpiX, 96) + max(0, frameWidth);
    targetHeight = MulDiv(780, dpiY, 96) + max(0, frameHeight);
    if (!SetWindowPos(window, NULL, 0, 0, targetWidth, targetHeight,
                      SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        return FALSE;
    }
    return wait_for_home_layout(window, generation, expectedMode,
                                currentGeneration);
}

static BOOL restore_window_size(HWND window, const RECT *original)
{
    LRESULT generation = 0;

    if (window == NULL || original == NULL ||
        !query_wordcraft_state(window, WCQ_RIBBON_LAYOUT_GENERATION, 0,
                               &generation) ||
        !SetWindowPos(window, NULL, 0, 0,
                      original->right - original->left,
                      original->bottom - original->top,
                      SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        return FALSE;
    }
    return wait_for_home_layout(window, generation, 0, NULL);
}

static BOOL force_home_group_paint(HWND window, HWND formatBar)
{
    RECT client;
    LRESULT before = 0;
    LRESULT after = 0;
    HDC referenceDc = NULL;
    HDC memoryDc = NULL;
    HBITMAP bitmap = NULL;
    HGDIOBJ previousBitmap = NULL;
    BOOL success = FALSE;

    if (window == NULL || formatBar == NULL ||
        !GetClientRect(formatBar, &client) ||
        client.right <= client.left || client.bottom <= client.top ||
        !query_wordcraft_state(window, WCQ_HOME_GROUP_PAINT_COUNT, 0,
                               &before)) {
        return FALSE;
    }
    referenceDc = GetDC(formatBar);
    if (referenceDc == NULL) {
        goto cleanup;
    }
    memoryDc = CreateCompatibleDC(referenceDc);
    bitmap = CreateCompatibleBitmap(
        referenceDc, client.right - client.left,
        client.bottom - client.top);
    if (memoryDc == NULL || bitmap == NULL) {
        goto cleanup;
    }
    previousBitmap = SelectObject(memoryDc, bitmap);
    if (previousBitmap == NULL ||
        previousBitmap == (HGDIOBJ)HGDI_ERROR ||
        !send_message_bounded(formatBar, WM_PRINTCLIENT,
                              (WPARAM)memoryDc,
                              PRF_CLIENT | PRF_ERASEBKGND, NULL)) {
        goto cleanup;
    }
    GdiFlush();
    if (!query_wordcraft_state(window, WCQ_HOME_GROUP_PAINT_COUNT, 0,
                               &after)) {
        goto cleanup;
    }
    success = after > before;

cleanup:
    if (memoryDc != NULL && previousBitmap != NULL &&
        previousBitmap != (HGDIOBJ)HGDI_ERROR) {
        SelectObject(memoryDc, previousBitmap);
    }
    if (bitmap != NULL) {
        DeleteObject(bitmap);
    }
    if (memoryDc != NULL) {
        DeleteDC(memoryDc);
    }
    if (referenceDc != NULL) {
        ReleaseDC(formatBar, referenceDc);
    }
    if (!success) {
        fwprintf(stderr,
                 L"Home offscreen group paint telemetry did not advance (%lld -> %lld)\n",
                 (long long)before, (long long)after);
    }
    return success;
}

static BOOL first_enabled_home_tabstop(HWND window, LRESULT *controlId)
{
    LRESULT count = 0;
    LRESULT index;

    if (controlId == NULL ||
        !query_wordcraft_state(window, WCQ_HOME_CONTROL_COUNT, 0, &count)) {
        return FALSE;
    }
    for (index = 0; index < count; ++index) {
        LRESULT id = 0;
        LRESULT flags = 0;
        UINT required = HOME_CONTROL_FLAG_CREATED |
                        HOME_CONTROL_FLAG_VISIBLE |
                        HOME_CONTROL_FLAG_ENABLED |
                        HOME_CONTROL_FLAG_TABSTOP;
        if (!query_wordcraft_state(window, WCQ_HOME_CONTROL_ID, index, &id) ||
            !query_wordcraft_state(window, WCQ_HOME_CONTROL_FLAGS, index,
                                   &flags)) {
            return FALSE;
        }
        if (((UINT)flags & required) == required) {
            *controlId = id;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL wait_for_ribbon_control_focus(HWND window,
                                          LRESULT expectedControlId)
{
    int attempt;

    for (attempt = 0; attempt < 100; ++attempt) {
        LRESULT controlId = 0;
        if (!query_wordcraft_state(window,
                                   WCQ_RIBBON_FOCUSED_CONTROL_ID, 0,
                                   &controlId)) {
            return FALSE;
        }
        if (controlId == expectedControlId) {
            return TRUE;
        }
        Sleep(20);
    }
    return FALSE;
}

static BOOL focus_home_control(HWND window, HWND formatBar,
                               LRESULT targetControlId)
{
    HWND target;

    if (window == NULL || formatBar == NULL || targetControlId == 0) {
        return FALSE;
    }
    target = find_control(formatBar, (int)targetControlId);
    if (target == NULL ||
        !send_message_bounded(target, WM_LBUTTONDOWN, MK_LBUTTON,
                              MAKELPARAM(2, 2), NULL) ||
        !wait_for_ribbon_control_focus(window, targetControlId) ||
        !send_message_bounded(target, WM_CANCELMODE, 0, 0, NULL)) {
        return FALSE;
    }
    return wait_for_ribbon_control_focus(window, targetControlId);
}

static BOOL validate_collapsed_style_combo(HWND window, HWND formatBar,
                                           HWND styleCombo)
{
    RECT comboRect;
    RECT groupRect;
    HDC dc;
    int dpiX = 96;
    int minimumDropWidth;
    LRESULT count = 0;
    LRESULT droppedWidth = 0;

    if (window == NULL || formatBar == NULL || styleCombo == NULL ||
        (GetWindowLongPtrW(styleCombo, GWL_STYLE) & WS_VISIBLE) == 0 ||
        !GetWindowRect(styleCombo, &comboRect) ||
        !query_home_group_rect(window, HOME_GROUP_STYLES, &groupRect) ||
        !send_message_bounded(styleCombo, CB_GETCOUNT, 0, 0, &count) ||
        !send_message_bounded(styleCombo, CB_GETDROPPEDWIDTH, 0, 0,
                              &droppedWidth) ||
        count != WORDCRAFT_STYLE_COUNT) {
        return FALSE;
    }
    MapWindowPoints(HWND_DESKTOP, formatBar, (POINT *)&comboRect, 2);
    if (comboRect.right <= comboRect.left ||
        comboRect.bottom <= comboRect.top ||
        comboRect.left < groupRect.left ||
        comboRect.right > groupRect.right ||
        comboRect.top < groupRect.top ||
        comboRect.bottom > groupRect.bottom) {
        return FALSE;
    }
    dc = GetDC(formatBar);
    if (dc != NULL) {
        dpiX = max(1, GetDeviceCaps(dc, LOGPIXELSX));
        ReleaseDC(formatBar, dc);
    }
    minimumDropWidth = MulDiv(180, dpiX, 96);
    return droppedWidth >= minimumDropWidth &&
           droppedWidth >= comboRect.right - comboRect.left;
}

static void expected_next_line_spacing(const PARAFORMAT2 *current,
                                       BYTE *rule, LONG *spacing)
{
    static const BYTE rules[] = {0, 5, 1, 2};
    static const LONG spacings[] = {0, 23, 0, 0};
    size_t currentIndex = 0;

    if (current != NULL && (current->dwMask & PFM_LINESPACING) != 0) {
        for (currentIndex = 0; currentIndex < ARRAYSIZE(rules);
             ++currentIndex) {
            if (current->bLineSpacingRule == rules[currentIndex] &&
                (rules[currentIndex] != 5 ||
                 current->dyLineSpacing == spacings[currentIndex])) {
                break;
            }
        }
    }
    if (currentIndex >= ARRAYSIZE(rules)) {
        currentIndex = 0;
    }
    currentIndex = (currentIndex + 1) % ARRAYSIZE(rules);
    *rule = rules[currentIndex];
    *spacing = spacings[currentIndex];
}

static BOOL ribbon_state_matches(HWND window, int expectedTab)
{
    LRESULT count = 0;
    LRESULT selected = -1;
    LRESULT visible = -1;
    int tab;

    if (!query_wordcraft_state(window, WCQ_RIBBON_TAB_COUNT, 0, &count) ||
        !query_wordcraft_state(window, WCQ_RIBBON_ACTIVE_TAB, 0, &selected) ||
        !query_wordcraft_state(window, WCQ_RIBBON_VISIBLE_PANEL, 0, &visible) ||
        count != RIBBON_TAB_COUNT || selected != expectedTab ||
        visible != expectedTab) {
        return FALSE;
    }
    for (tab = 0; tab < RIBBON_TAB_COUNT; ++tab) {
        LRESULT panelVisible = 0;
        if (!query_wordcraft_state(window, WCQ_RIBBON_PANEL_VISIBLE, tab,
                                   &panelVisible) ||
            ((panelVisible != 0) != (tab == expectedTab))) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL wait_for_ribbon_state(HWND window, int expectedTab)
{
    int attempt;

    for (attempt = 0; attempt < 100; ++attempt) {
        if (ribbon_state_matches(window, expectedTab)) {
            return TRUE;
        }
        Sleep(20);
    }
    return FALSE;
}

static BOOL wait_for_ribbon_focus(HWND window, LRESULT expectedArea)
{
    int attempt;

    for (attempt = 0; attempt < 100; ++attempt) {
        LRESULT area = RIBBON_FOCUS_OTHER;
        if (!query_wordcraft_state(window, WCQ_RIBBON_FOCUS_AREA, 0,
                                   &area)) {
            return FALSE;
        }
        if (area == expectedArea) {
            return TRUE;
        }
        Sleep(20);
    }
    return FALSE;
}

static BOOL comment_matches(HWND window, LPARAM index, LONG expectedStart,
                            LONG expectedEnd, const WCHAR *expectedText)
{
    LRESULT start = -1;
    LRESULT end = -1;
    LRESULT hash = 0;

    return query_wordcraft_state(window, WCQ_COMMENT_ANCHOR_START, index,
                                 &start) &&
           query_wordcraft_state(window, WCQ_COMMENT_ANCHOR_END, index,
                                 &end) &&
           query_wordcraft_state(window, WCQ_COMMENT_TEXT_HASH, index,
                                 &hash) &&
           start == expectedStart && end == expectedEnd &&
           (UINT)(DWORD_PTR)hash == probe_text_hash(expectedText);
}

static BOOL get_selection_bounded(HWND editor, DWORD *start, DWORD *end)
{
    LRESULT packedRange = 0;

    if (start == NULL || end == NULL ||
        !send_message_bounded(editor, EM_GETSEL, 0, 0, &packedRange)) {
        return FALSE;
    }
    /* EM_GETSEL pointers are process-local. The probe keeps its documents
     * below 64K so the cross-process-safe packed return value is sufficient. */
    *start = LOWORD((DWORD_PTR)packedRange);
    *end = HIWORD((DWORD_PTR)packedRange);
    return TRUE;
}

static BOOL wait_for_page_view(HWND window, LONG requiredPage,
                               LRESULT minimumFullyVisible,
                               BOOL requireBottom, LRESULT *firstPage,
                               LRESULT *lastPage, LRESULT *scrollY)
{
    int attempt;

    for (attempt = 0; attempt < 120; ++attempt) {
        LRESULT first = 0;
        LRESULT last = 0;
        LRESULT full = 0;
        LRESULT currentScroll = 0;
        LRESULT maximumScroll = 0;
        if (!query_wordcraft_state(window, WCQ_FIRST_VISIBLE_PAGE, 0,
                                   &first) ||
            !query_wordcraft_state(window, WCQ_LAST_VISIBLE_PAGE, 0,
                                   &last) ||
            !query_wordcraft_state(window, WCQ_FULLY_VISIBLE_PAGE_COUNT, 0,
                                   &full) ||
            !query_wordcraft_state(window, WCQ_VIEW_SCROLL_Y, 0,
                                   &currentScroll) ||
            !query_wordcraft_state(window, WCQ_VIEW_SCROLL_MAX, 0,
                                   &maximumScroll)) {
            return FALSE;
        }
        if (firstPage != NULL) {
            *firstPage = first;
        }
        if (lastPage != NULL) {
            *lastPage = last;
        }
        if (scrollY != NULL) {
            *scrollY = currentScroll;
        }
        if (first > 0 && last >= first && full >= minimumFullyVisible &&
            (requiredPage <= 0 ||
             (first <= requiredPage && requiredPage <= last)) &&
            (!requireBottom || currentScroll == maximumScroll)) {
            return TRUE;
        }
        Sleep(50);
    }
    return FALSE;
}

static BOOL get_vertical_scroll_state(HWND pageView, LRESULT *position,
                                      LRESULT *maximum)
{
    SCROLLINFO info;
    int scrollMaximum;

    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = SIF_ALL;
    if (!GetScrollInfo(pageView, SB_VERT, &info)) {
        return FALSE;
    }
    scrollMaximum = info.nMax - (int)info.nPage + 1;
    if (scrollMaximum < info.nMin) {
        scrollMaximum = info.nMin;
    }
    if (position != NULL) {
        *position = info.nPos;
    }
    if (maximum != NULL) {
        *maximum = scrollMaximum;
    }
    return TRUE;
}

static BOOL wait_for_smooth_scroll_settle(
    HWND window, HWND pageView, LRESULT initialPosition,
    LRESULT expectedTarget, int direction, LRESULT *finalFrameCount,
    int *distinctPositions)
{
    LRESULT previousPosition = initialPosition;
    int distinct = 0;
    int attempt;

    /* The attempt limit is only a hang watchdog. Pass/fail pacing comes from
     * the animation's deterministic frame counter and nominal interval, not
     * from elapsed wall-clock time on a potentially busy CI machine. */
    for (attempt = 0; attempt < 1000; ++attempt) {
        LRESULT animating = 0;
        LRESULT target = 0;
        LRESULT frameCount = 0;
        LRESULT position = 0;
        LRESULT maximum = 0;

        if (!query_wordcraft_state(window, WCQ_SCROLL_ANIMATING, 0,
                                   &animating) ||
            !query_wordcraft_state(window, WCQ_SCROLL_TARGET_Y, 0,
                                   &target) ||
            !query_wordcraft_state(window, WCQ_SCROLL_FRAME_COUNT, 0,
                                   &frameCount) ||
            !get_vertical_scroll_state(pageView, &position, &maximum) ||
            target != expectedTarget || position < 0 ||
            position > maximum ||
            (direction > 0 && position < previousPosition) ||
            (direction < 0 && position > previousPosition)) {
            return FALSE;
        }
        if (position != previousPosition) {
            ++distinct;
        }
        previousPosition = position;
        if (!animating) {
            if (position != expectedTarget) {
                return FALSE;
            }
            if (finalFrameCount != NULL) {
                *finalFrameCount = frameCount;
            }
            if (distinctPositions != NULL) {
                *distinctPositions = distinct;
            }
            return TRUE;
        }
        Sleep(5);
    }
    return FALSE;
}

static BOOL wait_for_assistance(HWND window, LRESULT minimumSpellErrors,
                                BOOL requireCompletion,
                                LRESULT *completionLength)
{
    int attempt;

    for (attempt = 0; attempt < 120; ++attempt) {
        LRESULT spellErrors = 0;
        LRESULT spellReady = 0;
        LRESULT completionVisible = 0;
        LRESULT length = 0;
        if (!query_wordcraft_state(window, WCQ_SPELL_ERROR_COUNT, 0,
                                   &spellErrors) ||
            !query_wordcraft_state(window, WCQ_COMPLETION_VISIBLE, 0,
                                   &completionVisible) ||
            !query_wordcraft_state(window, WCQ_COMPLETION_LENGTH, 0,
                                   &length) ||
            !query_wordcraft_state(window, WCQ_SPELL_RESULT_READY, 0,
                                   &spellReady)) {
            return FALSE;
        }
        if (spellReady != 0 && spellErrors >= minimumSpellErrors &&
            (!requireCompletion || completionVisible != 0) &&
            (!requireCompletion || length > 0)) {
            if (completionLength != NULL) {
                *completionLength = length;
            }
            return TRUE;
        }
        Sleep(50);
    }
    return FALSE;
}

static BOOL wait_for_window_text(HWND window, const WCHAR *expected)
{
    WCHAR text[256];
    int attempt;

    for (attempt = 0; attempt < 100; ++attempt) {
        if (!get_window_text_bounded(window, text, ARRAYSIZE(text))) {
            return FALSE;
        }
        if (wcscmp(text, expected) == 0) {
            return TRUE;
        }
        Sleep(50);
    }
    return FALSE;
}

static BOOL wait_for_completion_visibility(HWND window, BOOL visible)
{
    int attempt;

    for (attempt = 0; attempt < 100; ++attempt) {
        LRESULT current = 0;
        if (!query_wordcraft_state(window, WCQ_COMPLETION_VISIBLE, 0,
                                   &current)) {
            return FALSE;
        }
        if ((current != 0) == visible) {
            return TRUE;
        }
        Sleep(50);
    }
    return FALSE;
}

static BOOL wait_for_comment_margin(HWND window, LRESULT expectedCount,
                                    LRESULT expectedActive,
                                    BOOL requireActiveVisible,
                                    LRESULT *marginLeft,
                                    LRESULT *marginWidth)
{
    int attempt;

    for (attempt = 0; attempt < 100; ++attempt) {
        LRESULT visible = 0;
        LRESULT cardCount = 0;
        LRESULT active = -1;
        LRESULT activeVisible = 0;
        LRESULT left = 0;
        LRESULT width = 0;

        if (!query_wordcraft_state(window, WCQ_COMMENT_MARGIN_VISIBLE, 0,
                                   &visible) ||
            !query_wordcraft_state(window, WCQ_COMMENT_CARD_COUNT, 0,
                                   &cardCount) ||
            !query_wordcraft_state(window, WCQ_COMMENT_MARGIN_ACTIVE_INDEX,
                                   0, &active) ||
            !query_wordcraft_state(window,
                                   WCQ_COMMENT_ACTIVE_CARD_VISIBLE, 0,
                                   &activeVisible) ||
            !query_wordcraft_state(window, WCQ_COMMENT_MARGIN_LEFT, 0,
                                   &left) ||
            !query_wordcraft_state(window, WCQ_COMMENT_MARGIN_WIDTH, 0,
                                   &width)) {
            return FALSE;
        }
        if (visible != 0 && cardCount == expectedCount &&
            active == expectedActive && left > 0 && width > 0 &&
            (!requireActiveVisible || activeVisible != 0)) {
            if (marginLeft != NULL) {
                *marginLeft = left;
            }
            if (marginWidth != NULL) {
                *marginWidth = width;
            }
            return TRUE;
        }
        Sleep(20);
    }
    return FALSE;
}

static BOOL move_ribbon_to_tab(HWND window, HWND ribbonTabs,
                               int targetTab)
{
    LRESULT activeTab = -1;
    int guard;

    if (ribbonTabs == NULL || targetTab < 0 ||
        targetTab >= RIBBON_TAB_COUNT ||
        !query_wordcraft_state(window, WCQ_RIBBON_ACTIVE_TAB, 0,
                               &activeTab)) {
        return FALSE;
    }
    SetFocus(ribbonTabs);
    for (guard = 0; guard < RIBBON_TAB_COUNT && activeTab != targetTab;
         ++guard) {
        WPARAM key = activeTab < targetTab ? VK_RIGHT : VK_LEFT;
        if (!send_message_bounded(ribbonTabs, WM_KEYDOWN, key, 0, NULL) ||
            !query_wordcraft_state(window, WCQ_RIBBON_ACTIVE_TAB, 0,
                                   &activeTab)) {
            return FALSE;
        }
    }
    return activeTab == targetTab &&
           wait_for_ribbon_state(window, targetTab);
}

static BOOL focus_editor_from_ribbon(HWND window)
{
    int attempt;

    for (attempt = 0; attempt < 4; ++attempt) {
        LRESULT focusArea = RIBBON_FOCUS_OTHER;

        if (!query_wordcraft_state(window, WCQ_RIBBON_FOCUS_AREA, 0,
                                   &focusArea)) {
            return FALSE;
        }
        if (focusArea == RIBBON_FOCUS_EDITOR) {
            return TRUE;
        }
        if (!send_message_bounded(window, WM_COMMAND,
                                  MAKEWPARAM(IDM_RIBBON_FOCUS, 0), 0,
                                  NULL)) {
            return FALSE;
        }
    }
    return wait_for_ribbon_focus(window, RIBBON_FOCUS_EDITOR);
}

static BOOL focus_tabs_from_ribbon(HWND window)
{
    int attempt;

    for (attempt = 0; attempt < 4; ++attempt) {
        LRESULT focusArea = RIBBON_FOCUS_OTHER;

        if (!query_wordcraft_state(window, WCQ_RIBBON_FOCUS_AREA, 0,
                                   &focusArea)) {
            return FALSE;
        }
        if (focusArea == RIBBON_FOCUS_TABS) {
            return TRUE;
        }
        if (!send_message_bounded(window, WM_COMMAND,
                                  MAKEWPARAM(IDM_RIBBON_FOCUS, 0), 0,
                                  NULL)) {
            return FALSE;
        }
    }
    return wait_for_ribbon_focus(window, RIBBON_FOCUS_TABS);
}

static BOOL insert_undo_restores_text(HWND window, HWND editor,
                                      const WCHAR *before,
                                      LRESULT expectedPageCount)
{
    WCHAR restored[512];
    LRESULT pageCount = 0;
    LRESULT canUndo = 1;

    return send_message_bounded(window, WM_COMMAND,
                                MAKEWPARAM(IDM_EDIT_UNDO, 0), 0, NULL) &&
           get_window_text_bounded(editor, restored,
                                   ARRAYSIZE(restored)) &&
           wcscmp(restored, before) == 0 &&
           query_wordcraft_state(window, WCQ_PAGE_COUNT, 0,
                                 &pageCount) &&
           pageCount == expectedPageCount &&
           send_message_bounded(editor, EM_CANUNDO, 0, 0, &canUndo) &&
           canUndo == 0 &&
           send_message_bounded(editor, EM_SETMODIFY, FALSE, 0, NULL);
}

static BOOL validate_insert_page_command(HWND window, HWND editor,
                                         UINT command,
                                         BOOL requireSingleBreak)
{
    WCHAR before[512];
    WCHAR after[512];
    LRESULT beforePages = 0;
    LRESULT afterPages = 0;
    LRESULT modified = 0;
    size_t beforeLength;
    size_t afterLength;
    size_t index;
    size_t breakCount = 0;

    if (window == NULL || editor == NULL ||
        (command != IDM_INSERT_PAGE_BREAK &&
         command != IDM_INSERT_BLANK_PAGE) ||
        !get_window_text_bounded(editor, before, ARRAYSIZE(before)) ||
        !query_wordcraft_state(window, WCQ_PAGE_COUNT, 0, &beforePages)) {
        return FALSE;
    }
    beforeLength = wcslen(before);
    if (beforeLength + 3 >= ARRAYSIZE(after) ||
        !send_message_bounded(editor, EM_EMPTYUNDOBUFFER, 0, 0, NULL) ||
        !send_message_bounded(editor, EM_SETMODIFY, FALSE, 0, NULL) ||
        !send_message_bounded(editor, EM_SETSEL, beforeLength,
                              beforeLength, NULL) ||
        !send_message_bounded(window, WM_COMMAND,
                              MAKEWPARAM(command, 0), 0, NULL) ||
        !get_window_text_bounded(editor, after, ARRAYSIZE(after)) ||
        !query_wordcraft_state(window, WCQ_PAGE_COUNT, 0, &afterPages) ||
        !send_message_bounded(editor, EM_GETMODIFY, 0, 0, &modified)) {
        fwprintf(stderr,
                 L"Insert page command %u setup/dispatch/query failed (error=%lu)\n",
                 command, (unsigned long)GetLastError());
        return FALSE;
    }
    afterLength = wcslen(after);
    if (afterLength <= beforeLength ||
        wcsncmp(after, before, beforeLength) != 0 ||
        afterPages <= beforePages || modified == 0) {
        fwprintf(stderr,
                 L"Insert page command %u state mismatch (length %lu->%lu pages %ld->%ld modified=%ld prefix=%d)\n",
                 command, (unsigned long)beforeLength,
                 (unsigned long)afterLength, (long)beforePages,
                 (long)afterPages, (long)modified,
                 wcsncmp(after, before, min(beforeLength, afterLength)));
        return FALSE;
    }
    for (index = beforeLength; index < afterLength; ++index) {
        if (after[index] == L'\f') {
            ++breakCount;
        }
    }
    if (breakCount == 0 ||
        (requireSingleBreak &&
         (afterLength != beforeLength + 1 || breakCount != 1))) {
        fwprintf(stderr,
                 L"Insert page command %u break mismatch (inserted=%lu formfeeds=%lu)\n",
                 command, (unsigned long)(afterLength - beforeLength),
                 (unsigned long)breakCount);
        return FALSE;
    }
    if (!insert_undo_restores_text(window, editor, before, beforePages)) {
        fwprintf(stderr,
                 L"Insert page command %u did not undo atomically\n",
                 command);
        return FALSE;
    }
    return TRUE;
}

static BOOL validate_insert_datetime_command(HWND window, HWND editor)
{
    WCHAR before[512];
    WCHAR after[512];
    LRESULT beforePages = 0;
    LRESULT modified = 0;
    size_t beforeLength;
    size_t afterLength;
    size_t insertion = 4;
    size_t insertedLength;

    if (window == NULL || editor == NULL ||
        !get_window_text_bounded(editor, before, ARRAYSIZE(before)) ||
        !query_wordcraft_state(window, WCQ_PAGE_COUNT, 0, &beforePages)) {
        return FALSE;
    }
    beforeLength = wcslen(before);
    if (beforeLength < insertion ||
        !send_message_bounded(editor, EM_EMPTYUNDOBUFFER, 0, 0, NULL) ||
        !send_message_bounded(editor, EM_SETMODIFY, FALSE, 0, NULL) ||
        !send_message_bounded(editor, EM_SETSEL, insertion, insertion,
                              NULL) ||
        !send_message_bounded(window, WM_COMMAND,
                              MAKEWPARAM(IDM_INSERT_DATETIME, 0), 0,
                              NULL) ||
        !get_window_text_bounded(editor, after, ARRAYSIZE(after)) ||
        !send_message_bounded(editor, EM_GETMODIFY, 0, 0, &modified)) {
        return FALSE;
    }
    afterLength = wcslen(after);
    if (afterLength <= beforeLength || modified == 0) {
        return FALSE;
    }
    insertedLength = afterLength - beforeLength;
    if (wcsncmp(after, before, insertion) != 0 ||
        wcscmp(after + insertion + insertedLength,
               before + insertion) != 0) {
        return FALSE;
    }
    return insert_undo_restores_text(window, editor, before, beforePages);
}

static BOOL paper_size_state_matches(HWND window, HWND editor,
                                     HWND paperSizeCombo,
                                     PaperSizeId expectedId,
                                     LONG expectedWidth,
                                     LONG expectedHeight,
                                     const LRESULT expectedMargins[4],
                                     LRESULT expectedModified)
{
    LRESULT selected = CB_ERR;
    LRESULT paperId = -1;
    LRESULT modified = -1;
    LRESULT storedSize[2] = {0};
    LRESULT effectiveSize[2] = {0};
    LRESULT storedMargin[4] = {0};
    LRESULT effectiveMargin[4] = {0};
    HDC editorDc;
    int dpiX;
    int dpiY;
    LONG toleranceX;
    LONG toleranceY;
    int component;
    BOOL matches;

    if (window == NULL || editor == NULL || paperSizeCombo == NULL ||
        expectedMargins == NULL ||
        !send_message_bounded(paperSizeCombo, CB_GETCURSEL, 0, 0,
                              &selected) ||
        !query_wordcraft_state(window, WCQ_PAPER_SIZE_ID, 0, &paperId) ||
        !send_message_bounded(editor, EM_GETMODIFY, 0, 0, &modified)) {
        return FALSE;
    }
    for (component = 0; component < 2; ++component) {
        if (!query_wordcraft_state(window, WCQ_PAGE_SIZE_THOUSANDTHS,
                                   component, &storedSize[component]) ||
            !query_wordcraft_state(window, WCQ_PAGE_LAYOUT_SIZE_PIXELS,
                                   component, &effectiveSize[component])) {
            return FALSE;
        }
    }
    for (component = 0; component < 4; ++component) {
        if (!query_wordcraft_state(window, WCQ_PAGE_MARGIN_THOUSANDTHS,
                                   component, &storedMargin[component]) ||
            !query_wordcraft_state(window, WCQ_PAGE_LAYOUT_MARGIN_PIXELS,
                                   component, &effectiveMargin[component])) {
            return FALSE;
        }
    }

    editorDc = GetDC(editor);
    if (editorDc == NULL) {
        return FALSE;
    }
    dpiX = max(1, GetDeviceCaps(editorDc, LOGPIXELSX));
    dpiY = max(1, GetDeviceCaps(editorDc, LOGPIXELSY));
    ReleaseDC(editor, editorDc);
    toleranceX = max(2, dpiX / 48);
    toleranceY = max(2, dpiY / 48);

    matches = selected == (LRESULT)expectedId &&
              paperId == (LRESULT)expectedId &&
              modified == expectedModified &&
              storedSize[0] == expectedWidth &&
              storedSize[1] == expectedHeight &&
              scalar_within_tolerance(
                  effectiveSize[0], MulDiv(expectedWidth, dpiX, 1000),
                  toleranceX) &&
              scalar_within_tolerance(
                  effectiveSize[1], MulDiv(expectedHeight, dpiY, 1000),
                  toleranceY);
    for (component = 0; component < 4; ++component) {
        int dpi = component == 0 || component == 2 ? dpiX : dpiY;
        LONG tolerance = component == 0 || component == 2
                             ? toleranceX
                             : toleranceY;
        matches = matches &&
                  storedMargin[component] == expectedMargins[component] &&
                  scalar_within_tolerance(
                      effectiveMargin[component],
                      MulDiv((LONG)expectedMargins[component], dpi, 1000),
                      tolerance);
    }
    return matches;
}

static BOOL select_paper_size_and_wait(HWND window, HWND editor,
                                       HWND paperSizeCombo,
                                       PaperSizeId id, LONG width,
                                       LONG height,
                                       const LRESULT expectedMargins[4],
                                       LRESULT expectedModified)
{
    LRESULT selection = CB_ERR;
    int attempt;

    if (!send_message_bounded(paperSizeCombo, CB_SETCURSEL, (WPARAM)id, 0,
                              &selection) ||
        selection != (LRESULT)id ||
        !send_message_bounded(
            window, WM_COMMAND,
            MAKEWPARAM(IDC_PAPER_SIZE_COMBO, CBN_SELENDOK),
            (LPARAM)paperSizeCombo, NULL)) {
        return FALSE;
    }
    for (attempt = 0; attempt < 100; ++attempt) {
        if (paper_size_state_matches(window, editor, paperSizeCombo, id,
                                     width, height, expectedMargins,
                                     expectedModified)) {
            return TRUE;
        }
        Sleep(20);
    }
    return FALSE;
}

static BOOL menu_item_is_checked(HMENU menu, UINT command, BOOL checked)
{
    UINT state;

    if (menu == NULL) {
        return FALSE;
    }
    state = GetMenuState(menu, command, MF_BYCOMMAND);
    return state != (UINT)-1 &&
           (((state & MF_CHECKED) != 0) == checked);
}

static BOOL menu_alignment_matches(HMENU menu, UINT expectedCommand)
{
    static const UINT commands[] = {
        IDM_FORMAT_ALIGN_LEFT,
        IDM_FORMAT_ALIGN_CENTER,
        IDM_FORMAT_ALIGN_RIGHT,
        IDM_FORMAT_ALIGN_JUSTIFY
    };
    size_t index;

    for (index = 0; index < ARRAYSIZE(commands); ++index) {
        if (!menu_item_is_checked(menu, commands[index],
                                  commands[index] == expectedCommand)) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL click_alignment_button(HWND button, HMENU menu,
                                   UINT expectedCommand)
{
    int attempt;

    if (button == NULL ||
        !send_message_bounded(button, BM_CLICK, 0, 0, NULL)) {
        return FALSE;
    }
    for (attempt = 0; attempt < 100; ++attempt) {
        if (menu_alignment_matches(menu, expectedCommand)) {
            return TRUE;
        }
        Sleep(20);
    }
    return FALSE;
}

static BOOL click_bullets_button(HWND button, HMENU menu, BOOL expectedChecked)
{
    int attempt;

    if (button == NULL ||
        !send_message_bounded(button, BM_CLICK, 0, 0, NULL)) {
        return FALSE;
    }
    for (attempt = 0; attempt < 100; ++attempt) {
        if (menu_item_is_checked(menu, IDM_FORMAT_BULLETS,
                                 expectedChecked)) {
            return TRUE;
        }
        Sleep(20);
    }
    return FALSE;
}

static BOOL editor_state_matches(HWND window, HWND editor,
                                 const WCHAR *expectedText,
                                 const WCHAR *expectedTitle,
                                 LRESULT expectedModified)
{
    WCHAR text[128];
    WCHAR title[128];
    LRESULT modified = 0;

    ZeroMemory(text, sizeof(text));
    ZeroMemory(title, sizeof(title));
    return get_window_text_bounded(editor, text, ARRAYSIZE(text)) &&
           get_window_text_bounded(window, title, ARRAYSIZE(title)) &&
           send_message_bounded(editor, EM_GETMODIFY, 0, 0, &modified) &&
           wcscmp(text, expectedText) == 0 &&
           wcscmp(title, expectedTitle) == 0 &&
           modified == expectedModified;
}

static BOOL close_wordcraft_cleanly(HWND window, PROCESS_INFORMATION *process)
{
    DWORD exitCode = 1;
    DWORD waitResult;

    if (!send_message_bounded(window, WM_CLOSE, 0, 0, NULL)) {
        return FALSE;
    }
    waitResult = WaitForSingleObject(process->hProcess, 5000);
    if (waitResult != WAIT_OBJECT_0 ||
        !GetExitCodeProcess(process->hProcess, &exitCode)) {
        return FALSE;
    }
    CloseHandle(process->hProcess);
    process->hProcess = NULL;
    return exitCode == 0;
}

static void stop_process(PROCESS_INFORMATION *process)
{
    DWORD exitCode = 0;
    if (process->hProcess == NULL) {
        return;
    }
    if (GetExitCodeProcess(process->hProcess, &exitCode) &&
        exitCode == STILL_ACTIVE) {
        TerminateProcess(process->hProcess, 99);
        WaitForSingleObject(process->hProcess, 5000);
    }
    CloseHandle(process->hProcess);
    process->hProcess = NULL;
}

static BOOL resolve_test_app(const WCHAR *fallback, WCHAR *executable,
                             DWORD capacity)
{
    WCHAR configured[PATH_CAPACITY];
    const WCHAR *candidate = fallback;
    DWORD length;
    DWORD error;

    SetLastError(ERROR_SUCCESS);
    length = GetEnvironmentVariableW(L"WORDCRAFT_TEST_APP", configured,
                                     ARRAYSIZE(configured));
    error = GetLastError();
    if (length == 0) {
        if (error != ERROR_SUCCESS && error != ERROR_ENVVAR_NOT_FOUND) {
            return FALSE;
        }
    } else {
        if (length >= ARRAYSIZE(configured)) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        candidate = configured;
    }

    length = GetFullPathNameW(candidate, capacity, executable, NULL);
    if (length == 0 || length >= capacity) {
        if (length >= capacity) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
        }
        return FALSE;
    }
    return TRUE;
}

static BOOL create_temp_probe_path(const WCHAR *extension, WCHAR *path,
                                   size_t capacity)
{
    WCHAR tempDirectory[PATH_CAPACITY];
    WCHAR reservedPath[PATH_CAPACITY];
    DWORD directoryLength;
    int attempt;

    if (extension == NULL || extension[0] != L'.' ||
        path == NULL || capacity == 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    path[0] = L'\0';
    directoryLength = GetTempPathW(ARRAYSIZE(tempDirectory), tempDirectory);
    if (directoryLength == 0 ||
        directoryLength >= ARRAYSIZE(tempDirectory)) {
        if (directoryLength >= ARRAYSIZE(tempDirectory)) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
        }
        return FALSE;
    }

    for (attempt = 0; attempt < 16; ++attempt) {
        WCHAR *extensionStart;
        DWORD moveError;

        if (GetTempFileNameW(tempDirectory, L"WCP", 0, reservedPath) == 0) {
            return FALSE;
        }
        extensionStart = wcsrchr(reservedPath, L'.');
        if (extensionStart == NULL ||
            FAILED(StringCchPrintfW(path, capacity, L"%.*s%s",
                                    (int)(extensionStart - reservedPath),
                                    reservedPath, extension))) {
            DeleteFileW(reservedPath);
            path[0] = L'\0';
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        if (MoveFileExW(reservedPath, path, MOVEFILE_WRITE_THROUGH)) {
            return TRUE;
        }
        moveError = GetLastError();
        DeleteFileW(reservedPath);
        path[0] = L'\0';
        if (moveError != ERROR_ALREADY_EXISTS &&
            moveError != ERROR_FILE_EXISTS) {
            SetLastError(moveError);
            return FALSE;
        }
    }

    SetLastError(ERROR_FILE_EXISTS);
    return FALSE;
}

int wmain(void)
{
    WCHAR executable[PATH_CAPACITY];
    WCHAR sample[PATH_CAPACITY];
    WCHAR longSample[PATH_CAPACITY];
    PROCESS_INFORMATION process;
    PROCESS_INFORMATION longProcess;
    HWND window = NULL;
    HWND longWindow = NULL;
    HWND editor;
    HWND longEditor;
    HWND longPageView;
    HWND longRibbonTabs;
    HWND ribbonTabs;
    HWND formatBar;
    HWND paperSizeCombo;
    HWND commentEdit;
    HWND fontCombo;
    HWND sizeCombo;
    HWND alignLeftButton;
    HWND alignCenterButton;
    HWND alignRightButton;
    HWND alignJustifyButton;
    HWND bulletsButton;
    HMENU menu;
    HMENU longMenu;
    WCHAR text[128];
    WCHAR fontText[LF_FACESIZE];
    WCHAR sizeText[32];
    WCHAR originalText[128];
    WCHAR originalTitle[128];
    PARAFORMAT2 paragraph;
    TextEngineSnapshot initialTextEngine;
    TextEngineSnapshot currentTextEngine;
    LRESULT originalModified = 0;
    LRESULT paperModified = 0;
    LRESULT paperItemCount = 0;
    LRESULT paperPresetCount = 0;
    LRESULT pageCount = 0;
    LRESULT currentPage = 0;
    LRESULT nativePage = 0;
    LRESULT secondPageStart = 0;
    LRESULT darkMode = 0;
    LRESULT previousStart = -1;
    LRESULT longTextLength = 0;
    LRESULT longModified = 0;
    LRESULT viewKind = 0;
    LRESULT firstVisiblePage = 0;
    LRESULT lastVisiblePage = 0;
    LRESULT visiblePageCount = 0;
    LRESULT fullyVisiblePageCount = 0;
    LRESULT viewScrollY = 0;
    LRESULT viewScrollMax = 0;
    DWORD selectionStart = 0;
    DWORD selectionEnd = 0;
    LRESULT workerRunning = 0;
    LRESULT completionLength = 0;
    LRESULT ribbonCount = 0;
    LRESULT ribbonHash = 0;
    LRESULT commentCount = 0;
    LRESULT chatCount = 0;
    LRESULT versionCount = 0;
    LRESULT activeComment = -1;
    LRESULT commentMarginVisible = 0;
    LRESULT commentCardCount = 0;
    LRESULT commentMarginLeft = 0;
    LRESULT commentMarginWidth = 0;
    LRESULT commentCardLeft = 0;
    LRESULT commentCardTop = 0;
    LRESULT commentCardRight = 0;
    LRESULT commentCardBottom = 0;
    LRESULT commentHighlightVisible = 0;
    LRESULT commentHighlightStart = -1;
    LRESULT commentHighlightEnd = -1;
    LRESULT commentHighlightColor = 0;
    LRESULT commentCompositionActive = 0;
    LRESULT storedMargins[4] = {0};
    LRESULT effectiveMargins[4] = {0};
    LRESULT storedPageSize[2] = {0};
    LRESULT effectivePageSize[2] = {0};
    RECT commentEditorRect;
    RECT originalWindowRect;
    POINT commentClickPoint;
    LONG pageIndex;
    int ribbonIndex;
    int result = 1;

    SetProcessDPIAware();
    ZeroMemory(&process, sizeof(process));
    ZeroMemory(&longProcess, sizeof(longProcess));
    sample[0] = L'\0';
    longSample[0] = L'\0';

    if (!create_temp_probe_path(L".txt", sample, ARRAYSIZE(sample)) ||
        !create_temp_probe_path(L".rtf", longSample,
                                ARRAYSIZE(longSample)) ||
        !resolve_test_app(L"wordcraft.exe", executable,
                          ARRAYSIZE(executable)) ||
        !write_probe_file(sample) || !write_long_probe_file(longSample)) {
        fwprintf(stderr, L"could not prepare GUI probe input (error=%lu)\n", GetLastError());
        goto cleanup;
    }

    if (!launch_hidden_wordcraft(executable, sample, &process, &window)) {
        fwprintf(stderr, L"could not launch WordCraft or find its main window (error=%lu)\n",
                 GetLastError());
        goto cleanup;
    }
    editor = find_control(window, IDC_EDITOR);
    if (editor == NULL) {
        fwprintf(stderr, L"WordCraft editor control was not created\n");
        goto cleanup;
    }
    {
        HDC editorDc = GetDC(editor);
        int dpiX;
        int dpiY;
        LONG toleranceX;
        LONG toleranceY;
        int component;
        BOOL geometryValid = editorDc != NULL;

        if (editorDc != NULL) {
            dpiX = max(1, GetDeviceCaps(editorDc, LOGPIXELSX));
            dpiY = max(1, GetDeviceCaps(editorDc, LOGPIXELSY));
            ReleaseDC(editor, editorDc);
            toleranceX = max(2, dpiX / 48);
            toleranceY = max(2, dpiY / 48);
            for (component = 0; component < 4; ++component) {
                geometryValid = geometryValid &&
                    query_wordcraft_state(
                        window, WCQ_PAGE_MARGIN_THOUSANDTHS, component,
                        &storedMargins[component]) &&
                    query_wordcraft_state(
                        window, WCQ_PAGE_LAYOUT_MARGIN_PIXELS, component,
                        &effectiveMargins[component]);
            }
            for (component = 0; component < 2; ++component) {
                geometryValid = geometryValid &&
                    query_wordcraft_state(
                        window, WCQ_PAGE_SIZE_THOUSANDTHS, component,
                        &storedPageSize[component]) &&
                    query_wordcraft_state(
                        window, WCQ_PAGE_LAYOUT_SIZE_PIXELS, component,
                        &effectivePageSize[component]);
            }
            geometryValid = geometryValid &&
                storedMargins[0] == 1000 && storedMargins[1] == 1000 &&
                storedMargins[2] == 1000 && storedMargins[3] == 1000 &&
                storedPageSize[0] == 8500 && storedPageSize[1] == 11000 &&
                scalar_within_tolerance(effectiveMargins[0], dpiX,
                                        toleranceX) &&
                scalar_within_tolerance(effectiveMargins[1], dpiY,
                                        toleranceY) &&
                scalar_within_tolerance(effectiveMargins[2], dpiX,
                                        toleranceX) &&
                scalar_within_tolerance(effectiveMargins[3], dpiY,
                                        toleranceY) &&
                scalar_within_tolerance(
                    effectivePageSize[0], MulDiv(8500, dpiX, 1000),
                    toleranceX) &&
                scalar_within_tolerance(
                    effectivePageSize[1], MulDiv(11000, dpiY, 1000),
                    toleranceY);
        }
        if (!geometryValid) {
            fwprintf(stderr,
                     L"default page geometry mismatch stored margins=%lld,%lld,%lld,%lld page=%lldx%lld effective margins=%lld,%lld,%lld,%lld page=%lldx%lld\n",
                     (long long)storedMargins[0],
                     (long long)storedMargins[1],
                     (long long)storedMargins[2],
                     (long long)storedMargins[3],
                     (long long)storedPageSize[0],
                     (long long)storedPageSize[1],
                     (long long)effectiveMargins[0],
                     (long long)effectiveMargins[1],
                     (long long)effectiveMargins[2],
                     (long long)effectiveMargins[3],
                     (long long)effectivePageSize[0],
                     (long long)effectivePageSize[1]);
            goto cleanup;
        }
    }
    ribbonTabs = find_control(window, IDC_RIBBON_TABS);
    formatBar = find_control(window, IDC_FORMAT_BAR);
    commentEdit = find_control(window, IDC_COMMENT_EDIT);
    if (ribbonTabs == NULL || formatBar == NULL || commentEdit == NULL ||
        find_control(window, IDM_REVIEW_DOCUMENT_CHAT) == NULL ||
        find_control(window, IDM_REVIEW_VERSION_HISTORY) == NULL ||
        !query_wordcraft_state(window, WCQ_CHAT_COUNT, 0, &chatCount) ||
        !query_wordcraft_state(window, WCQ_VERSION_COUNT, 0, &versionCount) ||
        chatCount != 0 || versionCount != 1 ||
        !query_wordcraft_state(window, WCQ_RIBBON_TAB_COUNT, 0,
                               &ribbonCount) ||
        ribbonCount != RIBBON_TAB_COUNT ||
        !wait_for_ribbon_state(window, RIBBON_TAB_HOME)) {
        fwprintf(stderr,
                 L"the ribbon did not create all %d tabs with Home active\n",
                 RIBBON_TAB_COUNT);
        goto cleanup;
    }
    for (ribbonIndex = 0; ribbonIndex < RIBBON_TAB_COUNT; ++ribbonIndex) {
        if (!query_wordcraft_state(window, WCQ_RIBBON_TAB_NAME_HASH,
                                   ribbonIndex, &ribbonHash) ||
            (UINT)(DWORD_PTR)ribbonHash !=
                probe_text_hash(expectedRibbonTabNames[ribbonIndex])) {
            fwprintf(stderr,
                     L"ribbon tab %d did not have the expected '%s' name\n",
                     ribbonIndex, expectedRibbonTabNames[ribbonIndex]);
            goto cleanup;
        }
    }
    if (!GetWindowRect(window, &originalWindowRect)) {
        fwprintf(stderr, L"could not capture the initial ribbon window size\n");
        goto cleanup;
    }
    if (!resize_window_for_home_layout(window, 1320, RIBBON_LAYOUT_FULL,
                                       NULL) ||
        !validate_home_layout_contract(window, formatBar,
                                       RIBBON_LAYOUT_FULL)) {
        fwprintf(stderr, L"the full Home ribbon layout contract failed\n");
        goto cleanup;
    }
    if (!force_home_group_paint(window, formatBar)) {
        fwprintf(stderr, L"the Home ribbon group dividers were not painted\n");
        goto cleanup;
    }
    {
        HWND collapsedStyleCombo = find_control(formatBar,
                                                IDC_HOME_STYLE_COMBO);
        if (collapsedStyleCombo == NULL ||
            !focus_home_control(window, formatBar,
                                IDM_STYLE_HEADING_1)) {
            fwprintf(stderr,
                     L"the full Home gallery could not focus Heading 1\n");
            goto cleanup;
        }
        if (!resize_window_for_home_layout(window, 640,
                                           RIBBON_LAYOUT_COLLAPSED, NULL) ||
            !validate_home_layout_contract(window, formatBar,
                                           RIBBON_LAYOUT_COLLAPSED) ||
            !validate_collapsed_style_combo(window, formatBar,
                                            collapsedStyleCombo) ||
            !wait_for_ribbon_control_focus(window,
                                           IDC_HOME_STYLE_COMBO)) {
            fwprintf(stderr,
                     L"the collapsed Home layout, style selector, or hidden-control focus fallback failed\n");
            goto cleanup;
        }
    }
    if (!resize_window_for_home_layout(window, 960, RIBBON_LAYOUT_COMPACT,
                                       NULL) ||
        !validate_home_layout_contract(window, formatBar,
                                       RIBBON_LAYOUT_COMPACT)) {
        fwprintf(stderr, L"the compact Home ribbon layout contract failed\n");
        goto cleanup;
    }
    if (!resize_window_for_home_layout(window, 2048, RIBBON_LAYOUT_FULL,
                                       NULL) ||
        !move_ribbon_to_tab(window, ribbonTabs, RIBBON_TAB_INSERT) ||
        !validate_insert_layout_contract(window, formatBar,
                                         RIBBON_LAYOUT_FULL)) {
        fwprintf(stderr,
                 L"the full Insert ribbon group/control/icon contract failed\n");
        goto cleanup;
    }
    if (!force_insert_ribbon_paint(window, formatBar)) {
        fwprintf(stderr,
                 L"the Insert ribbon group labels or control icons were not painted\n");
        goto cleanup;
    }
    if (!resize_window_for_home_layout(window, 960,
                                       RIBBON_LAYOUT_COMPACT, NULL) ||
        !validate_insert_layout_contract(window, formatBar,
                                         RIBBON_LAYOUT_COMPACT)) {
        fwprintf(stderr,
                 L"the compact Insert ribbon layout contract failed\n");
        goto cleanup;
    }
    if (!resize_window_for_home_layout(window, 640,
                                       RIBBON_LAYOUT_COLLAPSED, NULL) ||
        !validate_insert_layout_contract(window, formatBar,
                                         RIBBON_LAYOUT_COLLAPSED)) {
        fwprintf(stderr,
                 L"the collapsed Insert ribbon layout contract failed\n");
        goto cleanup;
    }
    if (!resize_window_for_home_layout(window, 2048,
                                       RIBBON_LAYOUT_FULL, NULL) ||
        !validate_insert_layout_contract(window, formatBar,
                                         RIBBON_LAYOUT_FULL)) {
        fwprintf(stderr,
                 L"the full Insert ribbon layout did not restore after responsive checks\n");
        goto cleanup;
    }
    if (!move_ribbon_to_tab(window, ribbonTabs, RIBBON_TAB_HOME) ||
        !insert_controls_are_hidden(window, formatBar) ||
        !move_ribbon_to_tab(window, ribbonTabs, RIBBON_TAB_INSERT) ||
        !focus_tabs_from_ribbon(window) ||
        !send_message_bounded(window, WM_COMMAND,
                              MAKEWPARAM(IDM_RIBBON_FOCUS, 0), 0, NULL) ||
        !wait_for_ribbon_focus(window, RIBBON_FOCUS_PANEL) ||
        !wait_for_ribbon_control_focus(window,
                                       IDM_INSERT_COVER_PAGE)) {
        LRESULT focusArea = -1;
        LRESULT focusId = -1;
        query_wordcraft_state(window, WCQ_RIBBON_FOCUS_AREA, 0,
                              &focusArea);
        query_wordcraft_state(window, WCQ_RIBBON_FOCUSED_CONTROL_ID, 0,
                              &focusId);
        fwprintf(stderr,
                 L"Insert controls did not hide with the page or accept F6 focus in visual order (area=%ld id=%ld)\n",
                 (long)focusArea, (long)focusId);
        goto cleanup;
    }
    if (!validate_insert_page_command(window, editor,
                                      IDM_INSERT_PAGE_BREAK, TRUE)) {
        fwprintf(stderr,
                 L"Insert Page Break was not a one-step undoable pagination change\n");
        goto cleanup;
    }
    if (!validate_insert_page_command(window, editor,
                                      IDM_INSERT_BLANK_PAGE, FALSE)) {
        fwprintf(stderr,
                 L"Insert Blank Page was not a one-step undoable pagination change\n");
        goto cleanup;
    }
    if (!validate_insert_datetime_command(window, editor)) {
        fwprintf(stderr,
                 L"Insert Date & Time did not preserve surrounding text or undo in one step\n");
        goto cleanup;
    }
    if (!move_ribbon_to_tab(window, ribbonTabs, RIBBON_TAB_DRAW)) {
        fwprintf(stderr, L"the Draw ribbon tab could not be selected\n");
        goto cleanup;
    }
    if (!validate_draw_layout_contract(window, editor, formatBar,
                                       RIBBON_LAYOUT_FULL)) {
        fwprintf(stderr,
                 L"the full Draw ribbon group/control/icon contract failed\n");
        goto cleanup;
    }
    if (!force_draw_ribbon_offscreen_paint(window, formatBar)) {
        fwprintf(stderr,
                 L"the Draw ribbon group labels or control icons were not painted offscreen\n");
        goto cleanup;
    }
    if (!resize_window_for_home_layout(window, 960,
                                       RIBBON_LAYOUT_COMPACT, NULL) ||
        !validate_draw_layout_contract(window, editor, formatBar,
                                       RIBBON_LAYOUT_COMPACT)) {
        fwprintf(stderr,
                 L"the compact Draw ribbon layout contract failed\n");
        goto cleanup;
    }
    if (!resize_window_for_home_layout(window, 560,
                                       RIBBON_LAYOUT_COLLAPSED, NULL) ||
        !validate_draw_layout_contract(window, editor, formatBar,
                                       RIBBON_LAYOUT_COLLAPSED)) {
        fwprintf(stderr,
                 L"the collapsed Draw ribbon layout contract failed\n");
        goto cleanup;
    }
    if (!resize_window_for_home_layout(window, 2048,
                                       RIBBON_LAYOUT_FULL, NULL) ||
        !validate_draw_layout_contract(window, editor, formatBar,
                                       RIBBON_LAYOUT_FULL)) {
        fwprintf(stderr,
                 L"the full Draw ribbon layout did not restore after responsive checks\n");
        goto cleanup;
    }
    if (!move_ribbon_to_tab(window, ribbonTabs, RIBBON_TAB_HOME) ||
        !draw_controls_are_hidden(window, formatBar) ||
        !move_ribbon_to_tab(window, ribbonTabs, RIBBON_TAB_DRAW) ||
        !focus_tabs_from_ribbon(window) ||
        !send_message_bounded(window, WM_COMMAND,
                              MAKEWPARAM(IDM_RIBBON_FOCUS, 0), 0, NULL) ||
        !wait_for_ribbon_focus(window, RIBBON_FOCUS_PANEL) ||
        !wait_for_ribbon_control_focus(window, IDM_DRAW_MODE)) {
        LRESULT focusArea = -1;
        LRESULT focusId = -1;
        query_wordcraft_state(window, WCQ_RIBBON_FOCUS_AREA, 0,
                              &focusArea);
        query_wordcraft_state(window, WCQ_RIBBON_FOCUSED_CONTROL_ID, 0,
                              &focusId);
        fwprintf(stderr,
                 L"Draw controls did not hide with the page or accept F6 focus in visual order (area=%ld id=%ld)\n",
                 (long)focusArea, (long)focusId);
        goto cleanup;
    }
    if (!validate_draw_nonmutating_commands(window, editor)) {
        fwprintf(stderr,
                 L"Draw tool, ruler, or background state changed document content or failed to synchronize checkmarks\n");
        goto cleanup;
    }
    if (!move_ribbon_to_tab(window, ribbonTabs, RIBBON_TAB_DESIGN) ||
        !validate_design_layout_contract(window, formatBar,
                                         RIBBON_LAYOUT_FULL)) {
        fwprintf(stderr,
                 L"the full Design ribbon group/control/icon/default-state contract failed\n");
        goto cleanup;
    }
    if (!force_design_ribbon_offscreen_paint(window, formatBar)) {
        fwprintf(stderr,
                 L"the Design ribbon group labels or control icons were not painted offscreen\n");
        goto cleanup;
    }
    if (!validate_design_gallery_contract(window, formatBar)) {
        fwprintf(stderr,
                 L"the Design style-set gallery open, paint, item, focus, or Escape contract failed\n");
        goto cleanup;
    }
    if (!validate_design_active_selection(window, formatBar)) {
        fwprintf(stderr,
                 L"the Design active style-set selection or checkmark contract failed\n");
        goto cleanup;
    }
    if (!resize_window_for_home_layout(window, 960,
                                       RIBBON_LAYOUT_COMPACT, NULL) ||
        !validate_design_layout_contract(window, formatBar,
                                         RIBBON_LAYOUT_COMPACT)) {
        fwprintf(stderr,
                 L"the compact Design ribbon layout contract failed\n");
        goto cleanup;
    }
    if (!resize_window_for_home_layout(window, 560,
                                       RIBBON_LAYOUT_COLLAPSED, NULL) ||
        !validate_design_layout_contract(window, formatBar,
                                         RIBBON_LAYOUT_COLLAPSED)) {
        fwprintf(stderr,
                 L"the collapsed Design ribbon layout contract failed\n");
        goto cleanup;
    }
    if (!resize_window_for_home_layout(window, 2048,
                                       RIBBON_LAYOUT_FULL, NULL) ||
        !validate_design_layout_contract(window, formatBar,
                                         RIBBON_LAYOUT_FULL)) {
        fwprintf(stderr,
                 L"the full Design ribbon layout did not restore after responsive checks\n");
        goto cleanup;
    }
    if (!move_ribbon_to_tab(window, ribbonTabs, RIBBON_TAB_HOME) ||
        !design_controls_are_hidden(window, formatBar) ||
        !move_ribbon_to_tab(window, ribbonTabs, RIBBON_TAB_DESIGN) ||
        !focus_tabs_from_ribbon(window) ||
        !send_message_bounded(window, WM_COMMAND,
                              MAKEWPARAM(IDM_RIBBON_FOCUS, 0), 0, NULL) ||
        !wait_for_ribbon_focus(window, RIBBON_FOCUS_PANEL) ||
        !wait_for_ribbon_control_focus(window, IDM_DESIGN_THEMES)) {
        LRESULT focusArea = -1;
        LRESULT focusId = -1;
        query_wordcraft_state(window, WCQ_RIBBON_FOCUS_AREA, 0,
                              &focusArea);
        query_wordcraft_state(window, WCQ_RIBBON_FOCUSED_CONTROL_ID, 0,
                              &focusId);
        fwprintf(
            stderr,
            L"Design controls did not hide with the page or accept F6 focus in visual order (area=%ld id=%ld)\n",
            (long)focusArea, (long)focusId);
        goto cleanup;
    }
    if (!validate_design_unsupported_nonmutating(
            window, editor, formatBar)) {
        fwprintf(
            stderr,
            L"unsupported Design commands changed document content or coordinated Design state\n");
        goto cleanup;
    }
    if (!move_ribbon_to_tab(window, ribbonTabs, RIBBON_TAB_VIEW) ||
        !validate_view_layout_contract(window, formatBar,
                                       RIBBON_LAYOUT_FULL)) {
        fwprintf(stderr,
                 L"the full View ribbon group/control/icon/default-state contract failed\n");
        goto cleanup;
    }
    if (!force_view_ribbon_offscreen_paint(window, formatBar)) {
        fwprintf(stderr,
                 L"the View ribbon group labels or control icons were not painted offscreen\n");
        goto cleanup;
    }
    if (!resize_window_for_home_layout(window, 960,
                                       RIBBON_LAYOUT_COMPACT, NULL) ||
        !validate_view_layout_contract(window, formatBar,
                                       RIBBON_LAYOUT_COMPACT)) {
        fwprintf(stderr,
                 L"the compact View ribbon layout contract failed\n");
        goto cleanup;
    }
    if (!resize_window_for_home_layout(window, 560,
                                       RIBBON_LAYOUT_COLLAPSED, NULL) ||
        !validate_view_layout_contract(window, formatBar,
                                       RIBBON_LAYOUT_COLLAPSED)) {
        fwprintf(stderr,
                 L"the collapsed View ribbon layout contract failed\n");
        goto cleanup;
    }
    if (!resize_window_for_home_layout(window, 2048,
                                       RIBBON_LAYOUT_FULL, NULL) ||
        !validate_view_layout_contract(window, formatBar,
                                       RIBBON_LAYOUT_FULL)) {
        fwprintf(stderr,
                 L"the full View ribbon layout did not restore after responsive checks\n");
        goto cleanup;
    }
    if (!move_ribbon_to_tab(window, ribbonTabs, RIBBON_TAB_HOME) ||
        !view_controls_are_hidden(window, formatBar) ||
        !move_ribbon_to_tab(window, ribbonTabs, RIBBON_TAB_VIEW) ||
        !focus_tabs_from_ribbon(window) ||
        !send_message_bounded(window, WM_COMMAND,
                              MAKEWPARAM(IDM_RIBBON_FOCUS, 0), 0, NULL) ||
        !wait_for_ribbon_focus(window, RIBBON_FOCUS_PANEL) ||
        !wait_for_ribbon_control_focus(window, IDM_VIEW_READ_MODE)) {
        LRESULT focusArea = -1;
        LRESULT focusId = -1;
        query_wordcraft_state(window, WCQ_RIBBON_FOCUS_AREA, 0,
                              &focusArea);
        query_wordcraft_state(window, WCQ_RIBBON_FOCUSED_CONTROL_ID, 0,
                              &focusId);
        fwprintf(
            stderr,
            L"View controls did not hide with the page or accept F6 focus in visual order (area=%ld id=%ld)\n",
            (long)focusArea, (long)focusId);
        goto cleanup;
    }
    if (!validate_view_nonmutating_commands(window, editor)) {
        fwprintf(
            stderr,
            L"View modes, movement, overlays, focus, or zoom-fit actions changed document content or failed to synchronize state\n");
        goto cleanup;
    }
    if (!move_ribbon_to_tab(window, ribbonTabs, RIBBON_TAB_HOME) ||
        !restore_window_size(window, &originalWindowRect) ||
        !focus_editor_from_ribbon(window)) {
        fwprintf(stderr,
                 L"the ribbon probes could not restore their initial size, Home tab, and editor focus\n");
        goto cleanup;
    }
    if (!send_message_bounded(window, WM_COMMAND,
                              MAKEWPARAM(IDM_RIBBON_FOCUS, 0), 0, NULL) ||
        !wait_for_ribbon_focus(window, RIBBON_FOCUS_TABS) ||
        !send_message_bounded(ribbonTabs, WM_KEYDOWN, VK_LEFT, 0, NULL) ||
        !wait_for_ribbon_state(window, RIBBON_TAB_FILE)) {
        fwprintf(stderr,
                 L"F6 focus or keyboard navigation to the File ribbon failed\n");
        goto cleanup;
    }
    for (ribbonIndex = RIBBON_TAB_HOME;
         ribbonIndex < RIBBON_TAB_COUNT; ++ribbonIndex) {
        if (!send_message_bounded(ribbonTabs, WM_KEYDOWN, VK_RIGHT, 0,
                                  NULL) ||
            !wait_for_ribbon_state(window, ribbonIndex)) {
            fwprintf(stderr,
                     L"keyboard navigation did not expose ribbon tab %d ('%s')\n",
                     ribbonIndex, expectedRibbonTabNames[ribbonIndex]);
            goto cleanup;
        }
    }
    if (!send_message_bounded(window, WM_COMMAND,
                              MAKEWPARAM(IDM_RIBBON_FOCUS, 0), 0, NULL) ||
        !wait_for_ribbon_focus(window, RIBBON_FOCUS_PANEL) ||
        !send_message_bounded(window, WM_COMMAND,
                              MAKEWPARAM(IDM_RIBBON_FOCUS, 0), 0, NULL) ||
        !wait_for_ribbon_focus(window, RIBBON_FOCUS_EDITOR) ||
        !query_wordcraft_state(window, WCQ_COMMENT_COUNT, 0,
                               &commentCount) ||
        !query_wordcraft_state(window, WCQ_COMMENT_ACTIVE_INDEX, 0,
                               &activeComment) ||
        commentCount != 0 || activeComment != -1 ||
        !send_message_bounded(window, WM_COMMAND,
                              MAKEWPARAM(IDM_REVIEW_ADD_COMMENT, 0), 0,
                              NULL) ||
        !wait_for_ribbon_state(window, RIBBON_TAB_REVIEW) ||
        !wait_for_ribbon_focus(window, RIBBON_FOCUS_PANEL) ||
        !send_message_bounded(window, WM_COMMAND,
                              MAKEWPARAM(IDM_RIBBON_FOCUS, 0), 0, NULL) ||
        !wait_for_ribbon_focus(window, RIBBON_FOCUS_EDITOR) ||
        !query_wordcraft_state(window,
                               WCQ_COMMENT_COMPOSITION_ACTIVE, 0,
                               &commentCompositionActive) ||
        !query_wordcraft_state(window,
                               WCQ_COMMENT_HIGHLIGHT_VISIBLE, 0,
                               &commentHighlightVisible) ||
        commentCompositionActive != 0 || commentHighlightVisible != 0) {
        LRESULT diagnosticFocus = -1;
        LRESULT diagnosticTab = -1;
        LRESULT diagnosticComposition = -1;
        LRESULT diagnosticHighlight = -1;
        query_wordcraft_state(window, WCQ_RIBBON_FOCUS_AREA, 0,
                              &diagnosticFocus);
        query_wordcraft_state(window, WCQ_RIBBON_ACTIVE_TAB, 0,
                              &diagnosticTab);
        query_wordcraft_state(window, WCQ_COMMENT_COMPOSITION_ACTIVE, 0,
                              &diagnosticComposition);
        query_wordcraft_state(window, WCQ_COMMENT_HIGHLIGHT_VISIBLE, 0,
                              &diagnosticHighlight);
        fwprintf(stderr,
                 L"ribbon focus cycling or abandoned comment-draft cleanup failed "
                 L"(focus=%ld tab=%ld composition=%ld highlight=%ld)\n",
                 (long)diagnosticFocus, (long)diagnosticTab,
                 (long)diagnosticComposition, (long)diagnosticHighlight);
        goto cleanup;
    }
    paperSizeCombo = find_control(window, IDC_PAPER_SIZE_COMBO);
    if (paperSizeCombo == NULL ||
        !send_message_bounded(paperSizeCombo, CB_GETCOUNT, 0, 0,
                              &paperItemCount) ||
        !query_wordcraft_state(window, WCQ_PAPER_SIZE_COUNT, 0,
                               &paperPresetCount) ||
        !send_message_bounded(editor, EM_GETMODIFY, 0, 0,
                              &paperModified) ||
        paperItemCount != PAPER_SIZE_COUNT ||
        paperPresetCount != PAPER_SIZE_COUNT ||
        !move_ribbon_to_tab(window, ribbonTabs, RIBBON_TAB_LAYOUT) ||
        (GetWindowLongPtrW(paperSizeCombo, GWL_STYLE) & WS_VISIBLE) == 0 ||
        !paper_size_state_matches(
            window, editor, paperSizeCombo, PAPER_SIZE_LETTER,
            8500, 11000, storedMargins, paperModified)) {
        fwprintf(stderr,
                 L"the Layout paper-size selector did not expose all 25 presets with Letter selected\n");
        goto cleanup;
    }
    if (!select_paper_size_and_wait(
            window, editor, paperSizeCombo, PAPER_SIZE_A4,
            8268, 11693, storedMargins, paperModified) ||
        !select_paper_size_and_wait(
            window, editor, paperSizeCombo, PAPER_SIZE_LEDGER,
            17000, 11000, storedMargins, paperModified) ||
        !select_paper_size_and_wait(
            window, editor, paperSizeCombo, PAPER_SIZE_E_SHEET,
            34000, 44000, storedMargins, paperModified) ||
        !select_paper_size_and_wait(
            window, editor, paperSizeCombo, PAPER_SIZE_LETTER,
            8500, 11000, storedMargins, paperModified)) {
        fwprintf(stderr,
                 L"A4, Ledger, E Sheet, or restored Letter paper geometry did not match the Layout selection\n");
        goto cleanup;
    }
    /* Keep all established pagination and scrolling expectations on the
     * application's default Letter geometry. */
    if (!move_ribbon_to_tab(window, ribbonTabs, RIBBON_TAB_HOME) ||
        !focus_editor_from_ribbon(window)) {
        fwprintf(stderr,
                 L"the paper-size probe could not restore Home/editor focus\n");
        goto cleanup;
    }
    {
        LRESULT firstTabstop = 0;
        if (!first_enabled_home_tabstop(window, &firstTabstop) ||
            !PostMessageW(editor, WM_KEYDOWN, VK_F6, 1) ||
            !wait_for_ribbon_focus(window, RIBBON_FOCUS_TABS) ||
            !PostMessageW(ribbonTabs, WM_KEYDOWN, VK_TAB, 1) ||
            !wait_for_ribbon_focus(window, RIBBON_FOCUS_PANEL) ||
            !wait_for_ribbon_control_focus(window, firstTabstop) ||
            !focus_editor_from_ribbon(window)) {
            fwprintf(stderr,
                     L"queued F6/Tab did not enter the first enabled Home control and return to the editor\n");
            goto cleanup;
        }
    }
    fontCombo = find_control(window, IDC_FONT_COMBO);
    sizeCombo = find_control(window, IDC_SIZE_COMBO);
    ZeroMemory(fontText, sizeof(fontText));
    ZeroMemory(sizeText, sizeof(sizeText));
    if (fontCombo == NULL || sizeCombo == NULL ||
        !get_window_text_bounded(fontCombo, fontText,
                                 ARRAYSIZE(fontText)) ||
        !get_window_text_bounded(sizeCombo, sizeText,
                                 ARRAYSIZE(sizeText)) ||
        lstrcmpiW(fontText, L"Times New Roman") != 0 ||
        wcscmp(sizeText, L"12") != 0) {
        fwprintf(stderr,
                 L"plain-text documents did not start in Times New Roman 12 pt (font='%s' size='%s')\n",
                 fontText, sizeText);
        goto cleanup;
    }
    ZeroMemory(text, sizeof(text));
    if (!get_window_text_bounded(editor, text, ARRAYSIZE(text)) ||
        wcscmp(text, L"GUI probe caf\x00E9") != 0) {
        fwprintf(stderr, L"command-line document did not load: '%s'\n", text);
        goto cleanup;
    }
    ZeroMemory(&paragraph, sizeof(paragraph));
    if (!query_text_engine_snapshot(window, &initialTextEngine) ||
        !text_engine_snapshot_has_defaults(&initialTextEngine) ||
        !get_paragraph_format_bounded(editor, process.hProcess,
                                      &paragraph) ||
        !paragraph_has_text_engine_defaults(&paragraph)) {
        fwprintf(
            stderr,
            L"advanced text-engine defaults were not active "
            L"(enabled=%lld backend=%lld options=0x%llx rule=%lld "
            L"spacing=%lld after=%lld generation=%lld paragraph-mask=0x%lx)\n",
            (long long)initialTextEngine.enabled,
            (long long)initialTextEngine.backend,
            (unsigned long long)initialTextEngine.typographyOptions,
            (long long)initialTextEngine.lineSpacingRule,
            (long long)initialTextEngine.lineSpacing,
            (long long)initialTextEngine.paragraphSpaceAfter,
            (long long)initialTextEngine.layoutGeneration,
            (unsigned long)paragraph.dwMask);
        goto cleanup;
    }

    if (!query_wordcraft_state(window, WCQ_PAGE_COUNT, 0, &pageCount) ||
        !query_wordcraft_state(window, WCQ_CURRENT_PAGE, 0, &currentPage) ||
        pageCount != 1 || currentPage != 1) {
        fwprintf(stderr, L"short document page state was not Page 1 of 1 (page=%lld total=%lld)\n",
                 (long long)currentPage, (long long)pageCount);
        goto cleanup;
    }

    StringCchCopyW(originalText, ARRAYSIZE(originalText), text);
    ZeroMemory(originalTitle, sizeof(originalTitle));
    if (!get_window_text_bounded(window, originalTitle,
                                 ARRAYSIZE(originalTitle)) ||
        !send_message_bounded(editor, EM_GETMODIFY, 0, 0,
                              &originalModified)) {
        fwprintf(stderr, L"could not capture the initial document state\n");
        goto cleanup;
    }
    menu = GetMenu(window);
    if (menu == NULL ||
        !query_wordcraft_state(window, WCQ_DARK_MODE, 0, &darkMode) ||
        darkMode != 0 ||
        (GetMenuState(menu, IDM_VIEW_WORD_WRAP, MF_BYCOMMAND) & MF_CHECKED) == 0 ||
         (GetMenuState(menu, IDM_VIEW_WORD_WRAP, MF_BYCOMMAND) &
          (MF_DISABLED | MF_GRAYED)) == 0 ||
        (GetMenuState(menu, IDM_VIEW_DARK_MODE, MF_BYCOMMAND) & MF_CHECKED) != 0 ||
        (GetMenuState(menu, IDM_TOOLS_SPELL_CHECK, MF_BYCOMMAND) & MF_CHECKED) == 0 ||
        (GetMenuState(menu, IDM_TOOLS_AUTOCOMPLETE, MF_BYCOMMAND) & MF_CHECKED) == 0 ||
        !query_wordcraft_state(window, WCQ_ASSIST_WORKER_RUNNING, 0,
                               &workerRunning) ||
        workerRunning != 1) {
        fwprintf(stderr, L"dark mode did not start in the expected off state\n");
        goto cleanup;
    }

    if (!send_message_bounded(window, WM_COMMAND,
                              MAKEWPARAM(IDM_VIEW_DARK_MODE, 0), 0, NULL) ||
        !query_wordcraft_state(window, WCQ_DARK_MODE, 0, &darkMode) ||
        darkMode != 1 ||
        (GetMenuState(menu, IDM_VIEW_DARK_MODE, MF_BYCOMMAND) & MF_CHECKED) == 0 ||
        !query_text_engine_snapshot(window, &currentTextEngine) ||
        !text_engine_snapshot_has_defaults(&currentTextEngine) ||
        !text_engine_snapshots_equal(&initialTextEngine,
                                     &currentTextEngine) ||
        !get_paragraph_format_bounded(editor, process.hProcess,
                                      &paragraph) ||
        !paragraph_has_text_engine_defaults(&paragraph) ||
        !editor_state_matches(window, editor, originalText, originalTitle,
                              originalModified)) {
        fwprintf(stderr,
                 L"enabling dark mode changed document or advanced text-engine state\n");
        goto cleanup;
    }

    if (!send_message_bounded(window, WM_COMMAND,
                              MAKEWPARAM(IDM_VIEW_DARK_MODE, 0), 0, NULL) ||
        !query_wordcraft_state(window, WCQ_DARK_MODE, 0, &darkMode) ||
        darkMode != 0 ||
        (GetMenuState(menu, IDM_VIEW_DARK_MODE, MF_BYCOMMAND) & MF_CHECKED) != 0 ||
        !query_text_engine_snapshot(window, &currentTextEngine) ||
        !text_engine_snapshot_has_defaults(&currentTextEngine) ||
        !text_engine_snapshots_equal(&initialTextEngine,
                                     &currentTextEngine) ||
        !get_paragraph_format_bounded(editor, process.hProcess,
                                      &paragraph) ||
        !paragraph_has_text_engine_defaults(&paragraph) ||
        !editor_state_matches(window, editor, originalText, originalTitle,
                              originalModified)) {
        fwprintf(stderr,
                 L"disabling dark mode changed document or advanced text-engine state\n");
        goto cleanup;
    }

    if (!send_message_bounded(editor, WM_SETTEXT, 0,
                              (LPARAM)L"This is teh qzxvplm. Thank you", NULL) ||
        !send_message_bounded(editor, EM_SETSEL, (WPARAM)-1, (LPARAM)-1,
                              NULL) ||
        !wait_for_assistance(window, 1, TRUE, &completionLength) ||
        completionLength <= 0) {
        fwprintf(stderr, L"background spell checking or inline completion did not become ready\n");
        goto cleanup;
    }
    ZeroMemory(text, sizeof(text));
    if (!get_window_text_bounded(editor, text, ARRAYSIZE(text)) ||
        wcscmp(text, L"This is teh qzxvplm. Thank you") != 0) {
        fwprintf(stderr, L"assistance overlays changed the document before acceptance: '%s'\n",
                 text);
        goto cleanup;
    }
    if (!PostMessageW(editor, WM_KEYDOWN, VK_TAB, 1) ||
        !wait_for_window_text(
            editor, L"This is teh qzxvplm. Thank you for your time.")) {
        fwprintf(stderr, L"queued Tab completion acceptance timed out\n");
        goto cleanup;
    }
    ZeroMemory(text, sizeof(text));
    if (!get_window_text_bounded(editor, text, ARRAYSIZE(text)) ||
        wcscmp(text,
               L"This is teh qzxvplm. Thank you for your time.") != 0) {
        fwprintf(stderr, L"Tab did not accept the inline suffix: '%s'\n", text);
        goto cleanup;
    }
    if (!send_message_bounded(editor, EM_UNDO, 0, 0, NULL)) {
        fwprintf(stderr, L"accepted completion was not undoable as one edit\n");
        goto cleanup;
    }
    ZeroMemory(text, sizeof(text));
    if (!get_window_text_bounded(editor, text, ARRAYSIZE(text)) ||
        wcscmp(text, L"This is teh qzxvplm. Thank you") != 0 ||
        !wait_for_assistance(window, 1, TRUE, NULL) ||
        !PostMessageW(editor, WM_KEYDOWN, VK_ESCAPE, 1) ||
        !wait_for_completion_visibility(window, FALSE)) {
        fwprintf(stderr, L"completion undo or Escape dismissal failed\n");
        goto cleanup;
    }
    if (!send_message_bounded(window, WM_COMMAND,
                              MAKEWPARAM(IDM_TOOLS_AUTOCOMPLETE, 0), 0,
                              NULL) ||
        (GetMenuState(menu, IDM_TOOLS_AUTOCOMPLETE, MF_BYCOMMAND) &
         MF_CHECKED) != 0 ||
        !query_wordcraft_state(window, WCQ_COMPLETION_VISIBLE, 0,
                               &completionLength) ||
        completionLength != 0 ||
        !PostMessageW(editor, WM_KEYDOWN, VK_TAB, 1) ||
        !wait_for_window_text(editor,
                              L"This is teh qzxvplm. Thank you\t")) {
        fwprintf(stderr, L"disabling autocomplete or ordinary Tab behavior failed\n");
        goto cleanup;
    }
    ZeroMemory(text, sizeof(text));
    if (!get_window_text_bounded(editor, text, ARRAYSIZE(text)) ||
        wcscmp(text, L"This is teh qzxvplm. Thank you\t") != 0 ||
        !send_message_bounded(editor, EM_UNDO, 0, 0, NULL) ||
        !send_message_bounded(window, WM_COMMAND,
                              MAKEWPARAM(IDM_TOOLS_AUTOCOMPLETE, 0), 0,
                              NULL) ||
        (GetMenuState(menu, IDM_TOOLS_AUTOCOMPLETE, MF_BYCOMMAND) &
         MF_CHECKED) == 0) {
        fwprintf(stderr, L"autocomplete menu toggle failed\n");
        goto cleanup;
    }
    if (!send_message_bounded(window, WM_COMMAND,
                              MAKEWPARAM(IDM_TOOLS_SPELL_CHECK, 0), 0,
                              NULL) ||
        (GetMenuState(menu, IDM_TOOLS_SPELL_CHECK, MF_BYCOMMAND) &
         MF_CHECKED) != 0 ||
        !query_wordcraft_state(window, WCQ_SPELL_ERROR_COUNT, 0,
                               &completionLength) ||
        completionLength != 0 ||
        !send_message_bounded(window, WM_COMMAND,
                              MAKEWPARAM(IDM_TOOLS_SPELL_CHECK, 0), 0,
                              NULL) ||
        (GetMenuState(menu, IDM_TOOLS_SPELL_CHECK, MF_BYCOMMAND) &
         MF_CHECKED) == 0 ||
        !wait_for_assistance(window, 1, FALSE, NULL)) {
        fwprintf(stderr, L"spell-check menu toggle failed\n");
        goto cleanup;
    }
    if (!send_message_bounded(editor, WM_SETTEXT, 0,
                              (LPARAM)originalText, NULL) ||
        !send_message_bounded(editor, EM_EMPTYUNDOBUFFER, 0, 0, NULL)) {
        fwprintf(stderr, L"could not restore the short document after assistance checks\n");
        goto cleanup;
    }

    if (!send_message_bounded(editor, EM_SETSEL, (WPARAM)-1,
                              (LPARAM)-1, NULL) ||
        !send_message_bounded(editor, WM_CHAR, L'x', 0, NULL)) {
        fwprintf(stderr, L"simulated edit timed out\n");
        goto cleanup;
    }
    ZeroMemory(text, sizeof(text));
    if (!get_window_text_bounded(editor, text, ARRAYSIZE(text)) ||
        wcscmp(text, L"GUI probe caf\x00E9x") != 0) {
        fwprintf(stderr, L"simulated edit produced unexpected text: '%s'\n", text);
        goto cleanup;
    }
    ZeroMemory(text, sizeof(text));
    if (!get_window_text_bounded(window, text, ARRAYSIZE(text)) ||
        wcschr(text, L'*') == NULL) {
        fwprintf(stderr, L"simulated edit did not mark the document dirty: '%s'\n", text);
        goto cleanup;
    }
    if (!PostMessageW(window, WM_COMMAND,
                      MAKEWPARAM(IDM_FILE_SAVE, 0), 0) ||
        !accept_next_message_box(process.dwProcessId)) {
        fwprintf(stderr,
                 L"plain-text metadata warning was not shown or accepted\n");
        goto cleanup;
    }
    {
        int saveAttempt;
        BOOL saved = FALSE;
        for (saveAttempt = 0; saveAttempt < 50 && !saved; ++saveAttempt) {
            saved = verify_saved_file(sample);
            if (!saved) {
                Sleep(100);
            }
        }
        if (!saved) {
        fwprintf(stderr, L"Unicode text save or atomic replacement failed\n");
        goto cleanup;
        }
    }
    if (!close_wordcraft_cleanly(window, &process)) {
        fwprintf(stderr, L"WordCraft did not close cleanly after the short-document probe\n");
        goto cleanup;
    }
    window = NULL;

    if (!launch_hidden_wordcraft(executable, longSample, &longProcess, &longWindow)) {
        fwprintf(stderr, L"could not launch the long-document WordCraft probe (error=%lu)\n",
                 GetLastError());
        goto cleanup;
    }
    longEditor = find_control(longWindow, IDC_EDITOR);
    longPageView = longEditor != NULL ? GetParent(longEditor) : NULL;
    longRibbonTabs = find_control(longWindow, IDC_RIBBON_TABS);
    if (longEditor == NULL || longPageView == NULL ||
        longRibbonTabs == NULL) {
        fwprintf(stderr, L"long-document editor control was not created\n");
        goto cleanup;
    }
    if (!send_message_bounded(longEditor, EM_GETVIEWKIND, 0, 0, &viewKind) ||
        viewKind != VM_PAGE ||
        !send_message_bounded(longEditor, WM_GETTEXTLENGTH, 0, 0,
                              &longTextLength) ||
        !query_wordcraft_state(longWindow, WCQ_PAGE_COUNT, 0, &pageCount) ||
        longTextLength <= 0 || pageCount < 2 || pageCount > LONG_MAX ||
        pageCount > longTextLength || longTextLength > USHRT_MAX) {
        fwprintf(stderr, L"long document did not paginate to multiple pages (total=%lld)\n",
                 (long long)pageCount);
        goto cleanup;
    }
    for (pageIndex = 0; pageIndex < (LONG)pageCount; ++pageIndex) {
        LRESULT pageStart = -1;
        if (!query_wordcraft_state(longWindow, WCQ_PAGE_START, pageIndex,
                                   &pageStart) ||
            (pageIndex == 0 && pageStart != 0) ||
            pageStart < 0 || pageStart >= longTextLength ||
            (pageIndex > 0 && pageStart <= previousStart)) {
            fwprintf(stderr, L"page starts were not strictly increasing at index %ld (start=%lld previous=%lld)\n",
                     pageIndex, (long long)pageStart, (long long)previousStart);
            goto cleanup;
        }
        previousStart = pageStart;
    }
    if (!query_wordcraft_state(longWindow, WCQ_PAGE_START, 1,
                               &secondPageStart) ||
        secondPageStart < 20 || secondPageStart + 20 >= longTextLength ||
        !send_message_bounded(longEditor, EM_SETSEL,
                              (WPARAM)(secondPageStart - 20),
                              (LPARAM)(secondPageStart + 20), NULL) ||
        !query_wordcraft_state(longWindow, WCQ_CURRENT_PAGE, 0,
                               &currentPage) ||
        !send_message_bounded(longEditor, EM_GETPAGE, 0, 0,
                              &nativePage) ||
        !get_selection_bounded(longEditor, &selectionStart,
                               &selectionEnd) ||
        currentPage != 2 || nativePage != 1 ||
        selectionStart != (DWORD)(secondPageStart - 20) ||
        selectionEnd != (DWORD)(secondPageStart + 20)) {
        fwprintf(stderr, L"forward cross-page selection lost its active page (boundary=%lld selection=%lu..%lu page=%lld native=%lld)\n",
                 (long long)secondPageStart,
                 (unsigned long)selectionStart,
                 (unsigned long)selectionEnd,
                 (long long)currentPage, (long long)nativePage);
        goto cleanup;
    }
    if (!send_message_bounded(longEditor, EM_SETSEL,
                              (WPARAM)(secondPageStart + 20),
                              (LPARAM)(secondPageStart - 20), NULL) ||
        !query_wordcraft_state(longWindow, WCQ_CURRENT_PAGE, 0,
                               &currentPage) ||
        !send_message_bounded(longEditor, EM_GETPAGE, 0, 0,
                              &nativePage) ||
        !get_selection_bounded(longEditor, &selectionStart,
                               &selectionEnd) ||
        currentPage != 1 || nativePage != 0 ||
        selectionStart != (DWORD)(secondPageStart - 20) ||
        selectionEnd != (DWORD)(secondPageStart + 20)) {
        fwprintf(stderr, L"reverse cross-page selection lost its active page (boundary=%lld selection=%lu..%lu page=%lld native=%lld)\n",
                 (long long)secondPageStart,
                 (unsigned long)selectionStart,
                 (unsigned long)selectionEnd,
                 (long long)currentPage, (long long)nativePage);
        goto cleanup;
    }
    /* Use a tall hidden viewport so two pages intersect the client area even
     * on a high-DPI desktop; at least one page must remain fully visible. */
    if (!SetWindowPos(longWindow, NULL, 0, 0, 1400, 2800,
                      SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) ||
        !send_message_bounded(longWindow, WM_COMMAND,
                              MAKEWPARAM(IDM_VIEW_ZOOM_50, 0), 0, NULL) ||
        !send_message_bounded(longPageView, WM_VSCROLL,
                              MAKEWPARAM(SB_TOP, 0), 0, NULL) ||
        !wait_for_page_view(longWindow, 1, 1, FALSE,
                            &firstVisiblePage, &lastVisiblePage,
                            &viewScrollY) ||
        !query_wordcraft_state(longWindow, WCQ_VISIBLE_PAGE_COUNT, 0,
                               &visiblePageCount) ||
        !query_wordcraft_state(longWindow, WCQ_FULLY_VISIBLE_PAGE_COUNT, 0,
                               &fullyVisiblePageCount) ||
        !query_wordcraft_state(longWindow, WCQ_VIEW_SCROLL_MAX, 0,
                               &viewScrollMax) ||
        firstVisiblePage != 1 || lastVisiblePage < 2 ||
        visiblePageCount != lastVisiblePage - firstVisiblePage + 1 ||
        fullyVisiblePageCount < 1 || viewScrollY != 0 ||
        viewScrollMax <= 0) {
        fwprintf(stderr, L"continuous view did not expose multiple pages (first=%lld last=%lld visible=%lld full=%lld y=%lld max=%lld)\n",
                 (long long)firstVisiblePage, (long long)lastVisiblePage,
                 (long long)visiblePageCount,
                 (long long)fullyVisiblePageCount,
                 (long long)viewScrollY, (long long)viewScrollMax);
        goto cleanup;
    }
    if (!send_message_bounded(longEditor, EM_SETSEL, 10, 20, NULL) ||
        !get_selection_bounded(longEditor, &selectionStart, &selectionEnd) ||
        selectionStart != 10 || selectionEnd != 20 ||
        !query_wordcraft_state(longWindow, WCQ_CURRENT_PAGE, 0,
                               &currentPage) ||
        currentPage != 1) {
        fwprintf(stderr,
                 L"could not prepare the selection for smooth-scroll checks\n");
        goto cleanup;
    }
    {
        const int burstCount = 6;
        LRESULT animation = 0;
        LRESULT frameInterval = 0;
        LRESULT initialFrames = 0;
        LRESULT finalFrames = 0;
        LRESULT initialScroll = 0;
        LRESULT maximumScroll = 0;
        LRESULT target = 0;
        LRESULT oneWheelDistance = 0;
        LRESULT expectedBurstTarget = 0;
        int distinctPositions = 0;
        int wheelPart;

        if (!get_vertical_scroll_state(longPageView, &initialScroll,
                                       &maximumScroll) ||
            initialScroll != 0 || maximumScroll <= 0 ||
            !query_wordcraft_state(longWindow,
                                   WCQ_SCROLL_FRAME_INTERVAL_MS, 0,
                                   &frameInterval) ||
            frameInterval != 16 ||
            !query_wordcraft_state(longWindow, WCQ_SCROLL_FRAME_COUNT, 0,
                                   &initialFrames) ||
            !send_message_bounded(
                longEditor, WM_MOUSEWHEEL,
                MAKEWPARAM(0, (WORD)(SHORT)-WHEEL_DELTA), 0, NULL) ||
            !query_wordcraft_state(longWindow, WCQ_SCROLL_ANIMATING, 0,
                                   &animation) ||
            !query_wordcraft_state(longWindow, WCQ_SCROLL_TARGET_Y, 0,
                                   &target) ||
            animation == 0 || target <= initialScroll ||
            target > maximumScroll ||
            !wait_for_smooth_scroll_settle(
                longWindow, longPageView, initialScroll, target, 1,
                &finalFrames, &distinctPositions) ||
            finalFrames - initialFrames < 2 || distinctPositions < 2) {
            fwprintf(stderr,
                     L"one-notch scrolling was not a monotonic multi-frame 16 ms animation (interval=%lld active=%lld start=%lld target=%lld frames=%lld..%lld samples=%d)\n",
                     (long long)frameInterval, (long long)animation,
                     (long long)initialScroll, (long long)target,
                     (long long)initialFrames, (long long)finalFrames,
                     distinctPositions);
            goto cleanup;
        }
        oneWheelDistance = target - initialScroll;
        viewScrollY = target;
        if (!get_selection_bounded(longEditor, &selectionStart,
                                   &selectionEnd) ||
            selectionStart != 10 || selectionEnd != 20 ||
            !query_wordcraft_state(longWindow, WCQ_CURRENT_PAGE, 0,
                                   &currentPage) ||
            currentPage != 1) {
            fwprintf(stderr,
                     L"smooth mouse-wheel scrolling changed selection or caret page (selection=%lu..%lu page=%lld)\n",
                     (unsigned long)selectionStart,
                     (unsigned long)selectionEnd,
                     (long long)currentPage);
            goto cleanup;
        }

        if (!send_message_bounded(longPageView, WM_VSCROLL,
                                  MAKEWPARAM(SB_TOP, 0), 0, NULL) ||
            !get_vertical_scroll_state(longPageView, &viewScrollY, NULL) ||
            !query_wordcraft_state(longWindow, WCQ_SCROLL_ANIMATING, 0,
                                   &animation) ||
            viewScrollY != 0 || animation != 0 ||
            !query_wordcraft_state(longWindow, WCQ_SCROLL_FRAME_COUNT, 0,
                                   &initialFrames)) {
            fwprintf(stderr,
                     L"SB_TOP did not synchronously reset smooth scrolling (y=%lld active=%lld)\n",
                     (long long)viewScrollY, (long long)animation);
            goto cleanup;
        }
        for (wheelPart = 0; wheelPart < 4; ++wheelPart) {
            if (!send_message_bounded(
                    longEditor, WM_MOUSEWHEEL,
                    MAKEWPARAM(0, (WORD)(SHORT)-30), 0, NULL)) {
                fwprintf(stderr,
                         L"high-resolution wheel input timed out\n");
                goto cleanup;
            }
        }
        distinctPositions = 0;
        if (!query_wordcraft_state(longWindow, WCQ_SCROLL_ANIMATING, 0,
                                   &animation) ||
            !query_wordcraft_state(longWindow, WCQ_SCROLL_TARGET_Y, 0,
                                   &target) ||
            animation == 0 || target != oneWheelDistance ||
            !wait_for_smooth_scroll_settle(
                longWindow, longPageView, 0, target, 1, &finalFrames,
                &distinctPositions) ||
            finalFrames - initialFrames < 2) {
            fwprintf(stderr,
                     L"four high-resolution wheel deltas did not accumulate to one notch (active=%lld target=%lld expected=%lld frames=%lld..%lld)\n",
                     (long long)animation, (long long)target,
                     (long long)oneWheelDistance,
                     (long long)initialFrames, (long long)finalFrames);
            goto cleanup;
        }

        if (!send_message_bounded(longPageView, WM_VSCROLL,
                                  MAKEWPARAM(SB_TOP, 0), 0, NULL) ||
            !query_wordcraft_state(longWindow, WCQ_SCROLL_FRAME_COUNT, 0,
                                   &initialFrames)) {
            fwprintf(stderr,
                     L"could not reset before coalesced wheel checks\n");
            goto cleanup;
        }
        for (wheelPart = 0; wheelPart < burstCount; ++wheelPart) {
            if (!send_message_bounded(
                    longEditor, WM_MOUSEWHEEL,
                    MAKEWPARAM(0, (WORD)(SHORT)-WHEEL_DELTA), 0, NULL)) {
                fwprintf(stderr, L"rapid wheel burst timed out\n");
                goto cleanup;
            }
        }
        expectedBurstTarget = oneWheelDistance * burstCount;
        if (expectedBurstTarget > maximumScroll) {
            expectedBurstTarget = maximumScroll;
        }
        distinctPositions = 0;
        if (!query_wordcraft_state(longWindow, WCQ_SCROLL_ANIMATING, 0,
                                   &animation) ||
            !query_wordcraft_state(longWindow, WCQ_SCROLL_TARGET_Y, 0,
                                   &target) ||
            animation == 0 || target != expectedBurstTarget ||
            !wait_for_smooth_scroll_settle(
                longWindow, longPageView, 0, target, 1, &finalFrames,
                &distinctPositions) ||
            finalFrames - initialFrames < 2) {
            fwprintf(stderr,
                     L"rapid wheel input did not coalesce into one exact animation target (active=%lld target=%lld expected=%lld frames=%lld..%lld)\n",
                     (long long)animation, (long long)target,
                     (long long)expectedBurstTarget,
                     (long long)initialFrames, (long long)finalFrames);
            goto cleanup;
        }

        if (!send_message_bounded(longPageView, WM_VSCROLL,
                                  MAKEWPARAM(SB_TOP, 0), 0, NULL) ||
            !send_message_bounded(
                longEditor, WM_MOUSEWHEEL,
                MAKEWPARAM(0, (WORD)(SHORT)-WHEEL_DELTA), 0, NULL) ||
            !query_wordcraft_state(longWindow, WCQ_SCROLL_ANIMATING, 0,
                                   &animation) ||
            animation == 0 ||
            !send_message_bounded(longPageView, WM_VSCROLL,
                                  MAKEWPARAM(SB_BOTTOM, 0), 0, NULL) ||
            !query_wordcraft_state(longWindow, WCQ_SCROLL_ANIMATING, 0,
                                   &animation) ||
            !get_vertical_scroll_state(longPageView, &viewScrollY,
                                       &maximumScroll) ||
            animation != 0 || viewScrollY != maximumScroll ||
            !send_message_bounded(
                longEditor, WM_MOUSEWHEEL,
                MAKEWPARAM(0, (WORD)(SHORT)WHEEL_DELTA), 0, NULL) ||
            !query_wordcraft_state(longWindow, WCQ_SCROLL_ANIMATING, 0,
                                   &animation) ||
            animation == 0 ||
            !send_message_bounded(longPageView, WM_VSCROLL,
                                  MAKEWPARAM(SB_TOP, 0), 0, NULL) ||
            !query_wordcraft_state(longWindow, WCQ_SCROLL_ANIMATING, 0,
                                   &animation) ||
            !get_vertical_scroll_state(longPageView, &viewScrollY, NULL) ||
            animation != 0 || viewScrollY != 0) {
            fwprintf(stderr,
                     L"SB_TOP/SB_BOTTOM did not synchronously cancel active scrolling (y=%lld max=%lld active=%lld)\n",
                     (long long)viewScrollY, (long long)maximumScroll,
                     (long long)animation);
            goto cleanup;
        }
        if (!get_selection_bounded(longEditor, &selectionStart,
                                   &selectionEnd) ||
            selectionStart != 10 || selectionEnd != 20 ||
            !query_wordcraft_state(longWindow, WCQ_CURRENT_PAGE, 0,
                                   &currentPage) ||
            currentPage != 1) {
            fwprintf(stderr,
                     L"smooth-scroll accumulation or cancellation changed selection state\n");
            goto cleanup;
        }
    }
    {
        int scrollAttempt;
        LRESULT previousScroll = viewScrollY;
        for (scrollAttempt = 0; scrollAttempt < 50; ++scrollAttempt) {
            if (!send_message_bounded(longPageView, WM_VSCROLL,
                                      MAKEWPARAM(SB_PAGEDOWN, 0), 0,
                                      NULL) ||
                !query_wordcraft_state(longWindow, WCQ_VIEW_SCROLL_Y, 0,
                                       &viewScrollY) ||
                !query_wordcraft_state(longWindow,
                                       WCQ_FIRST_VISIBLE_PAGE, 0,
                                       &firstVisiblePage) ||
                viewScrollY < previousScroll) {
                fwprintf(stderr, L"continuous page-down scrolling was not monotonic\n");
                goto cleanup;
            }
            if (firstVisiblePage >= 2) {
                break;
            }
            previousScroll = viewScrollY;
        }
        if (firstVisiblePage < 2) {
            fwprintf(stderr, L"scrolling never crossed the first page boundary\n");
            goto cleanup;
        }
    }
    if (!send_message_bounded(longPageView, WM_VSCROLL,
                              MAKEWPARAM(SB_BOTTOM, 0), 0, NULL) ||
        !wait_for_page_view(longWindow, (LONG)pageCount, 0, TRUE,
                            &firstVisiblePage, &lastVisiblePage,
                            &viewScrollY) ||
        !query_wordcraft_state(longWindow, WCQ_VIEW_SCROLL_MAX, 0,
                               &viewScrollMax) ||
        viewScrollY != viewScrollMax || lastVisiblePage != pageCount ||
        !get_selection_bounded(longEditor, &selectionStart, &selectionEnd) ||
        selectionStart != 10 || selectionEnd != 20 ||
        !query_wordcraft_state(longWindow, WCQ_CURRENT_PAGE, 0,
                               &currentPage) ||
        currentPage != 1) {
        fwprintf(stderr, L"bottom scrolling changed selection/page state (selection=%lu..%lu page=%lld first=%lld last=%lld y=%lld max=%lld)\n",
                 (unsigned long)selectionStart,
                 (unsigned long)selectionEnd,
                 (long long)currentPage, (long long)firstVisiblePage,
                 (long long)lastVisiblePage, (long long)viewScrollY,
                 (long long)viewScrollMax);
        goto cleanup;
    }
    if (!send_message_bounded(longPageView, WM_VSCROLL,
                              MAKEWPARAM(SB_TOP, 0), 0, NULL) ||
        !wait_for_page_view(longWindow, 1, 1, FALSE, NULL, NULL, NULL) ||
        !send_message_bounded(longEditor, EM_SETSEL, 10, 10, NULL) ||
        !send_message_bounded(longPageView, WM_VSCROLL,
                              MAKEWPARAM(SB_BOTTOM, 0), 0, NULL) ||
        !wait_for_page_view(longWindow, (LONG)pageCount, 0, TRUE,
                            NULL, NULL, NULL) ||
        !get_selection_bounded(longEditor, &selectionStart, &selectionEnd) ||
        selectionStart != 10 || selectionEnd != 10 ||
        !query_wordcraft_state(longWindow, WCQ_CURRENT_PAGE, 0,
                               &currentPage) ||
        currentPage != 1) {
        fwprintf(stderr, L"continuous scrolling moved a collapsed caret (selection=%lu..%lu page=%lld)\n",
                 (unsigned long)selectionStart,
                 (unsigned long)selectionEnd, (long long)currentPage);
        goto cleanup;
    }
    {
        LONG targetPage = (LONG)pageCount / 2 + 1;
        LRESULT targetStart = -1;
        if (!query_wordcraft_state(longWindow, WCQ_PAGE_START,
                                   targetPage - 1, &targetStart) ||
            targetStart < 0 ||
            !send_message_bounded(longEditor, EM_SETSEL,
                                  (WPARAM)targetStart,
                                  (LPARAM)targetStart, NULL) ||
            !wait_for_page_view(longWindow, targetPage, 0, FALSE,
                                &firstVisiblePage, &lastVisiblePage,
                                NULL) ||
            !get_selection_bounded(longEditor, &selectionStart,
                                   &selectionEnd) ||
            selectionStart != (DWORD)targetStart ||
            selectionEnd != (DWORD)targetStart ||
            !query_wordcraft_state(longWindow, WCQ_CURRENT_PAGE, 0,
                                   &currentPage) ||
            currentPage != targetPage) {
            LRESULT selectionMessages = -1;
            LRESULT rendererSelectionStart = -1;
            LRESULT rendererSelectionResult = -1;
            LRESULT rendererSelectionPage = -1;
            query_wordcraft_state(
                longWindow, WCQ_RENDER_ENGINE_SELECTION_MESSAGE_COUNT, 0,
                &selectionMessages);
            query_wordcraft_state(
                longWindow, WCQ_RENDER_ENGINE_LAST_SELECTION_START, 0,
                &rendererSelectionStart);
            query_wordcraft_state(
                longWindow, WCQ_RENDER_ENGINE_LAST_SELECTION_RESULT, 0,
                &rendererSelectionResult);
            query_wordcraft_state(
                longWindow, WCQ_RENDER_ENGINE_LAST_SELECTION_PAGE, 0,
                &rendererSelectionPage);
            fwprintf(stderr, L"caret-page synchronization failed (target=%ld start=%lld selection=%lu..%lu page=%lld first=%lld last=%lld renderer_messages=%lld renderer_start=%lld renderer_hr=0x%llX renderer_page=%lld)\n",
                     targetPage, (long long)targetStart,
                     (unsigned long)selectionStart,
                     (unsigned long)selectionEnd,
                     (long long)currentPage,
                     (long long)firstVisiblePage,
                     (long long)lastVisiblePage,
                     (long long)selectionMessages,
                     (long long)rendererSelectionStart,
                     (unsigned long long)rendererSelectionResult,
                     (long long)rendererSelectionPage);
            goto cleanup;
        }
    }
    if (!send_message_bounded(longEditor, EM_GETMODIFY, 0, 0,
                              &longModified) ||
        longModified != 0) {
        fwprintf(stderr, L"pagination probe unexpectedly modified the long document\n");
        goto cleanup;
    }
    commentEdit = find_control(longWindow, IDC_COMMENT_EDIT);
    if (commentEdit == NULL ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_COUNT, 0,
                               &commentCount) ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_ACTIVE_INDEX, 0,
                               &activeComment) ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_MARGIN_VISIBLE, 0,
                               &commentMarginVisible) ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_CARD_COUNT, 0,
                               &commentCardCount) ||
        commentCount != 0 || activeComment != -1 ||
        commentMarginVisible != 0 || commentCardCount != 0 ||
        !send_message_bounded(longEditor, EM_SETSEL, 0, 3, NULL) ||
        !send_message_bounded(longWindow, WM_COMMAND,
                              MAKEWPARAM(IDM_REVIEW_ADD_COMMENT, 0), 0,
                              NULL) ||
        !wait_for_ribbon_state(longWindow, RIBBON_TAB_REVIEW) ||
        !wait_for_ribbon_focus(longWindow, RIBBON_FOCUS_PANEL) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_COMPOSITION_ACTIVE, 0,
                               &commentCompositionActive) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_VISIBLE, 0,
                               &commentHighlightVisible) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_START, 0,
                               &commentHighlightStart) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_END, 0,
                               &commentHighlightEnd) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_COLOR, 0,
                               &commentHighlightColor) ||
        !send_message_bounded(longEditor, EM_GETMODIFY, 0, 0,
                              &longModified) ||
        commentCompositionActive != 1 || commentHighlightVisible != 1 ||
        commentHighlightStart != 0 || commentHighlightEnd != 3 ||
        (COLORREF)commentHighlightColor !=
            WORDCRAFT_COMMENT_HIGHLIGHT_COLOR || longModified != 0) {
        fwprintf(stderr,
                 L"a selected comment draft was not highlighted temporarily "
                 L"without modifying the RTF\n");
        goto cleanup;
    }
    if (!send_message_bounded(commentEdit, WM_SETTEXT, 0,
                              (LPARAM)firstCommentText, NULL) ||
        !send_message_bounded(longWindow, WM_COMMAND,
                              MAKEWPARAM(IDM_REVIEW_ADD_COMMENT, 0), 0,
                              NULL) ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_COUNT, 0,
                               &commentCount) ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_ACTIVE_INDEX, 0,
                               &activeComment) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_COMPOSITION_ACTIVE, 0,
                               &commentCompositionActive) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_VISIBLE, 0,
                               &commentHighlightVisible) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_START, 0,
                               &commentHighlightStart) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_END, 0,
                               &commentHighlightEnd) ||
        commentCount != 1 || activeComment != 0 ||
        commentCompositionActive != 0 || commentHighlightVisible != 1 ||
        commentHighlightStart != 0 || commentHighlightEnd != 3 ||
        !comment_matches(longWindow, 0, 0, 3, firstCommentText) ||
        !send_message_bounded(longEditor, WM_GETTEXTLENGTH, 0, 0,
                              &currentPage) ||
        currentPage != longTextLength) {
        fwprintf(stderr,
                 L"adding the first anchored Unicode comment failed or changed document text\n");
        goto cleanup;
    }
    /* Give the hidden probe enough width to expose the entire page and rail;
     * real narrow windows reach the same cards with the horizontal bar. */
    if (!SetWindowPos(longWindow, NULL, 0, 0, 2200, 1200,
                      SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        fwprintf(stderr, L"could not resize the side-comment probe window\n");
        goto cleanup;
    }
    if (!wait_for_comment_margin(longWindow, 1, 0, TRUE,
                                 &commentMarginLeft,
                                 &commentMarginWidth) ||
        !GetWindowRect(longEditor, &commentEditorRect)) {
        fwprintf(stderr,
                 L"the first comment did not create a visible side-by-side card rail\n");
        goto cleanup;
    }
    MapWindowPoints(HWND_DESKTOP, longPageView,
                    (POINT *)&commentEditorRect, 2);
    if (commentMarginLeft <= commentEditorRect.right ||
        commentMarginWidth < 180) {
        fwprintf(stderr,
                 L"the comment rail was not positioned beside the page (page_right=%ld rail_left=%lld width=%lld)\n",
                 commentEditorRect.right, (long long)commentMarginLeft,
                 (long long)commentMarginWidth);
        goto cleanup;
    }
    if (!move_ribbon_to_tab(longWindow, longRibbonTabs, RIBBON_TAB_HOME) ||
        !wait_for_comment_margin(longWindow, 1, 0, TRUE, NULL, NULL)) {
        fwprintf(stderr,
                 L"the side comment rail disappeared after leaving the Review ribbon\n");
        goto cleanup;
    }
    if (!send_message_bounded(longWindow, WM_COMMAND,
                              MAKEWPARAM(IDM_VIEW_DARK_MODE, 0), 0, NULL) ||
        !query_wordcraft_state(longWindow, WCQ_DARK_MODE, 0, &darkMode) ||
        darkMode != 1 ||
        !wait_for_comment_margin(longWindow, 1, 0, TRUE, NULL, NULL) ||
        !send_message_bounded(longWindow, WM_COMMAND,
                              MAKEWPARAM(IDM_VIEW_DARK_MODE, 0), 0, NULL) ||
        !query_wordcraft_state(longWindow, WCQ_DARK_MODE, 0, &darkMode) ||
        darkMode != 0 ||
        !wait_for_comment_margin(longWindow, 1, 0, TRUE, NULL, NULL) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_VISIBLE, 0,
                               &commentHighlightVisible) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_START, 0,
                               &commentHighlightStart) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_END, 0,
                               &commentHighlightEnd) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_COLOR, 0,
                               &commentHighlightColor) ||
        commentHighlightVisible != 1 || commentHighlightStart != 0 ||
        commentHighlightEnd != 3 ||
        (COLORREF)commentHighlightColor !=
            WORDCRAFT_COMMENT_HIGHLIGHT_COLOR) {
        fwprintf(stderr,
                 L"the side comment rail or temporary yellow highlight did not "
                 L"survive a dark-mode round trip\n");
        goto cleanup;
    }
    if (!send_message_bounded(longEditor, EM_SETSEL, 10, 10, NULL) ||
        !wait_for_comment_margin(longWindow, 1, 0, TRUE, NULL, NULL) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_VISIBLE, 0,
                               &commentHighlightVisible) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_START, 0,
                               &commentHighlightStart) ||
        commentHighlightVisible != 0 || commentHighlightStart != -1 ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_ACTIVE_CARD_LEFT, 0,
                               &commentCardLeft) ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_ACTIVE_CARD_TOP, 0,
                               &commentCardTop) ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_ACTIVE_CARD_RIGHT, 0,
                               &commentCardRight) ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_ACTIVE_CARD_BOTTOM, 0,
                               &commentCardBottom) ||
        commentCardLeft < commentMarginLeft ||
        commentCardRight > commentMarginLeft + commentMarginWidth ||
        commentCardRight <= commentCardLeft ||
        commentCardBottom <= commentCardTop) {
        fwprintf(stderr,
                 L"the active side comment card did not expose valid page-view geometry\n");
        goto cleanup;
    }
    /* The hidden test window can require horizontal scrolling; click the
     * visible leading edge of the card rather than an off-viewport center. */
    commentClickPoint.x = (LONG)min(commentCardRight - 1,
                                    commentCardLeft + 10);
    commentClickPoint.y = (LONG)((commentCardTop + commentCardBottom) / 2);
    if (!send_message_bounded(longPageView, WM_LBUTTONDOWN, MK_LBUTTON,
                              MAKELPARAM(commentClickPoint.x,
                                         commentClickPoint.y), NULL) ||
        !get_selection_bounded(longEditor, &selectionStart, &selectionEnd) ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_ACTIVE_INDEX, 0,
                               &activeComment) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_VISIBLE, 0,
                               &commentHighlightVisible) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_START, 0,
                               &commentHighlightStart) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_END, 0,
                               &commentHighlightEnd) ||
        selectionStart != 0 || selectionEnd != 3 || activeComment != 0 ||
        commentHighlightVisible != 1 || commentHighlightStart != 0 ||
        commentHighlightEnd != 3) {
        fwprintf(stderr,
                 L"clicking a side comment card did not select its anchored text "
                 L"(card=%lld,%lld..%lld,%lld click=%ld,%ld selection=%lu..%lu active=%lld)\n",
                 (long long)commentCardLeft, (long long)commentCardTop,
                 (long long)commentCardRight, (long long)commentCardBottom,
                 commentClickPoint.x, commentClickPoint.y,
                 (unsigned long)selectionStart,
                 (unsigned long)selectionEnd, (long long)activeComment);
        goto cleanup;
    }
    if (!send_message_bounded(longEditor, EM_SETSEL,
                              (WPARAM)(secondPageStart + 5),
                              (LPARAM)(secondPageStart + 12), NULL) ||
        !send_message_bounded(commentEdit, WM_SETTEXT, 0,
                              (LPARAM)secondCommentText, NULL) ||
        !send_message_bounded(longWindow, WM_COMMAND,
                              MAKEWPARAM(IDM_REVIEW_ADD_COMMENT, 0), 0,
                              NULL) ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_COUNT, 0,
                               &commentCount) ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_ACTIVE_INDEX, 0,
                               &activeComment) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_START, 0,
                               &commentHighlightStart) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_END, 0,
                               &commentHighlightEnd) ||
        commentCount != 2 || activeComment != 1 ||
        commentHighlightStart != secondPageStart + 5 ||
        commentHighlightEnd != secondPageStart + 12 ||
        !comment_matches(longWindow, 0, 0, 3, firstCommentText) ||
        !comment_matches(longWindow, 1, (LONG)secondPageStart + 5,
                         (LONG)secondPageStart + 12, secondCommentText)) {
        fwprintf(stderr,
                 L"adding a second page-anchored comment failed\n");
        goto cleanup;
    }
    if (!wait_for_comment_margin(longWindow, 2, 1, TRUE, NULL, NULL)) {
        fwprintf(stderr,
                 L"the second page comment did not appear as the active side card\n");
        goto cleanup;
    }
    if (!send_message_bounded(longWindow, WM_COMMAND,
                              MAKEWPARAM(IDM_REVIEW_PREVIOUS_COMMENT, 0), 0,
                              NULL) ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_ACTIVE_INDEX, 0,
                               &activeComment) ||
        !get_selection_bounded(longEditor, &selectionStart, &selectionEnd) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_START, 0,
                               &commentHighlightStart) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_END, 0,
                               &commentHighlightEnd) ||
        activeComment != 0 || selectionStart != 0 || selectionEnd != 3 ||
        commentHighlightStart != 0 || commentHighlightEnd != 3 ||
        !send_message_bounded(longWindow, WM_COMMAND,
                              MAKEWPARAM(IDM_REVIEW_NEXT_COMMENT, 0), 0,
                              NULL) ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_ACTIVE_INDEX, 0,
                               &activeComment) ||
        !get_selection_bounded(longEditor, &selectionStart, &selectionEnd) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_START, 0,
                               &commentHighlightStart) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_END, 0,
                               &commentHighlightEnd) ||
        activeComment != 1 ||
        selectionStart != (DWORD)(secondPageStart + 5) ||
        selectionEnd != (DWORD)(secondPageStart + 12) ||
        commentHighlightStart != secondPageStart + 5 ||
        commentHighlightEnd != secondPageStart + 12) {
        fwprintf(stderr,
                 L"Previous/Next Comment did not navigate to the stored anchors\n");
        goto cleanup;
    }
    if (!send_message_bounded(longWindow, WM_COMMAND,
                              MAKEWPARAM(IDM_REVIEW_DELETE_COMMENT, 0), 0,
                              NULL) ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_COUNT, 0,
                               &commentCount) ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_ACTIVE_INDEX, 0,
                               &activeComment) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_VISIBLE, 0,
                               &commentHighlightVisible) ||
        commentCount != 1 || activeComment != 0 ||
        commentHighlightVisible != 0 ||
        !comment_matches(longWindow, 0, 0, 3, firstCommentText) ||
        !send_message_bounded(longEditor, WM_GETTEXTLENGTH, 0, 0,
                              &currentPage) ||
        currentPage != longTextLength) {
        fwprintf(stderr,
                 L"Delete Comment did not remove only the active comment\n");
        goto cleanup;
    }
    if (!wait_for_comment_margin(longWindow, 1, 0, FALSE, NULL, NULL)) {
        fwprintf(stderr,
                 L"deleting one comment removed or desynchronized the side rail\n");
        goto cleanup;
    }
    if (!send_message_bounded(longWindow, WM_COMMAND,
                              MAKEWPARAM(IDM_REVIEW_DELETE_COMMENT, 0), 0,
                              NULL) ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_COUNT, 0,
                               &commentCount) ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_MARGIN_VISIBLE, 0,
                               &commentMarginVisible) ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_CARD_COUNT, 0,
                               &commentCardCount) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_MARGIN_ACTIVE_INDEX, 0,
                               &activeComment) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_VISIBLE, 0,
                               &commentHighlightVisible) ||
        commentCount != 0 || commentMarginVisible != 0 ||
        commentCardCount != 0 || activeComment != -1 ||
        commentHighlightVisible != 0) {
        fwprintf(stderr,
                 L"the side comment rail did not collapse after deleting the last comment\n");
        goto cleanup;
    }
    if (!send_message_bounded(longEditor, EM_SETSEL, 0, 3, NULL) ||
        !send_message_bounded(commentEdit, WM_SETTEXT, 0,
                              (LPARAM)firstCommentText, NULL) ||
        !send_message_bounded(longWindow, WM_COMMAND,
                              MAKEWPARAM(IDM_REVIEW_ADD_COMMENT, 0), 0,
                              NULL) ||
        !wait_for_comment_margin(longWindow, 1, 0, TRUE, NULL, NULL) ||
        !comment_matches(longWindow, 0, 0, 3, firstCommentText)) {
        fwprintf(stderr,
                 L"the side comment rail did not return after re-adding a comment\n");
        goto cleanup;
    }
    {
        const LONG partialStart = 3;
        const LONG partialEnd = 8;
        const LONG firstOutsideStart = 24;
        WCHAR textBefore[256];
        WCHAR textAfter[256];
        CHARFORMAT2W firstCharacterBefore;
        CHARFORMAT2W firstCharacterAfter;
        CHARFORMAT2W secondCharacterBefore;
        CHARFORMAT2W secondCharacterAfter;
        PARAFORMAT2 firstParagraphBefore;
        PARAFORMAT2 firstParagraphAfter;
        PARAFORMAT2 secondParagraphBefore;
        PARAFORMAT2 secondParagraphAfter;
        LRESULT textLengthBefore = 0;
        LRESULT textLengthAfter = 0;
        LRESULT canUndo = 0;
        LONG secondParagraphStart = -1;
        DWORD rangeStart = 0;
        DWORD rangeEnd = 0;

        ZeroMemory(textBefore, sizeof(textBefore));
        ZeroMemory(textAfter, sizeof(textAfter));
        if (!find_text_bounded(longEditor, longProcess.hProcess,
                               L"Pagination probe line 0002",
                               &secondParagraphStart) ||
            secondParagraphStart <= firstOutsideStart + 6 ||
            !send_message_bounded(longEditor, EM_SETSEL, partialEnd,
                                  partialStart, NULL) ||
            !send_message_bounded(longWindow, WM_COMMAND,
                                  MAKEWPARAM(IDM_STYLE_NORMAL, 0), 0,
                                  NULL) ||
            !send_message_bounded(longEditor, EM_EMPTYUNDOBUFFER, 0, 0,
                                  NULL) ||
            !send_message_bounded(longEditor, WM_GETTEXTLENGTH, 0, 0,
                                  &textLengthBefore) ||
            !get_window_text_bounded(longEditor, textBefore,
                                     ARRAYSIZE(textBefore))) {
            fwprintf(stderr,
                     L"could not prepare the partial-paragraph style undo probe\n");
            goto cleanup;
        }

        if (!send_message_bounded(longEditor, EM_SETSEL,
                                  firstOutsideStart,
                                  firstOutsideStart + 6, NULL) ||
            !get_character_format_bounded(longEditor, longProcess.hProcess,
                                          &firstCharacterBefore) ||
            !get_paragraph_format_bounded(longEditor, longProcess.hProcess,
                                          &firstParagraphBefore) ||
            !send_message_bounded(longEditor, EM_SETSEL,
                                  secondParagraphStart + 3,
                                  secondParagraphStart + 9, NULL) ||
            !get_character_format_bounded(longEditor, longProcess.hProcess,
                                          &secondCharacterBefore) ||
            !get_paragraph_format_bounded(longEditor, longProcess.hProcess,
                                          &secondParagraphBefore) ||
            !send_message_bounded(longEditor, EM_SETSEL, partialEnd,
                                  partialStart, NULL) ||
            !get_selection_bounded(longEditor, &rangeStart, &rangeEnd) ||
            rangeStart != (DWORD)partialStart ||
            rangeEnd != (DWORD)partialEnd) {
            fwprintf(stderr,
                     L"could not establish a backward partial-paragraph selection\n");
            goto cleanup;
        }

        if (!send_message_bounded(longWindow, WM_COMMAND,
                                  MAKEWPARAM(IDM_STYLE_HEADING_1, 0), 0,
                                  NULL) ||
            !get_selection_bounded(longEditor, &rangeStart, &rangeEnd) ||
            rangeStart != (DWORD)partialStart ||
            rangeEnd != (DWORD)partialEnd ||
            !send_message_bounded(longEditor, EM_CANUNDO, 0, 0,
                                  &canUndo) ||
            canUndo == 0) {
            fwprintf(stderr,
                     L"Heading 1 did not preserve the partial selection or create an undo transaction\n");
            goto cleanup;
        }

        if (!send_message_bounded(longEditor, EM_SETSEL,
                                  firstOutsideStart,
                                  firstOutsideStart + 6, NULL) ||
            !get_character_format_bounded(longEditor, longProcess.hProcess,
                                          &firstCharacterAfter) ||
            !get_paragraph_format_bounded(longEditor, longProcess.hProcess,
                                          &firstParagraphAfter) ||
            firstCharacterAfter.yHeight != 320 ||
            (firstCharacterAfter.dwEffects & CFE_BOLD) == 0 ||
            firstCharacterAfter.crTextColor != RGB(37, 76, 132) ||
            firstParagraphAfter.wAlignment != PFA_LEFT ||
            firstParagraphAfter.dySpaceBefore != 240 ||
            firstParagraphAfter.dySpaceAfter != 60 ||
            !send_message_bounded(longEditor, EM_SETSEL,
                                  secondParagraphStart + 3,
                                  secondParagraphStart + 9, NULL) ||
            !get_character_format_bounded(longEditor, longProcess.hProcess,
                                          &secondCharacterAfter) ||
            !get_paragraph_format_bounded(longEditor, longProcess.hProcess,
                                          &secondParagraphAfter) ||
            !character_formats_match(&secondCharacterBefore,
                                     &secondCharacterAfter) ||
            !paragraph_formats_match(&secondParagraphBefore,
                                     &secondParagraphAfter)) {
            fwprintf(stderr,
                     L"a partial Heading 1 selection did not expand to exactly one paragraph\n");
            goto cleanup;
        }

        if (!send_message_bounded(longWindow, WM_COMMAND,
                                  MAKEWPARAM(IDM_EDIT_UNDO, 0), 0, NULL) ||
            !send_message_bounded(longEditor, EM_CANUNDO, 0, 0,
                                  &canUndo) ||
            canUndo != 0 ||
            !send_message_bounded(longEditor, EM_SETSEL,
                                  firstOutsideStart,
                                  firstOutsideStart + 6, NULL) ||
            !get_character_format_bounded(longEditor, longProcess.hProcess,
                                          &firstCharacterAfter) ||
            !get_paragraph_format_bounded(longEditor, longProcess.hProcess,
                                          &firstParagraphAfter) ||
            !character_formats_match(&firstCharacterBefore,
                                     &firstCharacterAfter) ||
            !paragraph_formats_match(&firstParagraphBefore,
                                     &firstParagraphAfter) ||
            !send_message_bounded(longEditor, EM_SETSEL,
                                  secondParagraphStart + 3,
                                  secondParagraphStart + 9, NULL) ||
            !get_character_format_bounded(longEditor, longProcess.hProcess,
                                          &secondCharacterAfter) ||
            !get_paragraph_format_bounded(longEditor, longProcess.hProcess,
                                          &secondParagraphAfter) ||
            !character_formats_match(&secondCharacterBefore,
                                     &secondCharacterAfter) ||
            !paragraph_formats_match(&secondParagraphBefore,
                                     &secondParagraphAfter) ||
            !send_message_bounded(longEditor, WM_GETTEXTLENGTH, 0, 0,
                                  &textLengthAfter) ||
            !get_window_text_bounded(longEditor, textAfter,
                                     ARRAYSIZE(textAfter)) ||
            textLengthAfter != textLengthBefore ||
            wcscmp(textAfter, textBefore) != 0) {
            fwprintf(stderr,
                     L"one Undo did not atomically restore both parts of the paragraph style\n");
            goto cleanup;
        }
    }
    {
        const DWORD scriptEffects = CFE_SUBSCRIPT | CFE_SUPERSCRIPT;
        const DWORD styleCharacterMask =
            CFM_FACE | CFM_SIZE | CFM_BOLD | CFM_COLOR;
        const DWORD styleParagraphMask =
            PFM_ALIGNMENT | PFM_NUMBERING | PFM_SPACEBEFORE |
            PFM_SPACEAFTER | PFM_LINESPACING;
        CHARFORMAT2W character;
        PARAFORMAT2 paragraphBefore;
        PARAFORMAT2 paragraphAfter;
        LONG originalSize;
        BYTE expectedSpacingRule;
        LONG expectedSpacing;

        ZeroMemory(&character, sizeof(character));
        if (!send_message_bounded(longEditor, EM_SETSEL, 0, 3, NULL) ||
            !get_character_format_bounded(longEditor, longProcess.hProcess,
                                          &character) ||
            (character.dwMask & CFM_SIZE) == 0 ||
            character.yHeight <= 0) {
            fwprintf(stderr,
                     L"could not capture a uniform selection for the expanded Font commands\n");
            goto cleanup;
        }
        originalSize = character.yHeight;
        if (!send_message_bounded(longWindow, WM_COMMAND,
                                  MAKEWPARAM(IDM_FORMAT_GROW_FONT, 0), 0,
                                  NULL) ||
            !get_character_format_bounded(longEditor, longProcess.hProcess,
                                          &character) ||
            (character.dwMask & CFM_SIZE) == 0 ||
            character.yHeight <= originalSize ||
            !send_message_bounded(longWindow, WM_COMMAND,
                                  MAKEWPARAM(IDM_FORMAT_SHRINK_FONT, 0), 0,
                                  NULL) ||
            !get_character_format_bounded(longEditor, longProcess.hProcess,
                                          &character) ||
            (character.dwMask & CFM_SIZE) == 0 ||
            character.yHeight != originalSize) {
            fwprintf(stderr,
                     L"Grow Font and Shrink Font did not make a reversible size change\n");
            goto cleanup;
        }

        if (!send_message_bounded(longWindow, WM_COMMAND,
                                  MAKEWPARAM(IDM_FORMAT_SUBSCRIPT, 0), 0,
                                  NULL) ||
            !get_character_format_bounded(longEditor, longProcess.hProcess,
                                          &character) ||
            (character.dwMask & CFM_SUBSCRIPT) != CFM_SUBSCRIPT ||
            (character.dwEffects & scriptEffects) != CFE_SUBSCRIPT ||
            !send_message_bounded(longWindow, WM_COMMAND,
                                  MAKEWPARAM(IDM_FORMAT_SUPERSCRIPT, 0), 0,
                                  NULL) ||
            !get_character_format_bounded(longEditor, longProcess.hProcess,
                                          &character) ||
            (character.dwMask & CFM_SUBSCRIPT) != CFM_SUBSCRIPT ||
            (character.dwEffects & scriptEffects) != CFE_SUPERSCRIPT ||
            !send_message_bounded(longWindow, WM_COMMAND,
                                  MAKEWPARAM(IDM_FORMAT_SUPERSCRIPT, 0), 0,
                                  NULL) ||
            !get_character_format_bounded(longEditor, longProcess.hProcess,
                                          &character) ||
            (character.dwEffects & scriptEffects) != 0) {
            fwprintf(stderr,
                     L"Subscript and Superscript were not mutually exclusive toggles\n");
            goto cleanup;
        }

        if (!send_message_bounded(longWindow, WM_COMMAND,
                                  MAKEWPARAM(IDM_FORMAT_HIGHLIGHT, 0), 0,
                                  NULL) ||
            !get_character_format_bounded(longEditor, longProcess.hProcess,
                                          &character) ||
            (character.dwMask & CFM_BACKCOLOR) == 0 ||
            (character.dwEffects & CFE_AUTOBACKCOLOR) != 0 ||
            character.crBackColor != RGB(255, 235, 92) ||
            !send_message_bounded(longWindow, WM_COMMAND,
                                  MAKEWPARAM(IDM_FORMAT_HIGHLIGHT, 0), 0,
                                  NULL) ||
            !get_character_format_bounded(longEditor, longProcess.hProcess,
                                          &character) ||
            (character.dwMask & CFM_BACKCOLOR) == 0 ||
            (character.dwEffects & CFE_AUTOBACKCOLOR) == 0) {
            fwprintf(stderr,
                     L"Highlight did not toggle the expected yellow character background\n");
            goto cleanup;
        }

        ZeroMemory(&paragraphAfter, sizeof(paragraphAfter));
        if (!send_message_bounded(longWindow, WM_COMMAND,
                                  MAKEWPARAM(IDM_FORMAT_NUMBERING, 0), 0,
                                  NULL) ||
            !get_paragraph_format_bounded(longEditor, longProcess.hProcess,
                                          &paragraphAfter) ||
            (paragraphAfter.dwMask &
             (PFM_NUMBERING | PFM_NUMBERINGSTYLE)) !=
                (PFM_NUMBERING | PFM_NUMBERINGSTYLE) ||
            paragraphAfter.wNumbering != PFN_ARABIC ||
            paragraphAfter.wNumberingStyle != PFNS_PERIOD ||
            !send_message_bounded(longWindow, WM_COMMAND,
                                  MAKEWPARAM(IDM_FORMAT_NUMBERING, 0), 0,
                                  NULL) ||
            !get_paragraph_format_bounded(longEditor, longProcess.hProcess,
                                          &paragraphAfter) ||
            (paragraphAfter.dwMask & PFM_NUMBERING) == 0 ||
            paragraphAfter.wNumbering != 0) {
            fwprintf(stderr,
                     L"Numbered List did not apply and remove Arabic paragraph numbering\n");
            goto cleanup;
        }

        ZeroMemory(&paragraphBefore, sizeof(paragraphBefore));
        if (!get_paragraph_format_bounded(longEditor, longProcess.hProcess,
                                          &paragraphBefore)) {
            fwprintf(stderr,
                     L"could not capture paragraph spacing before cycling it\n");
            goto cleanup;
        }
        expected_next_line_spacing(&paragraphBefore, &expectedSpacingRule,
                                   &expectedSpacing);
        if (!send_message_bounded(longWindow, WM_COMMAND,
                                  MAKEWPARAM(IDM_FORMAT_LINE_SPACING, 0), 0,
                                  NULL) ||
            !get_paragraph_format_bounded(longEditor, longProcess.hProcess,
                                          &paragraphAfter) ||
            (paragraphAfter.dwMask & PFM_LINESPACING) == 0 ||
            paragraphAfter.bLineSpacingRule != expectedSpacingRule ||
            paragraphAfter.dyLineSpacing != expectedSpacing) {
            fwprintf(stderr,
                     L"Line Spacing did not advance to the next documented spacing state\n");
            goto cleanup;
        }

        if (!send_message_bounded(longWindow, WM_COMMAND,
                                  MAKEWPARAM(IDM_STYLE_HEADING_1, 0), 0,
                                  NULL) ||
            !get_character_format_bounded(longEditor, longProcess.hProcess,
                                          &character) ||
            !get_paragraph_format_bounded(longEditor, longProcess.hProcess,
                                          &paragraphAfter) ||
            (character.dwMask & styleCharacterMask) != styleCharacterMask ||
            character.yHeight != 320 ||
            (character.dwEffects & CFE_BOLD) == 0 ||
            character.crTextColor != RGB(37, 76, 132) ||
            lstrcmpW(character.szFaceName,
                     WORDCRAFT_DEFAULT_FONT_FACE) != 0 ||
            (paragraphAfter.dwMask & styleParagraphMask) !=
                styleParagraphMask ||
            paragraphAfter.wAlignment != PFA_LEFT ||
            paragraphAfter.wNumbering != 0 ||
            paragraphAfter.dySpaceBefore != 240 ||
            paragraphAfter.dySpaceAfter != 60 ||
            paragraphAfter.bLineSpacingRule !=
                WORDCRAFT_DEFAULT_LINE_SPACING_RULE ||
            paragraphAfter.dyLineSpacing !=
                WORDCRAFT_DEFAULT_LINE_SPACING) {
            fwprintf(stderr,
                     L"Heading 1 did not apply its character and paragraph style\n");
            goto cleanup;
        }

        if (!send_message_bounded(longWindow, WM_COMMAND,
                                  MAKEWPARAM(IDM_STYLE_NORMAL, 0), 0,
                                  NULL) ||
            !get_character_format_bounded(longEditor, longProcess.hProcess,
                                          &character) ||
            !get_paragraph_format_bounded(longEditor, longProcess.hProcess,
                                          &paragraphAfter) ||
            (character.dwMask & styleCharacterMask) != styleCharacterMask ||
            character.yHeight != WORDCRAFT_DEFAULT_FONT_SIZE_TWIPS ||
            (character.dwEffects &
             (CFE_BOLD | CFE_ITALIC | CFE_UNDERLINE | CFE_STRIKEOUT |
              CFE_SUBSCRIPT | CFE_SUPERSCRIPT)) != 0 ||
            (character.dwEffects & CFE_AUTOBACKCOLOR) == 0 ||
            character.crTextColor != RGB(0, 0, 0) ||
            lstrcmpW(character.szFaceName,
                     WORDCRAFT_DEFAULT_FONT_FACE) != 0 ||
            (paragraphAfter.dwMask & styleParagraphMask) !=
                styleParagraphMask ||
            paragraphAfter.wAlignment != PFA_LEFT ||
            paragraphAfter.wNumbering != 0 ||
            paragraphAfter.dxStartIndent != 0 ||
            paragraphAfter.dxOffset != 0 ||
            paragraphAfter.dySpaceBefore != 0 ||
            paragraphAfter.dySpaceAfter !=
                WORDCRAFT_DEFAULT_PARAGRAPH_SPACE_AFTER_TWIPS ||
            paragraphAfter.bLineSpacingRule !=
                WORDCRAFT_DEFAULT_LINE_SPACING_RULE ||
            paragraphAfter.dyLineSpacing !=
                WORDCRAFT_DEFAULT_LINE_SPACING) {
            fwprintf(stderr,
                     L"Normal did not restore the WordCraft default character and paragraph style\n");
            goto cleanup;
        }
    }
    longMenu = GetMenu(longWindow);
    alignLeftButton = find_control(longWindow, IDC_ALIGN_LEFT);
    alignCenterButton = find_control(longWindow, IDC_ALIGN_CENTER);
    alignRightButton = find_control(longWindow, IDC_ALIGN_RIGHT);
    alignJustifyButton = find_control(longWindow, IDC_ALIGN_JUSTIFY);
    bulletsButton = find_control(longWindow, IDC_BULLETS);
    if (longMenu == NULL || alignLeftButton == NULL ||
        alignCenterButton == NULL || alignRightButton == NULL ||
        alignJustifyButton == NULL || bulletsButton == NULL ||
        !send_message_bounded(longEditor, EM_SETSEL, 0, (LPARAM)-1,
                              NULL) ||
        !click_alignment_button(alignCenterButton, longMenu,
                                IDM_FORMAT_ALIGN_CENTER) ||
        !click_alignment_button(alignLeftButton, longMenu,
                                IDM_FORMAT_ALIGN_LEFT) ||
        !click_alignment_button(alignRightButton, longMenu,
                                IDM_FORMAT_ALIGN_RIGHT) ||
        !click_alignment_button(alignCenterButton, longMenu,
                                IDM_FORMAT_ALIGN_CENTER) ||
        !click_alignment_button(alignJustifyButton, longMenu,
                                IDM_FORMAT_ALIGN_JUSTIFY)) {
        fwprintf(stderr,
                 L"format-bar alignment buttons did not update paragraph formatting and menu state\n");
        goto cleanup;
    }
    if (!menu_item_is_checked(longMenu, IDM_FORMAT_BULLETS, FALSE) ||
        !click_bullets_button(bulletsButton, longMenu, TRUE) ||
        !click_bullets_button(bulletsButton, longMenu, FALSE) ||
        !click_bullets_button(bulletsButton, longMenu, TRUE)) {
        fwprintf(stderr,
                 L"format-bar Bullets button did not toggle paragraph numbering and menu state\n");
        goto cleanup;
    }
    if (!send_message_bounded(longEditor, EM_GETMODIFY, 0, 0,
                              &longModified) ||
        longModified == 0 ||
        !send_message_bounded(longWindow, WM_COMMAND,
                              MAKEWPARAM(IDM_FILE_SAVE, 0), 0, NULL) ||
        !send_message_bounded(longEditor, EM_GETMODIFY, 0, 0,
                              &longModified) ||
        longModified != 0) {
        fwprintf(stderr,
                 L"RTF paragraph formatting was not marked modified or saved cleanly\n");
        goto cleanup;
    }
    {
        LRESULT animation = 0;
        if (!send_message_bounded(longPageView, WM_VSCROLL,
                                  MAKEWPARAM(SB_TOP, 0), 0, NULL) ||
            !send_message_bounded(
                longEditor, WM_MOUSEWHEEL,
                MAKEWPARAM(0, (WORD)(SHORT)-WHEEL_DELTA), 0, NULL) ||
            !query_wordcraft_state(longWindow, WCQ_SCROLL_ANIMATING, 0,
                                   &animation) ||
            animation == 0) {
            fwprintf(stderr,
                     L"could not start the shutdown-during-animation check\n");
            goto cleanup;
        }
    }
    if (!close_wordcraft_cleanly(longWindow, &longProcess)) {
        fwprintf(stderr,
                 L"WordCraft did not close cleanly with a scroll animation active\n");
        goto cleanup;
    }
    longWindow = NULL;

    if (!launch_hidden_wordcraft(executable, longSample, &longProcess,
                                 &longWindow)) {
        fwprintf(stderr,
                 L"could not reopen the commented RTF persistence probe (error=%lu)\n",
                 GetLastError());
        goto cleanup;
    }
    longEditor = find_control(longWindow, IDC_EDITOR);
    longPageView = longEditor != NULL ? GetParent(longEditor) : NULL;
    if (longEditor == NULL || longPageView == NULL ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_COUNT, 0,
                               &commentCount) ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_ACTIVE_INDEX, 0,
                               &activeComment) ||
        !query_wordcraft_state(longWindow,
                               WCQ_COMMENT_HIGHLIGHT_VISIBLE, 0,
                               &commentHighlightVisible) ||
        commentCount != 1 || activeComment != 0 ||
        commentHighlightVisible != 0 ||
        !comment_matches(longWindow, 0, 0, 3, firstCommentText) ||
        !send_message_bounded(longEditor, WM_GETTEXTLENGTH, 0, 0,
                              &currentPage) ||
        !send_message_bounded(longEditor, EM_GETMODIFY, 0, 0,
                              &longModified) ||
        currentPage != longTextLength || longModified != 0) {
        fwprintf(stderr,
                 L"RTF save/reopen did not preserve the comment metadata without changing text\n");
        goto cleanup;
    }
    if (!wait_for_comment_margin(longWindow, 1, 0, FALSE, NULL, NULL)) {
        fwprintf(stderr,
                 L"the persisted comment did not reopen in the side rail\n");
        goto cleanup;
    }
    if (!close_wordcraft_cleanly(longWindow, &longProcess)) {
        fwprintf(stderr,
                 L"WordCraft did not close cleanly after comment persistence validation\n");
        goto cleanup;
    }
    longWindow = NULL;

    printf("hidden_launch=ok command_line_utf8=ok default_page_margins=ok pagination=ok page_counter=ok "
           "ribbon_tabs=ok ribbon_panels=ok ribbon_keyboard=ok "
           "home_ribbon_groups=ok home_ribbon_controls=ok "
           "home_ribbon_full=ok home_ribbon_compact=ok "
           "home_ribbon_collapsed=ok collapsed_style_combo=ok "
           "collapsed_focus_fallback=ok "
           "insert_ribbon_groups=ok insert_ribbon_controls=ok "
           "insert_ribbon_icons=ok insert_ribbon_full=ok "
           "insert_ribbon_compact=ok insert_ribbon_collapsed=ok "
           "insert_ribbon_focus=ok "
           "insert_page_break=ok insert_blank_page=ok "
           "insert_datetime=ok "
           "draw_ribbon_groups=ok draw_ribbon_controls=ok "
           "draw_ribbon_icons=ok draw_ribbon_full=ok "
           "draw_ribbon_compact=ok draw_ribbon_collapsed=ok "
           "draw_ribbon_focus=ok draw_state_toggles=ok "
           "draw_commands_nonmutating=ok "
           "view_ribbon_groups=ok view_ribbon_controls=ok "
           "view_ribbon_icons=ok view_ribbon_full=ok "
           "view_ribbon_compact=ok view_ribbon_collapsed=ok "
           "view_ribbon_focus=ok view_default_state=ok "
           "view_state_toggles=ok view_commands_nonmutating=ok "
           "home_ribbon_formatting=ok home_ribbon_styles=ok "
           "style_partial_paragraph=ok style_undo_atomic=ok "
           "style_selection_endpoints=ok "
           "ribbon_tab_traversal=ok "
           "paper_sizes=ok paper_layout=ok "
           "advanced_typography=ok text_engine_defaults=ok "
           "dark_mode=ok theme_document_invariance=ok workers=ok spellcheck=ok "
           "inline_completion=ok tab_accept=ok ordinary_tab=ok toggles=ok "
           "undo=ok utf8_save=ok continuous_scroll=ok multi_page_view=ok "
           "smooth_scroll=ok scroll_60fps=ok high_resolution_wheel=ok "
           "scroll_coalescing=ok "
           "viewport_selection=ok cross_page_selection=ok "
           "caret_page_sync=ok default_font=ok format_bar=ok "
           "paragraph_formatting=ok comments=ok comment_margin=ok "
           "comment_margin_collapse=ok "
           "comment_margin_click=ok comment_navigation=ok "
           "comment_highlight=ok comment_draft_cleanup=ok "
           "highlight_nonmutating=ok "
           "comment_rtf_round_trip=ok rtf_format_save=ok clean_exit=ok\n");
    result = 0;

cleanup:
    stop_process(&longProcess);
    stop_process(&process);
    if (longSample[0] != L'\0') {
        DeleteFileW(longSample);
    }
    if (sample[0] != L'\0') {
        DeleteFileW(sample);
    }
    return result;
}
