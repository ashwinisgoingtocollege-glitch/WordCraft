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

#define APP_CLASS_NAME L"WordCraftMainWindow"
#define APP_NAME L"WordCraft"
#define PATH_CAPACITY 32768
#define FIND_CAPACITY 256
#define STATUS_TIMER_ID 1
#define SPELL_TIMER_ID 2
#define COMPLETION_TIMER_ID 3
#define WCM_QUERY_STATE (WM_APP + 40)
#define WCM_ASSIST_RESULT (WM_APP + 41)

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

typedef struct AssistContext AssistContext;

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
    HWND statusBar;
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
