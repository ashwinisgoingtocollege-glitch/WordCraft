#ifndef WORDCRAFT_EDITOR_H
#define WORDCRAFT_EDITOR_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef _RICHEDIT_VER
#define _RICHEDIT_VER 0x0500
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0500
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <shellapi.h>
#include <strsafe.h>
#include <wchar.h>
#include <wctype.h>

#include "resource.h"
#include "fonts.h"
#include "paper.h"

#define APP_CLASS_NAME L"WordCraftMainWindow"
#define APP_NAME L"WordCraft"
#define WORDCRAFT_DEFAULT_FONT_FACE L"Times New Roman"
#define WORDCRAFT_DEFAULT_FONT_SIZE_TWIPS 240
#define WORDCRAFT_DEFAULT_LINE_SPACING_RULE 5
#define WORDCRAFT_DEFAULT_LINE_SPACING 22
#define WORDCRAFT_DEFAULT_PARAGRAPH_SPACE_AFTER_TWIPS 120
#define WORDCRAFT_DEFAULT_PAGE_MARGIN_THOUSANDTHS 1000
#define WORDCRAFT_DEFAULT_PAGE_WIDTH_THOUSANDTHS 8500
#define WORDCRAFT_DEFAULT_PAGE_HEIGHT_THOUSANDTHS 11000
#define WORDCRAFT_COMMENT_HIGHLIGHT_COLOR RGB(255, 242, 153)
#define PATH_CAPACITY 32768
#define FIND_CAPACITY 256
#define COMMENT_TEXT_CAPACITY 2048
#define STATUS_TIMER_ID 1
#define SPELL_TIMER_ID 2
#define COMPLETION_TIMER_ID 3
#define LIVE_SHARE_TIMER_ID 4
#define HISTORY_TIMER_ID 5
#define HISTORY_CHAT_RETENTION_LIMIT 2048u
#define WCM_QUERY_STATE (WM_APP + 40)
#define WCM_ASSIST_RESULT (WM_APP + 41)
#define WCM_RENDERER_CHANGED (WM_APP + 42)
#define WCM_LIVE_EVENT (WM_APP + 43)

