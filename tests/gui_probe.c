#include "editor.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define PROBE_MESSAGE_TIMEOUT_MS 5000

static const WCHAR *const expectedRibbonTabNames[RIBBON_TAB_COUNT] = {
    L"File", L"Home", L"Insert", L"Draw", L"Design", L"Layout",
    L"References", L"Mailings", L"Review", L"View", L"Help"
};

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

int wmain(void)
{
    WCHAR executable[PATH_CAPACITY];
    WCHAR sample[PATH_CAPACITY];
    WCHAR longSample[PATH_CAPACITY];
    WCHAR relativeSample[128];
    WCHAR relativeLongSample[128];
    PROCESS_INFORMATION process;
    PROCESS_INFORMATION longProcess;
    HWND window = NULL;
    HWND longWindow = NULL;
    HWND editor;
    HWND longEditor;
    HWND longPageView;
    HWND ribbonTabs;
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
    LRESULT activeComment = -1;
    LONG pageIndex;
    int ribbonIndex;
    DWORD probeProcessId = GetCurrentProcessId();
    int result = 1;

    ZeroMemory(&process, sizeof(process));
    ZeroMemory(&longProcess, sizeof(longProcess));
    sample[0] = L'\0';
    longSample[0] = L'\0';

    if (FAILED(StringCchPrintfW(relativeSample, ARRAYSIZE(relativeSample),
                                L"build\\gui_probe_input_%lu.txt", probeProcessId)) ||
        FAILED(StringCchPrintfW(relativeLongSample, ARRAYSIZE(relativeLongSample),
                                L"build\\gui_probe_long_input_%lu.rtf", probeProcessId)) ||
        GetFullPathNameW(L"wordcraft.exe", ARRAYSIZE(executable), executable, NULL) == 0 ||
        GetFullPathNameW(relativeSample, ARRAYSIZE(sample), sample, NULL) == 0 ||
        GetFullPathNameW(relativeLongSample, ARRAYSIZE(longSample),
                         longSample, NULL) == 0 ||
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
    ribbonTabs = find_control(window, IDC_RIBBON_TABS);
    commentEdit = find_control(window, IDC_COMMENT_EDIT);
    if (ribbonTabs == NULL || commentEdit == NULL ||
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
        !wait_for_ribbon_focus(window, RIBBON_FOCUS_EDITOR)) {
        fwprintf(stderr,
                 L"ribbon focus cycling or empty New Comment activation failed\n");
        goto cleanup;
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
    if (!send_message_bounded(window, WM_COMMAND,
                              MAKEWPARAM(IDM_FILE_SAVE, 0), 0, NULL)) {
        fwprintf(stderr, L"save command timed out\n");
        goto cleanup;
    }
    if (!verify_saved_file(sample)) {
        fwprintf(stderr, L"Unicode text save or atomic replacement failed\n");
        goto cleanup;
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
    if (longEditor == NULL || longPageView == NULL) {
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
        commentCount != 0 || activeComment != -1 ||
        !send_message_bounded(longWindow, WM_COMMAND,
                              MAKEWPARAM(IDM_REVIEW_ADD_COMMENT, 0), 0,
                              NULL) ||
        !wait_for_ribbon_state(longWindow, RIBBON_TAB_REVIEW) ||
        !wait_for_ribbon_focus(longWindow, RIBBON_FOCUS_PANEL)) {
        fwprintf(stderr,
                 L"a comment-free RTF did not expose the Review ribbon editor correctly\n");
        goto cleanup;
    }
    if (!send_message_bounded(longEditor, EM_SETSEL, 0, 3, NULL) ||
        !send_message_bounded(commentEdit, WM_SETTEXT, 0,
                              (LPARAM)firstCommentText, NULL) ||
        !send_message_bounded(longWindow, WM_COMMAND,
                              MAKEWPARAM(IDM_REVIEW_ADD_COMMENT, 0), 0,
                              NULL) ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_COUNT, 0,
                               &commentCount) ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_ACTIVE_INDEX, 0,
                               &activeComment) ||
        commentCount != 1 || activeComment != 0 ||
        !comment_matches(longWindow, 0, 0, 3, firstCommentText) ||
        !send_message_bounded(longEditor, WM_GETTEXTLENGTH, 0, 0,
                              &currentPage) ||
        currentPage != longTextLength) {
        fwprintf(stderr,
                 L"adding the first anchored Unicode comment failed or changed document text\n");
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
        commentCount != 2 || activeComment != 1 ||
        !comment_matches(longWindow, 0, 0, 3, firstCommentText) ||
        !comment_matches(longWindow, 1, (LONG)secondPageStart + 5,
                         (LONG)secondPageStart + 12, secondCommentText)) {
        fwprintf(stderr,
                 L"adding a second page-anchored comment failed\n");
        goto cleanup;
    }
    if (!send_message_bounded(longWindow, WM_COMMAND,
                              MAKEWPARAM(IDM_REVIEW_PREVIOUS_COMMENT, 0), 0,
                              NULL) ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_ACTIVE_INDEX, 0,
                               &activeComment) ||
        !get_selection_bounded(longEditor, &selectionStart, &selectionEnd) ||
        activeComment != 0 || selectionStart != 0 || selectionEnd != 3 ||
        !send_message_bounded(longWindow, WM_COMMAND,
                              MAKEWPARAM(IDM_REVIEW_NEXT_COMMENT, 0), 0,
                              NULL) ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_ACTIVE_INDEX, 0,
                               &activeComment) ||
        !get_selection_bounded(longEditor, &selectionStart, &selectionEnd) ||
        activeComment != 1 ||
        selectionStart != (DWORD)(secondPageStart + 5) ||
        selectionEnd != (DWORD)(secondPageStart + 12)) {
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
        commentCount != 1 || activeComment != 0 ||
        !comment_matches(longWindow, 0, 0, 3, firstCommentText) ||
        !send_message_bounded(longEditor, WM_GETTEXTLENGTH, 0, 0,
                              &currentPage) ||
        currentPage != longTextLength) {
        fwprintf(stderr,
                 L"Delete Comment did not remove only the active comment\n");
        goto cleanup;
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
    if (longEditor == NULL ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_COUNT, 0,
                               &commentCount) ||
        !query_wordcraft_state(longWindow, WCQ_COMMENT_ACTIVE_INDEX, 0,
                               &activeComment) ||
        commentCount != 1 || activeComment != 0 ||
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
    if (!close_wordcraft_cleanly(longWindow, &longProcess)) {
        fwprintf(stderr,
                 L"WordCraft did not close cleanly after comment persistence validation\n");
        goto cleanup;
    }
    longWindow = NULL;

    printf("hidden_launch=ok command_line_utf8=ok pagination=ok page_counter=ok "
           "ribbon_tabs=ok ribbon_panels=ok ribbon_keyboard=ok "
           "advanced_typography=ok text_engine_defaults=ok "
           "dark_mode=ok theme_document_invariance=ok workers=ok spellcheck=ok "
           "inline_completion=ok tab_accept=ok ordinary_tab=ok toggles=ok "
           "undo=ok utf8_save=ok continuous_scroll=ok multi_page_view=ok "
           "smooth_scroll=ok scroll_60fps=ok high_resolution_wheel=ok "
           "scroll_coalescing=ok "
           "viewport_selection=ok cross_page_selection=ok "
           "caret_page_sync=ok default_font=ok format_bar=ok "
           "paragraph_formatting=ok comments=ok comment_navigation=ok "
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
