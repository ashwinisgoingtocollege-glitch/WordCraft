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

#define APP_CLASS_NAME L"WordCraftMainWindow"
#define APP_NAME L"WordCraft"
#define WORDCRAFT_DEFAULT_FONT_FACE L"Times New Roman"
#define WORDCRAFT_DEFAULT_FONT_SIZE_TWIPS 240
#define WORDCRAFT_DEFAULT_LINE_SPACING_RULE 5
#define WORDCRAFT_DEFAULT_LINE_SPACING 22
#define WORDCRAFT_DEFAULT_PARAGRAPH_SPACE_AFTER_TWIPS 120
#define PATH_CAPACITY 32768
#define FIND_CAPACITY 256
#define COMMENT_TEXT_CAPACITY 2048
#define STATUS_TIMER_ID 1
#define SPELL_TIMER_ID 2
#define COMPLETION_TIMER_ID 3
#define WCM_QUERY_STATE (WM_APP + 40)
#define WCM_ASSIST_RESULT (WM_APP + 41)
#define WCM_RENDERER_CHANGED (WM_APP + 42)

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

typedef struct AssistContext AssistContext;
typedef struct CommentStore CommentStore;
typedef struct RibbonContext RibbonContext;
typedef struct TextEngineContext TextEngineContext;

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
    HFONT uiFont;

    WCHAR currentPath[PATH_CAPACITY];
    DocumentIdentity fileIdentity;
    BOOL currentIsRtf;
    BOOL richFormattingUsed;
    BOOL modified;
    BOOL loading;
    BOOL wordWrap;
    BOOL showStatusBar;
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
    LONG *pageEnds;
    SIZE_T pageCapacity;
    AssistContext *assist;
    CommentStore *comments;
    RibbonContext *ribbon;
    TextEngineContext *textEngine;
    WCHAR statusText[4][128];
    AppPalette palette;
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
LRESULT pageview_query_state(AppState *app, UINT query);
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

/* comments.c */
BOOL comments_initialize(AppState *app);
void comments_clear(AppState *app);
BOOL comments_add(AppState *app, const WCHAR *text);
void comments_previous(AppState *app);
void comments_next(AppState *app);
void comments_delete_active(AppState *app);
void comments_selection_changed(AppState *app);
void comments_paint_overlays(AppState *app, HWND editor);
SIZE_T comments_count(const AppState *app);
BOOL comments_embed_rtf(AppState *app, const BYTE *rtf, SIZE_T rtfSize,
                        BYTE **output, SIZE_T *outputSize, DWORD *error);
BOOL comments_load_rtf_file(AppState *app, const WCHAR *path, DWORD *error);
LRESULT comments_query_state(const AppState *app, UINT query, LPARAM index);
void comments_shutdown(AppState *app);

/* ribbon.c */
BOOL ribbon_create(AppState *app);
void ribbon_layout(AppState *app, int width, int height, BOOL compact);
void ribbon_apply_theme(AppState *app);
BOOL ribbon_handle_notify(AppState *app, const NMHDR *notification);
BOOL ribbon_draw_item(AppState *app, const DRAWITEMSTRUCT *draw);
void ribbon_update_command_ui(AppState *app, BOOL hasSelection,
                              BOOL canUndo, BOOL canRedo, BOOL canPaste);
void ribbon_focus(AppState *app);
void ribbon_focus_comment_editor(AppState *app);
BOOL ribbon_handle_keyboard(AppState *app, const MSG *message);
BOOL ribbon_get_comment_text(AppState *app, WCHAR *text, size_t textCount);
void ribbon_clear_comment_text(AppState *app);
void ribbon_set_comment_summary(AppState *app, const WCHAR *summary);
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
void document_mark_modified(AppState *app, BOOL modified);
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
void format_change_indent(AppState *app, LONG deltaTwips);
void format_sync_controls(AppState *app);
void format_set_zoom(AppState *app, int percent);
void format_set_word_wrap(AppState *app, BOOL enabled);

/* dialogs.c */
void dialogs_show_find(AppState *app, BOOL replace);
void dialogs_handle_find_replace(AppState *app, FINDREPLACEW *request);
void dialogs_insert_datetime(AppState *app);
void dialogs_show_about(AppState *app);

/* printing.c */
void printing_page_setup(AppState *app);
void printing_print_document(AppState *app);

#endif