#define WCQ_PAGE_COUNT 1
#define WCQ_CURRENT_PAGE 2
#define WCQ_DARK_MODE 3
#define WCQ_PAGE_START 4
#define WCQ_SPELL_ERROR_COUNT 5
#define WCQ_COMPLETION_VISIBLE 6
#define WCQ_COMPLETION_LENGTH 7
#define WCQ_ASSIST_WORKER_RUNNING 8
#define WCQ_SPELL_RESULT_READY 9
#define WCQ_FIRST_VISIBLE_PAGE 10
#define WCQ_LAST_VISIBLE_PAGE 11
#define WCQ_VISIBLE_PAGE_COUNT 12
#define WCQ_VIEW_SCROLL_Y 13
#define WCQ_VIEW_SCROLL_MAX 14
#define WCQ_FULLY_VISIBLE_PAGE_COUNT 15
#define WCQ_SCROLL_ANIMATING 16
#define WCQ_SCROLL_TARGET_Y 17
#define WCQ_SCROLL_FRAME_COUNT 18
#define WCQ_SCROLL_FRAME_INTERVAL_MS 19
#define WCQ_RIBBON_TAB_COUNT 20
#define WCQ_RIBBON_ACTIVE_TAB 21
#define WCQ_RIBBON_TAB_NAME_HASH 22
#define WCQ_RIBBON_VISIBLE_PANEL 23
#define WCQ_RIBBON_PANEL_VISIBLE 24
#define WCQ_RIBBON_FOCUS_AREA 25
#define WCQ_COMMENT_COUNT 26
#define WCQ_COMMENT_ACTIVE_INDEX 27
#define WCQ_COMMENT_ANCHOR_START 28
#define WCQ_COMMENT_ANCHOR_END 29
#define WCQ_COMMENT_TEXT_HASH 30
#define WCQ_TEXT_ENGINE_ENABLED 31
#define WCQ_TEXT_ENGINE_BACKEND 32
#define WCQ_TEXT_ENGINE_TYPOGRAPHY_OPTIONS 33
#define WCQ_TEXT_ENGINE_LINE_SPACING_RULE 34
#define WCQ_TEXT_ENGINE_LINE_SPACING 35
#define WCQ_TEXT_ENGINE_PARAGRAPH_SPACE_AFTER 36
#define WCQ_TEXT_ENGINE_LAYOUT_GENERATION 37
#define WCQ_RENDER_ENGINE_WINDOWLESS 38
#define WCQ_RENDER_ENGINE_BACKEND 39
#define WCQ_RENDER_ENGINE_D2D_CAPABLE 40
#define WCQ_RENDER_ENGINE_PROPERTY_BITS 41
#define WCQ_RENDER_ENGINE_D2D_DRAW_COUNT 42
#define WCQ_RENDER_ENGINE_GDI_DRAW_COUNT 43
#define WCQ_RENDER_ENGINE_LAST_DRAW_PATH 44
#define WCQ_RENDER_ENGINE_LAST_DRAW_RESULT 45
#define WCQ_RENDER_ENGINE_FALLBACK_REASON 46
#define WCQ_RENDER_ENGINE_TARGET_GENERATION 47
#define WCQ_RENDER_ENGINE_SELECTION_MESSAGE_COUNT 48
#define WCQ_RENDER_ENGINE_LAST_SELECTION_START 49
#define WCQ_RENDER_ENGINE_LAST_SELECTION_RESULT 50
#define WCQ_RENDER_ENGINE_LAST_SELECTION_PAGE 51
#define WCQ_COMMENT_MARGIN_VISIBLE 52
#define WCQ_COMMENT_CARD_COUNT 53
#define WCQ_COMMENT_MARGIN_ACTIVE_INDEX 54
#define WCQ_COMMENT_ACTIVE_CARD_VISIBLE 55
#define WCQ_COMMENT_MARGIN_LEFT 56
#define WCQ_COMMENT_MARGIN_WIDTH 57
#define WCQ_COMMENT_ACTIVE_CARD_LEFT 58
#define WCQ_COMMENT_ACTIVE_CARD_TOP 59
#define WCQ_COMMENT_ACTIVE_CARD_RIGHT 60
#define WCQ_COMMENT_ACTIVE_CARD_BOTTOM 61
#define WCQ_PAGE_MARGIN_THOUSANDTHS 62
#define WCQ_PAGE_SIZE_THOUSANDTHS 63
#define WCQ_PAGE_LAYOUT_MARGIN_PIXELS 64
#define WCQ_PAGE_LAYOUT_SIZE_PIXELS 65
#define WCQ_COMMENT_HIGHLIGHT_VISIBLE 66
#define WCQ_COMMENT_HIGHLIGHT_START 67
#define WCQ_COMMENT_HIGHLIGHT_END 68
#define WCQ_COMMENT_HIGHLIGHT_COLOR 69
#define WCQ_COMMENT_COMPOSITION_ACTIVE 70
#define WCQ_PAPER_SIZE_ID 71
#define WCQ_PAPER_SIZE_COUNT 72
#define WCQ_PAPER_PRESET_DEVICE_ID 73
#define WCQ_PAPER_PRESET_WIDTH 74
#define WCQ_PAPER_PRESET_HEIGHT 75
#define WCQ_PAPER_PRESET_NAME_HASH 76
#define WCQ_UI_STYLE_FLAGS 77
#define WCQ_UI_PANEL_RADIUS_PIXELS 78
#define WCQ_UI_CONTROL_RADIUS_PIXELS 79
#define WCQ_UI_TAB_RADIUS_PIXELS 80
#define WCQ_UI_PANEL_PAINT_COUNT 81
#define WCQ_UI_CONTROL_PAINT_COUNT 82
#define WCQ_UI_TAB_PAINT_COUNT 83
#define WCQ_UI_LOGO_CURVE_COUNT 84
#define WCQ_UI_LOGO_CURVE_PAINT_COUNT 85
#define WCQ_UI_PANEL_CURVE_COUNT 86
#define WCQ_HOME_GROUP_COUNT 87
#define WCQ_HOME_GROUP_NAME_HASH 88
#define WCQ_HOME_GROUP_FLAGS 89
#define WCQ_HOME_GROUP_RECT_COMPONENT 90
#define WCQ_HOME_GROUP_PAINT_COUNT 91
#define WCQ_HOME_CONTROL_COUNT 92
#define WCQ_HOME_CONTROL_ID 93
#define WCQ_HOME_CONTROL_GROUP 94
#define WCQ_HOME_CONTROL_FLAGS 95
#define WCQ_RIBBON_LAYOUT_MODE 96
#define WCQ_RIBBON_LAYOUT_GENERATION 97
#define WCQ_RIBBON_FOCUSED_CONTROL_ID 98
#define WCQ_LIVE_ROLE 99
#define WCQ_LIVE_STATE 100
#define WCQ_LIVE_CLIENT_COUNT 101
#define WCQ_LIVE_LISTEN_PORT 102
#define WCQ_LIVE_REVISION_LOW 103
#define WCQ_LIVE_REVISION_HIGH 104
#define WCQ_LIVE_WORKER_RUNNING 105
#define WCQ_LIVE_LAST_ERROR 106
#define WCQ_LIVE_INBOUND_COUNT 107
#define WCQ_LIVE_OUTBOUND_COUNT 108
#define WCQ_LIVE_APPLIED_COUNT 109
#define WCQ_LIVE_APPLYING_REMOTE 110
#define WCQ_LIVE_PEER_ID 111
#define WCQ_LIVE_INVITATION_HASH 112
#define WCQ_LIVE_DOCUMENT_PENDING 113
#define WCQ_CHAT_COUNT 114
#define WCQ_CHAT_AUTHOR_HASH 115
#define WCQ_CHAT_TEXT_HASH 116
#define WCQ_VERSION_COUNT 117
#define WCQ_VERSION_AUTHOR_HASH 118
#define WCQ_VERSION_CHANGE_START 119
#define WCQ_VERSION_CHANGE_END 120
#define WCQ_VERSION_INSERTED_COUNT 121
#define WCQ_VERSION_DELETED_COUNT 122
#define WCQ_CHAT_DIALOG_VISIBLE 123
#define WCQ_VERSION_DIALOG_VISIBLE 124
#define WCQ_INSERT_GROUP_COUNT 125
#define WCQ_INSERT_GROUP_NAME_HASH 126
#define WCQ_INSERT_GROUP_FLAGS 127
#define WCQ_INSERT_GROUP_RECT_COMPONENT 128
#define WCQ_INSERT_GROUP_PAINT_COUNT 129
#define WCQ_INSERT_CONTROL_COUNT 130
#define WCQ_INSERT_CONTROL_ID 131
#define WCQ_INSERT_CONTROL_GROUP 132
#define WCQ_INSERT_CONTROL_FLAGS 133
#define WCQ_INSERT_CONTROL_ICON 134
#define WCQ_INSERT_ICON_PAINT_COUNT 135
#define WCQ_INSERT_LAYOUT_MODE 136
#define WCQ_DRAW_GROUP_COUNT 137
#define WCQ_DRAW_GROUP_NAME_HASH 138
#define WCQ_DRAW_GROUP_FLAGS 139
#define WCQ_DRAW_GROUP_RECT_COMPONENT 140
#define WCQ_DRAW_GROUP_PAINT_COUNT 141
#define WCQ_DRAW_CONTROL_COUNT 142
#define WCQ_DRAW_CONTROL_ID 143
#define WCQ_DRAW_CONTROL_GROUP 144
#define WCQ_DRAW_CONTROL_FLAGS 145
#define WCQ_DRAW_CONTROL_ICON 146
#define WCQ_DRAW_ICON_PAINT_COUNT 147
#define WCQ_DRAW_LAYOUT_MODE 148
#define WCQ_DRAW_ACTIVE_TOOL 149
#define WCQ_DRAW_RULER_VISIBLE 150
#define WCQ_DRAW_BACKGROUND_RULED 151
#define WCQ_VIEW_GROUP_COUNT 152
#define WCQ_VIEW_GROUP_NAME_HASH 153
#define WCQ_VIEW_GROUP_FLAGS 154
#define WCQ_VIEW_GROUP_RECT_COMPONENT 155
#define WCQ_VIEW_GROUP_PAINT_COUNT 156
#define WCQ_VIEW_CONTROL_COUNT 157
#define WCQ_VIEW_CONTROL_ID 158
#define WCQ_VIEW_CONTROL_GROUP 159
#define WCQ_VIEW_CONTROL_FLAGS 160
#define WCQ_VIEW_CONTROL_ICON 161
#define WCQ_VIEW_ICON_PAINT_COUNT 162
#define WCQ_VIEW_LAYOUT_MODE 163
#define WCQ_VIEW_ACTIVE_MODE 164
#define WCQ_VIEW_MOVEMENT_VERTICAL 165
#define WCQ_VIEW_RULER_VISIBLE 166
#define WCQ_VIEW_GRIDLINES_VISIBLE 167
#define WCQ_VIEW_NAVIGATION_VISIBLE 168
#define WCQ_VIEW_SIDE_BY_SIDE 169
#define WCQ_VIEW_FOCUS_MODE 170
#define WCQ_DESIGN_GROUP_COUNT 171
#define WCQ_DESIGN_GROUP_NAME_HASH 172
#define WCQ_DESIGN_GROUP_FLAGS 173
#define WCQ_DESIGN_GROUP_RECT_COMPONENT 174
#define WCQ_DESIGN_GROUP_PAINT_COUNT 175
#define WCQ_DESIGN_CONTROL_COUNT 176
#define WCQ_DESIGN_CONTROL_ID 177
#define WCQ_DESIGN_CONTROL_GROUP 178
#define WCQ_DESIGN_CONTROL_FLAGS 179
#define WCQ_DESIGN_CONTROL_ICON 180
#define WCQ_DESIGN_ICON_PAINT_COUNT 181
#define WCQ_DESIGN_LAYOUT_MODE 182
#define WCQ_DESIGN_ACTIVE_STYLE_SET 183
#define WCQ_DESIGN_GALLERY_VISIBLE 184
#define WCQ_DESIGN_GALLERY_PAINT_COUNT 185
#define WCQ_DESIGN_GALLERY_ITEM_COUNT 186
#define WCQ_DESIGN_GALLERY_ITEM_RECT_COMPONENT 187
#define WCQ_DESIGN_GALLERY_FOCUSED_INDEX 188
#define WCQ_DESIGN_COLOR_SCHEME 189
#define WCQ_DESIGN_FONT_SCHEME 190
#define WCQ_DESIGN_PARAGRAPH_SPACING 191
#define WCQ_DESIGN_EFFECT 192

