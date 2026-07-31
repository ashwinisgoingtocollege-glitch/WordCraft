#include "editor.h"

#include <stdio.h>
#include <string.h>

#define HISTORY_GUI_TIMEOUT_MS 15000u
#define HISTORY_GUI_MESSAGE_TIMEOUT_MS 5000u
#define HISTORY_GUI_MAX_VERSIONS 8u

typedef struct WindowSearch {
    DWORD processId;
    const WCHAR *className;
    HWND window;
} WindowSearch;

typedef struct ControlSearch {
    int id;
    HWND window;
} ControlSearch;

typedef struct DialogSearch {
    DWORD processId;
    int controlId;
    HWND window;
} DialogSearch;

static BOOL CALLBACK find_process_window(HWND window, LPARAM parameter)
{
    WindowSearch *search = (WindowSearch *)parameter;
    DWORD processId = 0;
    WCHAR className[128];

    GetWindowThreadProcessId(window, &processId);
    if (processId != search->processId) {
        return TRUE;
    }
    if (GetClassNameW(window, className, ARRAYSIZE(className)) > 0 &&
        lstrcmpW(className, search->className) == 0) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

static BOOL CALLBACK find_child_control(HWND window, LPARAM parameter)
{
    ControlSearch *search = (ControlSearch *)parameter;

    if (GetDlgCtrlID(window) == search->id) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

static HWND find_control(HWND parent, int id)
{
    ControlSearch search;

    search.id = id;
    search.window = NULL;
    EnumChildWindows(parent, find_child_control, (LPARAM)&search);
    return search.window;
}

static BOOL CALLBACK find_dialog_window(HWND window, LPARAM parameter)
{
    DialogSearch *search = (DialogSearch *)parameter;
    DWORD processId = 0;
    WCHAR className[32];

    GetWindowThreadProcessId(window, &processId);
    if (processId != search->processId ||
        GetClassNameW(window, className, ARRAYSIZE(className)) <= 0 ||
        lstrcmpW(className, L"#32770") != 0 ||
        GetDlgItem(window, search->controlId) == NULL) {
        return TRUE;
    }
    search->window = window;
    return FALSE;
}

static HWND find_dialog_with_control(DWORD processId, int controlId)
{
    DialogSearch search;

    search.processId = processId;
    search.controlId = controlId;
    search.window = NULL;
    EnumWindows(find_dialog_window, (LPARAM)&search);
    return search.window;
}

static HWND wait_for_dialog_with_control(DWORD processId, int controlId)
{
    DWORD start = GetTickCount();

    do {
        HWND dialog = find_dialog_with_control(processId, controlId);
        if (dialog != NULL) {
            return dialog;
        }
        Sleep(25u);
    } while (GetTickCount() - start < HISTORY_GUI_TIMEOUT_MS);
    return NULL;
}

static BOOL wait_for_window_destroyed(HWND window)
{
    DWORD start = GetTickCount();

    do {
        if (!IsWindow(window)) {
            return TRUE;
        }
        Sleep(25u);
    } while (GetTickCount() - start < HISTORY_GUI_TIMEOUT_MS);
    return FALSE;
}

static BOOL send_bounded(HWND window, UINT message, WPARAM wParam,
                         LPARAM lParam, LRESULT *result)
{
    DWORD_PTR raw = 0;

    if (window == NULL ||
        SendMessageTimeoutW(window, message, wParam, lParam,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK,
                            HISTORY_GUI_MESSAGE_TIMEOUT_MS, &raw) == 0) {
        return FALSE;
    }
    if (result != NULL) {
        *result = (LRESULT)raw;
    }
    return TRUE;
}

static BOOL query_state(HWND window, UINT query, LPARAM index,
                        LRESULT *value)
{
    return send_bounded(window, WCM_QUERY_STATE, query, index, value);
}

static BOOL wait_for_state(HWND window, UINT query, LPARAM index,
                           LRESULT expected)
{
    DWORD start = GetTickCount();

    do {
        LRESULT value = 0;
        if (query_state(window, query, index, &value) &&
            value == expected) {
            return TRUE;
        }
        Sleep(25u);
    } while (GetTickCount() - start < HISTORY_GUI_TIMEOUT_MS);
    return FALSE;
}

static BOOL wait_for_state_at_least(HWND window, UINT query,
                                    LRESULT minimum, LRESULT *actual)
{
    DWORD start = GetTickCount();

    do {
        LRESULT value = 0;
        if (query_state(window, query, 0, &value) &&
            value >= minimum) {
            if (actual != NULL) {
                *actual = value;
            }
            return TRUE;
        }
        Sleep(25u);
    } while (GetTickCount() - start < HISTORY_GUI_TIMEOUT_MS);
    return FALSE;
}

static UINT probe_hash_text(const WCHAR *text)
{
    UINT hash = 2166136261u;

    if (text == NULL) {
        return 0u;
    }
    while (*text != L'\0') {
        hash ^= (UINT)*text++;
        hash *= 16777619u;
    }
    return hash;
}

static BOOL write_initial_rtf(const WCHAR *path)
{
    static const char rtf[] =
        "{\\rtf1\\ansi\\deff0"
        "{\\fonttbl{\\f0 Times New Roman;}}"
        "\\f0\\fs24 History baseline}";
    HANDLE file;
    DWORD written = 0;
    BOOL success;

    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    success = WriteFile(file, rtf, (DWORD)(sizeof(rtf) - 1u),
                        &written, NULL) &&
              written == sizeof(rtf) - 1u &&
              FlushFileBuffers(file);
    if (!CloseHandle(file)) {
        success = FALSE;
    }
    return success;
}

static BOOL file_contains_ascii(const WCHAR *path, const char *needle)
{
    HANDLE file;
    LARGE_INTEGER size;
    BYTE *data = NULL;
    SIZE_T needleLength;
    SIZE_T index;
    DWORD read = 0;
    BOOL found = FALSE;

    needleLength = needle != NULL ? strlen(needle) : 0;
    if (needleLength == 0) {
        return FALSE;
    }
    file = CreateFileW(path, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE |
                           FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        (ULONGLONG)size.QuadPart > 32u * 1024u * 1024u) {
        CloseHandle(file);
        return FALSE;
    }
    if (size.QuadPart != 0) {
        data = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)size.QuadPart);
        if (data == NULL ||
            !ReadFile(file, data, (DWORD)size.QuadPart, &read, NULL) ||
            read != (DWORD)size.QuadPart) {
            HeapFree(GetProcessHeap(), 0, data);
            CloseHandle(file);
            return FALSE;
        }
    }
    CloseHandle(file);
    if (needleLength <= (SIZE_T)size.QuadPart) {
        for (index = 0;
             index <= (SIZE_T)size.QuadPart - needleLength; ++index) {
            if (memcmp(data + index, needle, needleLength) == 0) {
                found = TRUE;
                break;
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, data);
    return found;
}

static BOOL launch_hidden(const WCHAR *executable, const WCHAR *path,
                          PROCESS_INFORMATION *process, HWND *window)
{
    WCHAR commandLine[PATH_CAPACITY * 2];
    STARTUPINFOW startup;
    WindowSearch search;
    DWORD start;

    if (FAILED(StringCchPrintfW(commandLine, ARRAYSIZE(commandLine),
                                L"\"%s\" \"%s\"", executable, path))) {
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
    (void)WaitForInputIdle(process->hProcess, 5000u);

    search.processId = process->dwProcessId;
    search.className = APP_CLASS_NAME;
    search.window = NULL;
    start = GetTickCount();
    do {
        EnumWindows(find_process_window, (LPARAM)&search);
        if (search.window != NULL) {
            *window = search.window;
            return TRUE;
        }
        Sleep(25u);
    } while (GetTickCount() - start < HISTORY_GUI_TIMEOUT_MS);
    return FALSE;
}

static BOOL close_dialog(HWND dialog)
{
    HWND closeButton;

    if (dialog == NULL || !IsWindow(dialog)) {
        return FALSE;
    }
    closeButton = GetDlgItem(dialog, IDCANCEL);
    return closeButton != NULL &&
           send_bounded(closeButton, BM_CLICK, 0, 0, NULL) &&
           wait_for_window_destroyed(dialog);
}

static BOOL close_process_cleanly(HWND mainWindow,
                                  PROCESS_INFORMATION *process)
{
    DWORD wait;
    DWORD exitCode = 1;

    if (mainWindow == NULL || process == NULL ||
        process->hProcess == NULL ||
        !PostMessageW(mainWindow, WM_CLOSE, 0, 0)) {
        return FALSE;
    }
    wait = WaitForSingleObject(process->hProcess,
                               HISTORY_GUI_TIMEOUT_MS);
    if (wait != WAIT_OBJECT_0 ||
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

    if (process == NULL) {
        return;
    }
    if (process->hThread != NULL) {
        CloseHandle(process->hThread);
        process->hThread = NULL;
    }
    if (process->hProcess == NULL) {
        return;
    }
    if (GetExitCodeProcess(process->hProcess, &exitCode) &&
        exitCode == STILL_ACTIVE) {
        TerminateProcess(process->hProcess, 99u);
        (void)WaitForSingleObject(process->hProcess, 5000u);
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

int wmain(void)
{
    static const WCHAR revisedText[] =
        L"History baseline revised \x03A9";
    static const WCHAR chatMessage[] =
        L"Review \x03A9 \x4F60\x597D \x2014 caf\u00E9";
    WCHAR executable[PATH_CAPACITY];
    WCHAR fallback[PATH_CAPACITY];
    WCHAR sample[PATH_CAPACITY];
    WCHAR modulePath[PATH_CAPACITY];
    WCHAR *slash;
    PROCESS_INFORMATION process;
    HWND mainWindow = NULL;
    HWND editor;
    HWND chatDialog = NULL;
    HWND chatMessageEdit;
    HWND chatSendButton;
    HWND versionDialog = NULL;
    HWND versionList;
    HWND versionPreview;
    HWND versionRestore;
    LRESULT baselineVersions = 0;
    LRESULT versionCount = 0;
    LRESULT reopenedCount = 0;
    LRESULT chatCount = 0;
    LRESULT value = 0;
    LRESULT insertedCount = 0;
    LRESULT deletedCount = 0;
    LRESULT rowCount = 0;
    UINT chatTextHash = 0;
    UINT chatAuthorHash = 0;
    UINT versionAuthorHashes[HISTORY_GUI_MAX_VERSIONS];
    UINT reopenedHash;
    SIZE_T index;
    DWORD tempLength;
    int result = 1;

    ZeroMemory(&process, sizeof(process));
    ZeroMemory(versionAuthorHashes, sizeof(versionAuthorHashes));
    if (GetModuleFileNameW(NULL, modulePath, ARRAYSIZE(modulePath)) == 0) {
        fwprintf(stderr, L"could not locate history GUI probe\n");
        return 2;
    }
    slash = wcsrchr(modulePath, L'\\');
    if (slash == NULL) {
        fwprintf(stderr, L"history GUI probe path had no directory\n");
        return 3;
    }
    *slash = L'\0';
    if (FAILED(StringCchPrintfW(fallback, ARRAYSIZE(fallback),
                                L"%s\\..\\wordcraft.exe", modulePath)) ||
        !resolve_test_app(fallback, executable, ARRAYSIZE(executable))) {
        return 4;
    }
    tempLength = GetTempPathW(ARRAYSIZE(sample), sample);
    if (tempLength == 0 || tempLength >= ARRAYSIZE(sample) ||
        FAILED(StringCchPrintfW(
            sample + tempLength, ARRAYSIZE(sample) - tempLength,
            L"wordcraft-history-gui-%lu-%lu.rtf",
            GetCurrentProcessId(), GetTickCount())) ||
        !write_initial_rtf(sample)) {
        fwprintf(stderr, L"could not create history GUI sample\n");
        return 5;
    }

    if (!launch_hidden(executable, sample, &process, &mainWindow)) {
        fwprintf(stderr, L"WordCraft did not launch for history probe\n");
        result = 6;
        goto cleanup;
    }
    editor = find_control(mainWindow, IDC_EDITOR);
    if (editor == NULL ||
        find_control(mainWindow, IDM_REVIEW_DOCUMENT_CHAT) == NULL ||
        find_control(mainWindow, IDM_REVIEW_VERSION_HISTORY) == NULL ||
        !wait_for_state(mainWindow, WCQ_VERSION_COUNT, 0, 1)) {
        fwprintf(stderr, L"baseline version history was not initialized\n");
        result = 7;
        goto cleanup;
    }
    if (!query_state(mainWindow, WCQ_VERSION_COUNT, 0,
                     &baselineVersions) ||
        baselineVersions != 1) {
        fwprintf(stderr, L"unexpected baseline version count: %ld\n",
                 (long)baselineVersions);
        result = 8;
        goto cleanup;
    }

    if (!send_bounded(editor, WM_SETTEXT, 0, (LPARAM)revisedText, NULL) ||
        !wait_for_state_at_least(mainWindow, WCQ_VERSION_COUNT,
                                 baselineVersions + 1, &versionCount) ||
        versionCount != baselineVersions + 1) {
        fwprintf(stderr, L"edited document did not create one version\n");
        result = 9;
        goto cleanup;
    }
    if (!query_state(mainWindow, WCQ_VERSION_INSERTED_COUNT,
                     versionCount - 1, &insertedCount) ||
        !query_state(mainWindow, WCQ_VERSION_DELETED_COUNT,
                     versionCount - 1, &deletedCount) ||
        (insertedCount <= 0 && deletedCount <= 0)) {
        fwprintf(stderr,
                 L"edited version did not retain a change range\n");
        result = 10;
        goto cleanup;
    }
    if ((SIZE_T)versionCount > ARRAYSIZE(versionAuthorHashes)) {
        fwprintf(stderr, L"version count exceeded probe capacity\n");
        result = 11;
        goto cleanup;
    }
    for (index = 0; index < (SIZE_T)versionCount; ++index) {
        if (!query_state(mainWindow, WCQ_VERSION_AUTHOR_HASH,
                         (LPARAM)index, &value) ||
            (UINT)(DWORD_PTR)value == 0u) {
            fwprintf(stderr, L"version %llu had no author tag\n",
                     (unsigned long long)index);
            result = 12;
            goto cleanup;
        }
        versionAuthorHashes[index] = (UINT)(DWORD_PTR)value;
    }

    if (!PostMessageW(mainWindow, WM_COMMAND,
                      MAKEWPARAM(IDM_REVIEW_DOCUMENT_CHAT, 0), 0)) {
        fwprintf(stderr, L"could not request Document Chat\n");
        result = 13;
        goto cleanup;
    }
    chatDialog = wait_for_dialog_with_control(
        process.dwProcessId, IDC_CHAT_MESSAGE);
    chatMessageEdit = chatDialog != NULL
                          ? GetDlgItem(chatDialog, IDC_CHAT_MESSAGE)
                          : NULL;
    chatSendButton = chatDialog != NULL
                         ? GetDlgItem(chatDialog, IDC_CHAT_SEND)
                         : NULL;
    if (chatDialog == NULL || chatMessageEdit == NULL ||
        chatSendButton == NULL ||
        GetDlgItem(chatDialog, IDC_CHAT_TRANSCRIPT) == NULL ||
        !send_bounded(chatMessageEdit, WM_SETTEXT, 0,
                      (LPARAM)chatMessage, NULL) ||
        !send_bounded(chatSendButton, BM_CLICK, 0, 0, NULL) ||
        !wait_for_state(mainWindow, WCQ_CHAT_COUNT, 0, 1)) {
        fwprintf(stderr, L"Unicode document chat message was not posted\n");
        result = 14;
        goto cleanup;
    }
    if (!query_state(mainWindow, WCQ_CHAT_COUNT, 0, &chatCount) ||
        chatCount != 1 ||
        !query_state(mainWindow, WCQ_CHAT_TEXT_HASH, 0, &value)) {
        fwprintf(stderr, L"document chat state was unavailable\n");
        result = 15;
        goto cleanup;
    }
    chatTextHash = (UINT)(DWORD_PTR)value;
    if (chatTextHash != probe_hash_text(chatMessage) ||
        !query_state(mainWindow, WCQ_CHAT_AUTHOR_HASH, 0, &value) ||
        (chatAuthorHash = (UINT)(DWORD_PTR)value) == 0u ||
        !query_state(mainWindow, WCQ_VERSION_COUNT, 0, &value) ||
        value != versionCount) {
        fwprintf(stderr,
                 L"chat text/author hash or version count was incorrect\n");
        result = 16;
        goto cleanup;
    }
    if (!close_dialog(chatDialog)) {
        fwprintf(stderr, L"Document Chat did not close\n");
        result = 17;
        goto cleanup;
    }
    chatDialog = NULL;

    if (!send_bounded(mainWindow, WM_COMMAND,
                      MAKEWPARAM(IDM_FILE_SAVE, 0), 0, NULL) ||
        !file_contains_ascii(sample, "wordcrafthistory")) {
        fwprintf(stderr, L"chat/version metadata was not saved in the RTF\n");
        result = 18;
        goto cleanup;
    }
    if (!close_process_cleanly(mainWindow, &process)) {
        fwprintf(stderr, L"saved WordCraft process did not close cleanly\n");
        result = 19;
        goto cleanup;
    }
    mainWindow = NULL;

    if (!launch_hidden(executable, sample, &process, &mainWindow)) {
        fwprintf(stderr, L"WordCraft did not reopen the saved RTF\n");
        result = 20;
        goto cleanup;
    }
    if (!wait_for_state(mainWindow, WCQ_CHAT_COUNT, 0, chatCount) ||
        !wait_for_state(mainWindow, WCQ_VERSION_COUNT, 0, versionCount) ||
        !query_state(mainWindow, WCQ_CHAT_TEXT_HASH, 0, &value) ||
        (UINT)(DWORD_PTR)value != chatTextHash ||
        !query_state(mainWindow, WCQ_CHAT_AUTHOR_HASH, 0, &value) ||
        (UINT)(DWORD_PTR)value != chatAuthorHash) {
        fwprintf(stderr,
                 L"chat count or hashes did not survive save/reopen\n");
        result = 21;
        goto cleanup;
    }
    if (!query_state(mainWindow, WCQ_VERSION_COUNT, 0,
                     &reopenedCount) ||
        reopenedCount != versionCount) {
        fwprintf(stderr, L"version count did not survive save/reopen\n");
        result = 22;
        goto cleanup;
    }
    for (index = 0; index < (SIZE_T)reopenedCount; ++index) {
        if (!query_state(mainWindow, WCQ_VERSION_AUTHOR_HASH,
                         (LPARAM)index, &value)) {
            fwprintf(stderr, L"reopened version author was unavailable\n");
            result = 23;
            goto cleanup;
        }
        reopenedHash = (UINT)(DWORD_PTR)value;
        if (reopenedHash != versionAuthorHashes[index]) {
            fwprintf(stderr,
                     L"version %llu author hash changed after reopen\n",
                     (unsigned long long)index);
            result = 24;
            goto cleanup;
        }
    }

    if (!PostMessageW(mainWindow, WM_COMMAND,
                      MAKEWPARAM(IDM_REVIEW_VERSION_HISTORY, 0), 0)) {
        fwprintf(stderr, L"could not request Version History\n");
        result = 25;
        goto cleanup;
    }
    versionDialog = wait_for_dialog_with_control(
        process.dwProcessId, IDC_VERSION_LIST);
    versionList = versionDialog != NULL
                      ? GetDlgItem(versionDialog, IDC_VERSION_LIST)
                      : NULL;
    versionPreview = versionDialog != NULL
                         ? GetDlgItem(versionDialog,
                                      IDC_VERSION_PREVIEW)
                         : NULL;
    versionRestore = versionDialog != NULL
                         ? GetDlgItem(versionDialog,
                                      IDC_VERSION_RESTORE)
                         : NULL;
    if (versionDialog == NULL || versionList == NULL ||
        versionPreview == NULL || versionRestore == NULL ||
        !send_bounded(versionList, LB_GETCOUNT, 0, 0, &rowCount) ||
        rowCount != reopenedCount || !IsWindowEnabled(versionRestore)) {
        fwprintf(stderr,
                 L"Version History controls/rows were incomplete "
                 L"(rows=%ld expected=%ld)\n",
                 (long)rowCount, (long)reopenedCount);
        result = 26;
        goto cleanup;
    }
    if (!close_dialog(versionDialog)) {
        fwprintf(stderr, L"Version History did not close\n");
        result = 27;
        goto cleanup;
    }
    versionDialog = NULL;

    if (!close_process_cleanly(mainWindow, &process)) {
        fwprintf(stderr, L"reopened WordCraft process did not close cleanly\n");
        result = 28;
        goto cleanup;
    }
    mainWindow = NULL;

    printf("history_baseline=ok history_edit_checkpoint=ok "
           "unicode_chat=ok rtf_persistence=ok "
           "chat_hashes=ok version_author_hashes=ok "
           "version_dialog=ok rows=%ld clean_exit=ok\n",
           (long)reopenedCount);
    result = 0;

cleanup:
    stop_process(&process);
    DeleteFileW(sample);
    return result;
}
