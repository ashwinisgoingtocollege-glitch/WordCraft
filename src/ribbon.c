#ifndef COBJMACROS
#define COBJMACROS
#endif
#include "editor.h"

#include <limits.h>
#include <oleacc.h>
#include <windowsx.h>

#define RIBBON_MAX_CONTROLS_PER_PAGE 48
#define RIBBON_MAX_OWNED_CONTROLS 192
#define HOME_MAX_CONTROLS 48
#define INSERT_MAX_CONTROLS 29
#define DRAW_MAX_CONTROLS 21
#define DESIGN_MAX_CONTROLS 20
#define VIEW_MAX_CONTROLS 27
#define DESIGN_INLINE_STYLE_COUNT 10

#define DESIGN_GALLERY_CLASS_NAME L"WordCraftDesignGallery"
#define DESIGN_GALLERY_CURRENT_INDEX 0
#define DESIGN_GALLERY_RESET_INDEX DESIGN_STYLE_SET_COUNT
#define DESIGN_GALLERY_SAVE_INDEX (DESIGN_STYLE_SET_COUNT + 1)
#define DESIGN_GALLERY_ITEM_COUNT (DESIGN_STYLE_SET_COUNT + 2)

typedef struct RibbonPage {
    HWND controls[RIBBON_MAX_CONTROLS_PER_PAGE];
    size_t count;
} RibbonPage;

typedef struct HomeControlInfo {
    HWND window;
    UINT id;
    int group;
} HomeControlInfo;

typedef struct InsertControlInfo {
    HWND window;
    UINT id;
    int group;
    RibbonInsertIcon icon;
    UINT iconPaintCount;
} InsertControlInfo;

typedef struct InsertControlSpec {
    UINT id;
    const WCHAR *caption;
    int group;
    RibbonInsertIcon icon;
} InsertControlSpec;

typedef struct DrawControlInfo {
    HWND window;
    UINT id;
    int group;
    RibbonDrawIcon icon;
    UINT iconPaintCount;
} DrawControlInfo;

typedef struct DrawControlSpec {
    UINT id;
    const WCHAR *caption;
    int group;
    RibbonDrawIcon icon;
    BOOL toggle;
} DrawControlSpec;

typedef struct ViewControlInfo {
    HWND window;
    UINT id;
    int group;
    RibbonViewIcon icon;
    UINT iconPaintCount;
} ViewControlInfo;

typedef struct ViewControlSpec {
    UINT id;
    const WCHAR *caption;
    int group;
    RibbonViewIcon icon;
    BOOL toggle;
} ViewControlSpec;

typedef struct DesignControlInfo {
    HWND window;
    UINT id;
    int group;
    RibbonDesignIcon icon;
    UINT iconPaintCount;
    BOOL stylePreview;
} DesignControlInfo;

typedef struct DesignControlSpec {
    UINT id;
    const WCHAR *caption;
    int group;
    RibbonDesignIcon icon;
    BOOL toggle;
    BOOL stylePreview;
} DesignControlSpec;

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

    InsertControlInfo insertControls[INSERT_MAX_CONTROLS];
    size_t insertControlCount;
    RECT insertGroupRects[INSERT_GROUP_COUNT];
    UINT insertGroupPaintCount;
    int insertLayoutMode;
    HWND insertTooltip;

    DrawControlInfo drawControls[DRAW_MAX_CONTROLS];
    size_t drawControlCount;
    RECT drawGroupRects[DRAW_GROUP_COUNT];
    UINT drawGroupPaintCount;
    int drawLayoutMode;
    UINT activeDrawTool;
    BOOL drawModeActive;
    BOOL drawRulerVisible;
    BOOL drawBackgroundRuled;
    HWND drawTooltip;

    DesignControlInfo designControls[DESIGN_MAX_CONTROLS];
    size_t designControlCount;
    RECT designGroupRects[DESIGN_GROUP_COUNT];
    UINT designGroupPaintCount;
    int designLayoutMode;
    RECT designInlineGalleryRect;
    HWND designTooltip;
    HWND designGalleryWindow;
    HWND designGalleryReturnFocus;
    RECT designGalleryItemRects[DESIGN_GALLERY_ITEM_COUNT];
    UINT designGalleryPaintCount;
    int designGalleryFocusedIndex;
    int designGalleryHotIndex;
    BOOL designGalleryVisible;

    ViewControlInfo viewControls[VIEW_MAX_CONTROLS];
    size_t viewControlCount;
    RECT viewGroupRects[VIEW_GROUP_COUNT];
    UINT viewGroupPaintCount;
    int viewLayoutMode;
    int activeViewMode;
    BOOL viewMovementVertical;
    BOOL viewSideBySide;
    HWND viewSideBySidePeer;
    HWND viewTooltip;

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
    HWND documentChatButton;
    HWND versionHistoryButton;
    HWND darkModeButton;
    HWND paperSizeCombo;
};

static void ribbon_hide_design_gallery(RibbonContext *ribbon,
                                       BOOL restoreFocus);

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

static const WCHAR *const insertGroupNames[] = {
    L"Pages", L"Tables", L"Illustrations", L"Media", L"Links",
    L"Comments", L"Header & Footer", L"Text", L"Symbols", L"eSignature"
};

static const WCHAR *const drawGroupNames[] = {
    L"Input Mode", L"Undo", L"Drawing Tools", L"Stencils", L"Edit",
    L"Convert", L"Insert", L"Replay", L"Help"
};

static const WCHAR *const viewGroupNames[] = {
    L"Views", L"Immersive", L"Dark Mode", L"Page Movement", L"Show",
    L"Zoom", L"Window", L"Macros", L"SharePoint"
};

static const WCHAR *const designGroupNames[] = {
    L"Document Formatting", L"Page Background"
};

static const DesignControlSpec designControlSpecs[] = {
    {IDM_DESIGN_THEMES, L"Themes", DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_THEMES, FALSE, FALSE},
    {IDM_DESIGN_STYLE_OFFICE, L"Office", DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_STYLE_PREVIEW, TRUE, TRUE},
    {IDM_DESIGN_STYLE_BASIC_ELEGANT, L"Basic (Elegant)",
     DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_STYLE_PREVIEW, TRUE, TRUE},
    {IDM_DESIGN_STYLE_BASIC_SIMPLE, L"Basic (Simple)",
     DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_STYLE_PREVIEW, TRUE, TRUE},
    {IDM_DESIGN_STYLE_BASIC_STYLISH, L"Basic (Stylish)",
     DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_STYLE_PREVIEW, TRUE, TRUE},
    {IDM_DESIGN_STYLE_CENTERED, L"Centered",
     DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_STYLE_PREVIEW, TRUE, TRUE},
    {IDM_DESIGN_STYLE_CASUAL, L"Casual",
     DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_STYLE_PREVIEW, TRUE, TRUE},
    {IDM_DESIGN_STYLE_COMPACT, L"Compact",
     DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_STYLE_PREVIEW, TRUE, TRUE},
    {IDM_DESIGN_STYLE_LINES_DISTINCT, L"Lines (Distinctive)",
     DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_STYLE_PREVIEW, TRUE, TRUE},
    {IDM_DESIGN_STYLE_LINES_ELEGANT, L"Lines (Elegant)",
     DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_STYLE_PREVIEW, TRUE, TRUE},
    {IDM_DESIGN_STYLE_LINES_SIMPLE, L"Lines (Simple)",
     DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_STYLE_PREVIEW, TRUE, TRUE},
    {IDM_DESIGN_STYLE_GALLERY_MORE, L"More Style Sets",
     DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_MORE, FALSE, FALSE},
    {IDM_DESIGN_COLORS, L"Colors", DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_COLORS, FALSE, FALSE},
    {IDM_DESIGN_FONTS, L"Fonts", DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_FONTS, FALSE, FALSE},
    {IDM_DESIGN_PARAGRAPH_SPACING, L"Paragraph Spacing",
     DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_PARAGRAPH_SPACING, FALSE, FALSE},
    {IDM_DESIGN_EFFECTS, L"Effects", DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_EFFECTS, FALSE, FALSE},
    {IDM_DESIGN_SET_AS_DEFAULT, L"Set as Default",
     DESIGN_GROUP_DOCUMENT_FORMATTING,
     RIBBON_DESIGN_ICON_SET_AS_DEFAULT, FALSE, FALSE},
    {IDM_DESIGN_WATERMARK, L"Watermark",
     DESIGN_GROUP_PAGE_BACKGROUND,
     RIBBON_DESIGN_ICON_WATERMARK, FALSE, FALSE},
    {IDM_DESIGN_PAGE_COLOR, L"Page Color",
     DESIGN_GROUP_PAGE_BACKGROUND,
     RIBBON_DESIGN_ICON_PAGE_COLOR, FALSE, FALSE},
    {IDM_DESIGN_PAGE_BORDERS, L"Page Borders",
     DESIGN_GROUP_PAGE_BACKGROUND,
     RIBBON_DESIGN_ICON_PAGE_BORDERS, FALSE, FALSE}
};

static const UINT designStyleCommands[] = {
    IDM_DESIGN_STYLE_OFFICE,
    IDM_DESIGN_STYLE_BASIC_ELEGANT,
    IDM_DESIGN_STYLE_BASIC_SIMPLE,
    IDM_DESIGN_STYLE_BASIC_STYLISH,
    IDM_DESIGN_STYLE_CENTERED,
    IDM_DESIGN_STYLE_CASUAL,
    IDM_DESIGN_STYLE_COMPACT,
    IDM_DESIGN_STYLE_LINES_DISTINCT,
    IDM_DESIGN_STYLE_LINES_ELEGANT,
    IDM_DESIGN_STYLE_LINES_SIMPLE,
    IDM_DESIGN_STYLE_MODERN,
    IDM_DESIGN_STYLE_SHADED,
    IDM_DESIGN_STYLE_CLASSIC,
    IDM_DESIGN_STYLE_DISTINCTIVE,
    IDM_DESIGN_STYLE_ELEGANT,
    IDM_DESIGN_STYLE_FORMAL,
    IDM_DESIGN_STYLE_MANUSCRIPT,
    IDM_DESIGN_STYLE_TRADITIONAL,
    IDM_DESIGN_STYLE_WORD_2010
};

static const InsertControlSpec insertControlSpecs[] = {
    {IDM_INSERT_COVER_PAGE, L"Cover Page", INSERT_GROUP_PAGES,
     RIBBON_INSERT_ICON_COVER_PAGE},
    {IDM_INSERT_BLANK_PAGE, L"Blank Page", INSERT_GROUP_PAGES,
     RIBBON_INSERT_ICON_BLANK_PAGE},
    {IDM_INSERT_PAGE_BREAK, L"Page Break", INSERT_GROUP_PAGES,
     RIBBON_INSERT_ICON_PAGE_BREAK},
    {IDM_INSERT_TABLE, L"Table", INSERT_GROUP_TABLES,
     RIBBON_INSERT_ICON_TABLE},
    {IDM_INSERT_PICTURES, L"Pictures", INSERT_GROUP_ILLUSTRATIONS,
     RIBBON_INSERT_ICON_PICTURES},
    {IDM_INSERT_SHAPES, L"Shapes", INSERT_GROUP_ILLUSTRATIONS,
     RIBBON_INSERT_ICON_SHAPES},
    {IDM_INSERT_ICONS, L"Icons", INSERT_GROUP_ILLUSTRATIONS,
     RIBBON_INSERT_ICON_ICONS},
    {IDM_INSERT_3D_MODELS, L"3D Models", INSERT_GROUP_ILLUSTRATIONS,
     RIBBON_INSERT_ICON_3D_MODELS},
    {IDM_INSERT_SMARTART, L"SmartArt", INSERT_GROUP_ILLUSTRATIONS,
     RIBBON_INSERT_ICON_SMARTART},
    {IDM_INSERT_CHART, L"Chart", INSERT_GROUP_ILLUSTRATIONS,
     RIBBON_INSERT_ICON_CHART},
    {IDM_INSERT_SCREENSHOT, L"Screenshot", INSERT_GROUP_ILLUSTRATIONS,
     RIBBON_INSERT_ICON_SCREENSHOT},
    {IDM_INSERT_ONLINE_VIDEO, L"Online Videos", INSERT_GROUP_MEDIA,
     RIBBON_INSERT_ICON_ONLINE_VIDEO},
    {IDM_INSERT_LINK, L"Link", INSERT_GROUP_LINKS,
     RIBBON_INSERT_ICON_LINK},
    {IDM_INSERT_BOOKMARK, L"Bookmark", INSERT_GROUP_LINKS,
     RIBBON_INSERT_ICON_BOOKMARK},
    {IDM_INSERT_CROSS_REFERENCE, L"Cross-reference", INSERT_GROUP_LINKS,
     RIBBON_INSERT_ICON_CROSS_REFERENCE},
    {IDM_INSERT_COMMENT, L"Comment", INSERT_GROUP_COMMENTS,
     RIBBON_INSERT_ICON_COMMENT},
    {IDM_INSERT_HEADER, L"Header", INSERT_GROUP_HEADER_FOOTER,
     RIBBON_INSERT_ICON_HEADER},
    {IDM_INSERT_FOOTER, L"Footer", INSERT_GROUP_HEADER_FOOTER,
     RIBBON_INSERT_ICON_FOOTER},
    {IDM_INSERT_PAGE_NUMBER, L"Page Number", INSERT_GROUP_HEADER_FOOTER,
     RIBBON_INSERT_ICON_PAGE_NUMBER},
    {IDM_INSERT_TEXT_BOX, L"Text Box", INSERT_GROUP_TEXT,
     RIBBON_INSERT_ICON_TEXT_BOX},
    {IDM_INSERT_QUICK_PARTS, L"Quick Parts", INSERT_GROUP_TEXT,
     RIBBON_INSERT_ICON_QUICK_PARTS},
    {IDM_INSERT_WORDART, L"WordArt", INSERT_GROUP_TEXT,
     RIBBON_INSERT_ICON_WORDART},
    {IDM_INSERT_DROP_CAP, L"Drop Cap", INSERT_GROUP_TEXT,
     RIBBON_INSERT_ICON_DROP_CAP},
    {IDM_INSERT_SIGNATURE_LINE, L"Signature Line", INSERT_GROUP_TEXT,
     RIBBON_INSERT_ICON_SIGNATURE_LINE},
    {IDM_INSERT_DATETIME, L"Date & Time", INSERT_GROUP_TEXT,
     RIBBON_INSERT_ICON_DATETIME},
    {IDM_INSERT_OBJECT, L"Object", INSERT_GROUP_TEXT,
     RIBBON_INSERT_ICON_OBJECT},
    {IDM_INSERT_EQUATION, L"Equation", INSERT_GROUP_SYMBOLS,
     RIBBON_INSERT_ICON_EQUATION},
    {IDM_INSERT_SYMBOL, L"Symbol", INSERT_GROUP_SYMBOLS,
     RIBBON_INSERT_ICON_SYMBOL},
    {IDM_INSERT_ESIGNATURE_FIELDS, L"eSignature fields",
     INSERT_GROUP_ESIGNATURE, RIBBON_INSERT_ICON_ESIGNATURE_FIELDS}
};

static const DrawControlSpec drawControlSpecs[] = {
    {IDM_DRAW_MODE, L"Draw", DRAW_GROUP_INPUT_MODE,
     RIBBON_DRAW_ICON_DRAW, TRUE},
    {IDM_EDIT_UNDO, L"Undo", DRAW_GROUP_UNDO,
     RIBBON_DRAW_ICON_UNDO, FALSE},
    {IDM_EDIT_REDO, L"Redo", DRAW_GROUP_UNDO,
     RIBBON_DRAW_ICON_REDO, FALSE},
    {IDM_DRAW_SELECT, L"Select", DRAW_GROUP_DRAWING_TOOLS,
     RIBBON_DRAW_ICON_SELECT, TRUE},
    {IDM_DRAW_LASSO_SELECT, L"Lasso Select", DRAW_GROUP_DRAWING_TOOLS,
     RIBBON_DRAW_ICON_LASSO_SELECT, TRUE},
    {IDM_DRAW_ERASER, L"Eraser", DRAW_GROUP_DRAWING_TOOLS,
     RIBBON_DRAW_ICON_ERASER, TRUE},
    {IDM_DRAW_PEN_BLACK, L"Black Pen", DRAW_GROUP_DRAWING_TOOLS,
     RIBBON_DRAW_ICON_PEN_BLACK, TRUE},
    {IDM_DRAW_PEN_RED, L"Red Pen", DRAW_GROUP_DRAWING_TOOLS,
     RIBBON_DRAW_ICON_PEN_RED, TRUE},
    {IDM_DRAW_PENCIL, L"Pencil", DRAW_GROUP_DRAWING_TOOLS,
     RIBBON_DRAW_ICON_PENCIL, TRUE},
    {IDM_DRAW_HIGHLIGHTER, L"Yellow Highlighter",
     DRAW_GROUP_DRAWING_TOOLS, RIBBON_DRAW_ICON_HIGHLIGHTER, TRUE},
    {IDM_DRAW_PEN_BLUE, L"Blue Pen", DRAW_GROUP_DRAWING_TOOLS,
     RIBBON_DRAW_ICON_PEN_BLUE, TRUE},
    {IDM_DRAW_PEN_GREEN, L"Green Pen", DRAW_GROUP_DRAWING_TOOLS,
     RIBBON_DRAW_ICON_PEN_GREEN, TRUE},
    {IDM_DRAW_ACTION_PEN, L"Action Pen", DRAW_GROUP_DRAWING_TOOLS,
     RIBBON_DRAW_ICON_ACTION_PEN, TRUE},
    {IDM_DRAW_ADD_PEN, L"Add Pen", DRAW_GROUP_DRAWING_TOOLS,
     RIBBON_DRAW_ICON_ADD_PEN, FALSE},
    {IDM_DRAW_RULER, L"Ruler", DRAW_GROUP_STENCILS,
     RIBBON_DRAW_ICON_RULER, TRUE},
    {IDM_DRAW_FORMAT_BACKGROUND, L"Format Background", DRAW_GROUP_EDIT,
     RIBBON_DRAW_ICON_FORMAT_BACKGROUND, TRUE},
    {IDM_DRAW_INK_TO_SHAPE, L"Ink to Shape", DRAW_GROUP_CONVERT,
     RIBBON_DRAW_ICON_INK_TO_SHAPE, FALSE},
    {IDM_DRAW_INK_TO_MATH, L"Ink to Math", DRAW_GROUP_CONVERT,
     RIBBON_DRAW_ICON_INK_TO_MATH, FALSE},
    {IDM_DRAW_CANVAS, L"Drawing Canvas", DRAW_GROUP_INSERT,
     RIBBON_DRAW_ICON_CANVAS, FALSE},
    {IDM_DRAW_INK_REPLAY, L"Ink Replay", DRAW_GROUP_REPLAY,
     RIBBON_DRAW_ICON_REPLAY, FALSE},
    {IDM_DRAW_INK_HELP, L"Ink Help", DRAW_GROUP_HELP,
     RIBBON_DRAW_ICON_HELP, FALSE}
};