#define WORDCRAFT_UI_STYLE_MINIMAL 0x0001u
#define WORDCRAFT_UI_STYLE_ROUNDED_PANEL 0x0002u
#define WORDCRAFT_UI_STYLE_ROUNDED_TABS 0x0004u
#define WORDCRAFT_UI_STYLE_ROUNDED_CONTROLS 0x0008u
#define WORDCRAFT_UI_STYLE_LOGO_CURVES 0x0010u

#define TEXT_ENGINE_BACKEND_NONE 0
#define TEXT_ENGINE_BACKEND_RICHEDIT_COMPATIBLE 1
#define TEXT_ENGINE_BACKEND_RICHEDIT_ADVANCED 2

#define RENDER_ENGINE_BACKEND_NONE 0
#define RENDER_ENGINE_BACKEND_GDI 1
#define RENDER_ENGINE_BACKEND_DIRECTWRITE 2

#define RENDER_DRAW_PATH_NONE 0
#define RENDER_DRAW_PATH_GDI 1
#define RENDER_DRAW_PATH_DIRECTWRITE 2

#define RENDER_FALLBACK_NONE 0
#define RENDER_FALLBACK_FORCED 1
#define RENDER_FALLBACK_NO_TEXT_SERVICES2 2
#define RENDER_FALLBACK_NO_D2D_FACTORY 3
#define RENDER_FALLBACK_DRAW_FAILURE 4

#define RIBBON_TAB_FILE 0
#define RIBBON_TAB_HOME 1
#define RIBBON_TAB_INSERT 2
#define RIBBON_TAB_DRAW 3
#define RIBBON_TAB_DESIGN 4
#define RIBBON_TAB_LAYOUT 5
#define RIBBON_TAB_REFERENCES 6
#define RIBBON_TAB_MAILINGS 7
#define RIBBON_TAB_REVIEW 8
#define RIBBON_TAB_VIEW 9
#define RIBBON_TAB_HELP 10
#define RIBBON_TAB_COUNT 11

#define RIBBON_FOCUS_OTHER 0
#define RIBBON_FOCUS_EDITOR 1
#define RIBBON_FOCUS_TABS 2
#define RIBBON_FOCUS_PANEL 3

#define HOME_GROUP_CLIPBOARD 0
#define HOME_GROUP_FONT 1
#define HOME_GROUP_PARAGRAPH 2
#define HOME_GROUP_STYLES 3
#define HOME_GROUP_EDITING 4
#define HOME_GROUP_COUNT 5

#define HOME_GROUP_FLAG_VISIBLE 0x0001u
#define HOME_GROUP_FLAG_LABEL_VISIBLE 0x0002u
#define HOME_GROUP_FLAG_STYLE_GALLERY 0x0004u
#define HOME_GROUP_FLAG_COLLAPSED 0x0008u

#define HOME_CONTROL_FLAG_CREATED 0x0001u
#define HOME_CONTROL_FLAG_VISIBLE 0x0002u
#define HOME_CONTROL_FLAG_ENABLED 0x0004u
#define HOME_CONTROL_FLAG_TABSTOP 0x0008u

#define INSERT_GROUP_PAGES 0
#define INSERT_GROUP_TABLES 1
#define INSERT_GROUP_ILLUSTRATIONS 2
#define INSERT_GROUP_MEDIA 3
#define INSERT_GROUP_LINKS 4
#define INSERT_GROUP_COMMENTS 5
#define INSERT_GROUP_HEADER_FOOTER 6
#define INSERT_GROUP_TEXT 7
#define INSERT_GROUP_SYMBOLS 8
#define INSERT_GROUP_ESIGNATURE 9
#define INSERT_GROUP_COUNT 10

#define INSERT_GROUP_FLAG_VISIBLE 0x0001u
#define INSERT_GROUP_FLAG_LABEL_VISIBLE 0x0002u
#define INSERT_GROUP_FLAG_COLLAPSED 0x0004u

#define INSERT_CONTROL_FLAG_CREATED 0x0001u
#define INSERT_CONTROL_FLAG_VISIBLE 0x0002u
#define INSERT_CONTROL_FLAG_ENABLED 0x0004u
#define INSERT_CONTROL_FLAG_TABSTOP 0x0008u
#define INSERT_CONTROL_FLAG_HAS_ICON 0x0010u

typedef enum RibbonInsertIcon {
    RIBBON_INSERT_ICON_NONE = 0,
    RIBBON_INSERT_ICON_COVER_PAGE,
    RIBBON_INSERT_ICON_BLANK_PAGE,
    RIBBON_INSERT_ICON_PAGE_BREAK,
    RIBBON_INSERT_ICON_TABLE,
    RIBBON_INSERT_ICON_PICTURES,
    RIBBON_INSERT_ICON_SHAPES,
    RIBBON_INSERT_ICON_ICONS,
    RIBBON_INSERT_ICON_3D_MODELS,
    RIBBON_INSERT_ICON_SMARTART,
    RIBBON_INSERT_ICON_CHART,
    RIBBON_INSERT_ICON_SCREENSHOT,
    RIBBON_INSERT_ICON_ONLINE_VIDEO,
    RIBBON_INSERT_ICON_LINK,
    RIBBON_INSERT_ICON_BOOKMARK,
    RIBBON_INSERT_ICON_CROSS_REFERENCE,
    RIBBON_INSERT_ICON_COMMENT,
    RIBBON_INSERT_ICON_HEADER,
    RIBBON_INSERT_ICON_FOOTER,
    RIBBON_INSERT_ICON_PAGE_NUMBER,
    RIBBON_INSERT_ICON_TEXT_BOX,
    RIBBON_INSERT_ICON_QUICK_PARTS,
    RIBBON_INSERT_ICON_WORDART,
    RIBBON_INSERT_ICON_DROP_CAP,
    RIBBON_INSERT_ICON_SIGNATURE_LINE,
    RIBBON_INSERT_ICON_DATETIME,
    RIBBON_INSERT_ICON_OBJECT,
    RIBBON_INSERT_ICON_EQUATION,
    RIBBON_INSERT_ICON_SYMBOL,
    RIBBON_INSERT_ICON_ESIGNATURE_FIELDS
} RibbonInsertIcon;

#define DRAW_GROUP_INPUT_MODE 0
#define DRAW_GROUP_UNDO 1
#define DRAW_GROUP_DRAWING_TOOLS 2
#define DRAW_GROUP_STENCILS 3
#define DRAW_GROUP_EDIT 4
#define DRAW_GROUP_CONVERT 5
#define DRAW_GROUP_INSERT 6
#define DRAW_GROUP_REPLAY 7
#define DRAW_GROUP_HELP 8
#define DRAW_GROUP_COUNT 9

#define DRAW_GROUP_FLAG_VISIBLE 0x0001u
#define DRAW_GROUP_FLAG_LABEL_VISIBLE 0x0002u
#define DRAW_GROUP_FLAG_COLLAPSED 0x0004u

#define DRAW_CONTROL_FLAG_CREATED 0x0001u
#define DRAW_CONTROL_FLAG_VISIBLE 0x0002u
#define DRAW_CONTROL_FLAG_ENABLED 0x0004u
#define DRAW_CONTROL_FLAG_TABSTOP 0x0008u
#define DRAW_CONTROL_FLAG_HAS_ICON 0x0010u
#define DRAW_CONTROL_FLAG_CHECKED 0x0020u

typedef enum RibbonDrawIcon {
    RIBBON_DRAW_ICON_NONE = 0,
    RIBBON_DRAW_ICON_DRAW,
    RIBBON_DRAW_ICON_UNDO,
    RIBBON_DRAW_ICON_REDO,
    RIBBON_DRAW_ICON_SELECT,
    RIBBON_DRAW_ICON_LASSO_SELECT,
    RIBBON_DRAW_ICON_ERASER,
    RIBBON_DRAW_ICON_PEN_BLACK,
    RIBBON_DRAW_ICON_PEN_RED,
    RIBBON_DRAW_ICON_PENCIL,
    RIBBON_DRAW_ICON_HIGHLIGHTER,
    RIBBON_DRAW_ICON_PEN_BLUE,
    RIBBON_DRAW_ICON_PEN_GREEN,
    RIBBON_DRAW_ICON_ACTION_PEN,
    RIBBON_DRAW_ICON_ADD_PEN,
    RIBBON_DRAW_ICON_RULER,
    RIBBON_DRAW_ICON_FORMAT_BACKGROUND,
    RIBBON_DRAW_ICON_INK_TO_SHAPE,
    RIBBON_DRAW_ICON_INK_TO_MATH,
    RIBBON_DRAW_ICON_CANVAS,
    RIBBON_DRAW_ICON_REPLAY,
    RIBBON_DRAW_ICON_HELP
} RibbonDrawIcon;

#define VIEW_GROUP_VIEWS 0
#define VIEW_GROUP_IMMERSIVE 1
#define VIEW_GROUP_DARK_MODE 2
#define VIEW_GROUP_PAGE_MOVEMENT 3
#define VIEW_GROUP_SHOW 4
#define VIEW_GROUP_ZOOM 5
#define VIEW_GROUP_WINDOW 6
#define VIEW_GROUP_MACROS 7
#define VIEW_GROUP_SHAREPOINT 8
#define VIEW_GROUP_COUNT 9

#define VIEW_GROUP_FLAG_VISIBLE 0x0001u
#define VIEW_GROUP_FLAG_LABEL_VISIBLE 0x0002u
#define VIEW_GROUP_FLAG_COLLAPSED 0x0004u

#define VIEW_CONTROL_FLAG_CREATED 0x0001u
#define VIEW_CONTROL_FLAG_VISIBLE 0x0002u
#define VIEW_CONTROL_FLAG_ENABLED 0x0004u
#define VIEW_CONTROL_FLAG_TABSTOP 0x0008u
#define VIEW_CONTROL_FLAG_HAS_ICON 0x0010u
#define VIEW_CONTROL_FLAG_CHECKED 0x0020u

#define VIEW_MODE_READ 1
#define VIEW_MODE_PRINT_LAYOUT 2
#define VIEW_MODE_WEB_LAYOUT 3
#define VIEW_MODE_OUTLINE 4
#define VIEW_MODE_DRAFT 5