static const ViewControlSpec viewControlSpecs[] = {
    {IDM_VIEW_READ_MODE, L"Read Mode", VIEW_GROUP_VIEWS,
     RIBBON_VIEW_ICON_READ_MODE, TRUE},
    {IDM_VIEW_PRINT_LAYOUT, L"Print Layout", VIEW_GROUP_VIEWS,
     RIBBON_VIEW_ICON_PRINT_LAYOUT, TRUE},
    {IDM_VIEW_WEB_LAYOUT, L"Web Layout", VIEW_GROUP_VIEWS,
     RIBBON_VIEW_ICON_WEB_LAYOUT, TRUE},
    {IDM_VIEW_OUTLINE, L"Outline", VIEW_GROUP_VIEWS,
     RIBBON_VIEW_ICON_OUTLINE, TRUE},
    {IDM_VIEW_DRAFT, L"Draft", VIEW_GROUP_VIEWS,
     RIBBON_VIEW_ICON_DRAFT, TRUE},
    {IDM_VIEW_FOCUS, L"Focus", VIEW_GROUP_IMMERSIVE,
     RIBBON_VIEW_ICON_FOCUS, TRUE},
    {IDM_VIEW_IMMERSIVE_READER, L"Immersive Reader",
     VIEW_GROUP_IMMERSIVE, RIBBON_VIEW_ICON_IMMERSIVE_READER, FALSE},
    {IDM_VIEW_DARK_MODE, L"Switch Modes", VIEW_GROUP_DARK_MODE,
     RIBBON_VIEW_ICON_SWITCH_MODES, TRUE},
    {IDM_VIEW_VERTICAL, L"Vertical", VIEW_GROUP_PAGE_MOVEMENT,
     RIBBON_VIEW_ICON_VERTICAL, TRUE},
    {IDM_VIEW_SIDE_TO_SIDE, L"Side to Side", VIEW_GROUP_PAGE_MOVEMENT,
     RIBBON_VIEW_ICON_SIDE_TO_SIDE, TRUE},
    {IDM_VIEW_RULER, L"Ruler", VIEW_GROUP_SHOW,
     RIBBON_VIEW_ICON_RULER, TRUE},
    {IDM_VIEW_GRIDLINES, L"Gridlines", VIEW_GROUP_SHOW,
     RIBBON_VIEW_ICON_GRIDLINES, TRUE},
    {IDM_VIEW_NAVIGATION_PANE, L"Navigation Pane", VIEW_GROUP_SHOW,
     RIBBON_VIEW_ICON_NAVIGATION_PANE, TRUE},
    {IDM_VIEW_ZOOM_DIALOG, L"Zoom", VIEW_GROUP_ZOOM,
     RIBBON_VIEW_ICON_ZOOM, FALSE},
    {IDM_VIEW_ZOOM_100, L"100%", VIEW_GROUP_ZOOM,
     RIBBON_VIEW_ICON_100_PERCENT, FALSE},
    {IDM_VIEW_ONE_PAGE, L"One Page", VIEW_GROUP_ZOOM,
     RIBBON_VIEW_ICON_ONE_PAGE, FALSE},
    {IDM_VIEW_MULTIPLE_PAGES, L"Multiple Pages", VIEW_GROUP_ZOOM,
     RIBBON_VIEW_ICON_MULTIPLE_PAGES, FALSE},
    {IDM_VIEW_PAGE_WIDTH, L"Page Width", VIEW_GROUP_ZOOM,
     RIBBON_VIEW_ICON_PAGE_WIDTH, FALSE},
    {IDM_VIEW_NEW_WINDOW, L"New Window", VIEW_GROUP_WINDOW,
     RIBBON_VIEW_ICON_NEW_WINDOW, FALSE},
    {IDM_VIEW_ARRANGE_ALL, L"Arrange All", VIEW_GROUP_WINDOW,
     RIBBON_VIEW_ICON_ARRANGE_ALL, FALSE},
    {IDM_VIEW_SPLIT, L"Split", VIEW_GROUP_WINDOW,
     RIBBON_VIEW_ICON_SPLIT, FALSE},
    {IDM_VIEW_SIDE_BY_SIDE, L"View Side by Side", VIEW_GROUP_WINDOW,
     RIBBON_VIEW_ICON_SIDE_BY_SIDE, TRUE},
    {IDM_VIEW_SYNCHRONOUS_SCROLLING, L"Synchronous Scrolling",
     VIEW_GROUP_WINDOW, RIBBON_VIEW_ICON_SYNCHRONOUS_SCROLLING, TRUE},
    {IDM_VIEW_RESET_WINDOW_POSITION, L"Reset Window Position",
     VIEW_GROUP_WINDOW, RIBBON_VIEW_ICON_RESET_WINDOW_POSITION, FALSE},
    {IDM_VIEW_SWITCH_WINDOWS, L"Switch Windows", VIEW_GROUP_WINDOW,
     RIBBON_VIEW_ICON_SWITCH_WINDOWS, FALSE},
    {IDM_VIEW_MACROS, L"Macros", VIEW_GROUP_MACROS,
     RIBBON_VIEW_ICON_MACROS, FALSE},
    {IDM_VIEW_PROPERTIES, L"Properties", VIEW_GROUP_SHAREPOINT,
     RIBBON_VIEW_ICON_PROPERTIES, FALSE}
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
_Static_assert(ARRAYSIZE(insertGroupNames) == INSERT_GROUP_COUNT,
               "Insert group names must match INSERT_GROUP_COUNT");
_Static_assert(ARRAYSIZE(insertControlSpecs) == INSERT_MAX_CONTROLS,
               "Insert control specifications must remain complete");
_Static_assert(ARRAYSIZE(drawGroupNames) == DRAW_GROUP_COUNT,
               "Draw group names must match DRAW_GROUP_COUNT");
_Static_assert(ARRAYSIZE(drawControlSpecs) == DRAW_MAX_CONTROLS,
               "Draw control specifications must remain complete");
_Static_assert(ARRAYSIZE(designGroupNames) == DESIGN_GROUP_COUNT,
               "Design group names must match DESIGN_GROUP_COUNT");
_Static_assert(ARRAYSIZE(designControlSpecs) == DESIGN_MAX_CONTROLS,
               "Design control specifications must remain complete");
_Static_assert(ARRAYSIZE(designStyleCommands) == DESIGN_STYLE_SET_COUNT,
               "Design style commands must match DESIGN_STYLE_SET_COUNT");
_Static_assert(ARRAYSIZE(viewGroupNames) == VIEW_GROUP_COUNT,
               "View group names must match VIEW_GROUP_COUNT");
_Static_assert(ARRAYSIZE(viewControlSpecs) == VIEW_MAX_CONTROLS,
               "View control specifications must remain complete");
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

static BOOL ribbon_insert_track(RibbonContext *ribbon, HWND window,
                                UINT id, int group, RibbonInsertIcon icon)
{
    InsertControlInfo *control;

    if (ribbon == NULL || window == NULL || group < 0 ||
        group >= INSERT_GROUP_COUNT ||
        ribbon->insertControlCount >= ARRAYSIZE(ribbon->insertControls)) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    control = &ribbon->insertControls[ribbon->insertControlCount++];
    control->window = window;
    control->id = id;
    control->group = group;
    control->icon = icon;
    control->iconPaintCount = 0;
    return TRUE;
}

static HWND ribbon_create_insert_button(RibbonContext *ribbon,
                                        const InsertControlSpec *spec)
{
    HWND button;

    if (ribbon == NULL || spec == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    button = ribbon_create_button(ribbon, RIBBON_TAB_INSERT, spec->id,
                                  spec->caption, FALSE);
    if (button == NULL ||
        !ribbon_insert_track(ribbon, button, spec->id, spec->group,
                             spec->icon)) {
        return NULL;
    }
    ribbon_set_accessible_name(button, spec->caption);
    return button;
}

static BOOL ribbon_draw_track(RibbonContext *ribbon, HWND window,
                              UINT id, int group, RibbonDrawIcon icon)
{
    DrawControlInfo *control;

    if (ribbon == NULL || window == NULL || group < 0 ||
        group >= DRAW_GROUP_COUNT ||
        ribbon->drawControlCount >= ARRAYSIZE(ribbon->drawControls)) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    control = &ribbon->drawControls[ribbon->drawControlCount++];
    control->window = window;
    control->id = id;
    control->group = group;
    control->icon = icon;
    control->iconPaintCount = 0;
    return TRUE;
}

static HWND ribbon_create_draw_button(RibbonContext *ribbon,
                                      const DrawControlSpec *spec)
{
    HWND button;

    if (ribbon == NULL || spec == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    button = ribbon_create_button(ribbon, RIBBON_TAB_DRAW, spec->id,
                                  spec->caption, spec->toggle);
    if (button == NULL ||
        !ribbon_draw_track(ribbon, button, spec->id, spec->group,
                           spec->icon)) {
        return NULL;
    }
    ribbon_set_accessible_name(button, spec->caption);
    return button;
}

static BOOL ribbon_view_track(RibbonContext *ribbon, HWND window,
                              UINT id, int group, RibbonViewIcon icon)
{
    ViewControlInfo *control;

    if (ribbon == NULL || window == NULL || group < 0 ||
        group >= VIEW_GROUP_COUNT ||
        ribbon->viewControlCount >= ARRAYSIZE(ribbon->viewControls)) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    control = &ribbon->viewControls[ribbon->viewControlCount++];
    control->window = window;
    control->id = id;
    control->group = group;
    control->icon = icon;
    control->iconPaintCount = 0;
    return TRUE;
}

static HWND ribbon_create_view_button(RibbonContext *ribbon,
                                      const ViewControlSpec *spec)
{
    HWND button;

    if (ribbon == NULL || spec == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    button = ribbon_create_button(ribbon, RIBBON_TAB_VIEW, spec->id,
                                  spec->caption, spec->toggle);
    if (button == NULL ||
        !ribbon_view_track(ribbon, button, spec->id, spec->group,
                           spec->icon)) {
        return NULL;
    }
    ribbon_set_accessible_name(button, spec->caption);
    return button;
}

static BOOL ribbon_design_track(RibbonContext *ribbon, HWND window,
                                UINT id, int group,
                                RibbonDesignIcon icon,
                                BOOL stylePreview)
{
    DesignControlInfo *control;

    if (ribbon == NULL || window == NULL || group < 0 ||
        group >= DESIGN_GROUP_COUNT ||
        ribbon->designControlCount >=
            ARRAYSIZE(ribbon->designControls)) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    control = &ribbon->designControls[ribbon->designControlCount++];
    control->window = window;
    control->id = id;
    control->group = group;
    control->icon = icon;
    control->iconPaintCount = 0;
    control->stylePreview = stylePreview;
    return TRUE;
}

static HWND ribbon_create_design_button(
    RibbonContext *ribbon, const DesignControlSpec *spec)
{
    HWND button;

    if (ribbon == NULL || spec == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    button = ribbon_create_button(
        ribbon, RIBBON_TAB_DESIGN, spec->id, spec->caption,
        spec->toggle);
    if (button == NULL ||
        !ribbon_design_track(ribbon, button, spec->id, spec->group,
                             spec->icon, spec->stylePreview)) {
        return NULL;
    }
    ribbon_set_accessible_name(button, spec->caption);
    return button;
}

static const WCHAR *ribbon_insert_tooltip_text(
    const InsertControlSpec *spec)
{
    if (spec == NULL) {
        return L"";
    }
    switch (spec->id) {
    case IDM_INSERT_3D_MODELS:
        return L"3D Models - requires a future safe 3D document model";
    case IDM_INSERT_BOOKMARK:
        return L"Bookmark - requires persistent named document anchors";
    case IDM_INSERT_CROSS_REFERENCE:
        return L"Cross-reference - requires persistent bookmarks";
    case IDM_INSERT_HEADER:
        return L"Header - requires a persistent page header model";
    case IDM_INSERT_FOOTER:
        return L"Footer - requires a persistent page footer model";
    case IDM_INSERT_PAGE_NUMBER:
        return L"Page Number - requires dynamic page fields";
    case IDM_INSERT_OBJECT:
        return L"Object - requires a secure embedded-object model";
    default:
        return spec->caption;
    }
}

static const WCHAR *ribbon_draw_tooltip_text(const DrawControlSpec *spec)
{
    if (spec == NULL) {
        return L"";
    }
    switch (spec->id) {
    case IDM_DRAW_MODE:
        return L"Draw - open a drawing canvas using the selected tool";
    case IDM_DRAW_SELECT:
        return L"Select - stop drawing and use selection mode";
    case IDM_DRAW_LASSO_SELECT:
        return L"Lasso Select - select ink in the drawing canvas";
    case IDM_DRAW_ERASER:
        return L"Eraser - remove strokes from the drawing canvas";
    case IDM_DRAW_ADD_PEN:
        return L"Add Pen - choose another preset pen";
    case IDM_DRAW_RULER:
        return L"Ruler - show a guide in the drawing canvas";
    case IDM_DRAW_FORMAT_BACKGROUND:
        return L"Format Background - toggle ruled canvas paper";
    case IDM_DRAW_INK_TO_SHAPE:
        return L"Ink to Shape - shape recognition is not available yet";
    case IDM_DRAW_INK_TO_MATH:
        return L"Ink to Math - handwriting recognition is not available yet";
    case IDM_DRAW_CANVAS:
        return L"Drawing Canvas - create persistent inline ink";
    case IDM_DRAW_INK_REPLAY:
        return L"Ink Replay - retained stroke timing is not available yet";
    default:
        return spec->caption;
    }
}

static const WCHAR *ribbon_view_tooltip_text(const ViewControlSpec *spec)
{
    if (spec == NULL) {
        return L"";
    }
    switch (spec->id) {
    case IDM_VIEW_READ_MODE:
        return L"Read Mode - fit the paged document for comfortable reading";
    case IDM_VIEW_WEB_LAYOUT:
        return L"Web Layout - requires a continuous web-layout engine";
    case IDM_VIEW_OUTLINE:
        return L"Outline - requires a structured heading-outline model";
    case IDM_VIEW_DRAFT:
        return L"Draft - requires a non-paged document layout engine";
    case IDM_VIEW_FOCUS:
        return L"Focus - hide the ribbon and status chrome; press Esc to leave";
    case IDM_VIEW_IMMERSIVE_READER:
        return L"Immersive Reader - reading assistance is not available yet";
    case IDM_VIEW_SIDE_TO_SIDE:
        return L"Side to Side - horizontal page movement is not available yet";
    case IDM_VIEW_RULER:
        return L"Ruler - show a nonprinting ruler over the page";
    case IDM_VIEW_GRIDLINES:
        return L"Gridlines - show nonprinting alignment guides";
    case IDM_VIEW_NAVIGATION_PANE:
        return L"Navigation Pane - open or close document search";
    case IDM_VIEW_ZOOM_DIALOG:
        return L"Zoom - choose a document zoom preset";
    case IDM_VIEW_ONE_PAGE:
        return L"One Page - fit one complete page in the workspace";
    case IDM_VIEW_MULTIPLE_PAGES:
        return L"Multiple Pages - zoom out to show several vertical pages";
    case IDM_VIEW_PAGE_WIDTH:
        return L"Page Width - fit the paper width to the workspace";
    case IDM_VIEW_NEW_WINDOW:
        return L"New Window - open another WordCraft process; a clean saved "
               L"document reopens there";
    case IDM_VIEW_SPLIT:
        return L"Split - a second editable document viewport is not available";
    case IDM_VIEW_SYNCHRONOUS_SCROLLING:
        return L"Synchronous Scrolling - requires a shared scrolling model "
               L"and is not available yet";
    case IDM_VIEW_MACROS:
        return L"Macros - requires a trusted macro execution model";
    default:
        return spec->caption;
    }
}

static const WCHAR *ribbon_design_tooltip_text(
    const DesignControlSpec *spec)
{
    if (spec == NULL) {
        return L"";
    }
    switch (spec->id) {
    case IDM_DESIGN_THEMES:
        return L"Themes - change the coordinated style, colors, fonts, "
               L"and effects";
    case IDM_DESIGN_STYLE_GALLERY_MORE:
        return L"More Style Sets - open the visual document-formatting "
               L"gallery";
    case IDM_DESIGN_COLORS:
        return L"Colors - change the coordinated document accent colors";
    case IDM_DESIGN_FONTS:
        return L"Fonts - change the coordinated heading and body fonts";
    case IDM_DESIGN_PARAGRAPH_SPACING:
        return L"Paragraph Spacing - update matching WordCraft styles "
               L"throughout the document";
    case IDM_DESIGN_EFFECTS:
        return L"Effects - choose the coordinated object-effect family";
    case IDM_DESIGN_SET_AS_DEFAULT:
        return L"Set as Default - requires a persistent template settings "
               L"model";
    case IDM_DESIGN_WATERMARK:
        return L"Watermark - requires a persistent page background layer";
    case IDM_DESIGN_PAGE_COLOR:
        return L"Page Color - requires persisted and printable page "
               L"background metadata";
    case IDM_DESIGN_PAGE_BORDERS:
        return L"Page Borders - requires a persisted page-border model";
    default:
        return spec->caption;
    }
}

static void ribbon_add_draw_tooltip(RibbonContext *ribbon, HWND button,
                                    const DrawControlSpec *spec)
{
    TOOLINFOW tool;

    if (ribbon == NULL || ribbon->drawTooltip == NULL ||
        button == NULL || spec == NULL) {
        return;
    }
    ZeroMemory(&tool, sizeof(tool));
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.hwnd = ribbon->app->formatBar;
    tool.uId = (UINT_PTR)button;
    tool.lpszText = (LPWSTR)ribbon_draw_tooltip_text(spec);
    SendMessageW(ribbon->drawTooltip, TTM_ADDTOOLW, 0, (LPARAM)&tool);
}

static void ribbon_add_insert_tooltip(RibbonContext *ribbon, HWND button,
                                      const InsertControlSpec *spec)
{
    TOOLINFOW tool;

    if (ribbon == NULL || ribbon->insertTooltip == NULL ||
        button == NULL || spec == NULL) {
        return;
    }
    ZeroMemory(&tool, sizeof(tool));
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.hwnd = ribbon->app->formatBar;
    tool.uId = (UINT_PTR)button;
    tool.lpszText = (LPWSTR)ribbon_insert_tooltip_text(spec);
    SendMessageW(ribbon->insertTooltip, TTM_ADDTOOLW, 0,
                 (LPARAM)&tool);
}

static void ribbon_add_view_tooltip(RibbonContext *ribbon, HWND button,
                                    const ViewControlSpec *spec)
{
    TOOLINFOW tool;

    if (ribbon == NULL || ribbon->viewTooltip == NULL ||
        button == NULL || spec == NULL) {
        return;
    }
    ZeroMemory(&tool, sizeof(tool));
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.hwnd = ribbon->app->formatBar;
    tool.uId = (UINT_PTR)button;
    tool.lpszText = (LPWSTR)ribbon_view_tooltip_text(spec);
    SendMessageW(ribbon->viewTooltip, TTM_ADDTOOLW, 0, (LPARAM)&tool);
}

static void ribbon_add_design_tooltip(
    RibbonContext *ribbon, HWND button,
    const DesignControlSpec *spec)
{
    TOOLINFOW tool;

    if (ribbon == NULL || ribbon->designTooltip == NULL ||
        button == NULL || spec == NULL) {
        return;
    }
    ZeroMemory(&tool, sizeof(tool));
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.hwnd = ribbon->app->formatBar;
    tool.uId = (UINT_PTR)button;
    tool.lpszText = (LPWSTR)ribbon_design_tooltip_text(spec);
    SendMessageW(ribbon->designTooltip, TTM_ADDTOOLW, 0,
                 (LPARAM)&tool);
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
    size_t index;

    ribbon->insertTooltip = CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        ribbon->app->formatBar, NULL, ribbon->app->instance, NULL);
    if (ribbon->insertTooltip != NULL) {
        SetWindowPos(ribbon->insertTooltip, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SendMessageW(ribbon->insertTooltip, TTM_SETMAXTIPWIDTH, 0,
                     app_scale(ribbon->app->mainWindow, 360));
    }
    for (index = 0; index < ARRAYSIZE(insertControlSpecs); ++index) {
        HWND button = ribbon_create_insert_button(
            ribbon, &insertControlSpecs[index]);
        if (button == NULL) {
            return FALSE;
        }
        ribbon_add_insert_tooltip(
            ribbon, button, &insertControlSpecs[index]);
    }
    return TRUE;
}

static BOOL ribbon_create_draw_page(RibbonContext *ribbon)
{
    size_t index;

    ribbon->activeDrawTool = IDM_DRAW_SELECT;
    ribbon->drawTooltip = CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        ribbon->app->formatBar, NULL, ribbon->app->instance, NULL);
    if (ribbon->drawTooltip != NULL) {
        SetWindowPos(ribbon->drawTooltip, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SendMessageW(ribbon->drawTooltip, TTM_SETMAXTIPWIDTH, 0,
                     app_scale(ribbon->app->mainWindow, 360));
    }
    for (index = 0; index < ARRAYSIZE(drawControlSpecs); ++index) {
        HWND button = ribbon_create_draw_button(
            ribbon, &drawControlSpecs[index]);
        if (button == NULL) {
            return FALSE;
        }
        ribbon_add_draw_tooltip(
            ribbon, button, &drawControlSpecs[index]);
    }
    return TRUE;
}

static BOOL ribbon_create_design_page(RibbonContext *ribbon)
{
    size_t index;

    if (ribbon == NULL || ribbon->app == NULL) {
        return FALSE;
    }
    ribbon->designGalleryFocusedIndex = DESIGN_GALLERY_CURRENT_INDEX;
    ribbon->designGalleryHotIndex = -1;
    ribbon->designTooltip = CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        ribbon->app->formatBar, NULL, ribbon->app->instance, NULL);
    if (ribbon->designTooltip != NULL) {
        SetWindowPos(ribbon->designTooltip, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SendMessageW(ribbon->designTooltip, TTM_SETMAXTIPWIDTH, 0,
                     app_scale(ribbon->app->mainWindow, 380));
    }
    for (index = 0; index < ARRAYSIZE(designControlSpecs); ++index) {
        HWND button = ribbon_create_design_button(
            ribbon, &designControlSpecs[index]);
        if (button == NULL) {
            return FALSE;
        }
        ribbon_add_design_tooltip(
            ribbon, button, &designControlSpecs[index]);
    }
    return TRUE;
}

static BOOL ribbon_create_placeholder_pages(RibbonContext *ribbon)
{
    return ribbon_create_label(
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
    ribbon->documentChatButton = ribbon_create_button(
        ribbon, RIBBON_TAB_REVIEW, IDM_REVIEW_DOCUMENT_CHAT,
        L"Document Chat", FALSE);
    ribbon->versionHistoryButton = ribbon_create_button(
        ribbon, RIBBON_TAB_REVIEW, IDM_REVIEW_VERSION_HISTORY,
        L"Version History", FALSE);

    if (commentLabel == NULL || ribbon->commentEdit == NULL ||
        ribbon->addCommentButton == NULL || ribbon->commentSummary == NULL ||
        ribbon->previousCommentButton == NULL ||
        ribbon->nextCommentButton == NULL ||
        ribbon->deleteCommentButton == NULL ||
        ribbon->spellCheckButton == NULL ||
        ribbon->autoCompleteButton == NULL ||
        ribbon->liveShareButton == NULL ||
        ribbon->documentChatButton == NULL ||
        ribbon->versionHistoryButton == NULL) {
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

    ribbon->activeViewMode = VIEW_MODE_PRINT_LAYOUT;
    ribbon->viewMovementVertical = TRUE;
    ribbon->viewTooltip = CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        ribbon->app->formatBar, NULL, ribbon->app->instance, NULL);
    if (ribbon->viewTooltip != NULL) {
        SetWindowPos(ribbon->viewTooltip, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SendMessageW(ribbon->viewTooltip, TTM_SETMAXTIPWIDTH, 0,
                     app_scale(ribbon->app->mainWindow, 360));
    }
    for (index = 0; index < ARRAYSIZE(viewControlSpecs); ++index) {
        HWND button = ribbon_create_view_button(
            ribbon, &viewControlSpecs[index]);
        if (button == NULL) {
            return FALSE;
        }
        if (viewControlSpecs[index].id == IDM_VIEW_DARK_MODE) {
            ribbon->darkModeButton = button;
        }
        ribbon_add_view_tooltip(
            ribbon, button, &viewControlSpecs[index]);
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
           ribbon_create_draw_page(ribbon) &&
           ribbon_create_design_page(ribbon) &&
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
    if (activeTab != RIBBON_TAB_DESIGN) {
        ribbon_hide_design_gallery(ribbon, FALSE);
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

static size_t ribbon_insert_group_control_count(const RibbonContext *ribbon,
                                                int group)
{
    size_t count = 0;
    size_t index;

    if (ribbon == NULL || group < 0 || group >= INSERT_GROUP_COUNT) {
        return 0;
    }
    for (index = 0; index < ribbon->insertControlCount; ++index) {
        if (ribbon->insertControls[index].group == group) {
            ++count;
        }
    }
    return count;
}

static int ribbon_insert_group_label_width(RibbonContext *ribbon, int group)
{
    AppState *app;
    HDC dc;
    HFONT previousFont = NULL;
    SIZE extent;
    int width;

    if (ribbon == NULL || ribbon->app == NULL || group < 0 ||
        group >= INSERT_GROUP_COUNT) {
        return 0;
    }
    app = ribbon->app;
    width = app_scale(app->mainWindow, 42);
    ZeroMemory(&extent, sizeof(extent));
    dc = GetDC(app->formatBar);
    if (dc == NULL) {
        return width;
    }
    if (app->uiFont != NULL) {
        previousFont = SelectObject(dc, app->uiFont);
    }
    if (GetTextExtentPoint32W(dc, insertGroupNames[group],
                              lstrlenW(insertGroupNames[group]), &extent)) {
        width = max(width, extent.cx + app_scale(app->mainWindow, 10));
    }
    if (previousFont != NULL) {
        SelectObject(dc, previousFont);
    }
    ReleaseDC(app->formatBar, dc);
    return width;
}

static int ribbon_insert_group_width(RibbonContext *ribbon, int group,
                                     int layoutMode, int buttonWidth,
                                     int controlGap)
{
    size_t count = ribbon_insert_group_control_count(ribbon, group);
    size_t columns;
    int width;

    if (count == 0) {
        return 0;
    }
    if (layoutMode == RIBBON_LAYOUT_FULL) {
        columns = count;
    } else if (layoutMode == RIBBON_LAYOUT_COMPACT) {
        columns = (count + 1) / 2;
    } else {
        columns = (count + 2) / 3;
    }
    width = (int)columns * buttonWidth +
            max(0, (int)columns - 1) * controlGap;
    if (layoutMode != RIBBON_LAYOUT_COLLAPSED) {
        width = max(width, ribbon_insert_group_label_width(ribbon, group));
    }
    return width;
}

static int ribbon_insert_required_width(RibbonContext *ribbon,
                                        int layoutMode, int buttonWidth,
                                        int controlGap, int groupGap,
                                        int outer)
{
    int width = outer * 2;
    int group;

    for (group = 0; group < INSERT_GROUP_COUNT; ++group) {
        width += ribbon_insert_group_width(ribbon, group, layoutMode,
                                           buttonWidth, controlGap);
        if (group + 1 < INSERT_GROUP_COUNT) {
            width += groupGap;
        }
    }
    return width;
}

static void ribbon_layout_insert(RibbonContext *ribbon, int width, int height)
{
    AppState *app = ribbon->app;
    int outer = app_scale(app->mainWindow, 6);
    int groupGap = app_scale(app->mainWindow, 5);
    int fullControlGap = app_scale(app->mainWindow, 2);
    int compactControlGap = app_scale(app->mainWindow, 2);
    int collapsedControlGap = app_scale(app->mainWindow, 1);
    int fullButtonWidth = app_scale(app->mainWindow, 52);
    int compactButtonWidth = app_scale(app->mainWindow, 28);
    int collapsedButtonWidth = app_scale(app->mainWindow, 20);
    int fullRequired;
    int compactRequired;
    int controlGap;
    int buttonWidth;
    int rows;
    int controlTop;
    int controlBottom;
    int buttonHeight;
    int x;
    int group;

    fullRequired = ribbon_insert_required_width(
        ribbon, RIBBON_LAYOUT_FULL, fullButtonWidth, fullControlGap,
        groupGap, outer);
    compactRequired = ribbon_insert_required_width(
        ribbon, RIBBON_LAYOUT_COMPACT, compactButtonWidth,
        compactControlGap, groupGap, outer);
    if (width >= fullRequired) {
        ribbon->insertLayoutMode = RIBBON_LAYOUT_FULL;
        buttonWidth = fullButtonWidth;
        controlGap = fullControlGap;
        rows = 1;
    } else if (width >= compactRequired) {
        ribbon->insertLayoutMode = RIBBON_LAYOUT_COMPACT;
        buttonWidth = compactButtonWidth;
        controlGap = compactControlGap;
        rows = 2;
    } else {
        ribbon->insertLayoutMode = RIBBON_LAYOUT_COLLAPSED;
        buttonWidth = collapsedButtonWidth;
        controlGap = collapsedControlGap;
        rows = 3;
    }

    controlTop = app_scale(app->mainWindow, 5);
    controlBottom = height -
                    (ribbon->insertLayoutMode == RIBBON_LAYOUT_COLLAPSED
                         ? app_scale(app->mainWindow, 6)
                         : app_scale(app->mainWindow, 23));
    if (controlBottom < controlTop + rows) {
        controlBottom = controlTop + rows;
    }
    buttonHeight = max(1, (controlBottom - controlTop -
                           (rows - 1) * controlGap) / rows);
    x = outer;
    for (group = 0; group < INSERT_GROUP_COUNT; ++group) {
        size_t count = ribbon_insert_group_control_count(ribbon, group);
        size_t columns = rows > 0 ? (count + (size_t)rows - 1) /
                                       (size_t)rows
                                 : count;
        int groupWidth = ribbon_insert_group_width(
            ribbon, group, ribbon->insertLayoutMode, buttonWidth,
            controlGap);
        int controlsWidth = (int)columns * buttonWidth +
                            max(0, (int)columns - 1) * controlGap;
        int controlsLeft = x + max(0, (groupWidth - controlsWidth) / 2);
        size_t localIndex = 0;
        size_t index;

        SetRect(&ribbon->insertGroupRects[group], x,
                app_scale(app->mainWindow, 2), x + groupWidth,
                max(app_scale(app->mainWindow, 3),
                    height - app_scale(app->mainWindow, 3)));
        for (index = 0; index < ribbon->insertControlCount; ++index) {
            InsertControlInfo *control = &ribbon->insertControls[index];
            size_t row;
            size_t column;
            int controlX;
            int controlY;

            if (control->group != group) {
                continue;
            }
            column = columns != 0 ? localIndex % columns : 0;
            row = columns != 0 ? localIndex / columns : 0;
            controlX = controlsLeft +
                       (int)column * (buttonWidth + controlGap);
            controlY = controlTop +
                       (int)row * (buttonHeight + controlGap);
            ribbon_place_control(control->window, controlX, controlY,
                                 buttonWidth, buttonHeight);
            ++localIndex;
        }
        x += groupWidth + groupGap;
    }
}

static size_t ribbon_draw_group_control_count(const RibbonContext *ribbon,
                                              int group)
{
    size_t count = 0;
    size_t index;

    if (ribbon == NULL || group < 0 || group >= DRAW_GROUP_COUNT) {
        return 0;
    }
    for (index = 0; index < ribbon->drawControlCount; ++index) {
        if (ribbon->drawControls[index].group == group) {
            ++count;
        }
    }
    return count;
}

static int ribbon_draw_group_label_width(RibbonContext *ribbon, int group)
{
    AppState *app;
    HDC dc;
    HFONT previousFont = NULL;
    SIZE extent;
    int width;

    if (ribbon == NULL || ribbon->app == NULL || group < 0 ||
        group >= DRAW_GROUP_COUNT) {
        return 0;
    }
    app = ribbon->app;
    width = app_scale(app->mainWindow, 42);
    ZeroMemory(&extent, sizeof(extent));
    dc = GetDC(app->formatBar);
    if (dc == NULL) {
        return width;
    }
    if (app->uiFont != NULL) {
        previousFont = SelectObject(dc, app->uiFont);
    }
    if (GetTextExtentPoint32W(dc, drawGroupNames[group],
                              lstrlenW(drawGroupNames[group]), &extent)) {
        width = max(width, extent.cx + app_scale(app->mainWindow, 10));
    }
    if (previousFont != NULL) {
        SelectObject(dc, previousFont);
    }
    ReleaseDC(app->formatBar, dc);
    return width;
}

static int ribbon_draw_group_width(RibbonContext *ribbon, int group,
                                   int layoutMode, int buttonWidth,
                                   int controlGap)
{
    size_t count = ribbon_draw_group_control_count(ribbon, group);
    size_t columns;
    int width;

    if (count == 0) {
        return 0;
    }
    if (layoutMode == RIBBON_LAYOUT_FULL) {
        columns = group == DRAW_GROUP_UNDO ? 1 : count;
    } else if (layoutMode == RIBBON_LAYOUT_COMPACT) {
        columns = (count + 1) / 2;
    } else {
        columns = (count + 2) / 3;
    }
    width = (int)columns * buttonWidth +
            max(0, (int)columns - 1) * controlGap;
    if (layoutMode != RIBBON_LAYOUT_COLLAPSED) {
        width = max(width, ribbon_draw_group_label_width(ribbon, group));
    }
    return width;
}

static int ribbon_draw_required_width(RibbonContext *ribbon,
                                      int layoutMode, int buttonWidth,
                                      int controlGap, int groupGap,
                                      int outer)
{
    int width = outer * 2;
    int group;

    for (group = 0; group < DRAW_GROUP_COUNT; ++group) {
        width += ribbon_draw_group_width(ribbon, group, layoutMode,
                                         buttonWidth, controlGap);
        if (group + 1 < DRAW_GROUP_COUNT) {
            width += groupGap;
        }
    }
    return width;
}

static void ribbon_layout_draw(RibbonContext *ribbon, int width, int height)
{
    AppState *app = ribbon->app;
    int outer = app_scale(app->mainWindow, 6);
    int groupGap = app_scale(app->mainWindow, 5);
    int fullControlGap = app_scale(app->mainWindow, 2);
    int compactControlGap = app_scale(app->mainWindow, 2);
    int collapsedControlGap = app_scale(app->mainWindow, 1);
    int fullButtonWidth = app_scale(app->mainWindow, 52);
    int compactButtonWidth = app_scale(app->mainWindow, 28);
    int collapsedButtonWidth = app_scale(app->mainWindow, 20);
    int fullRequired;
    int compactRequired;
    int controlGap;
    int buttonWidth;
    int rows;
    int controlTop;
    int controlBottom;
    int x;
    int group;

    fullRequired = ribbon_draw_required_width(
        ribbon, RIBBON_LAYOUT_FULL, fullButtonWidth, fullControlGap,
        groupGap, outer);
    compactRequired = ribbon_draw_required_width(
        ribbon, RIBBON_LAYOUT_COMPACT, compactButtonWidth,
        compactControlGap, groupGap, outer);
    if (width >= fullRequired) {
        ribbon->drawLayoutMode = RIBBON_LAYOUT_FULL;
        buttonWidth = fullButtonWidth;
        controlGap = fullControlGap;
        rows = 1;
    } else if (width >= compactRequired) {
        ribbon->drawLayoutMode = RIBBON_LAYOUT_COMPACT;
        buttonWidth = compactButtonWidth;
        controlGap = compactControlGap;
        rows = 2;
    } else {
        ribbon->drawLayoutMode = RIBBON_LAYOUT_COLLAPSED;
        buttonWidth = collapsedButtonWidth;
        controlGap = collapsedControlGap;
        rows = 3;
    }

    controlTop = app_scale(app->mainWindow, 5);
    controlBottom = height -
                    (ribbon->drawLayoutMode == RIBBON_LAYOUT_COLLAPSED
                         ? app_scale(app->mainWindow, 6)
                         : app_scale(app->mainWindow, 23));
    if (controlBottom < controlTop + rows) {
        controlBottom = controlTop + rows;
    }
    x = outer;
    for (group = 0; group < DRAW_GROUP_COUNT; ++group) {
        size_t count = ribbon_draw_group_control_count(ribbon, group);
        int groupRows = ribbon->drawLayoutMode == RIBBON_LAYOUT_FULL &&
                                group == DRAW_GROUP_UNDO
                            ? 2
                            : rows;
        size_t columns = groupRows > 0
                             ? (count + (size_t)groupRows - 1) /
                                   (size_t)groupRows
                             : count;
        int groupWidth = ribbon_draw_group_width(
            ribbon, group, ribbon->drawLayoutMode, buttonWidth,
            controlGap);
        int controlsWidth = (int)columns * buttonWidth +
                            max(0, (int)columns - 1) * controlGap;
        int controlsLeft = x + max(0, (groupWidth - controlsWidth) / 2);
        size_t localIndex = 0;
        size_t index;

        SetRect(&ribbon->drawGroupRects[group], x,
                app_scale(app->mainWindow, 2), x + groupWidth,
                max(app_scale(app->mainWindow, 3),
                    height - app_scale(app->mainWindow, 3)));
        for (index = 0; index < ribbon->drawControlCount; ++index) {
            DrawControlInfo *control = &ribbon->drawControls[index];
            size_t row;
            size_t column;
            int controlX;
            int controlY;

            if (control->group != group) {
                continue;
            }
            column = columns != 0 ? localIndex % columns : 0;
            row = columns != 0 ? localIndex / columns : 0;
            controlX = controlsLeft +
                       (int)column * (buttonWidth + controlGap);
            controlY = controlTop +
                       (int)row *
                           (max(1, (controlBottom - controlTop -
                                    (groupRows - 1) * controlGap) /
                                   groupRows) +
                            controlGap);
            ribbon_place_control(control->window, controlX, controlY,
                                 buttonWidth,
                                 max(1, (controlBottom - controlTop -
                                         (groupRows - 1) * controlGap) /
                                        groupRows));
            ++localIndex;
        }
        x += groupWidth + groupGap;
    }
}

static void ribbon_design_place(RibbonContext *ribbon,
                                size_t controlIndex, BOOL visible,
                                int x, int y, int width, int height)
{
    HWND control;

    if (ribbon == NULL ||
        controlIndex >= ribbon->designControlCount) {
        return;
    }
    control = ribbon->designControls[controlIndex].window;
    if (visible) {
        ribbon_place_control(control, x, y, width, height);
    }
    ShowWindow(
        control,
        visible && ribbon->activeTab == RIBBON_TAB_DESIGN
            ? SW_SHOWNA : SW_HIDE);
}

static void ribbon_layout_design(RibbonContext *ribbon, int width,
                                 int height)
{
    AppState *app;
    int outer;
    int groupGap;
    int controlGap;
    int controlTop;
    int controlBottom;
    int controlHeight;
    int pageGroupWidth;
    int documentRight;
    int x;
    int themesWidth;
    int tileWidth;
    int moreWidth;
    int colorsWidth;
    int fontsWidth;
    int stackWidth;
    int styleCount;
    int stackHeight;
    int index;

    if (ribbon == NULL || ribbon->app == NULL ||
        ribbon->designControlCount != DESIGN_MAX_CONTROLS) {
        return;
    }
    app = ribbon->app;
    if (ribbon->designGalleryVisible) {
        ribbon_hide_design_gallery(ribbon, FALSE);
    }
    if (width >= app_scale(app->mainWindow, 1200)) {
        ribbon->designLayoutMode = RIBBON_LAYOUT_FULL;
    } else if (width >= app_scale(app->mainWindow, 700)) {
        ribbon->designLayoutMode = RIBBON_LAYOUT_COMPACT;
    } else {
        ribbon->designLayoutMode = RIBBON_LAYOUT_COLLAPSED;
    }

    outer = app_scale(app->mainWindow, 6);
    groupGap = app_scale(app->mainWindow, 6);
    controlGap = app_scale(
        app->mainWindow,
        ribbon->designLayoutMode == RIBBON_LAYOUT_COLLAPSED ? 1 : 2);
    controlTop = app_scale(app->mainWindow, 5);
    controlBottom = height -
                    (ribbon->designLayoutMode == RIBBON_LAYOUT_COLLAPSED
                         ? app_scale(app->mainWindow, 6)
                         : app_scale(app->mainWindow, 23));
    controlHeight = max(1, controlBottom - controlTop);

    if (ribbon->designLayoutMode == RIBBON_LAYOUT_FULL) {
        pageGroupWidth = app_scale(app->mainWindow, 190);
        themesWidth = app_scale(app->mainWindow, 62);
        tileWidth = app_scale(app->mainWindow, 68);
        moreWidth = app_scale(app->mainWindow, 22);
        colorsWidth = app_scale(app->mainWindow, 54);
        fontsWidth = app_scale(app->mainWindow, 54);
        stackWidth = app_scale(app->mainWindow, 146);
        styleCount = DESIGN_INLINE_STYLE_COUNT;
    } else if (ribbon->designLayoutMode == RIBBON_LAYOUT_COMPACT) {
        pageGroupWidth = app_scale(app->mainWindow, 142);
        themesWidth = app_scale(app->mainWindow, 52);
        tileWidth = app_scale(app->mainWindow, 54);
        moreWidth = app_scale(app->mainWindow, 20);
        colorsWidth = app_scale(app->mainWindow, 44);
        fontsWidth = app_scale(app->mainWindow, 44);
        stackWidth = app_scale(app->mainWindow, 108);
        styleCount = 4;
    } else {
        pageGroupWidth = app_scale(app->mainWindow, 74);
        themesWidth = app_scale(app->mainWindow, 38);
        tileWidth = app_scale(app->mainWindow, 46);
        moreWidth = app_scale(app->mainWindow, 18);
        colorsWidth = app_scale(app->mainWindow, 34);
        fontsWidth = app_scale(app->mainWindow, 34);
        stackWidth = app_scale(app->mainWindow, 54);
        styleCount = 1;
    }

    documentRight = max(
        outer + 1, width - outer - pageGroupWidth - groupGap);
    SetRect(&ribbon->designGroupRects[
                DESIGN_GROUP_DOCUMENT_FORMATTING],
            outer, app_scale(app->mainWindow, 2), documentRight,
            max(app_scale(app->mainWindow, 3),
                height - app_scale(app->mainWindow, 3)));
    SetRect(&ribbon->designGroupRects[
                DESIGN_GROUP_PAGE_BACKGROUND],
            documentRight + groupGap,
            app_scale(app->mainWindow, 2),
            max(documentRight + groupGap + 1, width - outer),
            max(app_scale(app->mainWindow, 3),
                height - app_scale(app->mainWindow, 3)));

    x = outer;
    ribbon_design_place(ribbon, 0, TRUE, x, controlTop,
                        themesWidth, controlHeight);
    x += themesWidth + controlGap;

    if (ribbon->designLayoutMode == RIBBON_LAYOUT_FULL) {
        int galleryLeft = x;
        int galleryRight = max(
            galleryLeft + moreWidth + 1,
            documentRight -
                (colorsWidth + fontsWidth + stackWidth +
                 controlGap * 4));
        int availableTiles = max(
            1, galleryRight - galleryLeft - moreWidth -
                   controlGap);
        tileWidth = min(
            tileWidth,
            max(app_scale(app->mainWindow, 38),
                (availableTiles -
                 max(0, styleCount - 1) * controlGap) /
                    max(1, styleCount)));
        SetRect(&ribbon->designInlineGalleryRect, galleryLeft,
                controlTop, galleryRight, controlBottom);
    } else {
        SetRectEmpty(&ribbon->designInlineGalleryRect);
    }
    for (index = 0; index < DESIGN_INLINE_STYLE_COUNT; ++index) {
        BOOL visible = index < styleCount;
        ribbon_design_place(ribbon, (size_t)index + 1, visible,
                            x, controlTop, tileWidth, controlHeight);
        if (visible) {
            x += tileWidth + controlGap;
        }
    }
    if (ribbon->designLayoutMode == RIBBON_LAYOUT_FULL) {
        int galleryRight = ribbon->designInlineGalleryRect.right;
        ribbon_design_place(
            ribbon, 11, TRUE, galleryRight - moreWidth,
            controlTop, moreWidth, controlHeight);
        x = galleryRight + controlGap;
    } else {
        ribbon_design_place(ribbon, 11, TRUE, x, controlTop,
                            moreWidth, controlHeight);
        x += moreWidth + controlGap;
        SetRect(&ribbon->designInlineGalleryRect,
                outer + themesWidth + controlGap, controlTop,
                x - controlGap, controlBottom);
    }
    ribbon_design_place(ribbon, 12, TRUE, x, controlTop,
                        colorsWidth, controlHeight);
    x += colorsWidth + controlGap;
    ribbon_design_place(ribbon, 13, TRUE, x, controlTop,
                        fontsWidth, controlHeight);
    x += fontsWidth + controlGap;

    stackHeight = max(
        1, (controlHeight - controlGap * 2) / 3);
    for (index = 0; index < 3; ++index) {
        ribbon_design_place(
            ribbon, (size_t)index + 14, TRUE, x,
            controlTop + index * (stackHeight + controlGap),
            max(1, min(stackWidth, documentRight - x)),
            stackHeight);
    }

    x = documentRight + groupGap;
    {
        int available = max(3, width - outer - x);
        int pageButtonWidth = max(
            1, (available - controlGap * 2) / 3);
        for (index = 0; index < 3; ++index) {
            ribbon_design_place(
                ribbon, (size_t)index + 17, TRUE,
                x + index * (pageButtonWidth + controlGap),
                controlTop, pageButtonWidth, controlHeight);
        }
    }
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
    int chatWidth = app_scale(app->mainWindow, 104);
    int historyWidth = app_scale(app->mainWindow, 108);
    int summaryMinimum = app_scale(app->mainWindow, 48);
    int available = width - x - labelWidth - addWidth - chatWidth -
                    historyWidth - summaryMinimum - gap * 5 -
                    app_scale(app->mainWindow, 8);
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
    MoveWindow(ribbon->documentChatButton, x, firstY, chatWidth,
               controlHeight, TRUE);
    x += chatWidth + gap;
    MoveWindow(ribbon->versionHistoryButton, x, firstY, historyWidth,
               controlHeight, TRUE);
    x += historyWidth + gap;
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

static size_t ribbon_view_group_control_count(const RibbonContext *ribbon,
                                              int group)
{
    size_t count = 0;
    size_t index;

    if (ribbon == NULL || group < 0 || group >= VIEW_GROUP_COUNT) {
        return 0;
    }
    for (index = 0; index < ribbon->viewControlCount; ++index) {
        if (ribbon->viewControls[index].group == group) {
            ++count;
        }
    }
    return count;
}

static int ribbon_view_group_label_width(RibbonContext *ribbon, int group)
{
    AppState *app;
    HDC dc;
    HFONT previousFont = NULL;
    SIZE extent;
    int width;

    if (ribbon == NULL || ribbon->app == NULL || group < 0 ||
        group >= VIEW_GROUP_COUNT) {
        return 0;
    }
    app = ribbon->app;
    width = app_scale(app->mainWindow, 42);
    ZeroMemory(&extent, sizeof(extent));
    dc = GetDC(app->formatBar);
    if (dc == NULL) {
        return width;
    }
    if (app->uiFont != NULL) {
        previousFont = SelectObject(dc, app->uiFont);
    }
    if (GetTextExtentPoint32W(dc, viewGroupNames[group],
                              lstrlenW(viewGroupNames[group]), &extent)) {
        width = max(width, extent.cx + app_scale(app->mainWindow, 10));
    }
    if (previousFont != NULL) {
        SelectObject(dc, previousFont);
    }
    ReleaseDC(app->formatBar, dc);
    return width;
}

static int ribbon_view_group_rows(int group, int layoutMode)
{
    if (layoutMode == RIBBON_LAYOUT_COMPACT) {
        return 2;
    }
    if (layoutMode == RIBBON_LAYOUT_COLLAPSED) {
        return 3;
    }
    switch (group) {
    case VIEW_GROUP_VIEWS:
        return 2;
    case VIEW_GROUP_SHOW:
    case VIEW_GROUP_ZOOM:
    case VIEW_GROUP_WINDOW:
        return 3;
    default:
        return 1;
    }
}

static size_t ribbon_view_full_group_columns(int group, size_t count)
{
    switch (group) {
    case VIEW_GROUP_VIEWS:
        return 4;
    case VIEW_GROUP_SHOW:
        return 1;
    case VIEW_GROUP_ZOOM:
        return 3;
    case VIEW_GROUP_WINDOW:
        return 5;
    default:
        return count;
    }
}

static int ribbon_view_group_width(RibbonContext *ribbon, int group,
                                   int layoutMode, int buttonWidth,
                                   int controlGap)
{
    size_t count = ribbon_view_group_control_count(ribbon, group);
    int rows = ribbon_view_group_rows(group, layoutMode);
    size_t columns =
        layoutMode == RIBBON_LAYOUT_FULL
            ? ribbon_view_full_group_columns(group, count)
            : (rows > 0
                   ? (count + (size_t)rows - 1) / (size_t)rows
                   : count);
    int controlsWidth = (int)columns * buttonWidth +
                        max(0, (int)columns - 1) * controlGap;
    int padding = app_scale(ribbon->app->mainWindow, 6);
    int width = controlsWidth + padding;

    if (layoutMode != RIBBON_LAYOUT_COLLAPSED) {
        width = max(width, ribbon_view_group_label_width(ribbon, group));
    }
    return max(1, width);
}

static int ribbon_view_required_width(RibbonContext *ribbon,
                                      int layoutMode, int buttonWidth,
                                      int controlGap, int groupGap,
                                      int outer)
{
    int width = outer * 2;
    int group;

    for (group = 0; group < VIEW_GROUP_COUNT; ++group) {
        width += ribbon_view_group_width(
            ribbon, group, layoutMode, buttonWidth, controlGap);
        if (group + 1 < VIEW_GROUP_COUNT) {
            width += groupGap;
        }
    }
    return width;
}

static void ribbon_layout_view(RibbonContext *ribbon, int width, int height)
{
    AppState *app = ribbon->app;
    int outer = app_scale(app->mainWindow, 6);
    int groupGap = app_scale(app->mainWindow, 5);
    int fullControlGap = app_scale(app->mainWindow, 2);
    int compactControlGap = app_scale(app->mainWindow, 2);
    int collapsedControlGap = app_scale(app->mainWindow, 1);
    int fullButtonWidth = app_scale(app->mainWindow, 66);
    int compactButtonWidth = app_scale(app->mainWindow, 30);
    int collapsedButtonWidth = app_scale(app->mainWindow, 20);
    int fullRequired;
    int compactRequired;
    int controlGap;
    int buttonWidth;
    int controlTop;
    int controlBottom;
    int x;
    int group;

    fullRequired = ribbon_view_required_width(
        ribbon, RIBBON_LAYOUT_FULL, fullButtonWidth, fullControlGap,
        groupGap, outer);
    compactRequired = ribbon_view_required_width(
        ribbon, RIBBON_LAYOUT_COMPACT, compactButtonWidth,
        compactControlGap, groupGap, outer);
    if (width >= fullRequired) {
        ribbon->viewLayoutMode = RIBBON_LAYOUT_FULL;
        buttonWidth = fullButtonWidth;
        controlGap = fullControlGap;
    } else if (width >= compactRequired) {
        ribbon->viewLayoutMode = RIBBON_LAYOUT_COMPACT;
        buttonWidth = compactButtonWidth;
        controlGap = compactControlGap;
    } else {
        ribbon->viewLayoutMode = RIBBON_LAYOUT_COLLAPSED;
        buttonWidth = collapsedButtonWidth;
        controlGap = collapsedControlGap;
    }

    controlTop = app_scale(app->mainWindow, 5);
    controlBottom = height -
                    (ribbon->viewLayoutMode == RIBBON_LAYOUT_COLLAPSED
                         ? app_scale(app->mainWindow, 6)
                         : app_scale(app->mainWindow, 23));
    if (controlBottom <= controlTop) {
        controlBottom = controlTop + 1;
    }
    x = outer;
    for (group = 0; group < VIEW_GROUP_COUNT; ++group) {
        size_t count = ribbon_view_group_control_count(ribbon, group);
        int rows = ribbon_view_group_rows(group, ribbon->viewLayoutMode);
        size_t columns =
            ribbon->viewLayoutMode == RIBBON_LAYOUT_FULL
                ? ribbon_view_full_group_columns(group, count)
                : (rows > 0
                       ? (count + (size_t)rows - 1) / (size_t)rows
                       : count);
        int groupWidth = ribbon_view_group_width(
            ribbon, group, ribbon->viewLayoutMode, buttonWidth,
            controlGap);
        int controlsWidth = (int)columns * buttonWidth +
                            max(0, (int)columns - 1) * controlGap;
        int controlsLeft = x + max(0, (groupWidth - controlsWidth) / 2);
        int controlHeight = max(
            1, (controlBottom - controlTop -
                max(0, rows - 1) * controlGap) / max(1, rows));
        size_t localIndex = 0;
        size_t index;

        SetRect(&ribbon->viewGroupRects[group], x,
                app_scale(app->mainWindow, 2), x + groupWidth,
                max(app_scale(app->mainWindow, 3),
                    height - app_scale(app->mainWindow, 3)));
        for (index = 0; index < ribbon->viewControlCount; ++index) {
            ViewControlInfo *control = &ribbon->viewControls[index];
            size_t row;
            size_t column;
            int controlY;
            int placedHeight;

            if (control->group != group) {
                continue;
            }
            if (ribbon->viewLayoutMode == RIBBON_LAYOUT_FULL) {
                BOOL large = FALSE;
                int stackedRows = 1;

                switch (group) {
                case VIEW_GROUP_VIEWS:
                    large = localIndex < 3;
                    column = large ? localIndex : 3;
                    row = large ? 0 : localIndex - 3;
                    stackedRows = 2;
                    break;
                case VIEW_GROUP_SHOW:
                    column = 0;
                    row = localIndex;
                    stackedRows = 3;
                    break;
                case VIEW_GROUP_ZOOM:
                    large = localIndex < 2;
                    column = large ? localIndex : 2;
                    row = large ? 0 : localIndex - 2;
                    stackedRows = 3;
                    break;
                case VIEW_GROUP_WINDOW:
                    large = localIndex < 3 || localIndex == 6;
                    column = localIndex < 3
                                 ? localIndex
                                 : (localIndex < 6 ? 3 : 4);
                    row = localIndex < 3 || localIndex == 6
                              ? 0 : localIndex - 3;
                    stackedRows = 3;
                    break;
                default:
                    large = TRUE;
                    column = localIndex;
                    row = 0;
                    break;
                }
                if (large) {
                    controlY = controlTop;
                    placedHeight = max(1, controlBottom - controlTop);
                } else {
                    placedHeight = max(
                        1, (controlBottom - controlTop -
                            (stackedRows - 1) * controlGap) /
                               stackedRows);
                    controlY = controlTop +
                               (int)row *
                                   (placedHeight + controlGap);
                }
            } else {
                column = columns != 0 ? localIndex % columns : 0;
                row = columns != 0 ? localIndex / columns : 0;
                controlY = controlTop +
                           (int)row *
                               (controlHeight + controlGap);
                placedHeight = controlHeight;
            }
            ribbon_place_control(
                control->window,
                controlsLeft + (int)column * (buttonWidth + controlGap),
                controlY, buttonWidth, placedHeight);
            ++localIndex;
        }
        x += groupWidth + groupGap;
    }
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
    ribbon_layout_insert(ribbon, width, height);
    ribbon_layout_draw(ribbon, width, height);
    ribbon_layout_design(ribbon, width, height);
    ribbon_layout_layout_page(ribbon);
    ribbon_layout_placeholder(ribbon, RIBBON_TAB_REFERENCES, width);
    ribbon_layout_placeholder(ribbon, RIBBON_TAB_MAILINGS, width);
    ribbon_layout_review(ribbon, width);
    ribbon_layout_view(ribbon, width, height);
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
        MoveToEx(dc, x, top, NULL);
        LineTo(dc, x, bottom);
        ++painted;
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

void ribbon_paint_insert_groups(AppState *app, HDC dc)
{
    RibbonContext *ribbon;
    COLORREF divider;
    COLORREF textColor;
    HPEN pen;
    HGDIOBJ previousPen;
    HGDIOBJ previousFont = NULL;
    int previousBackgroundMode;
    COLORREF previousTextColor;
    int group;
    UINT painted = 0;

    if (!ribbon_valid_context(app) || dc == NULL) {
        return;
    }
    ribbon = app->ribbon;
    if (ribbon->activeTab != RIBBON_TAB_INSERT) {
        return;
    }
    divider = app->useBrandColors
                  ? ribbon_blend_color(app->palette.controlBorder,
                                       app->palette.controlBackground, 42)
                  : GetSysColor(COLOR_3DSHADOW);
    textColor = app->useBrandColors
                    ? app->palette.formatText
                    : GetSysColor(COLOR_BTNTEXT);
    pen = CreatePen(PS_SOLID, max(1, app_scale(app->mainWindow, 1)),
                    divider);
    if (pen == NULL) {
        return;
    }
    previousPen = SelectObject(dc, pen);
    if (app->uiFont != NULL) {
        previousFont = SelectObject(dc, app->uiFont);
    }
    previousBackgroundMode = SetBkMode(dc, TRANSPARENT);
    previousTextColor = SetTextColor(dc, textColor);

    for (group = 0; group < INSERT_GROUP_COUNT; ++group) {
        const RECT *rect = &ribbon->insertGroupRects[group];

        if (IsRectEmpty(rect)) {
            continue;
        }
        if (ribbon->insertLayoutMode != RIBBON_LAYOUT_COLLAPSED) {
            RECT label = *rect;
            label.top = max(label.top, label.bottom -
                            app_scale(app->mainWindow, 19));
            InflateRect(&label, -app_scale(app->mainWindow, 2), 0);
            if (label.right > label.left) {
                DrawTextW(dc, insertGroupNames[group], -1, &label,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE |
                              DT_END_ELLIPSIS | DT_NOPREFIX);
                ++painted;
            }
        }
        if (group + 1 < INSERT_GROUP_COUNT) {
            const RECT *next = &ribbon->insertGroupRects[group + 1];
            int dividerX = rect->right + (next->left - rect->right) / 2;
            int top = rect->top + app_scale(app->mainWindow, 5);
            int bottom = rect->bottom - app_scale(app->mainWindow, 5);
            MoveToEx(dc, dividerX, top, NULL);
            LineTo(dc, dividerX, bottom);
            ++painted;
        }
    }

    SetTextColor(dc, previousTextColor);
    SetBkMode(dc, previousBackgroundMode);
    if (previousFont != NULL) {
        SelectObject(dc, previousFont);
    }
    SelectObject(dc, previousPen);
    DeleteObject(pen);
    if (painted > 0) {
        ribbon->insertGroupPaintCount =
            ribbon->insertGroupPaintCount == UINT_MAX
                ? 1
                : ribbon->insertGroupPaintCount + 1;
    }
}

void ribbon_paint_draw_groups(AppState *app, HDC dc)
{
    RibbonContext *ribbon;
    COLORREF divider;
    COLORREF textColor;
    HPEN pen;
    HGDIOBJ previousPen;
    HGDIOBJ previousFont = NULL;
    int previousBackgroundMode;
    COLORREF previousTextColor;
    int group;
    UINT painted = 0;

    if (!ribbon_valid_context(app) || dc == NULL) {
        return;
    }
    ribbon = app->ribbon;
    if (ribbon->activeTab != RIBBON_TAB_DRAW) {
        return;
    }
    divider = app->useBrandColors
                  ? ribbon_blend_color(app->palette.controlBorder,
                                       app->palette.controlBackground, 42)
                  : GetSysColor(COLOR_3DSHADOW);
    textColor = app->useBrandColors
                    ? app->palette.formatText
                    : GetSysColor(COLOR_BTNTEXT);
    pen = CreatePen(PS_SOLID, max(1, app_scale(app->mainWindow, 1)),
                    divider);
    if (pen == NULL) {
        return;
    }
    previousPen = SelectObject(dc, pen);
    if (app->uiFont != NULL) {
        previousFont = SelectObject(dc, app->uiFont);
    }
    previousBackgroundMode = SetBkMode(dc, TRANSPARENT);
    previousTextColor = SetTextColor(dc, textColor);

    for (group = 0; group < DRAW_GROUP_COUNT; ++group) {
        const RECT *rect = &ribbon->drawGroupRects[group];

        if (IsRectEmpty(rect)) {
            continue;
        }
        if (ribbon->drawLayoutMode != RIBBON_LAYOUT_COLLAPSED) {
            RECT label = *rect;
            label.top = max(label.top, label.bottom -
                            app_scale(app->mainWindow, 19));
            InflateRect(&label, -app_scale(app->mainWindow, 2), 0);
            if (label.right > label.left) {
                DrawTextW(dc, drawGroupNames[group], -1, &label,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE |
                              DT_END_ELLIPSIS | DT_NOPREFIX);
                ++painted;
            }
        }
        if (group + 1 < DRAW_GROUP_COUNT) {
            const RECT *next = &ribbon->drawGroupRects[group + 1];
            int dividerX = rect->right + (next->left - rect->right) / 2;
            int top = rect->top + app_scale(app->mainWindow, 5);
            int bottom = rect->bottom - app_scale(app->mainWindow, 5);
            MoveToEx(dc, dividerX, top, NULL);
            LineTo(dc, dividerX, bottom);
            ++painted;
        }
    }

    SetTextColor(dc, previousTextColor);
    SetBkMode(dc, previousBackgroundMode);
    if (previousFont != NULL) {
        SelectObject(dc, previousFont);
    }
    SelectObject(dc, previousPen);
    DeleteObject(pen);
    if (painted > 0) {
        ribbon->drawGroupPaintCount =
            ribbon->drawGroupPaintCount == UINT_MAX
                ? 1
                : ribbon->drawGroupPaintCount + 1;
    }
}

void ribbon_paint_design_groups(AppState *app, HDC dc)
{
    RibbonContext *ribbon;
    COLORREF divider;
    COLORREF textColor;
    HPEN pen;
    HGDIOBJ previousPen;
    HGDIOBJ previousFont = NULL;
    int previousBackgroundMode;
    COLORREF previousTextColor;
    int group;
    UINT painted = 0;

    if (!ribbon_valid_context(app) || dc == NULL) {
        return;
    }
    ribbon = app->ribbon;
    if (ribbon->activeTab != RIBBON_TAB_DESIGN) {
        return;
    }
    divider = app->useBrandColors
                  ? ribbon_blend_color(app->palette.controlBorder,
                                       app->palette.controlBackground, 42)
                  : GetSysColor(COLOR_3DSHADOW);
    textColor = app->useBrandColors
                    ? app->palette.formatText
                    : GetSysColor(COLOR_BTNTEXT);
    pen = CreatePen(PS_SOLID, max(1, app_scale(app->mainWindow, 1)),
                    divider);
    if (pen == NULL) {
        return;
    }
    previousPen = SelectObject(dc, pen);
    if (app->uiFont != NULL) {
        previousFont = SelectObject(dc, app->uiFont);
    }
    previousBackgroundMode = SetBkMode(dc, TRANSPARENT);
    previousTextColor = SetTextColor(dc, textColor);

    for (group = 0; group < DESIGN_GROUP_COUNT; ++group) {
        const RECT *rect = &ribbon->designGroupRects[group];

        if (IsRectEmpty(rect)) {
            continue;
        }
        if (ribbon->designLayoutMode != RIBBON_LAYOUT_COLLAPSED) {
            RECT label = *rect;
            label.top = max(
                label.top,
                label.bottom - app_scale(app->mainWindow, 19));
            InflateRect(&label, -app_scale(app->mainWindow, 2), 0);
            if (label.right > label.left) {
                DrawTextW(dc, designGroupNames[group], -1, &label,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE |
                              DT_END_ELLIPSIS | DT_NOPREFIX);
                ++painted;
            }
        }
        if (group + 1 < DESIGN_GROUP_COUNT) {
            const RECT *next =
                &ribbon->designGroupRects[group + 1];
            int dividerX =
                rect->right + (next->left - rect->right) / 2;
            int top = rect->top + app_scale(app->mainWindow, 5);
            int bottom =
                rect->bottom - app_scale(app->mainWindow, 5);
            MoveToEx(dc, dividerX, top, NULL);
            LineTo(dc, dividerX, bottom);
            ++painted;
        }
    }

    SetTextColor(dc, previousTextColor);
    SetBkMode(dc, previousBackgroundMode);
    if (previousFont != NULL) {
        SelectObject(dc, previousFont);
    }
    SelectObject(dc, previousPen);
    DeleteObject(pen);
    if (painted > 0) {
        ribbon->designGroupPaintCount =
            ribbon->designGroupPaintCount == UINT_MAX
                ? 1
                : ribbon->designGroupPaintCount + 1;
    }
}

void ribbon_paint_view_groups(AppState *app, HDC dc)
{
    RibbonContext *ribbon;
    COLORREF divider;
    COLORREF textColor;
    HPEN pen;
    HGDIOBJ previousPen;
    HGDIOBJ previousFont = NULL;
    int previousBackgroundMode;
    COLORREF previousTextColor;
    int group;
    UINT painted = 0;

    if (!ribbon_valid_context(app) || dc == NULL) {
        return;
    }
    ribbon = app->ribbon;
    if (ribbon->activeTab != RIBBON_TAB_VIEW) {
        return;
    }
    divider = app->useBrandColors
                  ? ribbon_blend_color(app->palette.controlBorder,
                                       app->palette.controlBackground, 42)
                  : GetSysColor(COLOR_3DSHADOW);
    textColor = app->useBrandColors
                    ? app->palette.formatText
                    : GetSysColor(COLOR_BTNTEXT);
    pen = CreatePen(PS_SOLID, max(1, app_scale(app->mainWindow, 1)),
                    divider);
    if (pen == NULL) {
        return;
    }
    previousPen = SelectObject(dc, pen);
    if (app->uiFont != NULL) {
        previousFont = SelectObject(dc, app->uiFont);
    }
    previousBackgroundMode = SetBkMode(dc, TRANSPARENT);
    previousTextColor = SetTextColor(dc, textColor);

    for (group = 0; group < VIEW_GROUP_COUNT; ++group) {
        const RECT *rect = &ribbon->viewGroupRects[group];

        if (IsRectEmpty(rect)) {
            continue;
        }
        if (ribbon->viewLayoutMode != RIBBON_LAYOUT_COLLAPSED) {
            RECT label = *rect;
            label.top = max(label.top, label.bottom -
                            app_scale(app->mainWindow, 19));
            InflateRect(&label, -app_scale(app->mainWindow, 2), 0);
            if (label.right > label.left) {
                DrawTextW(dc, viewGroupNames[group], -1, &label,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE |
                              DT_END_ELLIPSIS | DT_NOPREFIX);
                ++painted;
            }
        }
        if (group + 1 < VIEW_GROUP_COUNT) {
            const RECT *next = &ribbon->viewGroupRects[group + 1];
            int dividerX = rect->right + (next->left - rect->right) / 2;
            int top = rect->top + app_scale(app->mainWindow, 5);
            int bottom = rect->bottom - app_scale(app->mainWindow, 5);
            MoveToEx(dc, dividerX, top, NULL);
            LineTo(dc, dividerX, bottom);
            ++painted;
        }
    }

    SetTextColor(dc, previousTextColor);
    SetBkMode(dc, previousBackgroundMode);
    if (previousFont != NULL) {
        SelectObject(dc, previousFont);
    }
    SelectObject(dc, previousPen);
    DeleteObject(pen);
    if (painted > 0) {
        ribbon->viewGroupPaintCount =
            ribbon->viewGroupPaintCount == UINT_MAX
                ? 1
                : ribbon->viewGroupPaintCount + 1;
    }
}

static InsertControlInfo *ribbon_find_insert_control(RibbonContext *ribbon,
                                                     HWND window)
{
    size_t index;

    if (ribbon == NULL || window == NULL) {
        return NULL;
    }
    for (index = 0; index < ribbon->insertControlCount; ++index) {
        if (ribbon->insertControls[index].window == window) {
            return &ribbon->insertControls[index];
        }
    }
    return NULL;
}

static DrawControlInfo *ribbon_find_draw_control(RibbonContext *ribbon,
                                                 HWND window)
{
    size_t index;

    if (ribbon == NULL || window == NULL) {
        return NULL;
    }
    for (index = 0; index < ribbon->drawControlCount; ++index) {
        if (ribbon->drawControls[index].window == window) {
            return &ribbon->drawControls[index];
        }
    }
    return NULL;
}

static ViewControlInfo *ribbon_find_view_control(RibbonContext *ribbon,
                                                 HWND window)
{
    size_t index;

    if (ribbon == NULL || window == NULL) {
        return NULL;
    }
    for (index = 0; index < ribbon->viewControlCount; ++index) {
        if (ribbon->viewControls[index].window == window) {
            return &ribbon->viewControls[index];
        }
    }
    return NULL;
}

static DesignControlInfo *ribbon_find_design_control(
    RibbonContext *ribbon, HWND window)
{
    size_t index;

    if (ribbon == NULL || window == NULL) {
        return NULL;
    }
    for (index = 0; index < ribbon->designControlCount; ++index) {
        if (ribbon->designControls[index].window == window) {
            return &ribbon->designControls[index];
        }
    }
    return NULL;
}

static int ribbon_icon_coordinate(int start, int extent, int percent)
{
    return start + MulDiv(extent, percent, 100);
}

static POINT ribbon_icon_point(const RECT *rect, int xPercent, int yPercent)
{
    POINT point;
    point.x = ribbon_icon_coordinate(rect->left,
                                     rect->right - rect->left, xPercent);
    point.y = ribbon_icon_coordinate(rect->top,
                                     rect->bottom - rect->top, yPercent);
    return point;
}

static void ribbon_icon_line(HDC dc, const RECT *rect,
                             int x1, int y1, int x2, int y2)
{
    POINT first = ribbon_icon_point(rect, x1, y1);
    POINT second = ribbon_icon_point(rect, x2, y2);
    MoveToEx(dc, first.x, first.y, NULL);
    LineTo(dc, second.x, second.y);
}

static void ribbon_icon_rectangle(HDC dc, const RECT *rect,
                                  int left, int top, int right, int bottom)
{
    POINT first = ribbon_icon_point(rect, left, top);
    POINT second = ribbon_icon_point(rect, right, bottom);
    Rectangle(dc, first.x, first.y, second.x, second.y);
}

static void ribbon_icon_ellipse(HDC dc, const RECT *rect,
                                int left, int top, int right, int bottom)
{
    POINT first = ribbon_icon_point(rect, left, top);
    POINT second = ribbon_icon_point(rect, right, bottom);
    Ellipse(dc, first.x, first.y, second.x, second.y);
}

static COLORREF ribbon_draw_icon_accent(const AppState *app,
                                        RibbonDrawIcon icon,
                                        COLORREF fallback)
{
    if (app == NULL || !app->useBrandColors) {
        return fallback;
    }
    switch (icon) {
    case RIBBON_DRAW_ICON_PEN_BLACK:
        return RGB(24, 24, 26);
    case RIBBON_DRAW_ICON_PENCIL:
        return RGB(148, 158, 168);
    case RIBBON_DRAW_ICON_DRAW:
    case RIBBON_DRAW_ICON_PEN_RED:
        return RGB(224, 49, 66);
    case RIBBON_DRAW_ICON_HIGHLIGHTER:
        return RGB(244, 218, 32);
    case RIBBON_DRAW_ICON_PEN_BLUE:
    case RIBBON_DRAW_ICON_ACTION_PEN:
    case RIBBON_DRAW_ICON_FORMAT_BACKGROUND:
    case RIBBON_DRAW_ICON_INK_TO_SHAPE:
    case RIBBON_DRAW_ICON_INK_TO_MATH:
        return RGB(38, 126, 191);
    case RIBBON_DRAW_ICON_PEN_GREEN:
        return RGB(24, 154, 86);
    case RIBBON_DRAW_ICON_ERASER:
        return RGB(151, 54, 184);
    case RIBBON_DRAW_ICON_ADD_PEN:
    case RIBBON_DRAW_ICON_CANVAS:
        return RGB(45, 168, 92);
    default:
        return fallback;
    }
}

static void ribbon_draw_pen_body(HDC dc, const RECT *rect,
                                 HBRUSH accentBrush, BOOL highlighter,
                                 BOOL pencil)
{
    POINT body[5];
    POINT tip[4];

    if (pencil) {
        body[0] = ribbon_icon_point(rect, 29, 7);
        body[1] = ribbon_icon_point(rect, 71, 7);
        body[2] = ribbon_icon_point(rect, 68, 69);
        body[3] = ribbon_icon_point(rect, 50, 96);
        body[4] = ribbon_icon_point(rect, 32, 69);
        SelectObject(dc, accentBrush);
        Polygon(dc, body, ARRAYSIZE(body));
        SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        ribbon_icon_line(dc, rect, 32, 69, 68, 69);
        ribbon_icon_line(dc, rect, 50, 96, 50, 72);
        return;
    }

    body[0] = ribbon_icon_point(rect, 25, 5);
    body[1] = ribbon_icon_point(rect, 75, 5);
    body[2] = ribbon_icon_point(rect, 72, highlighter ? 72 : 68);
    body[3] = ribbon_icon_point(rect, 28, highlighter ? 72 : 68);
    body[4] = body[0];
    SelectObject(dc, accentBrush);
    Polygon(dc, body, ARRAYSIZE(body));
    SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    tip[0] = ribbon_icon_point(rect, 28, highlighter ? 72 : 68);
    tip[1] = ribbon_icon_point(rect, 72, highlighter ? 72 : 68);
    tip[2] = ribbon_icon_point(rect, highlighter ? 62 : 50, 96);
    tip[3] = ribbon_icon_point(rect, highlighter ? 35 : 28, 82);
    Polygon(dc, tip, ARRAYSIZE(tip));
    if (!highlighter) {
        ribbon_icon_ellipse(dc, rect, 44, 73, 56, 85);
    }
}

static BOOL ribbon_draw_draw_glyph(AppState *app, HDC dc,
                                   RibbonDrawIcon icon,
                                   const RECT *rect, COLORREF color)
{
    COLORREF accent;
    HPEN pen;
    HPEN accentPen;
    HBRUSH accentBrush;
    HGDIOBJ previousPen;
    HGDIOBJ previousBrush;
    COLORREF previousText;
    int previousBackground;
    int width;
    int height;
    int penWidth;
    BOOL drawn = TRUE;

    if (dc == NULL || rect == NULL || rect->right <= rect->left ||
        rect->bottom <= rect->top) {
        return FALSE;
    }
    width = rect->right - rect->left;
    height = rect->bottom - rect->top;
    penWidth = max(1, min(width, height) / 12);
    accent = ribbon_draw_icon_accent(app, icon, color);
    pen = CreatePen(PS_SOLID, penWidth, color);
    accentPen = CreatePen(PS_SOLID, penWidth, accent);
    accentBrush = CreateSolidBrush(accent);
    if (pen == NULL || accentPen == NULL || accentBrush == NULL) {
        if (pen != NULL) {
            DeleteObject(pen);
        }
        if (accentPen != NULL) {
            DeleteObject(accentPen);
        }
        if (accentBrush != NULL) {
            DeleteObject(accentBrush);
        }
        return FALSE;
    }
    previousPen = SelectObject(dc, pen);
    previousBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    previousText = SetTextColor(dc, color);
    previousBackground = SetBkMode(dc, TRANSPARENT);

    switch (icon) {
    case RIBBON_DRAW_ICON_DRAW: {
        POINT stroke[4];
        POINT nib[4];
        nib[0] = ribbon_icon_point(rect, 18, 69);
        nib[1] = ribbon_icon_point(rect, 65, 12);
        nib[2] = ribbon_icon_point(rect, 80, 25);
        nib[3] = ribbon_icon_point(rect, 31, 80);
        Polygon(dc, nib, ARRAYSIZE(nib));
        SelectObject(dc, accentPen);
        stroke[0] = ribbon_icon_point(rect, 6, 87);
        stroke[1] = ribbon_icon_point(rect, 31, 72);
        stroke[2] = ribbon_icon_point(rect, 52, 96);
        stroke[3] = ribbon_icon_point(rect, 94, 78);
        PolyBezier(dc, stroke, ARRAYSIZE(stroke));
        break;
    }
    case RIBBON_DRAW_ICON_UNDO:
    case RIBBON_DRAW_ICON_REDO: {
        BOOL redo = icon == RIBBON_DRAW_ICON_REDO;
        POINT arrow[3];
        POINT box1 = ribbon_icon_point(rect, 12, 18);
        POINT box2 = ribbon_icon_point(rect, 88, 91);
        POINT start = ribbon_icon_point(rect, redo ? 75 : 25, 76);
        POINT end = ribbon_icon_point(rect, redo ? 25 : 75, 24);
        Arc(dc, box1.x, box1.y, box2.x, box2.y,
            start.x, start.y, end.x, end.y);
        arrow[0] = ribbon_icon_point(rect, redo ? 73 : 27, 12);
        arrow[1] = ribbon_icon_point(rect, redo ? 91 : 9, 28);
        arrow[2] = ribbon_icon_point(rect, redo ? 73 : 27, 42);
        SelectObject(dc, accentBrush);
        Polygon(dc, arrow, ARRAYSIZE(arrow));
        break;
    }
    case RIBBON_DRAW_ICON_SELECT: {
        POINT pointer[7];
        pointer[0] = ribbon_icon_point(rect, 18, 5);
        pointer[1] = ribbon_icon_point(rect, 18, 88);
        pointer[2] = ribbon_icon_point(rect, 39, 69);
        pointer[3] = ribbon_icon_point(rect, 53, 96);
        pointer[4] = ribbon_icon_point(rect, 66, 89);
        pointer[5] = ribbon_icon_point(rect, 52, 63);
        pointer[6] = ribbon_icon_point(rect, 82, 60);
        SelectObject(dc, accentBrush);
        Polygon(dc, pointer, ARRAYSIZE(pointer));
        break;
    }
    case RIBBON_DRAW_ICON_LASSO_SELECT: {
        HPEN dotted = CreatePen(PS_DOT, max(1, penWidth / 2), color);
        if (dotted != NULL) {
            SelectObject(dc, dotted);
        }
        ribbon_icon_ellipse(dc, rect, 8, 8, 86, 78);
        ribbon_icon_line(dc, rect, 76, 68, 94, 93);
        if (dotted != NULL) {
            SelectObject(dc, pen);
            DeleteObject(dotted);
        }
        ribbon_icon_ellipse(dc, rect, 68, 71, 82, 85);
        break;
    }
    case RIBBON_DRAW_ICON_ERASER: {
        POINT eraser[4];
        eraser[0] = ribbon_icon_point(rect, 26, 7);
        eraser[1] = ribbon_icon_point(rect, 74, 7);
        eraser[2] = ribbon_icon_point(rect, 74, 82);
        eraser[3] = ribbon_icon_point(rect, 26, 82);
        SelectObject(dc, accentBrush);
        Polygon(dc, eraser, ARRAYSIZE(eraser));
        SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        ribbon_icon_line(dc, rect, 26, 65, 74, 65);
        ribbon_icon_rectangle(dc, rect, 20, 82, 80, 96);
        break;
    }
    case RIBBON_DRAW_ICON_PEN_BLACK:
    case RIBBON_DRAW_ICON_PEN_RED:
    case RIBBON_DRAW_ICON_PEN_BLUE:
    case RIBBON_DRAW_ICON_PEN_GREEN:
        ribbon_draw_pen_body(dc, rect, accentBrush, FALSE, FALSE);
        break;
    case RIBBON_DRAW_ICON_PENCIL:
        ribbon_draw_pen_body(dc, rect, accentBrush, FALSE, TRUE);
        break;
    case RIBBON_DRAW_ICON_HIGHLIGHTER:
        ribbon_draw_pen_body(dc, rect, accentBrush, TRUE, FALSE);
        break;
    case RIBBON_DRAW_ICON_ACTION_PEN:
        ribbon_draw_pen_body(dc, rect, accentBrush, FALSE, FALSE);
        SelectObject(dc, accentPen);
        ribbon_icon_line(dc, rect, 8, 23, 23, 23);
        ribbon_icon_line(dc, rect, 15, 15, 15, 31);
        ribbon_icon_line(dc, rect, 78, 76, 94, 76);
        ribbon_icon_line(dc, rect, 86, 68, 86, 84);
        break;
    case RIBBON_DRAW_ICON_ADD_PEN:
        SelectObject(dc, accentPen);
        ribbon_icon_line(dc, rect, 50, 8, 50, 92);
        ribbon_icon_line(dc, rect, 8, 50, 92, 50);
        break;
    case RIBBON_DRAW_ICON_RULER: {
        POINT ruler[4];
        size_t tick;
        ruler[0] = ribbon_icon_point(rect, 5, 59);
        ruler[1] = ribbon_icon_point(rect, 63, 5);
        ruler[2] = ribbon_icon_point(rect, 95, 39);
        ruler[3] = ribbon_icon_point(rect, 36, 94);
        Polygon(dc, ruler, ARRAYSIZE(ruler));
        for (tick = 0; tick < 5; ++tick) {
            int offset = 18 + (int)tick * 13;
            ribbon_icon_line(dc, rect, offset, 55 - (int)tick * 12,
                             offset + 9, 64 - (int)tick * 12);
        }
        break;
    }
    case RIBBON_DRAW_ICON_FORMAT_BACKGROUND:
        ribbon_icon_rectangle(dc, rect, 8, 8, 92, 92);
        SelectObject(dc, accentPen);
        ribbon_icon_line(dc, rect, 12, 27, 88, 27);
        ribbon_icon_line(dc, rect, 12, 45, 88, 45);
        ribbon_icon_line(dc, rect, 12, 63, 88, 63);
        ribbon_icon_line(dc, rect, 12, 81, 88, 81);
        break;
    case RIBBON_DRAW_ICON_INK_TO_SHAPE:
        SelectObject(dc, accentPen);
        ribbon_icon_line(dc, rect, 10, 18, 39, 18);
        ribbon_icon_line(dc, rect, 10, 18, 10, 47);
        ribbon_icon_line(dc, rect, 10, 47, 39, 47);
        ribbon_icon_rectangle(dc, rect, 51, 47, 94, 90);
        ribbon_icon_line(dc, rect, 34, 33, 66, 64);
        break;
    case RIBBON_DRAW_ICON_INK_TO_MATH: {
        RECT symbolRect = *rect;
        symbolRect.right = ribbon_icon_coordinate(
            rect->left, width, 68);
        DrawTextW(dc, L"\x03C0", 1, &symbolRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, accentPen);
        ribbon_icon_line(dc, rect, 58, 28, 92, 62);
        ribbon_icon_line(dc, rect, 79, 62, 92, 62);
        ribbon_icon_line(dc, rect, 92, 62, 92, 49);
        break;
    }
    case RIBBON_DRAW_ICON_CANVAS:
        ribbon_icon_rectangle(dc, rect, 12, 23, 92, 88);
        ribbon_icon_line(dc, rect, 20, 70, 38, 51);
        ribbon_icon_line(dc, rect, 38, 51, 54, 66);
        ribbon_icon_line(dc, rect, 54, 66, 76, 42);
        SelectObject(dc, accentPen);
        ribbon_icon_line(dc, rect, 25, 4, 25, 35);
        ribbon_icon_line(dc, rect, 9, 19, 41, 19);
        break;
    case RIBBON_DRAW_ICON_REPLAY: {
        POINT box1 = ribbon_icon_point(rect, 8, 8);
        POINT box2 = ribbon_icon_point(rect, 92, 92);
        POINT start = ribbon_icon_point(rect, 72, 84);
        POINT end = ribbon_icon_point(rect, 77, 17);
        POINT arrow[3];
        POINT play[3];
        Arc(dc, box1.x, box1.y, box2.x, box2.y,
            start.x, start.y, end.x, end.y);
        arrow[0] = ribbon_icon_point(rect, 67, 8);
        arrow[1] = ribbon_icon_point(rect, 92, 14);
        arrow[2] = ribbon_icon_point(rect, 77, 34);
        play[0] = ribbon_icon_point(rect, 42, 32);
        play[1] = ribbon_icon_point(rect, 42, 70);
        play[2] = ribbon_icon_point(rect, 70, 51);
        SelectObject(dc, accentBrush);
        Polygon(dc, arrow, ARRAYSIZE(arrow));
        Polygon(dc, play, ARRAYSIZE(play));
        break;
    }
    case RIBBON_DRAW_ICON_HELP: {
        RECT textRect = *rect;
        ribbon_icon_ellipse(dc, rect, 8, 5, 92, 95);
        DrawTextW(dc, L"?", 1, &textRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        break;
    }
    default:
        drawn = FALSE;
        break;
    }

    SetTextColor(dc, previousText);
    SetBkMode(dc, previousBackground);
    SelectObject(dc, previousBrush);
    SelectObject(dc, previousPen);
    DeleteObject(accentBrush);
    DeleteObject(accentPen);
    DeleteObject(pen);
    return drawn;
}

static COLORREF ribbon_view_icon_accent(const AppState *app,
                                        RibbonViewIcon icon,
                                        COLORREF fallback)
{
    if (app == NULL || !app->useBrandColors) {
        return fallback;
    }
    switch (icon) {
    case RIBBON_VIEW_ICON_SWITCH_MODES:
        return RGB(222, 154, 36);
    case RIBBON_VIEW_ICON_MACROS:
        return RGB(211, 52, 65);
    case RIBBON_VIEW_ICON_PROPERTIES:
        return RGB(20, 151, 142);
    case RIBBON_VIEW_ICON_NEW_WINDOW:
    case RIBBON_VIEW_ICON_SIDE_BY_SIDE:
        return RGB(38, 158, 91);
    default:
        return RGB(38, 126, 191);
    }
}

static BOOL ribbon_draw_view_glyph(AppState *app, HDC dc,
                                   RibbonViewIcon icon,
                                   const RECT *rect, COLORREF color)
{
    COLORREF accent;
    HPEN pen;
    HPEN accentPen;
    HBRUSH accentBrush;
    HGDIOBJ previousPen;
    HGDIOBJ previousBrush;
    COLORREF previousText;
    int previousBackground;
    int width;
    int height;
    int penWidth;
    BOOL drawn = TRUE;
    size_t index;

    if (dc == NULL || rect == NULL || rect->right <= rect->left ||
        rect->bottom <= rect->top) {
        return FALSE;
    }
    width = rect->right - rect->left;
    height = rect->bottom - rect->top;
    penWidth = max(1, min(width, height) / 13);
    accent = ribbon_view_icon_accent(app, icon, color);
    pen = CreatePen(PS_SOLID, penWidth, color);
    accentPen = CreatePen(PS_SOLID, penWidth, accent);
    accentBrush = CreateSolidBrush(accent);
    if (pen == NULL || accentPen == NULL || accentBrush == NULL) {
        if (pen != NULL) DeleteObject(pen);
        if (accentPen != NULL) DeleteObject(accentPen);
        if (accentBrush != NULL) DeleteObject(accentBrush);
        return FALSE;
    }
    previousPen = SelectObject(dc, pen);
    previousBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    previousText = SetTextColor(dc, accent);
    previousBackground = SetBkMode(dc, TRANSPARENT);

    switch (icon) {
    case RIBBON_VIEW_ICON_READ_MODE:
    case RIBBON_VIEW_ICON_IMMERSIVE_READER:
        ribbon_icon_line(dc, rect, 8, 17, 45, 24);
        ribbon_icon_line(dc, rect, 45, 24, 45, 91);
        ribbon_icon_line(dc, rect, 45, 91, 8, 82);
        ribbon_icon_line(dc, rect, 8, 82, 8, 17);
        ribbon_icon_line(dc, rect, 92, 17, 55, 24);
        ribbon_icon_line(dc, rect, 55, 24, 55, 91);
        ribbon_icon_line(dc, rect, 55, 91, 92, 82);
        ribbon_icon_line(dc, rect, 92, 82, 92, 17);
        if (icon == RIBBON_VIEW_ICON_IMMERSIVE_READER) {
            SelectObject(dc, accentPen);
            ribbon_icon_line(dc, rect, 67, 57, 77, 57);
            ribbon_icon_line(dc, rect, 77, 57, 87, 47);
            ribbon_icon_line(dc, rect, 77, 57, 87, 67);
        }
        break;
    case RIBBON_VIEW_ICON_PRINT_LAYOUT:
        ribbon_icon_rectangle(dc, rect, 19, 7, 81, 93);
        for (index = 0; index < 4; ++index) {
            int y = 27 + (int)index * 14;
            ribbon_icon_line(dc, rect, 29, y, 71, y);
        }
        break;
    case RIBBON_VIEW_ICON_WEB_LAYOUT:
        ribbon_icon_rectangle(dc, rect, 8, 12, 75, 78);
        for (index = 0; index < 3; ++index) {
            int y = 28 + (int)index * 14;
            ribbon_icon_line(dc, rect, 18, y, 61, y);
        }
        SelectObject(dc, accentPen);
        ribbon_icon_ellipse(dc, rect, 55, 50, 96, 94);
        ribbon_icon_line(dc, rect, 56, 72, 95, 72);
        ribbon_icon_line(dc, rect, 75, 51, 75, 93);
        break;
    case RIBBON_VIEW_ICON_OUTLINE:
    case RIBBON_VIEW_ICON_DRAFT:
        for (index = 0; index < 4; ++index) {
            int y = 16 + (int)index * 22;
            int left = icon == RIBBON_VIEW_ICON_OUTLINE &&
                               (index & 1u) != 0
                           ? 28 : 17;
            if (icon == RIBBON_VIEW_ICON_OUTLINE) {
                ribbon_icon_rectangle(dc, rect, left, y - 4,
                                      left + 8, y + 4);
                left += 15;
            }
            ribbon_icon_line(dc, rect, left, y, 87, y);
        }
        break;
    case RIBBON_VIEW_ICON_FOCUS:
        ribbon_icon_line(dc, rect, 8, 34, 8, 8);
        ribbon_icon_line(dc, rect, 8, 8, 34, 8);
        ribbon_icon_line(dc, rect, 66, 8, 92, 8);
        ribbon_icon_line(dc, rect, 92, 8, 92, 34);
        ribbon_icon_line(dc, rect, 8, 66, 8, 92);
        ribbon_icon_line(dc, rect, 8, 92, 34, 92);
        ribbon_icon_line(dc, rect, 66, 92, 92, 92);
        ribbon_icon_line(dc, rect, 92, 92, 92, 66);
        break;
    case RIBBON_VIEW_ICON_SWITCH_MODES: {
        static const int rays[8][4] = {
            {50, 4, 50, 20}, {50, 80, 50, 96},
            {4, 50, 20, 50}, {80, 50, 96, 50},
            {17, 17, 28, 28}, {72, 72, 83, 83},
            {17, 83, 28, 72}, {72, 28, 83, 17}
        };
        SelectObject(dc, accentPen);
        ribbon_icon_ellipse(dc, rect, 29, 29, 71, 71);
        for (index = 0; index < ARRAYSIZE(rays); ++index) {
            ribbon_icon_line(dc, rect, rays[index][0], rays[index][1],
                             rays[index][2], rays[index][3]);
        }
        break;
    }
    case RIBBON_VIEW_ICON_VERTICAL:
        ribbon_icon_rectangle(dc, rect, 31, 8, 82, 45);
        ribbon_icon_rectangle(dc, rect, 31, 55, 82, 92);
        SelectObject(dc, accentPen);
        ribbon_icon_line(dc, rect, 17, 14, 17, 86);
        ribbon_icon_line(dc, rect, 10, 22, 17, 14);
        ribbon_icon_line(dc, rect, 24, 22, 17, 14);
        ribbon_icon_line(dc, rect, 10, 78, 17, 86);
        ribbon_icon_line(dc, rect, 24, 78, 17, 86);
        break;
    case RIBBON_VIEW_ICON_SIDE_TO_SIDE:
        ribbon_icon_rectangle(dc, rect, 8, 25, 44, 78);
        ribbon_icon_rectangle(dc, rect, 56, 25, 92, 78);
        SelectObject(dc, accentPen);
        ribbon_icon_line(dc, rect, 16, 88, 84, 88);
        ribbon_icon_line(dc, rect, 16, 88, 25, 80);
        ribbon_icon_line(dc, rect, 16, 88, 25, 96);
        ribbon_icon_line(dc, rect, 84, 88, 75, 80);
        ribbon_icon_line(dc, rect, 84, 88, 75, 96);
        break;
    case RIBBON_VIEW_ICON_RULER:
        ribbon_icon_rectangle(dc, rect, 7, 28, 93, 72);
        for (index = 0; index < 7; ++index) {
            int x = 16 + (int)index * 12;
            ribbon_icon_line(dc, rect, x, 28, x,
                             index % 2 == 0 ? 52 : 44);
        }
        break;
    case RIBBON_VIEW_ICON_GRIDLINES:
        ribbon_icon_rectangle(dc, rect, 10, 10, 90, 90);
        ribbon_icon_line(dc, rect, 37, 10, 37, 90);
        ribbon_icon_line(dc, rect, 64, 10, 64, 90);
        ribbon_icon_line(dc, rect, 10, 37, 90, 37);
        ribbon_icon_line(dc, rect, 10, 64, 90, 64);
        break;
    case RIBBON_VIEW_ICON_NAVIGATION_PANE:
        ribbon_icon_rectangle(dc, rect, 9, 10, 91, 90);
        ribbon_icon_line(dc, rect, 34, 10, 34, 90);
        ribbon_icon_line(dc, rect, 14, 27, 29, 27);
        ribbon_icon_line(dc, rect, 14, 43, 29, 43);
        ribbon_icon_line(dc, rect, 14, 59, 29, 59);
        SelectObject(dc, accentPen);
        ribbon_icon_ellipse(dc, rect, 49, 32, 75, 59);
        ribbon_icon_line(dc, rect, 69, 54, 85, 72);
        break;
    case RIBBON_VIEW_ICON_ZOOM:
        ribbon_icon_ellipse(dc, rect, 10, 8, 70, 69);
        ribbon_icon_line(dc, rect, 61, 61, 93, 93);
        break;
    case RIBBON_VIEW_ICON_100_PERCENT: {
        RECT text = *rect;
        ribbon_icon_rectangle(dc, rect, 12, 7, 78, 92);
        text.left = ribbon_icon_coordinate(rect->left, width, 18);
        text.right = ribbon_icon_coordinate(rect->left, width, 94);
        text.top = ribbon_icon_coordinate(rect->top, height, 51);
        DrawTextW(dc, L"100", -1, &text,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        break;
    }
    case RIBBON_VIEW_ICON_ONE_PAGE:
        ribbon_icon_rectangle(dc, rect, 24, 7, 76, 93);
        break;
    case RIBBON_VIEW_ICON_MULTIPLE_PAGES:
        ribbon_icon_rectangle(dc, rect, 7, 17, 45, 82);
        ribbon_icon_rectangle(dc, rect, 55, 17, 93, 82);
        break;
    case RIBBON_VIEW_ICON_PAGE_WIDTH:
        ribbon_icon_rectangle(dc, rect, 22, 8, 78, 92);
        SelectObject(dc, accentPen);
        ribbon_icon_line(dc, rect, 6, 84, 94, 84);
        ribbon_icon_line(dc, rect, 6, 84, 17, 75);
        ribbon_icon_line(dc, rect, 6, 84, 17, 93);
        ribbon_icon_line(dc, rect, 94, 84, 83, 75);
        ribbon_icon_line(dc, rect, 94, 84, 83, 93);
        break;
    case RIBBON_VIEW_ICON_NEW_WINDOW:
        ribbon_icon_rectangle(dc, rect, 12, 20, 88, 88);
        SelectObject(dc, accentPen);
        ribbon_icon_line(dc, rect, 29, 5, 29, 39);
        ribbon_icon_line(dc, rect, 12, 22, 46, 22);
        break;
    case RIBBON_VIEW_ICON_ARRANGE_ALL:
        ribbon_icon_rectangle(dc, rect, 8, 12, 82, 54);
        ribbon_icon_rectangle(dc, rect, 18, 27, 92, 70);
        ribbon_icon_rectangle(dc, rect, 8, 45, 82, 88);
        break;
    case RIBBON_VIEW_ICON_SPLIT:
        ribbon_icon_rectangle(dc, rect, 10, 10, 90, 90);
        SelectObject(dc, accentPen);
        ribbon_icon_line(dc, rect, 10, 50, 90, 50);
        break;
    case RIBBON_VIEW_ICON_SIDE_BY_SIDE:
        ribbon_icon_rectangle(dc, rect, 7, 13, 45, 86);
        ribbon_icon_rectangle(dc, rect, 55, 13, 93, 86);
        break;
    case RIBBON_VIEW_ICON_SYNCHRONOUS_SCROLLING:
        ribbon_icon_rectangle(dc, rect, 7, 17, 42, 82);
        ribbon_icon_rectangle(dc, rect, 58, 17, 93, 82);
        SelectObject(dc, accentPen);
        ribbon_icon_line(dc, rect, 50, 17, 50, 83);
        ribbon_icon_line(dc, rect, 44, 25, 50, 17);
        ribbon_icon_line(dc, rect, 56, 25, 50, 17);
        ribbon_icon_line(dc, rect, 44, 75, 50, 83);
        ribbon_icon_line(dc, rect, 56, 75, 50, 83);
        break;
    case RIBBON_VIEW_ICON_RESET_WINDOW_POSITION: {
        POINT box1 = ribbon_icon_point(rect, 8, 8);
        POINT box2 = ribbon_icon_point(rect, 92, 92);
        POINT start = ribbon_icon_point(rect, 75, 84);
        POINT end = ribbon_icon_point(rect, 78, 17);
        POINT arrow[3];
        Arc(dc, box1.x, box1.y, box2.x, box2.y,
            start.x, start.y, end.x, end.y);
        arrow[0] = ribbon_icon_point(rect, 67, 8);
        arrow[1] = ribbon_icon_point(rect, 93, 14);
        arrow[2] = ribbon_icon_point(rect, 78, 35);
        SelectObject(dc, accentBrush);
        Polygon(dc, arrow, ARRAYSIZE(arrow));
        break;
    }
    case RIBBON_VIEW_ICON_SWITCH_WINDOWS:
        ribbon_icon_rectangle(dc, rect, 8, 10, 73, 66);
        ribbon_icon_rectangle(dc, rect, 27, 33, 92, 89);
        SelectObject(dc, accentPen);
        ribbon_icon_line(dc, rect, 13, 81, 47, 81);
        ribbon_icon_line(dc, rect, 13, 81, 22, 73);
        ribbon_icon_line(dc, rect, 13, 81, 22, 89);
        break;
    case RIBBON_VIEW_ICON_MACROS:
        ribbon_icon_rectangle(dc, rect, 11, 8, 82, 92);
        for (index = 0; index < 4; ++index) {
            int y = 25 + (int)index * 15;
            ribbon_icon_line(dc, rect, 22, y, 67, y);
        }
        SelectObject(dc, accentPen);
        ribbon_icon_rectangle(dc, rect, 64, 58, 94, 88);
        break;
    case RIBBON_VIEW_ICON_PROPERTIES: {
        RECT symbol = *rect;
        ribbon_icon_rectangle(dc, rect, 13, 7, 76, 92);
        ribbon_icon_line(dc, rect, 25, 25, 64, 25);
        ribbon_icon_line(dc, rect, 25, 40, 64, 40);
        SelectObject(dc, accentBrush);
        ribbon_icon_rectangle(dc, rect, 57, 55, 94, 92);
        SetTextColor(dc, RGB(255, 255, 255));
        symbol.left = ribbon_icon_coordinate(rect->left, width, 57);
        symbol.right = ribbon_icon_coordinate(rect->left, width, 94);
        symbol.top = ribbon_icon_coordinate(rect->top, height, 55);
        symbol.bottom = ribbon_icon_coordinate(rect->top, height, 92);
        DrawTextW(dc, L"S", -1, &symbol,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        break;
    }
    default:
        drawn = FALSE;
        break;
    }

    SetTextColor(dc, previousText);
    SetBkMode(dc, previousBackground);
    SelectObject(dc, previousBrush);
    SelectObject(dc, previousPen);
    DeleteObject(accentBrush);
    DeleteObject(accentPen);
    DeleteObject(pen);
    return drawn;
}

static int ribbon_design_style_from_command(UINT command)
{
    size_t index;

    for (index = 0; index < ARRAYSIZE(designStyleCommands); ++index) {
        if (designStyleCommands[index] == command) {
            return (int)index;
        }
    }
    return -1;
}

static COLORREF ribbon_design_accent_for_scheme(
    const AppState *app, COLORREF fallback)
{
    static const COLORREF accents[DESIGN_COLOR_SCHEME_COUNT] = {
        RGB(46, 116, 181), RGB(38, 126, 191), RGB(63, 139, 83),
        RGB(224, 112, 42), RGB(192, 57, 62), RGB(128, 83, 167)
    };
    int scheme;

    if (app == NULL) {
        return fallback;
    }
    scheme = app->designColorScheme;
    if (scheme < 0 || scheme >= DESIGN_COLOR_SCHEME_COUNT) {
        return fallback;
    }
    return accents[scheme];
}

static void ribbon_draw_design_preview(
    AppState *app, HDC dc, int styleSet, const RECT *bounds,
    BOOL selected, BOOL hot)
{
    WordcraftDesignStyleSetInfo info;
    RECT tile;
    RECT title;
    RECT heading;
    RECT body;
    COLORREF accent;
    COLORREF border;
    HBRUSH backgroundBrush;
    HPEN borderPen;
    HGDIOBJ previousBrush;
    HGDIOBJ previousPen;
    HFONT titleFont;
    HFONT headingFont;
    HGDIOBJ previousFont = NULL;
    int previousBackground;
    COLORREF previousText;
    int width;
    int height;
    int line;
    UINT titleFlags;

    if (dc == NULL || bounds == NULL || bounds->right <= bounds->left ||
        bounds->bottom <= bounds->top ||
        !format_get_design_style_set_info(styleSet, &info)) {
        return;
    }
    tile = *bounds;
    width = tile.right - tile.left;
    height = tile.bottom - tile.top;
    accent = ribbon_design_accent_for_scheme(app, info.accent);
    border = selected
                 ? accent
                 : (hot ? RGB(125, 125, 125) : RGB(185, 185, 185));
    backgroundBrush = CreateSolidBrush(RGB(250, 250, 248));
    borderPen = CreatePen(PS_SOLID, selected ? max(1, width / 45) : 1,
                          border);
    if (backgroundBrush == NULL || borderPen == NULL) {
        if (backgroundBrush != NULL) DeleteObject(backgroundBrush);
        if (borderPen != NULL) DeleteObject(borderPen);
        return;
    }
    previousBrush = SelectObject(dc, backgroundBrush);
    previousPen = SelectObject(dc, borderPen);
    Rectangle(dc, tile.left, tile.top, tile.right, tile.bottom);

    InflateRect(&tile, -max(2, width / 24), -max(2, height / 22));
    title = tile;
    title.bottom = title.top + max(8, height * 29 / 100);
    heading = tile;
    heading.top = title.bottom + max(1, height / 30);
    heading.bottom = heading.top + max(6, height * 18 / 100);
    body = tile;
    body.top = heading.bottom + max(2, height / 25);

    if (info.shadedPreview) {
        HBRUSH shade = CreateSolidBrush(
            ribbon_blend_color(RGB(250, 250, 248), accent, 13));
        if (shade != NULL) {
            FillRect(dc, &title, shade);
            DeleteObject(shade);
        }
    }
    titleFont = CreateFontW(
        -max(7, height * 19 / 100), 0, 0, 0, FW_SEMIBOLD, FALSE,
        FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
        info.headingFont);
    headingFont = CreateFontW(
        -max(6, height * 12 / 100), 0, 0, 0, FW_NORMAL, FALSE,
        FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
        info.bodyFont);
    previousBackground = SetBkMode(dc, TRANSPARENT);
    previousText = SetTextColor(dc, RGB(34, 34, 34));
    if (titleFont != NULL) {
        previousFont = SelectObject(dc, titleFont);
    }
    titleFlags = DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                 DT_NOPREFIX |
                 (info.centerTitle ? DT_CENTER : DT_LEFT);
    DrawTextW(dc,
              info.titleUppercasePreview ? L"TITLE" : L"Title",
              -1, &title, titleFlags);
    if (previousFont != NULL) {
        SelectObject(dc, previousFont);
        previousFont = NULL;
    }
    if (headingFont != NULL) {
        previousFont = SelectObject(dc, headingFont);
    }
    SetTextColor(dc, accent);
    DrawTextW(dc, L"Heading 1", -1, &heading,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                  DT_END_ELLIPSIS | DT_NOPREFIX);
    if (info.headingRulePreview) {
        HPEN rule = CreatePen(PS_SOLID, 1, accent);
        if (rule != NULL) {
            HGDIOBJ oldRule = SelectObject(dc, rule);
            MoveToEx(dc, heading.left, heading.bottom - 1, NULL);
            LineTo(dc, heading.right, heading.bottom - 1);
            SelectObject(dc, oldRule);
            DeleteObject(rule);
        }
    }
    SetTextColor(dc, RGB(62, 62, 62));
    for (line = 0; line < 4; ++line) {
        int y = body.top + line * max(2, (body.bottom - body.top) / 4);
        int right = body.right -
                    (line == 3 ? width / 5 :
                     line == 1 ? width / 12 : 0);
        MoveToEx(dc, body.left, y, NULL);
        LineTo(dc, max(body.left + 1, right), y);
    }

    if (previousFont != NULL) {
        SelectObject(dc, previousFont);
    }
    SetTextColor(dc, previousText);
    SetBkMode(dc, previousBackground);
    SelectObject(dc, previousPen);
    SelectObject(dc, previousBrush);
    if (titleFont != NULL) DeleteObject(titleFont);
    if (headingFont != NULL) DeleteObject(headingFont);
    DeleteObject(borderPen);
    DeleteObject(backgroundBrush);
}

static BOOL ribbon_draw_design_glyph(
    AppState *app, HDC dc, RibbonDesignIcon icon,
    const RECT *rect, COLORREF color)
{
    COLORREF accent;
    HPEN pen;
    HPEN accentPen;
    HBRUSH accentBrush;
    HBRUSH orangeBrush;
    HBRUSH blueBrush;
    HGDIOBJ previousPen;
    HGDIOBJ previousBrush;
    COLORREF previousText;
    int previousBackground;
    int width;
    int height;
    int penWidth;
    BOOL drawn = TRUE;

    if (dc == NULL || rect == NULL || rect->right <= rect->left ||
        rect->bottom <= rect->top) {
        return FALSE;
    }
    width = rect->right - rect->left;
    height = rect->bottom - rect->top;
    penWidth = max(1, min(width, height) / 13);
    accent = ribbon_design_accent_for_scheme(app, RGB(38, 126, 191));
    pen = CreatePen(PS_SOLID, penWidth, color);
    accentPen = CreatePen(PS_SOLID, penWidth, accent);
    accentBrush = CreateSolidBrush(accent);
    orangeBrush = CreateSolidBrush(RGB(224, 112, 42));
    blueBrush = CreateSolidBrush(RGB(54, 117, 153));
    if (pen == NULL || accentPen == NULL || accentBrush == NULL ||
        orangeBrush == NULL || blueBrush == NULL) {
        if (pen != NULL) DeleteObject(pen);
        if (accentPen != NULL) DeleteObject(accentPen);
        if (accentBrush != NULL) DeleteObject(accentBrush);
        if (orangeBrush != NULL) DeleteObject(orangeBrush);
        if (blueBrush != NULL) DeleteObject(blueBrush);
        return FALSE;
    }
    previousPen = SelectObject(dc, pen);
    previousBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    previousText = SetTextColor(dc, color);
    previousBackground = SetBkMode(dc, TRANSPARENT);

    switch (icon) {
    case RIBBON_DESIGN_ICON_THEMES: {
        RECT text = *rect;
        ribbon_icon_rectangle(dc, rect, 14, 4, 86, 88);
        text.left = ribbon_icon_coordinate(rect->left, width, 20);
        text.right = ribbon_icon_coordinate(rect->left, width, 80);
        text.top = ribbon_icon_coordinate(rect->top, height, 11);
        text.bottom = ribbon_icon_coordinate(rect->top, height, 68);
        DrawTextW(dc, L"Aa", -1, &text,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, blueBrush);
        ribbon_icon_rectangle(dc, rect, 20, 72, 37, 86);
        SelectObject(dc, orangeBrush);
        ribbon_icon_rectangle(dc, rect, 42, 72, 59, 86);
        SelectObject(dc, accentBrush);
        ribbon_icon_rectangle(dc, rect, 64, 72, 80, 86);
        break;
    }
    case RIBBON_DESIGN_ICON_MORE:
        ribbon_icon_line(dc, rect, 28, 43, 50, 65);
        ribbon_icon_line(dc, rect, 50, 65, 72, 43);
        break;
    case RIBBON_DESIGN_ICON_COLORS:
        SelectObject(dc, blueBrush);
        ribbon_icon_rectangle(dc, rect, 8, 8, 48, 48);
        SelectObject(dc, accentBrush);
        ribbon_icon_rectangle(dc, rect, 52, 8, 92, 48);
        SelectObject(dc, orangeBrush);
        ribbon_icon_rectangle(dc, rect, 52, 52, 92, 92);
        SelectObject(dc, GetStockObject(GRAY_BRUSH));
        ribbon_icon_rectangle(dc, rect, 8, 52, 48, 92);
        break;
    case RIBBON_DESIGN_ICON_FONTS: {
        RECT text = *rect;
        DrawTextW(dc, L"A", -1, &text,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        break;
    }
    case RIBBON_DESIGN_ICON_PARAGRAPH_SPACING:
        for (int row = 0; row < 4; ++row) {
            int y = 18 + row * 20;
            ribbon_icon_line(dc, rect, 27, y, 91, y);
        }
        SelectObject(dc, accentPen);
        ribbon_icon_line(dc, rect, 13, 17, 13, 82);
        ribbon_icon_line(dc, rect, 7, 25, 13, 17);
        ribbon_icon_line(dc, rect, 19, 25, 13, 17);
        ribbon_icon_line(dc, rect, 7, 74, 13, 82);
        ribbon_icon_line(dc, rect, 19, 74, 13, 82);
        break;
    case RIBBON_DESIGN_ICON_EFFECTS:
        SelectObject(dc, blueBrush);
        ribbon_icon_ellipse(dc, rect, 14, 14, 86, 86);
        break;
    case RIBBON_DESIGN_ICON_SET_AS_DEFAULT:
        SelectObject(dc, accentPen);
        ribbon_icon_ellipse(dc, rect, 8, 8, 92, 92);
        ribbon_icon_line(dc, rect, 25, 51, 43, 68);
        ribbon_icon_line(dc, rect, 43, 68, 76, 31);
        break;
    case RIBBON_DESIGN_ICON_WATERMARK:
        ribbon_icon_rectangle(dc, rect, 19, 5, 81, 95);
        SelectObject(dc, accentPen);
        ribbon_icon_line(dc, rect, 28, 22, 72, 78);
        break;
    case RIBBON_DESIGN_ICON_PAGE_COLOR:
        ribbon_icon_rectangle(dc, rect, 24, 7, 78, 86);
        ribbon_icon_rectangle(dc, rect, 15, 17, 69, 96);
        SelectObject(dc, accentBrush);
        ribbon_icon_rectangle(dc, rect, 50, 55, 91, 91);
        break;
    case RIBBON_DESIGN_ICON_PAGE_BORDERS:
        ribbon_icon_rectangle(dc, rect, 18, 5, 82, 95);
        SelectObject(dc, accentPen);
        ribbon_icon_rectangle(dc, rect, 27, 14, 73, 86);
        break;
    default:
        drawn = FALSE;
        break;
    }

    SetTextColor(dc, previousText);
    SetBkMode(dc, previousBackground);
    SelectObject(dc, previousBrush);
    SelectObject(dc, previousPen);
    DeleteObject(blueBrush);
    DeleteObject(orangeBrush);
    DeleteObject(accentBrush);
    DeleteObject(accentPen);
    DeleteObject(pen);
    return drawn;
}

static BOOL ribbon_draw_insert_glyph(HDC dc, RibbonInsertIcon icon,
                                     const RECT *rect, COLORREF color)
{
    HPEN pen;
    HBRUSH solidBrush;
    HGDIOBJ previousPen;
    HGDIOBJ previousBrush;
    COLORREF previousText;
    int previousBackground;
    int width;
    int height;
    int penWidth;
    BOOL drawn = TRUE;

    if (dc == NULL || rect == NULL || rect->right <= rect->left ||
        rect->bottom <= rect->top) {
        return FALSE;
    }
    width = rect->right - rect->left;
    height = rect->bottom - rect->top;
    penWidth = max(1, min(width, height) / 12);
    pen = CreatePen(PS_SOLID, penWidth, color);
    solidBrush = CreateSolidBrush(color);
    if (pen == NULL || solidBrush == NULL) {
        if (pen != NULL) {
            DeleteObject(pen);
        }
        if (solidBrush != NULL) {
            DeleteObject(solidBrush);
        }
        return FALSE;
    }
    previousPen = SelectObject(dc, pen);
    previousBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    previousText = SetTextColor(dc, color);
    previousBackground = SetBkMode(dc, TRANSPARENT);

    switch (icon) {
    case RIBBON_INSERT_ICON_COVER_PAGE:
        ribbon_icon_rectangle(dc, rect, 22, 4, 78, 96);
        SelectObject(dc, solidBrush);
        ribbon_icon_rectangle(dc, rect, 30, 22, 70, 37);
        SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        ribbon_icon_line(dc, rect, 32, 52, 68, 52);
        ribbon_icon_line(dc, rect, 32, 65, 60, 65);
        break;
    case RIBBON_INSERT_ICON_BLANK_PAGE:
        ribbon_icon_rectangle(dc, rect, 22, 4, 78, 96);
        ribbon_icon_line(dc, rect, 62, 4, 78, 20);
        ribbon_icon_line(dc, rect, 62, 4, 62, 20);
        ribbon_icon_line(dc, rect, 62, 20, 78, 20);
        break;
    case RIBBON_INSERT_ICON_PAGE_BREAK:
        ribbon_icon_rectangle(dc, rect, 22, 3, 78, 41);
        ribbon_icon_rectangle(dc, rect, 22, 59, 78, 97);
        ribbon_icon_line(dc, rect, 8, 50, 22, 50);
        ribbon_icon_line(dc, rect, 30, 50, 43, 50);
        ribbon_icon_line(dc, rect, 51, 50, 64, 50);
        ribbon_icon_line(dc, rect, 72, 50, 92, 50);
        break;
    case RIBBON_INSERT_ICON_TABLE:
        ribbon_icon_rectangle(dc, rect, 7, 12, 93, 88);
        ribbon_icon_line(dc, rect, 36, 12, 36, 88);
        ribbon_icon_line(dc, rect, 65, 12, 65, 88);
        ribbon_icon_line(dc, rect, 7, 38, 93, 38);
        ribbon_icon_line(dc, rect, 7, 63, 93, 63);
        break;
    case RIBBON_INSERT_ICON_PICTURES: {
        POINT mountains[4];
        ribbon_icon_rectangle(dc, rect, 5, 10, 95, 90);
        ribbon_icon_ellipse(dc, rect, 67, 22, 82, 38);
        mountains[0] = ribbon_icon_point(rect, 12, 80);
        mountains[1] = ribbon_icon_point(rect, 38, 48);
        mountains[2] = ribbon_icon_point(rect, 55, 68);
        mountains[3] = ribbon_icon_point(rect, 72, 45);
        Polyline(dc, mountains, ARRAYSIZE(mountains));
        ribbon_icon_line(dc, rect, 72, 45, 90, 80);
        break;
    }
    case RIBBON_INSERT_ICON_SHAPES:
        ribbon_icon_rectangle(dc, rect, 8, 10, 57, 58);
        ribbon_icon_ellipse(dc, rect, 42, 38, 92, 88);
        break;
    case RIBBON_INSERT_ICON_ICONS: {
        POINT star[11];
        static const BYTE x[] = {50, 60, 91, 67, 76, 50,
                                 24, 33, 9, 40, 50};
        static const BYTE y[] = {6, 37, 37, 56, 88, 69,
                                 88, 56, 37, 37, 6};
        size_t index;
        for (index = 0; index < ARRAYSIZE(star); ++index) {
            star[index] = ribbon_icon_point(rect, x[index], y[index]);
        }
        Polygon(dc, star, ARRAYSIZE(star));
        break;
    }
    case RIBBON_INSERT_ICON_3D_MODELS: {
        POINT topFace[5];
        topFace[0] = ribbon_icon_point(rect, 50, 5);
        topFace[1] = ribbon_icon_point(rect, 90, 28);
        topFace[2] = ribbon_icon_point(rect, 50, 50);
        topFace[3] = ribbon_icon_point(rect, 10, 28);
        topFace[4] = topFace[0];
        Polyline(dc, topFace, ARRAYSIZE(topFace));
        ribbon_icon_line(dc, rect, 10, 28, 10, 72);
        ribbon_icon_line(dc, rect, 10, 72, 50, 95);
        ribbon_icon_line(dc, rect, 50, 95, 90, 72);
        ribbon_icon_line(dc, rect, 90, 72, 90, 28);
        ribbon_icon_line(dc, rect, 50, 50, 50, 95);
        break;
    }
    case RIBBON_INSERT_ICON_SMARTART:
        ribbon_icon_rectangle(dc, rect, 5, 10, 43, 39);
        ribbon_icon_rectangle(dc, rect, 57, 10, 95, 39);
        ribbon_icon_rectangle(dc, rect, 31, 62, 69, 91);
        ribbon_icon_line(dc, rect, 24, 39, 42, 62);
        ribbon_icon_line(dc, rect, 76, 39, 58, 62);
        break;
    case RIBBON_INSERT_ICON_CHART:
        ribbon_icon_line(dc, rect, 8, 8, 8, 91);
        ribbon_icon_line(dc, rect, 8, 91, 94, 91);
        SelectObject(dc, solidBrush);
        ribbon_icon_rectangle(dc, rect, 20, 59, 37, 90);
        ribbon_icon_rectangle(dc, rect, 44, 36, 61, 90);
        ribbon_icon_rectangle(dc, rect, 68, 14, 85, 90);
        SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        break;
    case RIBBON_INSERT_ICON_SCREENSHOT:
        ribbon_icon_rectangle(dc, rect, 5, 12, 95, 84);
        ribbon_icon_line(dc, rect, 5, 31, 95, 31);
        ribbon_icon_ellipse(dc, rect, 13, 19, 20, 26);
        ribbon_icon_ellipse(dc, rect, 25, 19, 32, 26);
        ribbon_icon_line(dc, rect, 38, 67, 53, 49);
        ribbon_icon_line(dc, rect, 53, 49, 66, 63);
        ribbon_icon_line(dc, rect, 66, 63, 83, 42);
        break;
    case RIBBON_INSERT_ICON_ONLINE_VIDEO: {
        POINT play[3];
        ribbon_icon_rectangle(dc, rect, 5, 15, 95, 85);
        play[0] = ribbon_icon_point(rect, 42, 34);
        play[1] = ribbon_icon_point(rect, 42, 67);
        play[2] = ribbon_icon_point(rect, 70, 50);
        SelectObject(dc, solidBrush);
        Polygon(dc, play, ARRAYSIZE(play));
        SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        break;
    }
    case RIBBON_INSERT_ICON_LINK: {
        POINT first = ribbon_icon_point(rect, 8, 28);
        POINT second = ribbon_icon_point(rect, 62, 58);
        POINT third = ribbon_icon_point(rect, 38, 14);
        POINT fourth = ribbon_icon_point(rect, 92, 44);
        RoundRect(dc, first.x, first.y, second.x, second.y,
                  max(2, width / 3), max(2, height / 3));
        RoundRect(dc, third.x, third.y, fourth.x, fourth.y,
                  max(2, width / 3), max(2, height / 3));
        ribbon_icon_line(dc, rect, 37, 48, 64, 35);
        break;
    }
    case RIBBON_INSERT_ICON_BOOKMARK: {
        POINT bookmark[5];
        bookmark[0] = ribbon_icon_point(rect, 28, 6);
        bookmark[1] = ribbon_icon_point(rect, 72, 6);
        bookmark[2] = ribbon_icon_point(rect, 72, 94);
        bookmark[3] = ribbon_icon_point(rect, 50, 72);
        bookmark[4] = ribbon_icon_point(rect, 28, 94);
        Polygon(dc, bookmark, ARRAYSIZE(bookmark));
        break;
    }
    case RIBBON_INSERT_ICON_CROSS_REFERENCE:
        ribbon_icon_rectangle(dc, rect, 6, 8, 56, 72);
        ribbon_icon_rectangle(dc, rect, 44, 28, 94, 92);
        ribbon_icon_line(dc, rect, 22, 83, 63, 83);
        ribbon_icon_line(dc, rect, 22, 83, 34, 72);
        ribbon_icon_line(dc, rect, 22, 83, 34, 94);
        break;
    case RIBBON_INSERT_ICON_COMMENT: {
        POINT bubble[7];
        bubble[0] = ribbon_icon_point(rect, 7, 12);
        bubble[1] = ribbon_icon_point(rect, 93, 12);
        bubble[2] = ribbon_icon_point(rect, 93, 72);
        bubble[3] = ribbon_icon_point(rect, 54, 72);
        bubble[4] = ribbon_icon_point(rect, 34, 92);
        bubble[5] = ribbon_icon_point(rect, 36, 72);
        bubble[6] = ribbon_icon_point(rect, 7, 72);
        Polygon(dc, bubble, ARRAYSIZE(bubble));
        ribbon_icon_line(dc, rect, 50, 27, 50, 58);
        ribbon_icon_line(dc, rect, 34, 43, 66, 43);
        break;
    }
    case RIBBON_INSERT_ICON_HEADER:
        ribbon_icon_rectangle(dc, rect, 21, 5, 79, 95);
        SelectObject(dc, solidBrush);
        ribbon_icon_rectangle(dc, rect, 28, 15, 72, 31);
        SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        break;
    case RIBBON_INSERT_ICON_FOOTER:
        ribbon_icon_rectangle(dc, rect, 21, 5, 79, 95);
        SelectObject(dc, solidBrush);
        ribbon_icon_rectangle(dc, rect, 28, 69, 72, 85);
        SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        break;
    case RIBBON_INSERT_ICON_PAGE_NUMBER: {
        RECT numberRect = *rect;
        ribbon_icon_rectangle(dc, rect, 21, 5, 79, 95);
        numberRect.left = ribbon_icon_coordinate(
            rect->left, rect->right - rect->left, 30);
        numberRect.right = ribbon_icon_coordinate(
            rect->left, rect->right - rect->left, 70);
        numberRect.top = ribbon_icon_coordinate(
            rect->top, rect->bottom - rect->top, 62);
        numberRect.bottom = ribbon_icon_coordinate(
            rect->top, rect->bottom - rect->top, 88);
        DrawTextW(dc, L"1", 1, &numberRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        break;
    }
    case RIBBON_INSERT_ICON_TEXT_BOX: {
        RECT textRect = *rect;
        ribbon_icon_rectangle(dc, rect, 5, 15, 95, 85);
        InflateRect(&textRect, -max(1, width / 5), -max(1, height / 5));
        DrawTextW(dc, L"T", 1, &textRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        break;
    }
    case RIBBON_INSERT_ICON_QUICK_PARTS:
        ribbon_icon_rectangle(dc, rect, 6, 8, 46, 45);
        ribbon_icon_rectangle(dc, rect, 54, 8, 94, 45);
        ribbon_icon_rectangle(dc, rect, 6, 55, 46, 92);
        ribbon_icon_rectangle(dc, rect, 54, 55, 94, 92);
        break;
    case RIBBON_INSERT_ICON_WORDART: {
        RECT textRect = *rect;
        DrawTextW(dc, L"A", 1, &textRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        ribbon_icon_line(dc, rect, 18, 84, 82, 72);
        break;
    }
    case RIBBON_INSERT_ICON_DROP_CAP: {
        RECT letter = *rect;
        letter.right = ribbon_icon_coordinate(
            rect->left, rect->right - rect->left, 55);
        DrawTextW(dc, L"A", 1, &letter,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        ribbon_icon_line(dc, rect, 56, 25, 94, 25);
        ribbon_icon_line(dc, rect, 56, 48, 94, 48);
        ribbon_icon_line(dc, rect, 56, 71, 94, 71);
        break;
    }
    case RIBBON_INSERT_ICON_SIGNATURE_LINE: {
        POINT curve[4];
        curve[0] = ribbon_icon_point(rect, 8, 63);
        curve[1] = ribbon_icon_point(rect, 27, 25);
        curve[2] = ribbon_icon_point(rect, 43, 88);
        curve[3] = ribbon_icon_point(rect, 72, 42);
        PolyBezier(dc, curve, ARRAYSIZE(curve));
        ribbon_icon_line(dc, rect, 6, 88, 94, 88);
        break;
    }
    case RIBBON_INSERT_ICON_DATETIME:
        ribbon_icon_rectangle(dc, rect, 5, 15, 70, 88);
        ribbon_icon_line(dc, rect, 5, 34, 70, 34);
        ribbon_icon_line(dc, rect, 22, 8, 22, 25);
        ribbon_icon_line(dc, rect, 52, 8, 52, 25);
        ribbon_icon_ellipse(dc, rect, 55, 47, 96, 91);
        ribbon_icon_line(dc, rect, 75, 57, 75, 70);
        ribbon_icon_line(dc, rect, 75, 70, 86, 77);
        break;
    case RIBBON_INSERT_ICON_OBJECT:
        ribbon_icon_rectangle(dc, rect, 17, 5, 83, 95);
        ribbon_icon_rectangle(dc, rect, 28, 24, 72, 71);
        ribbon_icon_line(dc, rect, 28, 79, 61, 79);
        break;
    case RIBBON_INSERT_ICON_EQUATION: {
        RECT textRect = *rect;
        DrawTextW(dc, L"fx", 2, &textRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        break;
    }
    case RIBBON_INSERT_ICON_SYMBOL: {
        RECT textRect = *rect;
        DrawTextW(dc, L"\x03A9", 1, &textRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        break;
    }
    case RIBBON_INSERT_ICON_ESIGNATURE_FIELDS: {
        POINT shield[6];
        shield[0] = ribbon_icon_point(rect, 50, 5);
        shield[1] = ribbon_icon_point(rect, 87, 19);
        shield[2] = ribbon_icon_point(rect, 82, 66);
        shield[3] = ribbon_icon_point(rect, 50, 95);
        shield[4] = ribbon_icon_point(rect, 18, 66);
        shield[5] = ribbon_icon_point(rect, 13, 19);
        Polygon(dc, shield, ARRAYSIZE(shield));
        ribbon_icon_line(dc, rect, 28, 50, 44, 67);
        ribbon_icon_line(dc, rect, 44, 67, 73, 33);
        break;
    }
    default:
        drawn = FALSE;
        break;
    }

    SetTextColor(dc, previousText);
    SetBkMode(dc, previousBackground);
    SelectObject(dc, previousBrush);
    SelectObject(dc, previousPen);
    DeleteObject(solidBrush);
    DeleteObject(pen);
    return drawn;
}

BOOL ribbon_draw_insert_button_icon(AppState *app, HWND button, HDC dc,
                                    const RECT *bounds, COLORREF color,
                                    RECT *captionRect,
                                    UINT *captionFlags)
{
    RibbonContext *ribbon;
    InsertControlInfo *control;
    RECT iconRect;
    int availableWidth;
    int availableHeight;
    int iconSize;
    BOOL drawn;

    if (!ribbon_valid_context(app) || button == NULL || dc == NULL ||
        bounds == NULL || captionRect == NULL || captionFlags == NULL) {
        return FALSE;
    }
    ribbon = app->ribbon;
    control = ribbon_find_insert_control(ribbon, button);
    if (control == NULL) {
        return FALSE;
    }

    *captionRect = *bounds;
    availableWidth = max(1, bounds->right - bounds->left);
    availableHeight = max(1, bounds->bottom - bounds->top);
    if (ribbon->insertLayoutMode == RIBBON_LAYOUT_FULL) {
        int captionHeight = max(app_scale(app->mainWindow, 26),
                                availableHeight * 42 / 100);
        int iconAvailableHeight = max(
            1, availableHeight - captionHeight -
                   app_scale(app->mainWindow, 4));
        iconSize = min(app_scale(app->mainWindow, 28),
                       min(availableWidth - app_scale(app->mainWindow, 8),
                           iconAvailableHeight));
        iconSize = max(1, iconSize);
        iconRect.left = bounds->left + (availableWidth - iconSize) / 2;
        iconRect.right = iconRect.left + iconSize;
        iconRect.top = bounds->top + app_scale(app->mainWindow, 4);
        iconRect.bottom = iconRect.top + iconSize;
        captionRect->left += app_scale(app->mainWindow, 2);
        captionRect->right -= app_scale(app->mainWindow, 2);
        captionRect->top = iconRect.bottom + app_scale(app->mainWindow, 2);
        captionRect->bottom -= app_scale(app->mainWindow, 2);
        *captionFlags = DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS |
                        DT_NOPREFIX;
    } else {
        int maximum = ribbon->insertLayoutMode == RIBBON_LAYOUT_COMPACT
                          ? app_scale(app->mainWindow, 22)
                          : app_scale(app->mainWindow, 17);
        iconSize = min(maximum,
                       min(availableWidth - app_scale(app->mainWindow, 4),
                           availableHeight - app_scale(app->mainWindow, 4)));
        iconSize = max(1, iconSize);
        iconRect.left = bounds->left + (availableWidth - iconSize) / 2;
        iconRect.right = iconRect.left + iconSize;
        iconRect.top = bounds->top + (availableHeight - iconSize) / 2;
        iconRect.bottom = iconRect.top + iconSize;
        *captionFlags = 0;
    }

    drawn = ribbon_draw_insert_glyph(dc, control->icon, &iconRect, color);
    if (drawn) {
        control->iconPaintCount =
            control->iconPaintCount == UINT_MAX
                ? 1
                : control->iconPaintCount + 1;
    }
    return TRUE;
}

BOOL ribbon_draw_draw_button_icon(AppState *app, HWND button, HDC dc,
                                  const RECT *bounds, COLORREF color,
                                  RECT *captionRect,
                                  UINT *captionFlags)
{
    RibbonContext *ribbon;
    DrawControlInfo *control;
    RECT iconRect;
    int availableWidth;
    int availableHeight;
    int iconSize;
    BOOL iconOnly;
    BOOL drawn;

    if (!ribbon_valid_context(app) || button == NULL || dc == NULL ||
        bounds == NULL || captionRect == NULL || captionFlags == NULL) {
        return FALSE;
    }
    ribbon = app->ribbon;
    control = ribbon_find_draw_control(ribbon, button);
    if (control == NULL) {
        return FALSE;
    }

    *captionRect = *bounds;
    availableWidth = max(1, bounds->right - bounds->left);
    availableHeight = max(1, bounds->bottom - bounds->top);
    iconOnly = ribbon->drawLayoutMode != RIBBON_LAYOUT_FULL ||
               control->group == DRAW_GROUP_UNDO ||
               (control->group == DRAW_GROUP_DRAWING_TOOLS &&
                control->id != IDM_DRAW_ADD_PEN);
    if (!iconOnly) {
        int captionHeight = max(app_scale(app->mainWindow, 26),
                                availableHeight * 42 / 100);
        int iconAvailableHeight = max(
            1, availableHeight - captionHeight -
                   app_scale(app->mainWindow, 4));
        iconSize = min(app_scale(app->mainWindow, 28),
                       min(availableWidth - app_scale(app->mainWindow, 8),
                           iconAvailableHeight));
        iconSize = max(1, iconSize);
        iconRect.left = bounds->left + (availableWidth - iconSize) / 2;
        iconRect.right = iconRect.left + iconSize;
        iconRect.top = bounds->top + app_scale(app->mainWindow, 4);
        iconRect.bottom = iconRect.top + iconSize;
        captionRect->left += app_scale(app->mainWindow, 2);
        captionRect->right -= app_scale(app->mainWindow, 2);
        captionRect->top = iconRect.bottom + app_scale(app->mainWindow, 2);
        captionRect->bottom -= app_scale(app->mainWindow, 2);
        *captionFlags = DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS |
                        DT_NOPREFIX;
    } else {
        int maximum = ribbon->drawLayoutMode == RIBBON_LAYOUT_FULL
                          ? app_scale(app->mainWindow, 34)
                          : ribbon->drawLayoutMode == RIBBON_LAYOUT_COMPACT
                                ? app_scale(app->mainWindow, 22)
                                : app_scale(app->mainWindow, 17);
        iconSize = min(maximum,
                       min(availableWidth - app_scale(app->mainWindow, 4),
                           availableHeight - app_scale(app->mainWindow, 4)));
        iconSize = max(1, iconSize);
        iconRect.left = bounds->left + (availableWidth - iconSize) / 2;
        iconRect.right = iconRect.left + iconSize;
        iconRect.top = bounds->top + (availableHeight - iconSize) / 2;
        iconRect.bottom = iconRect.top + iconSize;
        *captionFlags = 0;
    }

    drawn = ribbon_draw_draw_glyph(
        app, dc, control->icon, &iconRect, color);
    if (drawn) {
        control->iconPaintCount =
            control->iconPaintCount == UINT_MAX
                ? 1
                : control->iconPaintCount + 1;
    }
    return TRUE;
}

static BOOL ribbon_design_control_is_full_large(
    const DesignControlInfo *control)
{
    if (control == NULL) {
        return FALSE;
    }
    return control->id == IDM_DESIGN_THEMES ||
           control->id == IDM_DESIGN_COLORS ||
           control->id == IDM_DESIGN_FONTS ||
           control->group == DESIGN_GROUP_PAGE_BACKGROUND;
}

BOOL ribbon_draw_design_button_icon(
    AppState *app, HWND button, HDC dc, const RECT *bounds,
    COLORREF color, RECT *captionRect, UINT *captionFlags)
{
    RibbonContext *ribbon;
    DesignControlInfo *control;
    RECT iconRect;
    int availableWidth;
    int availableHeight;
    int iconSize;
    BOOL drawn;

    if (!ribbon_valid_context(app) || button == NULL || dc == NULL ||
        bounds == NULL || captionRect == NULL || captionFlags == NULL) {
        return FALSE;
    }
    ribbon = app->ribbon;
    control = ribbon_find_design_control(ribbon, button);
    if (control == NULL) {
        return FALSE;
    }

    *captionRect = *bounds;
    availableWidth = max(1, bounds->right - bounds->left);
    availableHeight = max(1, bounds->bottom - bounds->top);
    if (control->stylePreview) {
        int styleSet = ribbon_design_style_from_command(control->id);
        iconRect = *bounds;
        InflateRect(&iconRect, -app_scale(app->mainWindow, 2),
                    -app_scale(app->mainWindow, 2));
        ribbon_draw_design_preview(
            app, dc, styleSet, &iconRect,
            styleSet == app->designStyleSet, FALSE);
        SetRectEmpty(captionRect);
        *captionFlags = 0;
        drawn = styleSet >= 0;
    } else if (ribbon->designLayoutMode == RIBBON_LAYOUT_FULL &&
               ribbon_design_control_is_full_large(control)) {
        int captionHeight = max(app_scale(app->mainWindow, 24),
                                availableHeight * 38 / 100);
        int iconAvailableHeight = max(
            1, availableHeight - captionHeight -
                   app_scale(app->mainWindow, 4));
        iconSize = min(
            app_scale(app->mainWindow, 30),
            min(availableWidth - app_scale(app->mainWindow, 8),
                iconAvailableHeight));
        iconSize = max(1, iconSize);
        iconRect.left = bounds->left +
                        (availableWidth - iconSize) / 2;
        iconRect.right = iconRect.left + iconSize;
        iconRect.top = bounds->top + app_scale(app->mainWindow, 4);
        iconRect.bottom = iconRect.top + iconSize;
        captionRect->left += app_scale(app->mainWindow, 2);
        captionRect->right -= app_scale(app->mainWindow, 2);
        captionRect->top =
            iconRect.bottom + app_scale(app->mainWindow, 2);
        captionRect->bottom -= app_scale(app->mainWindow, 2);
        *captionFlags = DT_CENTER | DT_WORDBREAK |
                        DT_END_ELLIPSIS | DT_NOPREFIX;
        drawn = ribbon_draw_design_glyph(
            app, dc, control->icon, &iconRect, color);
    } else if (ribbon->designLayoutMode == RIBBON_LAYOUT_FULL &&
               control->id != IDM_DESIGN_STYLE_GALLERY_MORE) {
        iconSize = min(app_scale(app->mainWindow, 16),
                       availableHeight -
                           app_scale(app->mainWindow, 4));
        iconSize = max(1, iconSize);
        iconRect.left =
            bounds->left + app_scale(app->mainWindow, 3);
        iconRect.right = iconRect.left + iconSize;
        iconRect.top =
            bounds->top + (availableHeight - iconSize) / 2;
        iconRect.bottom = iconRect.top + iconSize;
        captionRect->left =
            iconRect.right + app_scale(app->mainWindow, 4);
        captionRect->right -= app_scale(app->mainWindow, 2);
        *captionFlags = DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                        DT_END_ELLIPSIS | DT_NOPREFIX;
        drawn = ribbon_draw_design_glyph(
            app, dc, control->icon, &iconRect, color);
    } else {
        int maximum =
            ribbon->designLayoutMode == RIBBON_LAYOUT_FULL
                ? app_scale(app->mainWindow, 17)
                : ribbon->designLayoutMode == RIBBON_LAYOUT_COMPACT
                      ? app_scale(app->mainWindow, 22)
                      : app_scale(app->mainWindow, 17);
        iconSize = min(
            maximum,
            min(availableWidth - app_scale(app->mainWindow, 4),
                availableHeight - app_scale(app->mainWindow, 4)));
        iconSize = max(1, iconSize);
        iconRect.left = bounds->left +
                        (availableWidth - iconSize) / 2;
        iconRect.right = iconRect.left + iconSize;
        iconRect.top = bounds->top +
                       (availableHeight - iconSize) / 2;
        iconRect.bottom = iconRect.top + iconSize;
        *captionFlags = 0;
        drawn = ribbon_draw_design_glyph(
            app, dc, control->icon, &iconRect, color);
    }

    if (drawn) {
        control->iconPaintCount =
            control->iconPaintCount == UINT_MAX
                ? 1
                : control->iconPaintCount + 1;
    }
    return TRUE;
}

static void ribbon_sync_design_checkmarks(RibbonContext *ribbon)
{
    size_t index;

    if (ribbon == NULL || ribbon->app == NULL) {
        return;
    }
    for (index = 0; index < ribbon->designControlCount; ++index) {
        DesignControlInfo *control = &ribbon->designControls[index];
        int styleSet = control->stylePreview
                           ? ribbon_design_style_from_command(control->id)
                           : -1;
        SendMessageW(
            control->window, BM_SETCHECK,
            styleSet >= 0 &&
                    styleSet == ribbon->app->designStyleSet
                ? BST_CHECKED
                : BST_UNCHECKED,
            0);
        if (control->stylePreview) {
            InvalidateRect(control->window, NULL, TRUE);
        }
    }
}

static BOOL ribbon_apply_design_style_set(
    RibbonContext *ribbon, int styleSet)
{
    WordcraftDesignStyleSetInfo info;
    AppState *app;
    WCHAR status[160];

    if (ribbon == NULL || ribbon->app == NULL ||
        styleSet < 0 || styleSet >= DESIGN_STYLE_SET_COUNT ||
        !format_get_design_style_set_info(styleSet, &info)) {
        return FALSE;
    }
    app = ribbon->app;
    if (!format_apply_document_design(
            app, styleSet, app->designColorScheme,
            app->designFontScheme, app->designParagraphSpacing)) {
        return FALSE;
    }
    StringCchPrintfW(status, ARRAYSIZE(status),
                     L"Document style set: %s", info.name);
    app_set_status_message(app, status);
    ribbon_sync_design_checkmarks(ribbon);
    app_update_command_ui(app);
    return TRUE;
}

static void ribbon_design_gallery_layout(RibbonContext *ribbon,
                                         const RECT *client)
{
    AppState *app;
    int margin;
    int tileWidth;
    int tileHeight;
    int gap;
    int currentTop;
    int separator;
    int builtTop;
    int available;
    int columns;
    int rows;
    int footerTop;
    int index;

    if (ribbon == NULL || ribbon->app == NULL || client == NULL) {
        return;
    }
    app = ribbon->app;
    margin = app_scale(app->mainWindow, 5);
    tileWidth = app_scale(app->mainWindow, 68);
    tileHeight = app_scale(app->mainWindow, 55);
    gap = app_scale(app->mainWindow, 5);
    currentTop = app_scale(app->mainWindow, 30);
    SetRect(&ribbon->designGalleryItemRects[0],
            margin, currentTop, margin + tileWidth,
            currentTop + tileHeight);
    separator =
        currentTop + tileHeight + app_scale(app->mainWindow, 10);
    builtTop = separator + app_scale(app->mainWindow, 32);
    available = max(tileWidth, client->right - client->left -
                                   margin * 2);
    columns = max(1, min(15, (available + gap) /
                                 max(1, tileWidth + gap)));
    rows = (DESIGN_STYLE_SET_COUNT - 1 + columns - 1) / columns;
    for (index = 1; index < DESIGN_STYLE_SET_COUNT; ++index) {
        int local = index - 1;
        int column = local % columns;
        int row = local / columns;
        int left = margin + column * (tileWidth + gap);
        int top = builtTop + row * (tileHeight + gap);
        SetRect(&ribbon->designGalleryItemRects[index],
                left, top, left + tileWidth, top + tileHeight);
    }
    footerTop = builtTop + rows * tileHeight +
                max(0, rows - 1) * gap +
                app_scale(app->mainWindow, 12);
    SetRect(&ribbon->designGalleryItemRects[
                DESIGN_GALLERY_RESET_INDEX],
            margin, footerTop + app_scale(app->mainWindow, 5),
            client->right - margin,
            footerTop + app_scale(app->mainWindow, 31));
    SetRect(&ribbon->designGalleryItemRects[
                DESIGN_GALLERY_SAVE_INDEX],
            margin,
            footerTop + app_scale(app->mainWindow, 35),
            client->right - margin,
            min(client->bottom - app_scale(app->mainWindow, 5),
                footerTop + app_scale(app->mainWindow, 61)));
}

static int ribbon_design_gallery_hit_test(
    const RibbonContext *ribbon, POINT point)
{
    int index;

    if (ribbon == NULL) {
        return -1;
    }
    for (index = 0; index < DESIGN_GALLERY_ITEM_COUNT; ++index) {
        if (PtInRect(&ribbon->designGalleryItemRects[index], point)) {
            return index;
        }
    }
    return -1;
}

static void ribbon_design_gallery_paint(RibbonContext *ribbon,
                                        HDC dc)
{
    AppState *app;
    RECT client;
    RECT header;
    RECT grip;
    COLORREF background;
    COLORREF textColor;
    COLORREF divider;
    HBRUSH backgroundBrush;
    HBRUSH hotBrush;
    HPEN dividerPen;
    HGDIOBJ previousPen;
    HGDIOBJ previousBrush;
    HFONT boldFont = NULL;
    HGDIOBJ previousFont = NULL;
    LOGFONTW logFont;
    int oldBackground;
    COLORREF oldText;
    int index;
    int builtSeparatorY;
    int footerSeparatorY;

    if (ribbon == NULL || ribbon->app == NULL || dc == NULL ||
        ribbon->designGalleryWindow == NULL) {
        return;
    }
    app = ribbon->app;
    GetClientRect(ribbon->designGalleryWindow, &client);
    ribbon_design_gallery_layout(ribbon, &client);
    background = app->useBrandColors
                     ? app->palette.controlBackground
                     : GetSysColor(COLOR_MENU);
    textColor = app->useBrandColors
                    ? app->palette.controlText
                    : GetSysColor(COLOR_MENUTEXT);
    divider = app->useBrandColors
                  ? ribbon_blend_color(app->palette.controlBorder,
                                       background, 48)
                  : GetSysColor(COLOR_3DSHADOW);
    backgroundBrush = CreateSolidBrush(background);
    hotBrush = CreateSolidBrush(
        app->useBrandColors
            ? ribbon_blend_color(background,
                                 app->palette.toolbarHotBackground, 22)
            : GetSysColor(COLOR_MENUHILIGHT));
    dividerPen = CreatePen(PS_SOLID, 1, divider);
    if (backgroundBrush == NULL || hotBrush == NULL ||
        dividerPen == NULL) {
        if (backgroundBrush != NULL) DeleteObject(backgroundBrush);
        if (hotBrush != NULL) DeleteObject(hotBrush);
        if (dividerPen != NULL) DeleteObject(dividerPen);
        return;
    }
    FillRect(dc, &client, backgroundBrush);
    previousPen = SelectObject(dc, dividerPen);
    previousBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    oldBackground = SetBkMode(dc, TRANSPARENT);
    oldText = SetTextColor(dc, textColor);
    if (app->uiFont != NULL &&
        GetObjectW(app->uiFont, sizeof(logFont), &logFont) ==
            sizeof(logFont)) {
        logFont.lfWeight = FW_SEMIBOLD;
        boldFont = CreateFontIndirectW(&logFont);
    }
    if (boldFont != NULL) {
        previousFont = SelectObject(dc, boldFont);
    } else if (app->uiFont != NULL) {
        previousFont = SelectObject(dc, app->uiFont);
    }

    SetRect(&header, app_scale(app->mainWindow, 12),
            app_scale(app->mainWindow, 3),
            client.right - app_scale(app->mainWindow, 8),
            app_scale(app->mainWindow, 29));
    DrawTextW(dc, L"This Document", -1, &header,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    builtSeparatorY =
        ribbon->designGalleryItemRects[0].bottom +
        app_scale(app->mainWindow, 10);
    MoveToEx(dc, app_scale(app->mainWindow, 5), builtSeparatorY,
             NULL);
    LineTo(dc, client.right - app_scale(app->mainWindow, 5),
           builtSeparatorY);
    SetRect(&header, app_scale(app->mainWindow, 12),
            builtSeparatorY + app_scale(app->mainWindow, 2),
            client.right - app_scale(app->mainWindow, 8),
            builtSeparatorY + app_scale(app->mainWindow, 30));
    DrawTextW(dc, L"Built-In", -1, &header,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (previousFont != NULL) {
        SelectObject(dc, previousFont);
        previousFont = NULL;
    }
    if (app->uiFont != NULL) {
        previousFont = SelectObject(dc, app->uiFont);
    }
    for (index = 0; index < DESIGN_STYLE_SET_COUNT; ++index) {
        const RECT *item = &ribbon->designGalleryItemRects[index];
        if (index == ribbon->designGalleryHotIndex ||
            index == ribbon->designGalleryFocusedIndex) {
            RECT highlight = *item;
            InflateRect(&highlight, app_scale(app->mainWindow, 2),
                        app_scale(app->mainWindow, 2));
            FillRect(dc, &highlight, hotBrush);
        }
        ribbon_draw_design_preview(
            app, dc,
            index == 0 ? app->designStyleSet : index,
            item,
            (index == 0 ||
             index == app->designStyleSet),
            index == ribbon->designGalleryHotIndex ||
                index == ribbon->designGalleryFocusedIndex);
    }
    footerSeparatorY =
        ribbon->designGalleryItemRects[
            DESIGN_GALLERY_RESET_INDEX]
            .top -
        app_scale(app->mainWindow, 5);
    MoveToEx(dc, app_scale(app->mainWindow, 5), footerSeparatorY,
             NULL);
    LineTo(dc, client.right - app_scale(app->mainWindow, 5),
           footerSeparatorY);
    for (index = DESIGN_GALLERY_RESET_INDEX;
         index <= DESIGN_GALLERY_SAVE_INDEX; ++index) {
        RECT item = ribbon->designGalleryItemRects[index];
        RECT text = item;
        const WCHAR *caption =
            index == DESIGN_GALLERY_RESET_INDEX
                ? L"Reset to the Default Style Set"
                : L"Save as a New Style Set...";
        if (index == ribbon->designGalleryHotIndex ||
            index == ribbon->designGalleryFocusedIndex) {
            FillRect(dc, &item, hotBrush);
        }
        text.left += app_scale(app->mainWindow, 28);
        DrawTextW(dc, caption, -1, &text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                      DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    grip = client;
    grip.top = max(grip.top, grip.bottom -
                                 app_scale(app->mainWindow, 8));
    grip.left = client.left + (client.right - client.left) / 2 -
                app_scale(app->mainWindow, 8);
    grip.right = grip.left + app_scale(app->mainWindow, 16);
    SetTextColor(dc, divider);
    DrawTextW(dc, L"\x00B7\x00B7\x00B7\x00B7", -1, &grip,
              DT_CENTER | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);

    if (GetFocus() == ribbon->designGalleryWindow &&
        ribbon->designGalleryFocusedIndex >= 0 &&
        ribbon->designGalleryFocusedIndex <
            DESIGN_GALLERY_ITEM_COUNT) {
        RECT focus =
            ribbon->designGalleryItemRects[
                ribbon->designGalleryFocusedIndex];
        InflateRect(&focus, -1, -1);
        DrawFocusRect(dc, &focus);
    }
    if (previousFont != NULL) {
        SelectObject(dc, previousFont);
    }
    SetTextColor(dc, oldText);
    SetBkMode(dc, oldBackground);
    SelectObject(dc, previousBrush);
    SelectObject(dc, previousPen);
    if (boldFont != NULL) DeleteObject(boldFont);
    DeleteObject(dividerPen);
    DeleteObject(hotBrush);
    DeleteObject(backgroundBrush);
    ribbon->designGalleryPaintCount =
        ribbon->designGalleryPaintCount == UINT_MAX
            ? 1
            : ribbon->designGalleryPaintCount + 1;
}

static void ribbon_design_gallery_activate(RibbonContext *ribbon,
                                           int index)
{
    if (ribbon == NULL || ribbon->app == NULL) {
        return;
    }
    if (index >= 0 && index < DESIGN_STYLE_SET_COUNT) {
        int styleSet =
            index == DESIGN_GALLERY_CURRENT_INDEX
                ? ribbon->app->designStyleSet
                : index;
        ribbon_hide_design_gallery(ribbon, FALSE);
        ribbon_apply_design_style_set(ribbon, styleSet);
        SetFocus(ribbon->app->editor);
    } else if (index == DESIGN_GALLERY_RESET_INDEX) {
        ribbon_hide_design_gallery(ribbon, FALSE);
        ribbon_apply_design_style_set(
            ribbon, DESIGN_STYLE_SET_OFFICE);
        SetFocus(ribbon->app->editor);
    } else if (index == DESIGN_GALLERY_SAVE_INDEX) {
        ribbon_hide_design_gallery(ribbon, FALSE);
        MessageBeep(MB_ICONINFORMATION);
        app_set_status_message(
            ribbon->app,
            L"Saving custom style sets requires a persistent template "
            L"model");
        SetFocus(ribbon->app->editor);
    }
}

static void ribbon_design_gallery_move_focus(
    RibbonContext *ribbon, int delta)
{
    int index;

    if (ribbon == NULL) {
        return;
    }
    index = ribbon->designGalleryFocusedIndex;
    if (index < 0 || index >= DESIGN_GALLERY_ITEM_COUNT) {
        index = 0;
    }
    index = (index + DESIGN_GALLERY_ITEM_COUNT + delta) %
            DESIGN_GALLERY_ITEM_COUNT;
    ribbon->designGalleryFocusedIndex = index;
    InvalidateRect(ribbon->designGalleryWindow, NULL, FALSE);
}

static LRESULT CALLBACK ribbon_design_gallery_proc(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    RibbonContext *ribbon =
        (RibbonContext *)GetWindowLongPtrW(window, GWLP_USERDATA);

    if (message == WM_NCCREATE) {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lParam;
        ribbon = (RibbonContext *)create->lpCreateParams;
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          (LONG_PTR)ribbon);
        return TRUE;
    }
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        ribbon_design_gallery_paint(ribbon, dc);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_PRINTCLIENT:
        if (wParam != 0) {
            ribbon_design_gallery_paint(ribbon, (HDC)wParam);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (ribbon != NULL) {
            POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int hot =
                ribbon_design_gallery_hit_test(ribbon, point);
            if (hot != ribbon->designGalleryHotIndex) {
                ribbon->designGalleryHotIndex = hot;
                InvalidateRect(window, NULL, FALSE);
            }
            {
                TRACKMOUSEEVENT track;
                ZeroMemory(&track, sizeof(track));
                track.cbSize = sizeof(track);
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = window;
                TrackMouseEvent(&track);
            }
        }
        return 0;
    case WM_MOUSELEAVE:
        if (ribbon != NULL) {
            ribbon->designGalleryHotIndex = -1;
            InvalidateRect(window, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(window);
        if (ribbon != NULL) {
            POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int hit = ribbon_design_gallery_hit_test(ribbon, point);
            if (hit >= 0) {
                ribbon->designGalleryFocusedIndex = hit;
                InvalidateRect(window, NULL, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (ribbon != NULL) {
            POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int hit = ribbon_design_gallery_hit_test(ribbon, point);
            if (hit >= 0 &&
                hit == ribbon->designGalleryFocusedIndex) {
                ribbon_design_gallery_activate(ribbon, hit);
            }
        }
        return 0;
    case WM_KEYDOWN:
        if (ribbon == NULL) {
            break;
        }
        switch (wParam) {
        case VK_ESCAPE:
            ribbon_hide_design_gallery(ribbon, TRUE);
            return 0;
        case VK_LEFT:
            ribbon_design_gallery_move_focus(ribbon, -1);
            return 0;
        case VK_RIGHT:
            ribbon_design_gallery_move_focus(ribbon, 1);
            return 0;
        case VK_UP:
            ribbon_design_gallery_move_focus(ribbon, -15);
            return 0;
        case VK_DOWN:
            ribbon_design_gallery_move_focus(ribbon, 15);
            return 0;
        case VK_TAB:
            ribbon_design_gallery_move_focus(
                ribbon,
                (GetKeyState(VK_SHIFT) & 0x8000) != 0 ? -1 : 1);
            return 0;
        case VK_RETURN:
        case VK_SPACE:
            ribbon_design_gallery_activate(
                ribbon, ribbon->designGalleryFocusedIndex);
            return 0;
        default:
            break;
        }
        break;
    case WM_KILLFOCUS:
        if (ribbon != NULL &&
            (HWND)wParam != ribbon->designGalleryReturnFocus) {
            ribbon_hide_design_gallery(ribbon, FALSE);
        }
        return 0;
    case WM_CLOSE:
        ribbon_hide_design_gallery(ribbon, TRUE);
        return 0;
    case WM_NCDESTROY:
        if (ribbon != NULL) {
            ribbon->designGalleryWindow = NULL;
            ribbon->designGalleryVisible = FALSE;
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

static BOOL ribbon_register_design_gallery_class(AppState *app)
{
    WNDCLASSEXW existing;
    WNDCLASSEXW windowClass;

    if (app == NULL) {
        return FALSE;
    }
    ZeroMemory(&existing, sizeof(existing));
    existing.cbSize = sizeof(existing);
    if (GetClassInfoExW(app->instance, DESIGN_GALLERY_CLASS_NAME,
                        &existing)) {
        return TRUE;
    }
    ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = ribbon_design_gallery_proc;
    windowClass.hInstance = app->instance;
    windowClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    windowClass.lpszClassName = DESIGN_GALLERY_CLASS_NAME;
    return RegisterClassExW(&windowClass) != 0;
}

static BOOL ribbon_show_design_gallery(RibbonContext *ribbon,
                                       HWND source)
{
    AppState *app;
    RECT anchor;
    RECT work;
    HMONITOR monitor;
    MONITORINFO monitorInfo;
    POINT points[2];
    int width;
    int height;
    int x;
    int y;

    if (ribbon == NULL || ribbon->app == NULL ||
        ribbon->activeTab != RIBBON_TAB_DESIGN) {
        return FALSE;
    }
    if (ribbon->designGalleryVisible) {
        ribbon_hide_design_gallery(ribbon, TRUE);
        return TRUE;
    }
    app = ribbon->app;
    anchor = ribbon->designInlineGalleryRect;
    points[0].x = anchor.left;
    points[0].y = anchor.top;
    points[1].x = anchor.right;
    points[1].y = anchor.bottom;
    MapWindowPoints(app->formatBar, NULL, points, 2);
    anchor.left = points[0].x;
    anchor.top = points[0].y;
    anchor.right = points[1].x;
    anchor.bottom = points[1].y;
    width = max(app_scale(app->mainWindow, 520),
                anchor.right - anchor.left);
    height = app_scale(app->mainWindow, 312);
    x = anchor.left;
    y = anchor.top;

    monitor = MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
    ZeroMemory(&monitorInfo, sizeof(monitorInfo));
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (monitor != NULL &&
        GetMonitorInfoW(monitor, &monitorInfo)) {
        work = monitorInfo.rcWork;
        width = min(width, max(1, work.right - x));
        if (y + height > work.bottom) {
            y = max(work.top, work.bottom - height);
        }
        height = min(height, max(1, work.bottom - y));
    }
    if (ribbon->designGalleryWindow == NULL) {
        if (!ribbon_register_design_gallery_class(app)) {
            return FALSE;
        }
        ribbon->designGalleryWindow = CreateWindowExW(
            WS_EX_TOOLWINDOW, DESIGN_GALLERY_CLASS_NAME,
            L"Document Style Sets", WS_POPUP | WS_BORDER,
            x, y, width, height, app->mainWindow, NULL,
            app->instance, ribbon);
        if (ribbon->designGalleryWindow == NULL) {
            return FALSE;
        }
    }
    ribbon->designGalleryReturnFocus =
        source != NULL ? source : ribbon->designControls[11].window;
    ribbon->designGalleryFocusedIndex =
        DESIGN_GALLERY_CURRENT_INDEX;
    ribbon->designGalleryHotIndex = -1;
    ribbon->designGalleryVisible = TRUE;
    SetWindowPos(ribbon->designGalleryWindow, HWND_TOP, x, y,
                 width, height,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);
    InvalidateRect(ribbon->designGalleryWindow, NULL, TRUE);
    UpdateWindow(ribbon->designGalleryWindow);
    SetFocus(ribbon->designGalleryWindow);
    return TRUE;
}

static void ribbon_hide_design_gallery(RibbonContext *ribbon,
                                       BOOL restoreFocus)
{
    HWND focus;

    if (ribbon == NULL) {
        return;
    }
    focus = ribbon->designGalleryReturnFocus;
    ribbon->designGalleryVisible = FALSE;
    ribbon->designGalleryHotIndex = -1;
    if (ribbon->designGalleryWindow != NULL &&
        IsWindow(ribbon->designGalleryWindow)) {
        ShowWindow(ribbon->designGalleryWindow, SW_HIDE);
    }
    if (ribbon->app != NULL && ribbon->app->formatBar != NULL) {
        InvalidateRect(ribbon->app->formatBar, NULL, TRUE);
    }
    if (restoreFocus && focus != NULL && IsWindow(focus) &&
        IsWindowVisible(focus)) {
        SetFocus(focus);
    }
}

static BOOL ribbon_view_control_is_full_large(
    const ViewControlInfo *control)
{
    if (control == NULL) {
        return FALSE;
    }
    switch (control->group) {
    case VIEW_GROUP_VIEWS:
        return control->id == IDM_VIEW_READ_MODE ||
               control->id == IDM_VIEW_PRINT_LAYOUT ||
               control->id == IDM_VIEW_WEB_LAYOUT;
    case VIEW_GROUP_SHOW:
        return FALSE;
    case VIEW_GROUP_ZOOM:
        return control->id == IDM_VIEW_ZOOM_DIALOG ||
               control->id == IDM_VIEW_ZOOM_100;
    case VIEW_GROUP_WINDOW:
        return control->id == IDM_VIEW_NEW_WINDOW ||
               control->id == IDM_VIEW_ARRANGE_ALL ||
               control->id == IDM_VIEW_SPLIT ||
               control->id == IDM_VIEW_SWITCH_WINDOWS;
    default:
        return TRUE;
    }
}

BOOL ribbon_draw_view_button_icon(AppState *app, HWND button, HDC dc,
                                  const RECT *bounds, COLORREF color,
                                  RECT *captionRect,
                                  UINT *captionFlags)
{
    RibbonContext *ribbon;
    ViewControlInfo *control;
    RECT iconRect;
    int availableWidth;
    int availableHeight;
    int iconSize;
    BOOL drawn;

    if (!ribbon_valid_context(app) || button == NULL || dc == NULL ||
        bounds == NULL || captionRect == NULL || captionFlags == NULL) {
        return FALSE;
    }
    ribbon = app->ribbon;
    control = ribbon_find_view_control(ribbon, button);
    if (control == NULL) {
        return FALSE;
    }

    *captionRect = *bounds;
    availableWidth = max(1, bounds->right - bounds->left);
    availableHeight = max(1, bounds->bottom - bounds->top);
    if (ribbon->viewLayoutMode == RIBBON_LAYOUT_FULL &&
        ribbon_view_control_is_full_large(control)) {
        int captionHeight = max(app_scale(app->mainWindow, 24),
                                availableHeight * 40 / 100);
        int iconAvailableHeight = max(
            1, availableHeight - captionHeight -
                   app_scale(app->mainWindow, 4));
        iconSize = min(app_scale(app->mainWindow, 30),
                       min(availableWidth - app_scale(app->mainWindow, 8),
                           iconAvailableHeight));
        iconSize = max(1, iconSize);
        iconRect.left = bounds->left + (availableWidth - iconSize) / 2;
        iconRect.right = iconRect.left + iconSize;
        iconRect.top = bounds->top + app_scale(app->mainWindow, 4);
        iconRect.bottom = iconRect.top + iconSize;
        captionRect->left += app_scale(app->mainWindow, 2);
        captionRect->right -= app_scale(app->mainWindow, 2);
        captionRect->top = iconRect.bottom + app_scale(app->mainWindow, 2);
        captionRect->bottom -= app_scale(app->mainWindow, 2);
        *captionFlags = DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS |
                        DT_NOPREFIX;
    } else if (ribbon->viewLayoutMode == RIBBON_LAYOUT_FULL) {
        iconSize = min(app_scale(app->mainWindow, 16),
                       availableHeight - app_scale(app->mainWindow, 4));
        iconSize = max(1, iconSize);
        iconRect.left = bounds->left + app_scale(app->mainWindow, 3);
        iconRect.right = iconRect.left + iconSize;
        iconRect.top = bounds->top + (availableHeight - iconSize) / 2;
        iconRect.bottom = iconRect.top + iconSize;
        captionRect->left = iconRect.right +
                            app_scale(app->mainWindow, 3);
        captionRect->right -= app_scale(app->mainWindow, 2);
        *captionFlags = DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                        DT_END_ELLIPSIS | DT_NOPREFIX;
    } else {
        int maximum = ribbon->viewLayoutMode == RIBBON_LAYOUT_COMPACT
                          ? app_scale(app->mainWindow, 22)
                          : app_scale(app->mainWindow, 17);
        iconSize = min(maximum,
                       min(availableWidth - app_scale(app->mainWindow, 4),
                           availableHeight - app_scale(app->mainWindow, 4)));
        iconSize = max(1, iconSize);
        iconRect.left = bounds->left + (availableWidth - iconSize) / 2;
        iconRect.right = iconRect.left + iconSize;
        iconRect.top = bounds->top + (availableHeight - iconSize) / 2;
        iconRect.bottom = iconRect.top + iconSize;
        *captionFlags = 0;
    }

    drawn = ribbon_draw_view_glyph(
        app, dc, control->icon, &iconRect, color);
    if (drawn) {
        control->iconPaintCount =
            control->iconPaintCount == UINT_MAX
                ? 1
                : control->iconPaintCount + 1;
    }
    return TRUE;
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
    if (IsWindow(ribbon->designGalleryWindow) &&
        ribbon->designGalleryVisible) {
        RedrawWindow(ribbon->designGalleryWindow, NULL, NULL,
                     RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
    }
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

typedef struct DesignThemePreset {
    UINT command;
    const WCHAR *name;
    int styleSet;
    int colorScheme;
    int fontScheme;
    int effect;
} DesignThemePreset;

static const DesignThemePreset designThemePresets[] = {
    {IDM_DESIGN_THEME_OFFICE, L"Office",
     DESIGN_STYLE_SET_OFFICE, DESIGN_COLOR_SCHEME_OFFICE,
     DESIGN_FONT_SCHEME_OFFICE, DESIGN_EFFECT_OFFICE},
    {IDM_DESIGN_THEME_FACET, L"Facet",
     DESIGN_STYLE_SET_LINES_DISTINCTIVE, DESIGN_COLOR_SCHEME_BLUE,
     DESIGN_FONT_SCHEME_MODERN, DESIGN_EFFECT_LINE},
    {IDM_DESIGN_THEME_GALLERY, L"Gallery",
     DESIGN_STYLE_SET_SHADED, DESIGN_COLOR_SCHEME_PURPLE,
     DESIGN_FONT_SCHEME_HUMANIST, DESIGN_EFFECT_SHADOW},
    {IDM_DESIGN_THEME_INTEGRAL, L"Integral",
     DESIGN_STYLE_SET_FORMAL, DESIGN_COLOR_SCHEME_GREEN,
     DESIGN_FONT_SCHEME_CLASSIC, DESIGN_EFFECT_SUBTLE},
    {IDM_DESIGN_THEME_ION, L"Ion",
     DESIGN_STYLE_SET_MODERN, DESIGN_COLOR_SCHEME_BLUE,
     DESIGN_FONT_SCHEME_MODERN, DESIGN_EFFECT_GLOW},
    {IDM_DESIGN_THEME_RETROSPECT, L"Retrospect",
     DESIGN_STYLE_SET_CLASSIC, DESIGN_COLOR_SCHEME_ORANGE,
     DESIGN_FONT_SCHEME_EDITORIAL, DESIGN_EFFECT_FLAT}
};

static const WCHAR *const designColorNames[DESIGN_COLOR_SCHEME_COUNT] = {
    L"Office", L"Blue", L"Green", L"Orange", L"Red", L"Purple"
};

static const WCHAR *const designFontNames[DESIGN_FONT_SCHEME_COUNT] = {
    L"Office", L"Classic", L"Modern", L"Humanist", L"Editorial",
    L"Monospace"
};

static const WCHAR *const
    designSpacingNames[DESIGN_PARAGRAPH_SPACING_COUNT] = {
        L"No Paragraph Space", L"Compact", L"Tight", L"Open",
        L"Relaxed", L"Double"
    };

static const WCHAR *const designEffectNames[DESIGN_EFFECT_COUNT] = {
    L"Office", L"Subtle", L"Shadow", L"Line", L"Glow", L"Flat"
};

static UINT ribbon_track_design_menu(AppState *app, HWND source,
                                     HMENU menu)
{
    RECT sourceRect;
    POINT point;

    if (app == NULL || menu == NULL) {
        return 0;
    }
    if (source != NULL && GetWindowRect(source, &sourceRect)) {
        point.x = sourceRect.left;
        point.y = sourceRect.bottom;
    } else {
        GetCursorPos(&point);
    }
    return TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN |
                  TPM_RIGHTBUTTON,
        point.x, point.y, 0, app->mainWindow, NULL);
}

static void ribbon_append_design_menu_item(HMENU menu, UINT command,
                                           const WCHAR *caption,
                                           BOOL checked)
{
    AppendMenuW(menu, MF_STRING | (checked ? MF_CHECKED : MF_UNCHECKED),
                command, caption);
}

static void ribbon_dispatch_design_menu(AppState *app, HWND source,
                                        HMENU menu)
{
    UINT selected;

    if (menu == NULL) {
        return;
    }
    selected = ribbon_track_design_menu(app, source, menu);
    DestroyMenu(menu);
    if (selected != 0) {
        SendMessageW(app->mainWindow, WM_COMMAND,
                     MAKEWPARAM(selected, 0), 0);
    }
}

static void ribbon_show_design_themes_menu(AppState *app, HWND source)
{
    HMENU menu;
    size_t index;

    if (app == NULL) {
        return;
    }
    menu = CreatePopupMenu();
    if (menu == NULL) {
        return;
    }
    for (index = 0; index < ARRAYSIZE(designThemePresets); ++index) {
        const DesignThemePreset *preset = &designThemePresets[index];
        BOOL checked =
            app->designStyleSet == preset->styleSet &&
            app->designColorScheme == preset->colorScheme &&
            app->designFontScheme == preset->fontScheme &&
            app->designEffect == preset->effect;
        ribbon_append_design_menu_item(
            menu, preset->command, preset->name, checked);
    }
    ribbon_dispatch_design_menu(app, source, menu);
}

static void ribbon_show_design_indexed_menu(
    AppState *app, HWND source, UINT commandBase,
    const WCHAR *const *names, int count, int selectedIndex,
    BOOL customSpacingItem)
{
    HMENU menu;
    int index;

    if (app == NULL || names == NULL || count <= 0) {
        return;
    }
    menu = CreatePopupMenu();
    if (menu == NULL) {
        return;
    }
    for (index = 0; index < count; ++index) {
        ribbon_append_design_menu_item(
            menu, commandBase + (UINT)index, names[index],
            index == selectedIndex);
    }
    if (customSpacingItem) {
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(menu, MF_STRING, IDM_DESIGN_SPACING_CUSTOM,
                    L"Custom Paragraph Spacing...");
    }
    ribbon_dispatch_design_menu(app, source, menu);
}

static BOOL ribbon_apply_design_components(
    AppState *app, int styleSet, int colorScheme, int fontScheme,
    int paragraphSpacing, int effect, const WCHAR *statusPrefix,
    const WCHAR *statusValue)
{
    WCHAR status[192];

    if (!ribbon_valid_context(app) ||
        !format_apply_document_design(
            app, styleSet, colorScheme, fontScheme,
            paragraphSpacing)) {
        MessageBeep(MB_ICONWARNING);
        app_set_status_message(
            app, L"The document design could not be applied");
        return FALSE;
    }
    app->designEffect = effect;
    ribbon_sync_design_checkmarks(app->ribbon);
    if (statusPrefix != NULL && statusValue != NULL) {
        StringCchPrintfW(status, ARRAYSIZE(status), L"%s: %s",
                         statusPrefix, statusValue);
        app_set_status_message(app, status);
    }
    InvalidateRect(app->formatBar, NULL, TRUE);
    if (IsWindow(app->ribbon->designGalleryWindow)) {
        InvalidateRect(app->ribbon->designGalleryWindow, NULL, TRUE);
    }
    app_update_command_ui(app);
    return TRUE;
}

static BOOL ribbon_execute_design_command(AppState *app, UINT command,
                                          HWND source)
{
    RibbonContext *ribbon;
    int styleSet;
    size_t index;

    if (!ribbon_valid_context(app)) {
        return FALSE;
    }
    ribbon = app->ribbon;
    styleSet = ribbon_design_style_from_command(command);
    if (styleSet >= 0) {
        return ribbon_apply_design_style_set(ribbon, styleSet);
    }
    switch (command) {
    case IDM_DESIGN_THEMES:
        ribbon_show_design_themes_menu(app, source);
        return TRUE;
    case IDM_DESIGN_STYLE_GALLERY_MORE:
        if (!ribbon_show_design_gallery(ribbon, source)) {
            MessageBeep(MB_ICONWARNING);
            app_set_status_message(
                app, L"The document style gallery could not be opened");
        }
        return TRUE;
    case IDM_DESIGN_COLORS:
        ribbon_show_design_indexed_menu(
            app, source, IDM_DESIGN_COLOR_OFFICE,
            designColorNames, DESIGN_COLOR_SCHEME_COUNT,
            app->designColorScheme, FALSE);
        return TRUE;
    case IDM_DESIGN_FONTS:
        ribbon_show_design_indexed_menu(
            app, source, IDM_DESIGN_FONT_OFFICE,
            designFontNames, DESIGN_FONT_SCHEME_COUNT,
            app->designFontScheme, FALSE);
        return TRUE;
    case IDM_DESIGN_PARAGRAPH_SPACING:
        ribbon_show_design_indexed_menu(
            app, source, IDM_DESIGN_SPACING_NONE,
            designSpacingNames, DESIGN_PARAGRAPH_SPACING_COUNT,
            app->designParagraphSpacing, TRUE);
        return TRUE;
    case IDM_DESIGN_EFFECTS:
        ribbon_show_design_indexed_menu(
            app, source, IDM_DESIGN_EFFECT_OFFICE,
            designEffectNames, DESIGN_EFFECT_COUNT,
            app->designEffect, FALSE);
        return TRUE;
    case IDM_DESIGN_SET_AS_DEFAULT:
        MessageBoxW(
            app->mainWindow,
            L"WordCraft can apply this design to the current document. "
            L"Saving it as the default for every new document requires "
            L"a persistent template model.",
            L"Set as Default", MB_OK | MB_ICONINFORMATION);
        return TRUE;
    case IDM_DESIGN_WATERMARK:
        MessageBoxW(
            app->mainWindow,
            L"Watermarks require a persistent, printable page-background "
            L"layer and are not available in this document format yet.",
            L"Watermark", MB_OK | MB_ICONINFORMATION);
        return TRUE;
    case IDM_DESIGN_PAGE_COLOR:
        MessageBoxW(
            app->mainWindow,
            L"Page color requires persisted page-background metadata so "
            L"it prints and reopens correctly. That document feature is "
            L"not available yet.",
            L"Page Color", MB_OK | MB_ICONINFORMATION);
        return TRUE;
    case IDM_DESIGN_PAGE_BORDERS:
        MessageBoxW(
            app->mainWindow,
            L"Page borders require a persisted page-border and printing "
            L"model and are not available yet.",
            L"Page Borders", MB_OK | MB_ICONINFORMATION);
        return TRUE;
    case IDM_DESIGN_RESET_STYLE_SET:
        return ribbon_apply_design_style_set(
            ribbon, DESIGN_STYLE_SET_OFFICE);
    case IDM_DESIGN_SAVE_STYLE_SET:
        MessageBeep(MB_ICONINFORMATION);
        app_set_status_message(
            app,
            L"Saving custom style sets requires a persistent template "
            L"model");
        return TRUE;
    case IDM_DESIGN_SPACING_CUSTOM:
        MessageBoxW(
            app->mainWindow,
            L"Choose one of the document-wide spacing presets. Custom "
            L"style-set persistence is not available yet.",
            L"Custom Paragraph Spacing",
            MB_OK | MB_ICONINFORMATION);
        return TRUE;
    default:
        break;
    }

    for (index = 0; index < ARRAYSIZE(designThemePresets); ++index) {
        const DesignThemePreset *preset = &designThemePresets[index];
        if (preset->command == command) {
            return ribbon_apply_design_components(
                app, preset->styleSet, preset->colorScheme,
                preset->fontScheme, app->designParagraphSpacing,
                preset->effect, L"Document theme", preset->name);
        }
    }
    if (command >= IDM_DESIGN_COLOR_OFFICE &&
        command < IDM_DESIGN_COLOR_OFFICE +
                      DESIGN_COLOR_SCHEME_COUNT) {
        int scheme = (int)(command - IDM_DESIGN_COLOR_OFFICE);
        return ribbon_apply_design_components(
            app, app->designStyleSet, scheme,
            app->designFontScheme, app->designParagraphSpacing,
            app->designEffect, L"Document colors",
            designColorNames[scheme]);
    }
    if (command >= IDM_DESIGN_FONT_OFFICE &&
        command < IDM_DESIGN_FONT_OFFICE +
                      DESIGN_FONT_SCHEME_COUNT) {
        int scheme = (int)(command - IDM_DESIGN_FONT_OFFICE);
        return ribbon_apply_design_components(
            app, app->designStyleSet, app->designColorScheme,
            scheme, app->designParagraphSpacing,
            app->designEffect, L"Document fonts",
            designFontNames[scheme]);
    }
    if (command >= IDM_DESIGN_SPACING_NONE &&
        command < IDM_DESIGN_SPACING_NONE +
                      DESIGN_PARAGRAPH_SPACING_COUNT) {
        int spacing = (int)(command - IDM_DESIGN_SPACING_NONE);
        return ribbon_apply_design_components(
            app, app->designStyleSet, app->designColorScheme,
            app->designFontScheme, spacing, app->designEffect,
            L"Paragraph spacing", designSpacingNames[spacing]);
    }
    if (command >= IDM_DESIGN_EFFECT_OFFICE &&
        command < IDM_DESIGN_EFFECT_OFFICE + DESIGN_EFFECT_COUNT) {
        int effect = (int)(command - IDM_DESIGN_EFFECT_OFFICE);
        WCHAR status[128];
        app->designEffect = effect;
        StringCchPrintfW(status, ARRAYSIZE(status),
                         L"Document effects: %s",
                         designEffectNames[effect]);
        app_set_status_message(app, status);
        InvalidateRect(app->formatBar, NULL, TRUE);
        app_update_command_ui(app);
        return TRUE;
    }
    return FALSE;
}

static BOOL ribbon_draw_is_tool_command(UINT command)
{
    return command >= IDM_DRAW_SELECT &&
           command <= IDM_DRAW_ACTION_PEN;
}

static const WCHAR *ribbon_draw_tool_status(UINT command)
{
    switch (command) {
    case IDM_DRAW_SELECT:
        return L"Select applies to document content; Drawing Canvas will use Black Pen";
    case IDM_DRAW_LASSO_SELECT:
        return L"Lasso Select needs editable ink and is not available yet";
    case IDM_DRAW_ERASER:
        return L"Eraser selected for the next drawing canvas";
    case IDM_DRAW_PEN_BLACK:
        return L"Black pen selected";
    case IDM_DRAW_PEN_RED:
        return L"Red pen selected";
    case IDM_DRAW_PENCIL:
        return L"Pencil selected";
    case IDM_DRAW_HIGHLIGHTER:
        return L"Yellow highlighter selected";
    case IDM_DRAW_PEN_BLUE:
        return L"Blue pen selected";
    case IDM_DRAW_PEN_GREEN:
        return L"Green pen selected";
    case IDM_DRAW_ACTION_PEN:
        return L"Action pen selected";
    default:
        return L"Drawing tool selected";
    }
}

static void ribbon_sync_draw_checkmarks(RibbonContext *ribbon)
{
    size_t index;

    if (ribbon == NULL) {
        return;
    }
    for (index = 0; index < ribbon->drawControlCount; ++index) {
        DrawControlInfo *control = &ribbon->drawControls[index];
        BOOL checked = FALSE;

        if (control->id == IDM_DRAW_MODE) {
            checked = ribbon->drawModeActive;
        } else if (control->id == IDM_DRAW_RULER) {
            checked = ribbon->drawRulerVisible;
        } else if (control->id == IDM_DRAW_FORMAT_BACKGROUND) {
            checked = ribbon->drawBackgroundRuled;
        } else if (ribbon_draw_is_tool_command(control->id)) {
            checked = ribbon->activeDrawTool == control->id;
        }
        SendMessageW(control->window, BM_SETCHECK,
                     checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

static void ribbon_show_draw_pen_menu(AppState *app, HWND source)
{
    HMENU menu;
    RECT sourceRect;
    POINT point;
    UINT selected;

    if (!ribbon_valid_context(app)) {
        return;
    }
    menu = CreatePopupMenu();
    if (menu == NULL) {
        return;
    }
    AppendMenuW(menu, MF_STRING, IDM_DRAW_PEN_BLACK, L"Black Pen");
    AppendMenuW(menu, MF_STRING, IDM_DRAW_PEN_RED, L"Red Pen");
    AppendMenuW(menu, MF_STRING, IDM_DRAW_PENCIL, L"Pencil");
    AppendMenuW(menu, MF_STRING, IDM_DRAW_HIGHLIGHTER,
                L"Yellow Highlighter");
    AppendMenuW(menu, MF_STRING, IDM_DRAW_PEN_BLUE, L"Blue Pen");
    AppendMenuW(menu, MF_STRING, IDM_DRAW_PEN_GREEN, L"Green Pen");
    AppendMenuW(menu, MF_STRING, IDM_DRAW_ACTION_PEN, L"Action Pen");
    if (source != NULL && GetWindowRect(source, &sourceRect)) {
        point.x = sourceRect.left;
        point.y = sourceRect.bottom;
    } else {
        GetCursorPos(&point);
    }
    selected = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN |
                  TPM_RIGHTBUTTON,
        point.x, point.y, 0, app->mainWindow, NULL);
    DestroyMenu(menu);
    if (selected != 0) {
        SendMessageW(app->mainWindow, WM_COMMAND,
                     MAKEWPARAM(selected, 0), 0);
    }
}

static BOOL ribbon_execute_draw_command(AppState *app, UINT command,
                                        HWND source)
{
    RibbonContext *ribbon;

    if (!ribbon_valid_context(app) ||
        command < IDM_DRAW_MODE || command > IDM_DRAW_INK_HELP) {
        return FALSE;
    }
    ribbon = app->ribbon;
    if (ribbon_draw_is_tool_command(command)) {
        ribbon->activeDrawTool = command;
        ribbon->drawModeActive = FALSE;
        ribbon_sync_draw_checkmarks(ribbon);
        app_set_status_message(app, ribbon_draw_tool_status(command));
        return TRUE;
    }
    switch (command) {
    case IDM_DRAW_MODE:
        ribbon->drawModeActive = TRUE;
        if (ribbon->activeDrawTool == IDM_DRAW_SELECT ||
            ribbon->activeDrawTool == IDM_DRAW_LASSO_SELECT) {
            ribbon->activeDrawTool = IDM_DRAW_PEN_BLACK;
        }
        ribbon_sync_draw_checkmarks(ribbon);
        if (!draw_show_canvas(
                app, ribbon->activeDrawTool,
                ribbon->drawRulerVisible,
                ribbon->drawBackgroundRuled)) {
            app_show_error(app->mainWindow,
                           L"The drawing canvas could not be opened.",
                           GetLastError());
        }
        ribbon->drawModeActive = FALSE;
        ribbon_sync_draw_checkmarks(ribbon);
        return TRUE;
    case IDM_DRAW_ADD_PEN:
        ribbon_show_draw_pen_menu(app, source);
        return TRUE;
    case IDM_DRAW_RULER:
        ribbon->drawRulerVisible = !ribbon->drawRulerVisible;
        ribbon_sync_draw_checkmarks(ribbon);
        app_set_status_message(
            app, ribbon->drawRulerVisible
                     ? L"Ruler will be shown on the drawing canvas"
                     : L"Ruler hidden");
        return TRUE;
    case IDM_DRAW_FORMAT_BACKGROUND:
        ribbon->drawBackgroundRuled = !ribbon->drawBackgroundRuled;
        ribbon_sync_draw_checkmarks(ribbon);
        app_set_status_message(
            app, ribbon->drawBackgroundRuled
                     ? L"Ruled drawing background selected"
                     : L"Plain drawing background selected");
        return TRUE;
    case IDM_DRAW_INK_TO_SHAPE:
        MessageBeep(MB_ICONINFORMATION);
        app_set_status_message(
            app,
            L"Ink to Shape needs editable stroke recognition and is not available yet");
        return TRUE;
    case IDM_DRAW_INK_TO_MATH:
        MessageBeep(MB_ICONINFORMATION);
        app_set_status_message(
            app,
            L"Ink to Math needs handwriting recognition and is not available yet");
        return TRUE;
    case IDM_DRAW_CANVAS:
        if (ribbon->activeDrawTool == IDM_DRAW_SELECT ||
            ribbon->activeDrawTool == IDM_DRAW_LASSO_SELECT) {
            ribbon->activeDrawTool = IDM_DRAW_PEN_BLACK;
            ribbon_sync_draw_checkmarks(ribbon);
        }
        if (!draw_show_canvas(
                app, ribbon->activeDrawTool,
                ribbon->drawRulerVisible,
                ribbon->drawBackgroundRuled)) {
            app_show_error(app->mainWindow,
                           L"The drawing canvas could not be opened.",
                           GetLastError());
        }
        return TRUE;
    case IDM_DRAW_INK_REPLAY:
        MessageBeep(MB_ICONINFORMATION);
        app_set_status_message(
            app,
            L"Ink Replay needs retained stroke timing and is not available yet");
        return TRUE;
    case IDM_DRAW_INK_HELP:
        MessageBoxW(
            app->mainWindow,
            L"Choose a pen preset, then select Draw or Drawing Canvas. "
            L"Use the mouse or pen to sketch, Undo or Clear inside the "
            L"canvas, and choose Insert Drawing to add it to the document.\n\n"
            L"Inserted drawings are self-contained, printable, saved with "
            L"the document, and tracked in version history. For safety, "
            L"static drawings cannot be added during a live sharing session.",
            L"WordCraft Ink Help", MB_OK | MB_ICONINFORMATION);
        return TRUE;
    default:
        return FALSE;
    }
}

typedef struct RibbonWindowList {
    HWND windows[32];
    UINT count;
} RibbonWindowList;

static BOOL CALLBACK ribbon_collect_window_proc(HWND window, LPARAM data)
{
    RibbonWindowList *list = (RibbonWindowList *)data;
    WCHAR className[64];

    if (list == NULL || list->count >= ARRAYSIZE(list->windows) ||
        !IsWindowVisible(window) ||
        GetClassNameW(window, className, ARRAYSIZE(className)) <= 0 ||
        lstrcmpW(className, APP_CLASS_NAME) != 0) {
        return TRUE;
    }
    list->windows[list->count++] = window;
    return TRUE;
}

static void ribbon_collect_windows(RibbonWindowList *list)
{
    if (list == NULL) {
        return;
    }
    ZeroMemory(list, sizeof(*list));
    EnumWindows(ribbon_collect_window_proc, (LPARAM)list);
}

static void ribbon_sync_view_checkmarks(RibbonContext *ribbon)
{
    size_t index;

    if (ribbon == NULL || ribbon->app == NULL) {
        return;
    }
    for (index = 0; index < ribbon->viewControlCount; ++index) {
        ViewControlInfo *control = &ribbon->viewControls[index];
        BOOL checked = FALSE;

        switch (control->id) {
        case IDM_VIEW_READ_MODE:
            checked = ribbon->activeViewMode == VIEW_MODE_READ;
            break;
        case IDM_VIEW_PRINT_LAYOUT:
            checked = ribbon->activeViewMode == VIEW_MODE_PRINT_LAYOUT;
            break;
        case IDM_VIEW_WEB_LAYOUT:
            checked = ribbon->activeViewMode == VIEW_MODE_WEB_LAYOUT;
            break;
        case IDM_VIEW_OUTLINE:
            checked = ribbon->activeViewMode == VIEW_MODE_OUTLINE;
            break;
        case IDM_VIEW_DRAFT:
            checked = ribbon->activeViewMode == VIEW_MODE_DRAFT;
            break;
        case IDM_VIEW_FOCUS:
            checked = ribbon->app->focusMode;
            break;
        case IDM_VIEW_DARK_MODE:
            checked = ribbon->app->darkMode;
            break;
        case IDM_VIEW_VERTICAL:
            checked = ribbon->viewMovementVertical;
            break;
        case IDM_VIEW_SIDE_TO_SIDE:
            checked = !ribbon->viewMovementVertical;
            break;
        case IDM_VIEW_RULER:
            checked = ribbon->app->viewRulerVisible;
            break;
        case IDM_VIEW_GRIDLINES:
            checked = ribbon->app->viewGridlinesVisible;
            break;
        case IDM_VIEW_NAVIGATION_PANE:
            checked = ribbon->app->findDialog != NULL &&
                      IsWindow(ribbon->app->findDialog);
            break;
        case IDM_VIEW_SIDE_BY_SIDE:
            checked = ribbon->viewSideBySide;
            break;
        default:
            break;
        }
        SendMessageW(control->window, BM_SETCHECK,
                     checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

static BOOL ribbon_view_zoom_percent(UINT command, int *percent)
{
    size_t index;

    if (percent == NULL) {
        return FALSE;
    }
    for (index = 0; index < ARRAYSIZE(ribbonZoomCommands); ++index) {
        if (ribbonZoomCommands[index] == command) {
            *percent = ribbonZoomPercents[index];
            return TRUE;
        }
    }
    return FALSE;
}

static int ribbon_view_fit_zoom(AppState *app, BOOL fitHeight,
                                BOOL multiplePages)
{
    RECT client;
    HDC dc;
    int dpiX;
    int dpiY;
    int pageWidth;
    int pageHeight;
    int availableWidth;
    int availableHeight;
    int widthZoom;
    int heightZoom;
    int result;

    if (app == NULL || app->pageView == NULL ||
        !GetClientRect(app->pageView, &client)) {
        return 100;
    }
    dc = GetDC(app->pageView);
    if (dc != NULL) {
        dpiX = max(1, GetDeviceCaps(dc, LOGPIXELSX));
        dpiY = max(1, GetDeviceCaps(dc, LOGPIXELSY));
        ReleaseDC(app->pageView, dc);
    } else {
        dpiX = dpiY = 96;
    }
    pageWidth = max(1, MulDiv(
        app->pageSize.x > 0 ? app->pageSize.x : 8500, dpiX, 1000));
    pageHeight = max(1, MulDiv(
        app->pageSize.y > 0 ? app->pageSize.y : 11000, dpiY, 1000));
    availableWidth = max(
        1, client.right - client.left - app_scale(app->pageView, 64));
    availableHeight = max(
        1, client.bottom - client.top - app_scale(app->pageView, 64));
    if (comments_count(app) > 0) {
        availableWidth = max(
            1, availableWidth - app_scale(app->pageView, 304));
    }
    widthZoom = MulDiv(availableWidth, 100, pageWidth);
    heightZoom = MulDiv(
        availableHeight, 100,
        multiplePages
            ? max(1, pageHeight * 2 + app_scale(app->pageView, 24))
            : pageHeight);
    result = fitHeight ? min(widthZoom, heightZoom) : widthZoom;
    return ribbon_clamp(result, 10, 500);
}

static void ribbon_show_zoom_menu(AppState *app, HWND source)
{
    HMENU menu;
    RECT sourceRect;
    POINT point;
    UINT selected;
    size_t index;

    menu = CreatePopupMenu();
    if (menu == NULL) {
        return;
    }
    for (index = 0; index < ARRAYSIZE(ribbonZoomCommands); ++index) {
        AppendMenuW(
            menu,
            MF_STRING |
                (app->zoomPercent == ribbonZoomPercents[index]
                     ? MF_CHECKED : 0),
            ribbonZoomCommands[index], ribbonZoomLabels[index]);
    }
    if (source != NULL && GetWindowRect(source, &sourceRect)) {
        point.x = sourceRect.left;
        point.y = sourceRect.bottom;
    } else {
        GetCursorPos(&point);
    }
    selected = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN |
                  TPM_RIGHTBUTTON,
        point.x, point.y, 0, app->mainWindow, NULL);
    DestroyMenu(menu);
    if (selected != 0) {
        SendMessageW(app->mainWindow, WM_COMMAND,
                     MAKEWPARAM(selected, 0), 0);
    }
}

static BOOL ribbon_start_new_window(AppState *app)
{
    WCHAR executable[PATH_CAPACITY];
    WCHAR commandLine[PATH_CAPACITY * 2 + 8];
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;

    if (GetModuleFileNameW(NULL, executable, ARRAYSIZE(executable)) == 0 ||
        FAILED(StringCchPrintfW(
            commandLine, ARRAYSIZE(commandLine),
            app != NULL && !app->modified && app->currentPath[0] != L'\0'
                ? L"\"%s\" \"%s\"" : L"\"%s\"",
            executable,
            app != NULL ? app->currentPath : L""))) {
        return FALSE;
    }
    ZeroMemory(&startup, sizeof(startup));
    startup.cb = sizeof(startup);
    ZeroMemory(&process, sizeof(process));
    if (!CreateProcessW(executable, commandLine, NULL, NULL, FALSE, 0,
                        NULL, NULL, &startup, &process)) {
        return FALSE;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return TRUE;
}

static BOOL ribbon_arrange_windows(AppState *app, BOOL sideBySide,
                                   HWND preferredPeer, HWND *selectedPeer)
{
    RibbonWindowList list;
    MONITORINFO monitorInfo;
    HMONITOR monitor;
    RECT work;
    UINT count;
    UINT columns;
    UINT rows;
    UINT index;

    if (selectedPeer != NULL) {
        *selectedPeer = NULL;
    }
    if (app == NULL || app->mainWindow == NULL) {
        return FALSE;
    }
    ribbon_collect_windows(&list);
    if (list.count < (sideBySide ? 2u : 1u)) {
        return FALSE;
    }
    for (index = 0; index < list.count; ++index) {
        if (list.windows[index] == app->mainWindow) {
            HWND first = list.windows[0];
            list.windows[0] = app->mainWindow;
            list.windows[index] = first;
            break;
        }
    }
    if (sideBySide && preferredPeer != NULL) {
        BOOL foundPeer = FALSE;
        for (index = 1; index < list.count; ++index) {
            if (list.windows[index] == preferredPeer) {
                HWND second = list.windows[1];
                list.windows[1] = preferredPeer;
                list.windows[index] = second;
                foundPeer = TRUE;
                break;
            }
        }
        if (!foundPeer) {
            return FALSE;
        }
    }
    count = sideBySide ? min(list.count, 2u) : list.count;
    monitor = MonitorFromWindow(
        app->mainWindow, MONITOR_DEFAULTTONEAREST);
    ZeroMemory(&monitorInfo, sizeof(monitorInfo));
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (monitor == NULL || !GetMonitorInfoW(monitor, &monitorInfo)) {
        return FALSE;
    }
    work = monitorInfo.rcWork;
    columns = sideBySide ? 2u : (count <= 2u ? count : 2u);
    rows = (count + columns - 1u) / columns;
    for (index = 0; index < count; ++index) {
        UINT column = index % columns;
        UINT row = index / columns;
        int left = work.left +
                   MulDiv(work.right - work.left, (int)column,
                          (int)columns);
        int right = work.left +
                    MulDiv(work.right - work.left, (int)(column + 1u),
                           (int)columns);
        int top = work.top +
                  MulDiv(work.bottom - work.top, (int)row, (int)rows);
        int bottom = work.top +
                     MulDiv(work.bottom - work.top, (int)(row + 1u),
                            (int)rows);
        if (!IsWindow(list.windows[index])) {
            return FALSE;
        }
        ShowWindow(list.windows[index], SW_RESTORE);
        if (!SetWindowPos(list.windows[index], NULL, left, top,
                          max(1, right - left), max(1, bottom - top),
                          SWP_NOACTIVATE | SWP_NOZORDER)) {
            return FALSE;
        }
    }
    if (selectedPeer != NULL && sideBySide) {
        *selectedPeer = list.windows[1];
    }
    SetForegroundWindow(app->mainWindow);
    return TRUE;
}

static void ribbon_show_window_menu(AppState *app, HWND source)
{
    RibbonWindowList list;
    HMENU menu;
    RECT sourceRect;
    POINT point;
    UINT selected;
    UINT index;

    ribbon_collect_windows(&list);
    menu = CreatePopupMenu();
    if (menu == NULL) {
        return;
    }
    for (index = 0; index < list.count; ++index) {
        WCHAR title[128];
        if (GetWindowTextW(list.windows[index], title,
                           ARRAYSIZE(title)) <= 0) {
            StringCchCopyW(title, ARRAYSIZE(title), APP_NAME);
        }
        AppendMenuW(
            menu, MF_STRING |
                      (list.windows[index] == app->mainWindow
                           ? MF_CHECKED : 0),
            0x7600u + index, title);
    }
    if (list.count == 0) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"No windows");
    }
    if (source != NULL && GetWindowRect(source, &sourceRect)) {
        point.x = sourceRect.left;
        point.y = sourceRect.bottom;
    } else {
        GetCursorPos(&point);
    }
    selected = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN |
                  TPM_RIGHTBUTTON,
        point.x, point.y, 0, app->mainWindow, NULL);
    DestroyMenu(menu);
    if (selected >= 0x7600u &&
        selected - 0x7600u < list.count) {
        HWND target = list.windows[selected - 0x7600u];
        ShowWindow(target, SW_RESTORE);
        SetForegroundWindow(target);
    }
}

static void ribbon_show_document_properties(AppState *app)
{
    WCHAR message[768];
    const WCHAR *path;

    if (app == NULL) {
        return;
    }
    app_update_status(app, TRUE);
    path = app->currentPath[0] != L'\0'
               ? app->currentPath : L"Not saved yet";
    StringCchPrintfW(
        message, ARRAYSIZE(message),
        L"Document: %s\n\nPages: %ld\nWords: %ld\nZoom: %d%%\n"
        L"Paper: %.2f \x00D7 %.2f inches\nFormat: %s",
        path, app->pageCount, app->cachedWordCount, app->zoomPercent,
        app->pageSize.x / 1000.0, app->pageSize.y / 1000.0,
        app->currentIsRtf ? L"Rich Text Format" : L"Plain text");
    MessageBoxW(app->mainWindow, message, L"Document Properties",
                MB_OK | MB_ICONINFORMATION);
}

static BOOL ribbon_execute_view_command(AppState *app, UINT command,
                                        HWND source)
{
    RibbonContext *ribbon;
    int zoom;

    if (!ribbon_valid_context(app)) {
        return FALSE;
    }
    if (command == IDM_VIEW_DARK_MODE) {
        return FALSE;
    }
    if (ribbon_view_zoom_percent(command, &zoom)) {
        format_set_zoom(app, zoom);
        return TRUE;
    }
    if (command < IDM_VIEW_READ_MODE ||
        command > IDM_VIEW_PROPERTIES) {
        return FALSE;
    }
    ribbon = app->ribbon;
    switch (command) {
    case IDM_VIEW_READ_MODE:
        ribbon->activeViewMode = VIEW_MODE_READ;
        format_set_zoom(app, ribbon_view_fit_zoom(app, FALSE, FALSE));
        app_set_status_message(
            app, L"Read Mode fitted the paged document for reading");
        break;
    case IDM_VIEW_PRINT_LAYOUT:
        ribbon->activeViewMode = VIEW_MODE_PRINT_LAYOUT;
        app_set_status_message(app, L"Print Layout active");
        break;
    case IDM_VIEW_WEB_LAYOUT:
        MessageBeep(MB_ICONINFORMATION);
        app_set_status_message(
            app, L"Web Layout requires a continuous web-layout engine");
        break;
    case IDM_VIEW_OUTLINE:
        MessageBeep(MB_ICONINFORMATION);
        app_set_status_message(
            app, L"Outline requires a structured heading-outline model");
        break;
    case IDM_VIEW_DRAFT:
        MessageBeep(MB_ICONINFORMATION);
        app_set_status_message(
            app, L"Draft requires a non-paged document layout engine");
        break;
    case IDM_VIEW_FOCUS:
        app->focusMode = !app->focusMode;
        ribbon_sync_view_checkmarks(ribbon);
        app_layout(app);
        SetFocus(app->editor);
        return TRUE;
    case IDM_VIEW_IMMERSIVE_READER:
        MessageBoxW(
            app->mainWindow,
            L"Immersive Reader needs a dedicated reading-assistance "
            L"engine and is not available yet. Read Mode and Focus remain "
            L"available independently.",
            L"Immersive Reader", MB_OK | MB_ICONINFORMATION);
        break;
    case IDM_VIEW_VERTICAL:
        ribbon->viewMovementVertical = TRUE;
        app_set_status_message(app, L"Vertical page movement active");
        break;
    case IDM_VIEW_SIDE_TO_SIDE:
        MessageBeep(MB_ICONINFORMATION);
        app_set_status_message(
            app,
            L"Side to Side requires a horizontal page-layout engine");
        break;
    case IDM_VIEW_RULER:
        app->viewRulerVisible = !app->viewRulerVisible;
        InvalidateRect(app->editor, NULL, FALSE);
        app_set_status_message(
            app, app->viewRulerVisible
                     ? L"Nonprinting page ruler shown"
                     : L"Page ruler hidden");
        break;
    case IDM_VIEW_GRIDLINES:
        app->viewGridlinesVisible = !app->viewGridlinesVisible;
        InvalidateRect(app->editor, NULL, FALSE);
        app_set_status_message(
            app, app->viewGridlinesVisible
                     ? L"Nonprinting page gridlines shown"
                     : L"Page gridlines hidden");
        break;
    case IDM_VIEW_NAVIGATION_PANE:
        if (app->findDialog != NULL && IsWindow(app->findDialog)) {
            SendMessageW(app->findDialog, WM_CLOSE, 0, 0);
        } else {
            dialogs_show_find(app, FALSE);
        }
        break;
    case IDM_VIEW_ZOOM_DIALOG:
        ribbon_show_zoom_menu(app, source);
        return TRUE;
    case IDM_VIEW_ONE_PAGE:
        format_set_zoom(app, ribbon_view_fit_zoom(app, TRUE, FALSE));
        break;
    case IDM_VIEW_MULTIPLE_PAGES:
        format_set_zoom(app, ribbon_view_fit_zoom(app, TRUE, TRUE));
        break;
    case IDM_VIEW_PAGE_WIDTH:
        format_set_zoom(app, ribbon_view_fit_zoom(app, FALSE, FALSE));
        break;
    case IDM_VIEW_NEW_WINDOW:
        if (!ribbon_start_new_window(app)) {
            app_show_error(app->mainWindow,
                           L"A new WordCraft window could not be opened.",
                           GetLastError());
        } else if (!app->modified && app->currentPath[0] != L'\0') {
            app_set_status_message(
                app, L"Document reopened in another WordCraft process");
        } else {
            app_set_status_message(
                app,
                L"New blank WordCraft window opened; save this document "
                L"before reopening it there");
        }
        break;
    case IDM_VIEW_ARRANGE_ALL:
        ribbon->viewSideBySide = FALSE;
        ribbon->viewSideBySidePeer = NULL;
        if (!ribbon_arrange_windows(app, FALSE, NULL, NULL)) {
            app_set_status_message(app, L"No WordCraft windows to arrange");
        } else {
            app_set_status_message(app, L"WordCraft windows arranged");
        }
        break;
    case IDM_VIEW_SPLIT:
        MessageBeep(MB_ICONINFORMATION);
        app_set_status_message(
            app, L"Split needs a second editable viewport and is not available yet");
        break;
    case IDM_VIEW_SIDE_BY_SIDE: {
        HWND peer = NULL;
        if (ribbon_arrange_windows(app, TRUE, NULL, &peer)) {
            ribbon->viewSideBySide = TRUE;
            ribbon->viewSideBySidePeer = peer;
            app_set_status_message(
                app, L"WordCraft windows arranged side by side");
        } else {
            ribbon->viewSideBySide = FALSE;
            ribbon->viewSideBySidePeer = NULL;
            app_set_status_message(
                app, L"Open another WordCraft window to view side by side");
        }
        break;
    }
    case IDM_VIEW_SYNCHRONOUS_SCROLLING:
        MessageBeep(MB_ICONINFORMATION);
        app_set_status_message(
            app,
            L"Synchronous scrolling is not available across separate windows");
        break;
    case IDM_VIEW_RESET_WINDOW_POSITION: {
        HWND peer = NULL;
        if (!ribbon->viewSideBySide ||
            !ribbon_arrange_windows(app, TRUE,
                                    ribbon->viewSideBySidePeer, &peer)) {
            ribbon->viewSideBySide = FALSE;
            ribbon->viewSideBySidePeer = NULL;
        } else {
            ribbon->viewSideBySidePeer = peer;
        }
        break;
    }
    case IDM_VIEW_SWITCH_WINDOWS:
        ribbon_show_window_menu(app, source);
        return TRUE;
    case IDM_VIEW_MACROS:
        MessageBoxW(
            app->mainWindow,
            L"Macros require a trusted execution and permission model. "
            L"WordCraft does not execute document macros.",
            L"Macros", MB_OK | MB_ICONINFORMATION);
        break;
    case IDM_VIEW_PROPERTIES:
        ribbon_show_document_properties(app);
        break;
    default:
        return FALSE;
    }
    ribbon_sync_view_checkmarks(ribbon);
    app_update_command_ui(app);
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
    if (ribbon_execute_design_command(
            app, LOWORD(wParam), (HWND)lParam)) {
        return TRUE;
    }
    if (ribbon_execute_draw_command(
            app, LOWORD(wParam), (HWND)lParam)) {
        return TRUE;
    }
    if (ribbon_execute_view_command(
            app, LOWORD(wParam), (HWND)lParam)) {
        return TRUE;
    }
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
    if (ribbon->viewSideBySide &&
        (ribbon->viewSideBySidePeer == NULL ||
         ribbon->viewSideBySidePeer == app->mainWindow ||
         !IsWindow(ribbon->viewSideBySidePeer))) {
        ribbon->viewSideBySide = FALSE;
        ribbon->viewSideBySidePeer = NULL;
    }

    SendMessageW(ribbon->spellCheckButton, BM_SETCHECK,
                 app->spellCheckEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(ribbon->autoCompleteButton, BM_SETCHECK,
                 app->autoCompleteEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(ribbon->darkModeButton, BM_SETCHECK,
                 app->darkMode ? BST_CHECKED : BST_UNCHECKED, 0);
    EnableWindow(ribbon->addCommentButton, app->editor != NULL);
    EnableWindow(ribbon->previousCommentButton, commentCount > 0);
    EnableWindow(ribbon->nextCommentButton, commentCount > 0);
    EnableWindow(ribbon->deleteCommentButton, commentCount > 0);
    EnableWindow(ribbon->liveShareButton, app->editor != NULL);
    EnableWindow(ribbon->documentChatButton, app->history != NULL);
    EnableWindow(ribbon->versionHistoryButton,
                 app->history != NULL && history_version_count(app) > 0);
    EnableWindow(ribbon->clipboardButtons[0], canPaste);
    EnableWindow(ribbon->clipboardButtons[1], hasSelection);
    EnableWindow(ribbon->clipboardButtons[2], hasSelection);
    EnableWindow(ribbon->editingButtons[0], app->editor != NULL);
    EnableWindow(ribbon->editingButtons[1], app->editor != NULL);
    EnableWindow(ribbon->editingButtons[2], app->editor != NULL);
    for (index = 0; index < ribbon->insertControlCount; ++index) {
        EnableWindow(ribbon->insertControls[index].window,
                     app->editor != NULL);
    }
    for (index = 0; index < ribbon->drawControlCount; ++index) {
        BOOL enabled = app->editor != NULL;
        if (ribbon->drawControls[index].id == IDM_EDIT_UNDO) {
            enabled = canUndo;
        } else if (ribbon->drawControls[index].id == IDM_EDIT_REDO) {
            enabled = canRedo;
        }
        EnableWindow(ribbon->drawControls[index].window, enabled);
    }
    for (index = 0; index < ribbon->viewControlCount; ++index) {
        UINT id = ribbon->viewControls[index].id;
        BOOL enabled = app->editor != NULL;

        if (id == IDM_VIEW_SYNCHRONOUS_SCROLLING) {
            enabled = FALSE;
        } else if (id == IDM_VIEW_RESET_WINDOW_POSITION) {
            enabled = ribbon->viewSideBySide;
        }
        EnableWindow(ribbon->viewControls[index].window, enabled);
    }
    for (index = 0; index < ribbon->designControlCount; ++index) {
        EnableWindow(ribbon->designControls[index].window,
                     app->editor != NULL);
    }
    ribbon_sync_draw_checkmarks(ribbon);
    ribbon_sync_view_checkmarks(ribbon);
    ribbon_sync_design_checkmarks(ribbon);
    ribbon_sync_paper_size(app);
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
    case WCQ_INSERT_GROUP_COUNT:
        return INSERT_GROUP_COUNT;
    case WCQ_INSERT_GROUP_NAME_HASH:
        if (index < 0 || index >= INSERT_GROUP_COUNT) {
            return 0;
        }
        return (LRESULT)ribbon_name_hash(insertGroupNames[index]);
    case WCQ_INSERT_GROUP_FLAGS:
        if (index < 0 || index >= INSERT_GROUP_COUNT) {
            return 0;
        }
        return (ribbon->activeTab == RIBBON_TAB_INSERT
                    ? INSERT_GROUP_FLAG_VISIBLE
                    : 0) |
               (ribbon->activeTab == RIBBON_TAB_INSERT &&
                        ribbon->insertLayoutMode != RIBBON_LAYOUT_COLLAPSED
                    ? INSERT_GROUP_FLAG_LABEL_VISIBLE
                    : 0) |
               (ribbon->insertLayoutMode == RIBBON_LAYOUT_COLLAPSED
                    ? INSERT_GROUP_FLAG_COLLAPSED
                    : 0);
    case WCQ_INSERT_GROUP_RECT_COMPONENT: {
        int group = (int)(index / 4);
        int component = (int)(index % 4);
        const RECT *rect;
        if (index < 0 || group < 0 || group >= INSERT_GROUP_COUNT) {
            return 0;
        }
        rect = &ribbon->insertGroupRects[group];
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
    case WCQ_INSERT_GROUP_PAINT_COUNT:
        return ribbon->insertGroupPaintCount;
    case WCQ_INSERT_CONTROL_COUNT:
        return (LRESULT)ribbon->insertControlCount;
    case WCQ_INSERT_CONTROL_ID:
        if (index < 0 ||
            (size_t)index >= ribbon->insertControlCount) {
            return 0;
        }
        return ribbon->insertControls[index].id;
    case WCQ_INSERT_CONTROL_GROUP:
        if (index < 0 ||
            (size_t)index >= ribbon->insertControlCount) {
            return -1;
        }
        return ribbon->insertControls[index].group;
    case WCQ_INSERT_CONTROL_FLAGS:
        if (index < 0 ||
            (size_t)index >= ribbon->insertControlCount) {
            return 0;
        } else {
            HWND control = ribbon->insertControls[index].window;
            LONG_PTR style = GetWindowLongPtrW(control, GWL_STYLE);
            return INSERT_CONTROL_FLAG_CREATED |
                   ((style & WS_VISIBLE) != 0
                        ? INSERT_CONTROL_FLAG_VISIBLE
                        : 0) |
                   (IsWindowEnabled(control)
                        ? INSERT_CONTROL_FLAG_ENABLED
                        : 0) |
                   ((style & WS_TABSTOP) != 0
                        ? INSERT_CONTROL_FLAG_TABSTOP
                        : 0) |
                   (ribbon->insertControls[index].icon !=
                            RIBBON_INSERT_ICON_NONE
                        ? INSERT_CONTROL_FLAG_HAS_ICON
                        : 0);
        }
    case WCQ_INSERT_CONTROL_ICON:
        if (index < 0 ||
            (size_t)index >= ribbon->insertControlCount) {
            return RIBBON_INSERT_ICON_NONE;
        }
        return ribbon->insertControls[index].icon;
    case WCQ_INSERT_ICON_PAINT_COUNT:
        if (index < 0 ||
            (size_t)index >= ribbon->insertControlCount) {
            return 0;
        }
        return ribbon->insertControls[index].iconPaintCount;
    case WCQ_INSERT_LAYOUT_MODE:
        return ribbon->insertLayoutMode;
    case WCQ_DRAW_GROUP_COUNT:
        return DRAW_GROUP_COUNT;
    case WCQ_DRAW_GROUP_NAME_HASH:
        if (index < 0 || index >= DRAW_GROUP_COUNT) {
            return 0;
        }
        return (LRESULT)ribbon_name_hash(drawGroupNames[index]);
    case WCQ_DRAW_GROUP_FLAGS:
        if (index < 0 || index >= DRAW_GROUP_COUNT) {
            return 0;
        }
        return (ribbon->activeTab == RIBBON_TAB_DRAW
                    ? DRAW_GROUP_FLAG_VISIBLE
                    : 0) |
               (ribbon->activeTab == RIBBON_TAB_DRAW &&
                        ribbon->drawLayoutMode != RIBBON_LAYOUT_COLLAPSED
                    ? DRAW_GROUP_FLAG_LABEL_VISIBLE
                    : 0) |
               (ribbon->drawLayoutMode == RIBBON_LAYOUT_COLLAPSED
                    ? DRAW_GROUP_FLAG_COLLAPSED
                    : 0);
    case WCQ_DRAW_GROUP_RECT_COMPONENT: {
        int group = (int)(index / 4);
        int component = (int)(index % 4);
        const RECT *rect;
        if (index < 0 || group < 0 || group >= DRAW_GROUP_COUNT) {
            return 0;
        }
        rect = &ribbon->drawGroupRects[group];
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
    case WCQ_DRAW_GROUP_PAINT_COUNT:
        return ribbon->drawGroupPaintCount;
    case WCQ_DRAW_CONTROL_COUNT:
        return (LRESULT)ribbon->drawControlCount;
    case WCQ_DRAW_CONTROL_ID:
        if (index < 0 || (size_t)index >= ribbon->drawControlCount) {
            return 0;
        }
        return ribbon->drawControls[index].id;
    case WCQ_DRAW_CONTROL_GROUP:
        if (index < 0 || (size_t)index >= ribbon->drawControlCount) {
            return -1;
        }
        return ribbon->drawControls[index].group;
    case WCQ_DRAW_CONTROL_FLAGS:
        if (index < 0 || (size_t)index >= ribbon->drawControlCount) {
            return 0;
        } else {
            HWND control = ribbon->drawControls[index].window;
            LONG_PTR style = GetWindowLongPtrW(control, GWL_STYLE);
            return DRAW_CONTROL_FLAG_CREATED |
                   ((style & WS_VISIBLE) != 0
                        ? DRAW_CONTROL_FLAG_VISIBLE
                        : 0) |
                   (IsWindowEnabled(control)
                        ? DRAW_CONTROL_FLAG_ENABLED
                        : 0) |
                   ((style & WS_TABSTOP) != 0
                        ? DRAW_CONTROL_FLAG_TABSTOP
                        : 0) |
                   (ribbon->drawControls[index].icon !=
                            RIBBON_DRAW_ICON_NONE
                        ? DRAW_CONTROL_FLAG_HAS_ICON
                        : 0) |
                   (SendMessageW(control, BM_GETCHECK, 0, 0) ==
                            BST_CHECKED
                        ? DRAW_CONTROL_FLAG_CHECKED
                        : 0);
        }
    case WCQ_DRAW_CONTROL_ICON:
        if (index < 0 || (size_t)index >= ribbon->drawControlCount) {
            return RIBBON_DRAW_ICON_NONE;
        }
        return ribbon->drawControls[index].icon;
    case WCQ_DRAW_ICON_PAINT_COUNT:
        if (index < 0 || (size_t)index >= ribbon->drawControlCount) {
            return 0;
        }
        return ribbon->drawControls[index].iconPaintCount;
    case WCQ_DRAW_LAYOUT_MODE:
        return ribbon->drawLayoutMode;
    case WCQ_DRAW_ACTIVE_TOOL:
        return ribbon->activeDrawTool;
    case WCQ_DRAW_RULER_VISIBLE:
        return ribbon->drawRulerVisible;
    case WCQ_DRAW_BACKGROUND_RULED:
        return ribbon->drawBackgroundRuled;
    case WCQ_VIEW_GROUP_COUNT:
        return VIEW_GROUP_COUNT;
    case WCQ_VIEW_GROUP_NAME_HASH:
        if (index < 0 || index >= VIEW_GROUP_COUNT) {
            return 0;
        }
        return (LRESULT)ribbon_name_hash(viewGroupNames[index]);
    case WCQ_VIEW_GROUP_FLAGS:
        if (index < 0 || index >= VIEW_GROUP_COUNT) {
            return 0;
        }
        return (ribbon->activeTab == RIBBON_TAB_VIEW
                    ? VIEW_GROUP_FLAG_VISIBLE
                    : 0) |
               (ribbon->activeTab == RIBBON_TAB_VIEW &&
                        ribbon->viewLayoutMode != RIBBON_LAYOUT_COLLAPSED
                    ? VIEW_GROUP_FLAG_LABEL_VISIBLE
                    : 0) |
               (ribbon->viewLayoutMode == RIBBON_LAYOUT_COLLAPSED
                    ? VIEW_GROUP_FLAG_COLLAPSED
                    : 0);
    case WCQ_VIEW_GROUP_RECT_COMPONENT: {
        int group = (int)(index / 4);
        int component = (int)(index % 4);
        const RECT *rect;
        if (index < 0 || group < 0 || group >= VIEW_GROUP_COUNT) {
            return 0;
        }
        rect = &ribbon->viewGroupRects[group];
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
    case WCQ_VIEW_GROUP_PAINT_COUNT:
        return ribbon->viewGroupPaintCount;
    case WCQ_VIEW_CONTROL_COUNT:
        return (LRESULT)ribbon->viewControlCount;
    case WCQ_VIEW_CONTROL_ID:
        if (index < 0 || (size_t)index >= ribbon->viewControlCount) {
            return 0;
        }
        return ribbon->viewControls[index].id;
    case WCQ_VIEW_CONTROL_GROUP:
        if (index < 0 || (size_t)index >= ribbon->viewControlCount) {
            return -1;
        }
        return ribbon->viewControls[index].group;
    case WCQ_VIEW_CONTROL_FLAGS:
        if (index < 0 || (size_t)index >= ribbon->viewControlCount) {
            return 0;
        } else {
            HWND control = ribbon->viewControls[index].window;
            LONG_PTR style = GetWindowLongPtrW(control, GWL_STYLE);
            return VIEW_CONTROL_FLAG_CREATED |
                   ((style & WS_VISIBLE) != 0
                        ? VIEW_CONTROL_FLAG_VISIBLE
                        : 0) |
                   (IsWindowEnabled(control)
                        ? VIEW_CONTROL_FLAG_ENABLED
                        : 0) |
                   ((style & WS_TABSTOP) != 0
                        ? VIEW_CONTROL_FLAG_TABSTOP
                        : 0) |
                   (ribbon->viewControls[index].icon !=
                            RIBBON_VIEW_ICON_NONE
                        ? VIEW_CONTROL_FLAG_HAS_ICON
                        : 0) |
                   (SendMessageW(control, BM_GETCHECK, 0, 0) ==
                            BST_CHECKED
                        ? VIEW_CONTROL_FLAG_CHECKED
                        : 0);
        }
    case WCQ_VIEW_CONTROL_ICON:
        if (index < 0 || (size_t)index >= ribbon->viewControlCount) {
            return RIBBON_VIEW_ICON_NONE;
        }
        return ribbon->viewControls[index].icon;
    case WCQ_VIEW_ICON_PAINT_COUNT:
        if (index < 0 || (size_t)index >= ribbon->viewControlCount) {
            return 0;
        }
        return ribbon->viewControls[index].iconPaintCount;
    case WCQ_VIEW_LAYOUT_MODE:
        return ribbon->viewLayoutMode;
    case WCQ_VIEW_ACTIVE_MODE:
        return ribbon->activeViewMode;
    case WCQ_VIEW_MOVEMENT_VERTICAL:
        return ribbon->viewMovementVertical;
    case WCQ_VIEW_RULER_VISIBLE:
        return app->viewRulerVisible;
    case WCQ_VIEW_GRIDLINES_VISIBLE:
        return app->viewGridlinesVisible;
    case WCQ_VIEW_NAVIGATION_VISIBLE:
        return app->findDialog != NULL && IsWindow(app->findDialog);
    case WCQ_VIEW_SIDE_BY_SIDE:
        return ribbon->viewSideBySide;
    case WCQ_VIEW_FOCUS_MODE:
        return app->focusMode;
    case WCQ_DESIGN_GROUP_COUNT:
        return DESIGN_GROUP_COUNT;
    case WCQ_DESIGN_GROUP_NAME_HASH:
        if (index < 0 || index >= DESIGN_GROUP_COUNT) {
            return 0;
        }
        return (LRESULT)ribbon_name_hash(designGroupNames[index]);
    case WCQ_DESIGN_GROUP_FLAGS:
        if (index < 0 || index >= DESIGN_GROUP_COUNT) {
            return 0;
        }
        return (ribbon->activeTab == RIBBON_TAB_DESIGN
                    ? DESIGN_GROUP_FLAG_VISIBLE
                    : 0) |
               (ribbon->activeTab == RIBBON_TAB_DESIGN &&
                        ribbon->designLayoutMode !=
                            RIBBON_LAYOUT_COLLAPSED
                    ? DESIGN_GROUP_FLAG_LABEL_VISIBLE
                    : 0) |
               (ribbon->designLayoutMode == RIBBON_LAYOUT_COLLAPSED
                    ? DESIGN_GROUP_FLAG_COLLAPSED
                    : 0) |
               (index == DESIGN_GROUP_DOCUMENT_FORMATTING &&
                        ribbon->designGalleryVisible
                    ? DESIGN_GROUP_FLAG_GALLERY_OPEN
                    : 0);
    case WCQ_DESIGN_GROUP_RECT_COMPONENT: {
        int group;
        int component;
        const RECT *rect;

        if (index < 0) {
            return 0;
        }
        group = (int)(index / 4);
        component = (int)(index % 4);
        if (group < 0 || group >= DESIGN_GROUP_COUNT) {
            return 0;
        }
        rect = &ribbon->designGroupRects[group];
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
    case WCQ_DESIGN_GROUP_PAINT_COUNT:
        return ribbon->designGroupPaintCount;
    case WCQ_DESIGN_CONTROL_COUNT:
        return (LRESULT)ribbon->designControlCount;
    case WCQ_DESIGN_CONTROL_ID:
        if (index < 0 ||
            (size_t)index >= ribbon->designControlCount) {
            return 0;
        }
        return ribbon->designControls[index].id;
    case WCQ_DESIGN_CONTROL_GROUP:
        if (index < 0 ||
            (size_t)index >= ribbon->designControlCount) {
            return -1;
        }
        return ribbon->designControls[index].group;
    case WCQ_DESIGN_CONTROL_FLAGS:
        if (index < 0 ||
            (size_t)index >= ribbon->designControlCount) {
            return 0;
        } else {
            DesignControlInfo *control =
                &ribbon->designControls[index];
            LONG_PTR style =
                GetWindowLongPtrW(control->window, GWL_STYLE);
            return DESIGN_CONTROL_FLAG_CREATED |
                   ((style & WS_VISIBLE) != 0
                        ? DESIGN_CONTROL_FLAG_VISIBLE
                        : 0) |
                   (IsWindowEnabled(control->window)
                        ? DESIGN_CONTROL_FLAG_ENABLED
                        : 0) |
                   ((style & WS_TABSTOP) != 0
                        ? DESIGN_CONTROL_FLAG_TABSTOP
                        : 0) |
                   (control->icon != RIBBON_DESIGN_ICON_NONE
                        ? DESIGN_CONTROL_FLAG_HAS_ICON
                        : 0) |
                   (SendMessageW(control->window, BM_GETCHECK,
                                 0, 0) == BST_CHECKED
                        ? DESIGN_CONTROL_FLAG_CHECKED
                        : 0) |
                   (control->stylePreview
                        ? DESIGN_CONTROL_FLAG_STYLE_PREVIEW
                        : 0);
        }
    case WCQ_DESIGN_CONTROL_ICON:
        if (index < 0 ||
            (size_t)index >= ribbon->designControlCount) {
            return RIBBON_DESIGN_ICON_NONE;
        }
        return ribbon->designControls[index].icon;
    case WCQ_DESIGN_ICON_PAINT_COUNT:
        if (index < 0 ||
            (size_t)index >= ribbon->designControlCount) {
            return 0;
        }
        return ribbon->designControls[index].iconPaintCount;
    case WCQ_DESIGN_LAYOUT_MODE:
        return ribbon->designLayoutMode;
    case WCQ_DESIGN_ACTIVE_STYLE_SET:
        return app->designStyleSet;
    case WCQ_DESIGN_GALLERY_VISIBLE:
        return ribbon->designGalleryVisible &&
               IsWindow(ribbon->designGalleryWindow) &&
               IsWindowVisible(ribbon->designGalleryWindow);
    case WCQ_DESIGN_GALLERY_PAINT_COUNT:
        return ribbon->designGalleryPaintCount;
    case WCQ_DESIGN_GALLERY_ITEM_COUNT:
        return DESIGN_GALLERY_ITEM_COUNT;
    case WCQ_DESIGN_GALLERY_ITEM_RECT_COMPONENT: {
        int item;
        int component;
        const RECT *rect;

        if (index < 0) {
            return 0;
        }
        item = (int)(index / 4);
        component = (int)(index % 4);
        if (item < 0 || item >= DESIGN_GALLERY_ITEM_COUNT) {
            return 0;
        }
        rect = &ribbon->designGalleryItemRects[item];
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
    case WCQ_DESIGN_GALLERY_FOCUSED_INDEX:
        return ribbon->designGalleryVisible
                   ? ribbon->designGalleryFocusedIndex
                   : -1;
    case WCQ_DESIGN_COLOR_SCHEME:
        return app->designColorScheme;
    case WCQ_DESIGN_FONT_SCHEME:
        return app->designFontScheme;
    case WCQ_DESIGN_PARAGRAPH_SPACING:
        return app->designParagraphSpacing;
    case WCQ_DESIGN_EFFECT:
        return app->designEffect;
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
    if (IsWindow(ribbon->designGalleryWindow)) {
        DestroyWindow(ribbon->designGalleryWindow);
        ribbon->designGalleryWindow = NULL;
        ribbon->designGalleryVisible = FALSE;
    }
    if (IsWindow(ribbon->designTooltip)) {
        DestroyWindow(ribbon->designTooltip);
        ribbon->designTooltip = NULL;
    }
    if (IsWindow(ribbon->insertTooltip)) {
        DestroyWindow(ribbon->insertTooltip);
        ribbon->insertTooltip = NULL;
    }
    if (IsWindow(ribbon->drawTooltip)) {
        DestroyWindow(ribbon->drawTooltip);
        ribbon->drawTooltip = NULL;
    }
    if (IsWindow(ribbon->viewTooltip)) {
        DestroyWindow(ribbon->viewTooltip);
        ribbon->viewTooltip = NULL;
    }
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