typedef enum RibbonViewIcon {
    RIBBON_VIEW_ICON_NONE = 0,
    RIBBON_VIEW_ICON_READ_MODE,
    RIBBON_VIEW_ICON_PRINT_LAYOUT,
    RIBBON_VIEW_ICON_WEB_LAYOUT,
    RIBBON_VIEW_ICON_OUTLINE,
    RIBBON_VIEW_ICON_DRAFT,
    RIBBON_VIEW_ICON_FOCUS,
    RIBBON_VIEW_ICON_IMMERSIVE_READER,
    RIBBON_VIEW_ICON_SWITCH_MODES,
    RIBBON_VIEW_ICON_VERTICAL,
    RIBBON_VIEW_ICON_SIDE_TO_SIDE,
    RIBBON_VIEW_ICON_RULER,
    RIBBON_VIEW_ICON_GRIDLINES,
    RIBBON_VIEW_ICON_NAVIGATION_PANE,
    RIBBON_VIEW_ICON_ZOOM,
    RIBBON_VIEW_ICON_100_PERCENT,
    RIBBON_VIEW_ICON_ONE_PAGE,
    RIBBON_VIEW_ICON_MULTIPLE_PAGES,
    RIBBON_VIEW_ICON_PAGE_WIDTH,
    RIBBON_VIEW_ICON_NEW_WINDOW,
    RIBBON_VIEW_ICON_ARRANGE_ALL,
    RIBBON_VIEW_ICON_SPLIT,
    RIBBON_VIEW_ICON_SIDE_BY_SIDE,
    RIBBON_VIEW_ICON_SYNCHRONOUS_SCROLLING,
    RIBBON_VIEW_ICON_RESET_WINDOW_POSITION,
    RIBBON_VIEW_ICON_SWITCH_WINDOWS,
    RIBBON_VIEW_ICON_MACROS,
    RIBBON_VIEW_ICON_PROPERTIES
} RibbonViewIcon;

#define DESIGN_GROUP_DOCUMENT_FORMATTING 0
#define DESIGN_GROUP_PAGE_BACKGROUND 1
#define DESIGN_GROUP_COUNT 2

#define DESIGN_GROUP_FLAG_VISIBLE 0x0001u
#define DESIGN_GROUP_FLAG_LABEL_VISIBLE 0x0002u
#define DESIGN_GROUP_FLAG_COLLAPSED 0x0004u
#define DESIGN_GROUP_FLAG_GALLERY_OPEN 0x0008u

#define DESIGN_CONTROL_FLAG_CREATED 0x0001u
#define DESIGN_CONTROL_FLAG_VISIBLE 0x0002u
#define DESIGN_CONTROL_FLAG_ENABLED 0x0004u
#define DESIGN_CONTROL_FLAG_TABSTOP 0x0008u
#define DESIGN_CONTROL_FLAG_HAS_ICON 0x0010u
#define DESIGN_CONTROL_FLAG_CHECKED 0x0020u
#define DESIGN_CONTROL_FLAG_STYLE_PREVIEW 0x0040u

typedef enum RibbonDesignIcon {
    RIBBON_DESIGN_ICON_NONE = 0,
    RIBBON_DESIGN_ICON_THEMES,
    RIBBON_DESIGN_ICON_STYLE_PREVIEW,
    RIBBON_DESIGN_ICON_MORE,
    RIBBON_DESIGN_ICON_COLORS,
    RIBBON_DESIGN_ICON_FONTS,
    RIBBON_DESIGN_ICON_PARAGRAPH_SPACING,
    RIBBON_DESIGN_ICON_EFFECTS,
    RIBBON_DESIGN_ICON_SET_AS_DEFAULT,
    RIBBON_DESIGN_ICON_WATERMARK,
    RIBBON_DESIGN_ICON_PAGE_COLOR,
    RIBBON_DESIGN_ICON_PAGE_BORDERS
} RibbonDesignIcon;

#define DESIGN_STYLE_SET_OFFICE 0
#define DESIGN_STYLE_SET_BASIC_ELEGANT 1
#define DESIGN_STYLE_SET_BASIC_SIMPLE 2
#define DESIGN_STYLE_SET_BASIC_STYLISH 3
#define DESIGN_STYLE_SET_CENTERED 4
#define DESIGN_STYLE_SET_CASUAL 5
#define DESIGN_STYLE_SET_COMPACT 6
#define DESIGN_STYLE_SET_LINES_DISTINCTIVE 7
#define DESIGN_STYLE_SET_LINES_ELEGANT 8
#define DESIGN_STYLE_SET_LINES_SIMPLE 9
#define DESIGN_STYLE_SET_MODERN 10
#define DESIGN_STYLE_SET_SHADED 11
#define DESIGN_STYLE_SET_CLASSIC 12
#define DESIGN_STYLE_SET_DISTINCTIVE 13
#define DESIGN_STYLE_SET_ELEGANT 14
#define DESIGN_STYLE_SET_FORMAL 15
#define DESIGN_STYLE_SET_MANUSCRIPT 16
#define DESIGN_STYLE_SET_TRADITIONAL 17
#define DESIGN_STYLE_SET_WORD_2010 18
#define DESIGN_STYLE_SET_COUNT 19

#define DESIGN_COLOR_SCHEME_OFFICE 0
#define DESIGN_COLOR_SCHEME_BLUE 1
#define DESIGN_COLOR_SCHEME_GREEN 2
#define DESIGN_COLOR_SCHEME_ORANGE 3
#define DESIGN_COLOR_SCHEME_RED 4
#define DESIGN_COLOR_SCHEME_PURPLE 5
#define DESIGN_COLOR_SCHEME_COUNT 6

#define DESIGN_FONT_SCHEME_OFFICE 0
#define DESIGN_FONT_SCHEME_CLASSIC 1
#define DESIGN_FONT_SCHEME_MODERN 2
#define DESIGN_FONT_SCHEME_HUMANIST 3
#define DESIGN_FONT_SCHEME_EDITORIAL 4
#define DESIGN_FONT_SCHEME_MONOSPACE 5
#define DESIGN_FONT_SCHEME_COUNT 6

#define DESIGN_PARAGRAPH_SPACING_NONE 0
#define DESIGN_PARAGRAPH_SPACING_COMPACT 1
#define DESIGN_PARAGRAPH_SPACING_TIGHT 2
#define DESIGN_PARAGRAPH_SPACING_OPEN 3
#define DESIGN_PARAGRAPH_SPACING_RELAXED 4
#define DESIGN_PARAGRAPH_SPACING_DOUBLE 5
#define DESIGN_PARAGRAPH_SPACING_COUNT 6

#define DESIGN_EFFECT_OFFICE 0
#define DESIGN_EFFECT_SUBTLE 1
#define DESIGN_EFFECT_SHADOW 2
#define DESIGN_EFFECT_LINE 3
#define DESIGN_EFFECT_GLOW 4
#define DESIGN_EFFECT_FLAT 5
#define DESIGN_EFFECT_COUNT 6

typedef struct WordcraftDesignStyleSetInfo {
    const WCHAR *name;
    const WCHAR *headingFont;
    const WCHAR *bodyFont;
    COLORREF accent;
    LONG titleSize;
    LONG heading1Size;
    LONG heading2Size;
    BOOL centerTitle;
    BOOL compactSpacing;
    BOOL titleUppercasePreview;
    BOOL headingRulePreview;
    BOOL shadedPreview;
} WordcraftDesignStyleSetInfo;

#define RIBBON_LAYOUT_FULL 1
#define RIBBON_LAYOUT_COMPACT 2
#define RIBBON_LAYOUT_COLLAPSED 3

typedef enum WordcraftStyle {
    WORDCRAFT_STYLE_NORMAL = 0,
    WORDCRAFT_STYLE_NO_SPACING,
    WORDCRAFT_STYLE_HEADING_1,
    WORDCRAFT_STYLE_HEADING_2,
    WORDCRAFT_STYLE_TITLE,
    WORDCRAFT_STYLE_COUNT
} WordcraftStyle;

typedef struct AssistContext AssistContext;
typedef struct CommentStore CommentStore;
typedef struct RibbonContext RibbonContext;
typedef struct TextEngineContext TextEngineContext;
typedef struct WordcraftSplash WordcraftSplash;
typedef struct LiveShareContext LiveShareContext;
typedef struct HistoryContext HistoryContext;

typedef struct HistoryChatToken {
    BYTE bytes[16];
} HistoryChatToken;

typedef struct AppPalette {
    COLORREF toolbarBackground;
    COLORREF toolbarText;
    COLORREF toolbarDisabledText;
    COLORREF toolbarHotBackground;
    COLORREF toolbarHotText;
    COLORREF formatBackground;
    COLORREF formatText;
    COLORREF controlBackground;
    COLORREF controlText;
    COLORREF controlBorder;
    COLORREF workspaceBackground;
    COLORREF pageBackground;
    COLORREF pageBorder;
    COLORREF pageShadow;
    COLORREF statusBackground;
    COLORREF statusText;
    COLORREF statusDivider;
} AppPalette;

typedef struct DocumentIdentity {
    BOOL valid;
    DWORD volumeSerialNumber;
    DWORD fileIndexHigh;
    DWORD fileIndexLow;
    DWORD fileSizeHigh;
    DWORD fileSizeLow;
    FILETIME lastWriteTime;
} DocumentIdentity;

typedef struct AppState {
    HINSTANCE instance;
    HMODULE richEditModule;
    HWND mainWindow;
    HWND pageView;
    HWND editor;
    HWND toolbar;
    HWND formatBar;
    HWND ribbonTabs;
    HWND statusBar;
    HWND fontLabel;
    HWND fontCombo;
    HWND sizeCombo;
    HWND boldButton;
    HWND italicButton;
    HWND underlineButton;
    HWND strikeButton;
    HWND alignLeftButton;
    HWND alignCenterButton;
    HWND alignRightButton;
    HWND alignJustifyButton;
    HWND bulletsButton;
    HWND colorButton;
    WordcraftSplash *startupSplash;
    HFONT uiFont;

    WCHAR currentPath[PATH_CAPACITY];
    DocumentIdentity fileIdentity;
    BOOL currentIsRtf;
    BOOL richFormattingUsed;
    BOOL modified;
    BOOL loading;
    BOOL wordWrap;
    BOOL showStatusBar;
    BOOL focusMode;
    BOOL viewRulerVisible;
    BOOL viewGridlinesVisible;
    int designStyleSet;
    int designColorScheme;
    int designFontScheme;
    int designParagraphSpacing;
    int designEffect;
    BOOL useBrandColors;
    BOOL darkMode;
    BOOL spellCheckEnabled;
    BOOL autoCompleteEnabled;
    BOOL wordCountDirty;
    BOOL paginationDirty;
    BOOL pageLayoutBusy;
    LONG cachedWordCount;
    LONG pageCount;
    LONG currentPage;
    int zoomPercent;
    RECT pageMargins;
    POINT pageSize;
    PaperSizeId paperSizeId;
    LONG *pageEnds;
    SIZE_T pageCapacity;
    AssistContext *assist;
    CommentStore *comments;
    RibbonContext *ribbon;
    TextEngineContext *textEngine;
    LiveShareContext *liveShare;
    HistoryContext *history;
    WCHAR statusText[4][128];
    AppPalette palette;
    int uiPanelCornerRadius;
    int uiControlCornerRadius;
    int uiTabCornerRadius;
    LONG uiPanelPaintCount;
    LONG uiControlPaintCount;
    LONG uiTabPaintCount;
    LONG uiLogoCurvePaintCount;
    UINT uiPanelCurveCount;
    UINT uiLogoCurveCount;
    COLORREF customColors[16];
    HGLOBAL printerDevMode;
    HGLOBAL printerDevNames;

    UINT findMessage;
    HWND findDialog;
    BOOL findDialogIsReplace;
    FINDREPLACEW findReplace;
    WCHAR findText[FIND_CAPACITY];
    WCHAR replaceText[FIND_CAPACITY];
} AppState;

/* main.c */
LRESULT CALLBACK main_window_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
BOOL app_create_children(AppState *app);
void app_layout(AppState *app);
void app_update_status(AppState *app, BOOL recountWords);
void app_set_status_message(AppState *app, const WCHAR *message);
void app_update_command_ui(AppState *app);
void app_show_error(HWND owner, const WCHAR *action, DWORD errorCode);
int app_scale(HWND hwnd, int value);
BOOL editor_get_text_length(HWND editor, BOOL useCrlf, SIZE_T *length, DWORD *error);
BOOL editor_get_all_text(HWND editor, BOOL useCrlf, WCHAR **text,
                         SIZE_T *length, DWORD *error);

/* pageview.c */
BOOL pageview_create(AppState *app);
void pageview_layout(AppState *app);
void pageview_apply_theme(AppState *app);
void pageview_mark_dirty(AppState *app);
BOOL pageview_paginate(AppState *app);
void pageview_sync_to_caret(AppState *app, BOOL ensureVisible);
LONG pageview_page_start(AppState *app, LONG page);
LONG pageview_character_page(AppState *app, LONG character);
BOOL pageview_get_comment_margin_rect(AppState *app, LONG page, RECT *rect);
BOOL pageview_map_character_to_client(AppState *app, LONG character,
                                      LONG *page, POINT *point);
LRESULT pageview_query_state(AppState *app, UINT query, LPARAM component);
BOOL pageview_is_scrolling(AppState *app);
void pageview_free(AppState *app);

/* assist.c */
BOOL assist_initialize(AppState *app);
void assist_schedule(AppState *app);
void assist_selection_changed(AppState *app);
void assist_document_changed(AppState *app);
void assist_handle_timer(AppState *app, UINT_PTR timerId);
void assist_handle_result(AppState *app, LPARAM resultPointer);
void assist_clear_completion(AppState *app);
BOOL assist_accept_completion(AppState *app);
BOOL assist_has_completion(const AppState *app);
void assist_paint_overlays(AppState *app, HWND editor);
void assist_set_spell_check(AppState *app, BOOL enabled);
void assist_set_auto_complete(AppState *app, BOOL enabled);
void assist_set_ime_composing(AppState *app, BOOL composing);
LRESULT assist_query_state(const AppState *app, UINT query);
void assist_request_stop(AppState *app);
void assist_shutdown(AppState *app);

/* liveui.c */
BOOL live_share_initialize(AppState *app);
void live_share_show_dialog(AppState *app);
void live_share_start_host_command(AppState *app);
void live_share_join_command(AppState *app);
void live_share_copy_invitation_command(AppState *app);
void live_share_leave_command(AppState *app);
void live_share_leave_for_document_replacement(AppState *app);
void live_share_document_changed(AppState *app);
void live_share_chat_changed(AppState *app,
                             const HistoryChatToken *chatToken);
void live_share_handle_timer(AppState *app, UINT_PTR timerId);
void live_share_handle_event(AppState *app);
LRESULT live_share_query_state(const AppState *app, UINT query);
void live_share_apply_status_notice(AppState *app);
void live_share_shutdown(AppState *app);

/* history.c */
BOOL history_initialize(AppState *app);
void history_clear(AppState *app);
void history_seed_if_empty(AppState *app);
void history_note_document_changed(AppState *app);
void history_handle_timer(AppState *app, UINT_PTR timerId);
BOOL history_flush_pending(AppState *app);
BOOL history_record_revision(AppState *app, const WCHAR *author,
                             BOOL retagMatching);
BOOL history_post_chat(AppState *app, const WCHAR *text);
void history_show_chat(AppState *app);
void history_show_versions(AppState *app);
void history_refresh_dialogs(AppState *app);
BOOL history_embed_rtf(AppState *app, const BYTE *rtf, SIZE_T rtfSize,
                       BYTE **output, SIZE_T *outputSize, DWORD *error);
BOOL history_embed_rtf_bounded(
    AppState *app, const BYTE *rtf, SIZE_T rtfSize,
    SIZE_T maximumOutputSize,
    const HistoryChatToken *requiredChats,
    SIZE_T requiredChatCount, BYTE **output, SIZE_T *outputSize,
    DWORD *error);
BOOL history_load_rtf_memory(AppState *app, const BYTE *data, SIZE_T size,
                             DWORD *error);
BOOL history_merge_rtf_memory(AppState *app, const BYTE *data, SIZE_T size,
                              DWORD *error);
BOOL history_reconcile_rtf_memory(AppState *app, const BYTE *data,
                                  SIZE_T size, DWORD *error);
BOOL history_reconcile_chat_ack_rtf_memory(
    AppState *app, const BYTE *data, SIZE_T size,
    const HistoryChatToken *acknowledgedChats,
    SIZE_T acknowledgedChatCount, DWORD *error);
BOOL history_merge_chat_rtf_memory(
    AppState *app, const BYTE *data, SIZE_T size,
    const WCHAR *authenticatedAuthor, BOOL includeKnownChats,
    HistoryChatToken *acceptedChats, SIZE_T acceptedChatCapacity,
    SIZE_T *acceptedChatCount, DWORD *error);
BOOL history_load_rtf_file(AppState *app, const WCHAR *path, DWORD *error);
SIZE_T history_chat_count(const AppState *app);
SIZE_T history_version_count(const AppState *app);
BOOL history_contains_chat_token(
    const AppState *app, const HistoryChatToken *token);
void history_get_local_author(WCHAR *author, SIZE_T capacity);
BOOL history_author_is_acceptable(const WCHAR *author);
void history_cancel_pending_revision(AppState *app);
LRESULT history_query_state(const AppState *app, UINT query, LPARAM index);
void history_shutdown(AppState *app);

/* comments.c */
BOOL comments_initialize(AppState *app);
void comments_clear(AppState *app);
BOOL comments_begin_draft(AppState *app);
void comments_cancel_draft(AppState *app);
void comments_dismiss_highlight(AppState *app);
BOOL comments_add(AppState *app, const WCHAR *text);
void comments_previous(AppState *app);
void comments_next(AppState *app);
void comments_delete_active(AppState *app);
void comments_selection_changed(AppState *app);
void comments_paint_overlays(AppState *app, HWND editor);
void comments_paint_margin(AppState *app, HDC dc, const RECT *dirtyRect);
BOOL comments_handle_margin_click(AppState *app, POINT point);
SIZE_T comments_count(const AppState *app);
BOOL comments_embed_rtf(AppState *app, const BYTE *rtf, SIZE_T rtfSize,
                        BYTE **output, SIZE_T *outputSize, DWORD *error);
BOOL comments_load_rtf_memory(AppState *app, const BYTE *data, SIZE_T size,
                              DWORD *error);
BOOL comments_load_rtf_file(AppState *app, const WCHAR *path, DWORD *error);
LRESULT comments_query_state(const AppState *app, UINT query, LPARAM index);
void comments_shutdown(AppState *app);

/* ribbon.c */
BOOL ribbon_create(AppState *app);
void ribbon_layout(AppState *app, int width, int height, BOOL compact);
void ribbon_apply_theme(AppState *app);
void ribbon_paint_home_groups(AppState *app, HDC dc);
void ribbon_paint_insert_groups(AppState *app, HDC dc);
void ribbon_paint_draw_groups(AppState *app, HDC dc);
void ribbon_paint_design_groups(AppState *app, HDC dc);
void ribbon_paint_view_groups(AppState *app, HDC dc);
BOOL ribbon_draw_insert_button_icon(
    AppState *app, HWND button, HDC dc, const RECT *bounds,
    COLORREF color, RECT *captionRect, UINT *captionFlags);
BOOL ribbon_draw_draw_button_icon(
    AppState *app, HWND button, HDC dc, const RECT *bounds,
    COLORREF color, RECT *captionRect, UINT *captionFlags);
BOOL ribbon_draw_design_button_icon(
    AppState *app, HWND button, HDC dc, const RECT *bounds,
    COLORREF color, RECT *captionRect, UINT *captionFlags);
BOOL ribbon_draw_view_button_icon(
    AppState *app, HWND button, HDC dc, const RECT *bounds,
    COLORREF color, RECT *captionRect, UINT *captionFlags);
BOOL ribbon_handle_notify(AppState *app, const NMHDR *notification);
BOOL ribbon_handle_command(AppState *app, WPARAM wParam, LPARAM lParam);
BOOL ribbon_draw_item(AppState *app, const DRAWITEMSTRUCT *draw);
void ribbon_update_command_ui(AppState *app, BOOL hasSelection,
                              BOOL canUndo, BOOL canRedo, BOOL canPaste);
void ribbon_focus(AppState *app);
void ribbon_focus_comment_editor(AppState *app);
BOOL ribbon_handle_keyboard(AppState *app, const MSG *message);
BOOL ribbon_get_comment_text(AppState *app, WCHAR *text, size_t textCount);
void ribbon_clear_comment_text(AppState *app);
void ribbon_set_comment_summary(AppState *app, const WCHAR *summary);
void ribbon_set_live_share_status(AppState *app, const WCHAR *caption,
                                  BOOL active);
void ribbon_sync_paper_size(AppState *app);
void ribbon_sync_home_formatting(AppState *app, BOOL subscript,
                                 BOOL superscript, BOOL numbering,
                                 BOOL highlighted, int lineSpacingPercent);
void ribbon_set_active_style(AppState *app, int style);
LRESULT ribbon_query_state(AppState *app, UINT query, LPARAM index);
void ribbon_free(AppState *app);

/* textengine.c */
BOOL text_engine_initialize(AppState *app);
void text_engine_apply_document_defaults(AppState *app);
void text_engine_note_layout_change(AppState *app);
LRESULT text_engine_query_state(const AppState *app, UINT query);
void text_engine_shutdown(AppState *app);

/* document.c */
BOOL document_new(AppState *app, BOOL askToSave);
BOOL document_open_dialog(AppState *app);
BOOL document_open_path(AppState *app, const WCHAR *path, BOOL askToSave);
BOOL document_save(AppState *app);
BOOL document_save_as(AppState *app);
BOOL document_prompt_save(AppState *app);
BOOL document_capture_live_snapshot(AppState *app,
                                    const HistoryChatToken *requiredChats,
                                    SIZE_T requiredChatCount, BYTE **data,
                                    SIZE_T *size, DWORD *error);
BOOL document_capture_revision_snapshot(AppState *app, BYTE **data,
                                        SIZE_T *size, DWORD *error);
BOOL document_validate_live_snapshot(const BYTE *data, SIZE_T size,
                                     DWORD *error);
BOOL document_validate_history_snapshot(const BYTE *data, SIZE_T size,
                                        DWORD *error);
BOOL document_apply_live_snapshot(AppState *app, const BYTE *data,
                                  SIZE_T size, DWORD *error);
BOOL document_apply_merged_live_snapshot(AppState *app, const BYTE *data,
                                         SIZE_T size, DWORD *error);
BOOL document_apply_reconciled_live_snapshot(AppState *app,
                                             const BYTE *data, SIZE_T size,
                                             DWORD *error);
BOOL document_apply_acknowledged_live_snapshot(
    AppState *app, const BYTE *data, SIZE_T size,
    const HistoryChatToken *acknowledgedChats,
    SIZE_T acknowledgedChatCount, DWORD *error);
BOOL document_apply_history_snapshot(AppState *app, const BYTE *data,
                                     SIZE_T size, DWORD *error);
void document_mark_modified(AppState *app, BOOL modified);
void document_mark_metadata_modified(AppState *app);
void document_update_title(AppState *app);

/* format.c */
void format_initialize_document(AppState *app);
void format_toggle_character_effect(AppState *app, DWORD mask, DWORD effect);
void format_set_font_name(AppState *app, const WCHAR *name);
void format_set_font_size(AppState *app, double points);
void format_choose_font(AppState *app);
void format_choose_color(AppState *app);
void format_set_alignment(AppState *app, WORD alignment);
void format_toggle_bullets(AppState *app);
void format_toggle_numbering(AppState *app);
void format_change_indent(AppState *app, LONG deltaTwips);
void format_adjust_font_size(AppState *app, int direction);
void format_toggle_script(AppState *app, BOOL superscript);
void format_toggle_highlight(AppState *app);
void format_clear_formatting(AppState *app);
void format_cycle_line_spacing(AppState *app);
BOOL format_apply_style(AppState *app, WordcraftStyle style);
BOOL format_apply_document_design(AppState *app, int styleSet,
                                  int colorScheme, int fontScheme,
                                  int paragraphSpacing);
BOOL format_get_design_style_set_info(
    int styleSet, WordcraftDesignStyleSetInfo *info);
void format_reset_document_design(AppState *app);
void format_sync_controls(AppState *app);
void format_set_zoom(AppState *app, int percent);
void format_set_word_wrap(AppState *app, BOOL enabled);

/* dialogs.c */
void dialogs_show_find(AppState *app, BOOL replace);
void dialogs_handle_find_replace(AppState *app, FINDREPLACEW *request);
void dialogs_insert_datetime(AppState *app);
void dialogs_show_about(AppState *app);

/* insert.c */
BOOL insert_execute_command(AppState *app, UINT command);
BOOL insert_emf_picture(AppState *app, const BYTE *data, SIZE_T size,
                        UINT width, UINT height,
                        const WCHAR *statusText);

/* draw.c */
BOOL draw_show_canvas(AppState *app, UINT initialTool, BOOL showRuler,
                      BOOL ruledBackground);

/* printing.c */
void printing_page_setup(AppState *app);
void printing_print_document(AppState *app);

#endif
