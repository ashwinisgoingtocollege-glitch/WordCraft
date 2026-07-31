#include "editor.h"

#include <stdio.h>
#include <string.h>

#define LIVE_GUI_TIMEOUT_MS 12000u
#define LIVE_GUI_MESSAGE_TIMEOUT_MS 5000u

#ifndef WCQ_LIVE_DOCUMENT_PENDING
#define WCQ_LIVE_DOCUMENT_PENDING 113
#endif

typedef struct WindowSearch {
    DWORD processId;
    const WCHAR *className;
    HWND window;
} WindowSearch;

typedef struct ControlSearch {
    int id;
    HWND window;
} ControlSearch;

typedef struct DialogControlSearch {
    DWORD processId;
    int controlId;
    HWND excludedWindow;
    HWND window;
} DialogControlSearch;

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

static BOOL CALLBACK find_process_dialog_control(HWND window,
                                                 LPARAM parameter)
{
    DialogControlSearch *search = (DialogControlSearch *)parameter;
    DWORD processId = 0;
    WCHAR className[32];

    if (window == search->excludedWindow) {
        return TRUE;
    }
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

static HWND find_dialog_with_control(DWORD processId, int controlId,
                                     HWND excludedWindow)
{
    DialogControlSearch search;

    search.processId = processId;
    search.controlId = controlId;
    search.excludedWindow = excludedWindow;
    search.window = NULL;
    EnumWindows(find_process_dialog_control, (LPARAM)&search);
    return search.window;
}

static HWND wait_for_dialog_with_control(DWORD processId, int controlId,
                                         HWND excludedWindow)
{
    DWORD start = GetTickCount();

    do {
        HWND dialog = find_dialog_with_control(processId, controlId,
                                               excludedWindow);
        if (dialog != NULL) {
            return dialog;
        }
        Sleep(25);
    } while (GetTickCount() - start < LIVE_GUI_TIMEOUT_MS);
    return NULL;
}

static BOOL wait_for_window_destroyed(HWND window)
{
    DWORD start = GetTickCount();

    do {
        if (!IsWindow(window)) {
            return TRUE;
        }
        Sleep(25);
    } while (GetTickCount() - start < LIVE_GUI_TIMEOUT_MS);
    return FALSE;
}

static BOOL send_bounded(HWND window, UINT message, WPARAM wParam,
                         LPARAM lParam, LRESULT *result)
{
    DWORD_PTR raw = 0;
    if (SendMessageTimeoutW(window, message, wParam, lParam,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK,
                            LIVE_GUI_MESSAGE_TIMEOUT_MS, &raw) == 0) {
        return FALSE;
    }
    if (result != NULL) {
        *result = (LRESULT)raw;
    }
    return TRUE;
}

static BOOL query_state(HWND window, UINT query, LRESULT *value)
{
    return send_bounded(window, WCM_QUERY_STATE, query, 0, value);
}

static BOOL query_state_component(HWND window, UINT query, LPARAM component,
                                  LRESULT *value)
{
    return send_bounded(window, WCM_QUERY_STATE, query, component, value);
}

static BOOL wait_for_state(HWND window, UINT query, LRESULT expected)
{
    DWORD start = GetTickCount();
    do {
        LRESULT value = 0;
        if (query_state(window, query, &value) && value == expected) {
            return TRUE;
        }
        Sleep(25);
    } while (GetTickCount() - start < LIVE_GUI_TIMEOUT_MS);
    return FALSE;
}

static BOOL get_text(HWND window, WCHAR *text, size_t capacity)
{
    return text != NULL && capacity > 0 &&
           send_bounded(window, WM_GETTEXT, capacity, (LPARAM)text, NULL);
}

static BOOL wait_for_text(HWND editor, const WCHAR *expected)
{
    DWORD start = GetTickCount();
    WCHAR text[512];
    do {
        text[0] = L'\0';
        if (get_text(editor, text, ARRAYSIZE(text)) &&
            wcscmp(text, expected) == 0) {
            return TRUE;
        }
        Sleep(25);
    } while (GetTickCount() - start < LIVE_GUI_TIMEOUT_MS);
    return FALSE;
}

static BOOL wait_for_revision_convergence(HWND host, HWND client,
                                          LRESULT minimum,
                                          LRESULT *revision)
{
    DWORD start = GetTickCount();
    do {
        LRESULT hostValue = 0;
        LRESULT clientValue = 0;
        if (query_state(host, WCQ_LIVE_REVISION_LOW, &hostValue) &&
            query_state(client, WCQ_LIVE_REVISION_LOW, &clientValue) &&
            hostValue >= minimum && hostValue == clientValue) {
            if (revision != NULL) {
                *revision = hostValue;
            }
            return TRUE;
        }
        Sleep(25);
    } while (GetTickCount() - start < LIVE_GUI_TIMEOUT_MS);
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

static BOOL wait_for_chat_convergence(HWND host, HWND client,
                                      LRESULT expectedCount,
                                      UINT expectedTextHash)
{
    DWORD start = GetTickCount();

    do {
        LRESULT hostCount = 0;
        LRESULT clientCount = 0;
        LRESULT hostHash = 0;
        LRESULT clientHash = 0;
        LRESULT hostAuthor = 0;
        LRESULT clientAuthor = 0;
        LPARAM index = expectedCount > 0 ? expectedCount - 1 : 0;

        if (query_state(host, WCQ_CHAT_COUNT, &hostCount) &&
            query_state(client, WCQ_CHAT_COUNT, &clientCount) &&
            query_state_component(host, WCQ_CHAT_TEXT_HASH,
                                  index, &hostHash) &&
            query_state_component(client, WCQ_CHAT_TEXT_HASH,
                                  index, &clientHash) &&
            query_state_component(host, WCQ_CHAT_AUTHOR_HASH,
                                  index, &hostAuthor) &&
            query_state_component(client, WCQ_CHAT_AUTHOR_HASH,
                                  index, &clientAuthor) &&
            hostCount == expectedCount &&
            clientCount == expectedCount &&
            (UINT)(DWORD_PTR)hostHash == expectedTextHash &&
            (UINT)(DWORD_PTR)clientHash == expectedTextHash &&
            hostAuthor != 0 && hostAuthor == clientAuthor) {
            return TRUE;
        }
        Sleep(25);
    } while (GetTickCount() - start < LIVE_GUI_TIMEOUT_MS);
    return FALSE;
}

static BOOL wait_for_local_chat(HWND window, LRESULT expectedCount,
                                LPARAM expectedIndex,
                                UINT expectedTextHash)
{
    DWORD start = GetTickCount();

    do {
        LRESULT count = 0;
        LRESULT textHash = 0;

        if (query_state(window, WCQ_CHAT_COUNT, &count) &&
            query_state_component(window, WCQ_CHAT_TEXT_HASH,
                                  expectedIndex, &textHash) &&
            count == expectedCount &&
            (UINT)(DWORD_PTR)textHash == expectedTextHash) {
            return TRUE;
        }
        Sleep(25);
    } while (GetTickCount() - start < LIVE_GUI_TIMEOUT_MS);
    return FALSE;
}

static BOOL wait_for_version_author_convergence(HWND host, HWND client)
{
    DWORD start = GetTickCount();

    do {
        LRESULT hostCount = 0;
        LRESULT clientCount = 0;
        LRESULT hostAuthor = 0;
        LRESULT clientAuthor = 0;

        if (query_state(host, WCQ_VERSION_COUNT, &hostCount) &&
            query_state(client, WCQ_VERSION_COUNT, &clientCount) &&
            hostCount >= 2 && hostCount == clientCount &&
            query_state_component(
                host, WCQ_VERSION_AUTHOR_HASH, hostCount - 1,
                &hostAuthor) &&
            query_state_component(
                client, WCQ_VERSION_AUTHOR_HASH, clientCount - 1,
                &clientAuthor) &&
            hostAuthor != 0 && hostAuthor == clientAuthor) {
            return TRUE;
        }
        Sleep(25);
    } while (GetTickCount() - start < LIVE_GUI_TIMEOUT_MS);
    return FALSE;
}

static BOOL wait_for_paper_state(HWND window, PaperSizeId expectedId,
                                 LRESULT expectedWidth,
                                 LRESULT expectedHeight)
{
    DWORD start = GetTickCount();

    do {
        LRESULT id = -1;
        LRESULT width = 0;
        LRESULT height = 0;
        if (query_state(window, WCQ_PAPER_SIZE_ID, &id) &&
            query_state_component(window, WCQ_PAGE_SIZE_THOUSANDTHS,
                                  0, &width) &&
            query_state_component(window, WCQ_PAGE_SIZE_THOUSANDTHS,
                                  1, &height) &&
            id == (LRESULT)expectedId && width == expectedWidth &&
            height == expectedHeight) {
            return TRUE;
        }
        Sleep(25);
    } while (GetTickCount() - start < LIVE_GUI_TIMEOUT_MS);
    return FALSE;
}

static BOOL write_initial_rtf(const WCHAR *path)
{
    static const char rtf[] =
        "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Times New Roman;}}"
        "\\f0\\fs24 \\b Homework\\b0  seed}";
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    BOOL success;

    if (file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    success = WriteFile(file, rtf, (DWORD)(sizeof(rtf) - 1), &written, NULL) &&
              written == sizeof(rtf) - 1;
    CloseHandle(file);
    return success;
}

static BOOL launch_hidden(const WCHAR *executable, const WCHAR *path,
                          PROCESS_INFORMATION *process, HWND *window)
{
    WCHAR commandLine[PATH_CAPACITY * 2];
    STARTUPINFOW startup;
    WindowSearch search;
    DWORD start;

    if (path != NULL && path[0] != L'\0') {
        if (FAILED(StringCchPrintfW(commandLine, ARRAYSIZE(commandLine),
                                    L"\"%s\" \"%s\"", executable, path))) {
            return FALSE;
        }
    } else if (FAILED(StringCchPrintfW(commandLine, ARRAYSIZE(commandLine),
                                       L"\"%s\"", executable))) {
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
    search.className = APP_CLASS_NAME;
    search.window = NULL;
    start = GetTickCount();
    do {
        EnumWindows(find_process_window, (LPARAM)&search);
        if (search.window != NULL) {
            *window = search.window;
            return TRUE;
        }
        Sleep(50);
    } while (GetTickCount() - start < LIVE_GUI_TIMEOUT_MS);
    return FALSE;
}

static BOOL selected_text_is_bold(HWND editor, HANDLE process)
{
    CHARFORMAT2W request;
    CHARFORMAT2W result;
    void *remote;
    SIZE_T transferred = 0;
    BOOL success = FALSE;

    ZeroMemory(&request, sizeof(request));
    request.cbSize = sizeof(request);
    remote = VirtualAllocEx(process, NULL, sizeof(request), MEM_COMMIT | MEM_RESERVE,
                            PAGE_READWRITE);
    if (remote == NULL) {
        return FALSE;
    }
    if (WriteProcessMemory(process, remote, &request, sizeof(request),
                           &transferred) && transferred == sizeof(request) &&
        send_bounded(editor, EM_GETCHARFORMAT, SCF_SELECTION,
                     (LPARAM)remote, NULL) &&
        ReadProcessMemory(process, remote, &result, sizeof(result),
                          &transferred) && transferred == sizeof(result)) {
        success = (result.dwMask & CFM_BOLD) != 0 &&
                  (result.dwEffects & CFE_BOLD) != 0;
    }
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    return success;
}

static void clear_live_environment(void)
{
    SetEnvironmentVariableA("WORDCRAFT_LIVE_TEST_MODE", NULL);
    SetEnvironmentVariableA("WORDCRAFT_LIVE_AUTOSTART_HOST", NULL);
    SetEnvironmentVariableA("WORDCRAFT_LIVE_AUTOSTART_JOIN", NULL);
    SetEnvironmentVariableA("WORDCRAFT_LIVE_PORT", NULL);
    SetEnvironmentVariableA("WORDCRAFT_LIVE_TOKEN", NULL);
    SetEnvironmentVariableA("WORDCRAFT_LIVE_ADVERTISED_HOST", NULL);
}

static BOOL close_discarding_changes(HWND mainWindow,
                                     PROCESS_INFORMATION *process)
{
    DWORD start;

    if (!PostMessageW(mainWindow, WM_CLOSE, 0, 0)) {
        return FALSE;
    }
    start = GetTickCount();
    do {
        WindowSearch dialog;
        DWORD wait = WaitForSingleObject(process->hProcess, 0);
        if (wait == WAIT_OBJECT_0) {
            DWORD exitCode = 1;
            BOOL success = GetExitCodeProcess(process->hProcess, &exitCode) &&
                           exitCode == 0;
            CloseHandle(process->hProcess);
            process->hProcess = NULL;
            return success;
        }
        dialog.processId = process->dwProcessId;
        dialog.className = L"#32770";
        dialog.window = NULL;
        EnumWindows(find_process_window, (LPARAM)&dialog);
        if (dialog.window != NULL) {
            HWND noButton = GetDlgItem(dialog.window, IDNO);
            if (noButton != NULL) {
                (void)send_bounded(noButton, BM_CLICK, 0, 0, NULL);
            }
        }
        Sleep(25);
    } while (GetTickCount() - start < LIVE_GUI_TIMEOUT_MS);
    return FALSE;
}

static void stop_process(PROCESS_INFORMATION *process)
{
    DWORD code = 0;
    if (process->hProcess == NULL) {
        return;
    }
    if (GetExitCodeProcess(process->hProcess, &code) && code == STILL_ACTIVE) {
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

int wmain(void)
{
    static const char token[] = "00112233445566778899AABBCCDDEEFF";
    static const WCHAR collaborativeChat[] =
        L"Live review \x03A9 \x4F60\x597D \x2014 caf\u00E9";
    static const WCHAR collaborativeChatFollowup[] =
        L"Queued follow-up before the content update";
    static const WCHAR collaborativeContent[] =
        L"Client chat and content update \x03A9";
    WCHAR executable[PATH_CAPACITY];
    WCHAR fallback[PATH_CAPACITY];
    WCHAR sample[PATH_CAPACITY];
    WCHAR modulePath[PATH_CAPACITY];
    WCHAR *slash;
    char invitation[512];
    PROCESS_INFORMATION hostProcess;
    PROCESS_INFORMATION clientProcess;
    HWND hostWindow = NULL;
    HWND clientWindow = NULL;
    HWND hostEditor;
    HWND clientEditor;
    HWND commentEditor;
    LRESULT port = 0;
    LRESULT hostRevision = 0;
    LRESULT clientRevision = 0;
    DWORD tempLength;
    int result = 1;

    ZeroMemory(&hostProcess, sizeof(hostProcess));
    ZeroMemory(&clientProcess, sizeof(clientProcess));
    clear_live_environment();
    if (GetModuleFileNameW(NULL, modulePath, ARRAYSIZE(modulePath)) == 0) {
        fwprintf(stderr, L"could not locate probe executable\n");
        return 2;
    }
    slash = wcsrchr(modulePath, L'\\');
    if (slash == NULL) {
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
        FAILED(StringCchPrintfW(sample + tempLength,
                                ARRAYSIZE(sample) - tempLength,
                                L"wordcraft-live-gui-%lu-%lu.rtf",
                                GetCurrentProcessId(), GetTickCount())) ||
        !write_initial_rtf(sample)) {
        fwprintf(stderr, L"could not create live GUI sample\n");
        return 5;
    }

    SetEnvironmentVariableA("WORDCRAFT_LIVE_TEST_MODE", "1");
    SetEnvironmentVariableA("WORDCRAFT_LIVE_AUTOSTART_HOST", "1");
    SetEnvironmentVariableA("WORDCRAFT_LIVE_PORT", "0");
    SetEnvironmentVariableA("WORDCRAFT_LIVE_TOKEN", token);
    SetEnvironmentVariableA("WORDCRAFT_LIVE_ADVERTISED_HOST", "127.0.0.1");
    if (!launch_hidden(executable, sample, &hostProcess, &hostWindow)) {
        fwprintf(stderr, L"host did not launch\n");
        result = 6;
        goto cleanup;
    }
    clear_live_environment();
    hostEditor = find_control(hostWindow, IDC_EDITOR);
    if (hostEditor == NULL ||
        find_control(hostWindow, IDM_REVIEW_LIVE_SHARE) == NULL ||
        !wait_for_state(hostWindow, WCQ_LIVE_ROLE, 1) ||
        !wait_for_state(hostWindow, WCQ_LIVE_STATE, 2) ||
        !query_state(hostWindow, WCQ_LIVE_LISTEN_PORT, &port) || port <= 0) {
        fwprintf(stderr, L"host did not begin listening\n");
        result = 7;
        goto cleanup;
    }
    if (FAILED(StringCchPrintfA(invitation, ARRAYSIZE(invitation),
                                "wordcraft://127.0.0.1:%ld/%s",
                                (long)port, token))) {
        result = 8;
        goto cleanup;
    }
    SetEnvironmentVariableA("WORDCRAFT_LIVE_TEST_MODE", "1");
    SetEnvironmentVariableA("WORDCRAFT_LIVE_AUTOSTART_JOIN", invitation);
    if (!launch_hidden(executable, NULL, &clientProcess, &clientWindow)) {
        fwprintf(stderr, L"client did not launch\n");
        result = 9;
        goto cleanup;
    }
    clear_live_environment();
    clientEditor = find_control(clientWindow, IDC_EDITOR);
    if (clientEditor == NULL ||
        !wait_for_state(clientWindow, WCQ_LIVE_ROLE, 2) ||
        !wait_for_state(clientWindow, WCQ_LIVE_STATE, 4) ||
        !wait_for_state(hostWindow, WCQ_LIVE_CLIENT_COUNT, 1) ||
        !wait_for_text(clientEditor, L"Homework seed")) {
        fwprintf(stderr, L"initial GUI snapshot did not arrive\n");
        result = 10;
        goto cleanup;
    }
    if (!send_bounded(clientEditor, EM_SETSEL, 0, 8, NULL) ||
        !selected_text_is_bold(clientEditor, clientProcess.hProcess)) {
        fwprintf(stderr, L"RTF bold formatting was not synchronized\n");
        result = 11;
        goto cleanup;
    }

    commentEditor = find_control(hostWindow, IDC_COMMENT_EDIT);
    if (commentEditor == NULL ||
        !send_bounded(hostEditor, EM_SETSEL, 0, 8, NULL) ||
        !send_bounded(commentEditor, WM_SETTEXT, 0,
                      (LPARAM)L"Shared review comment", NULL) ||
        !send_bounded(hostWindow, WM_COMMAND,
                      MAKEWPARAM(IDM_REVIEW_ADD_COMMENT, 0), 0, NULL) ||
        !wait_for_state(clientWindow, WCQ_COMMENT_COUNT, 1)) {
        fwprintf(stderr, L"embedded comments were not synchronized\n");
        result = 12;
        goto cleanup;
    }

    if (!send_bounded(hostEditor, WM_SETTEXT, 0,
                      (LPARAM)L"Host side update", NULL) ||
        !wait_for_text(clientEditor, L"Host side update")) {
        fwprintf(stderr, L"host edit did not reach client\n");
        result = 13;
        goto cleanup;
    }
    if (!send_bounded(clientEditor, WM_SETTEXT, 0,
                      (LPARAM)L"Client side update", NULL) ||
        !wait_for_text(hostEditor, L"Client side update") ||
        !wait_for_text(clientEditor, L"Client side update")) {
        fwprintf(stderr, L"client edit did not converge through host\n");
        result = 14;
        goto cleanup;
    }
    if (!wait_for_revision_convergence(hostWindow, clientWindow, 4,
                                       &hostRevision)) {
        (void)query_state(clientWindow, WCQ_LIVE_REVISION_LOW,
                          &clientRevision);
        fwprintf(stderr, L"live revisions did not converge (%ld/%ld)\n",
                 (long)hostRevision, (long)clientRevision);
        result = 15;
        goto cleanup;
    }
    clientRevision = hostRevision;
    Sleep(800);
    {
        LRESULT stableHost = 0;
        LRESULT stableClient = 0;
        if (!query_state(hostWindow, WCQ_LIVE_REVISION_LOW, &stableHost) ||
            !query_state(clientWindow, WCQ_LIVE_REVISION_LOW, &stableClient) ||
            stableHost != hostRevision || stableClient != clientRevision ||
            !wait_for_state(hostWindow, WCQ_LIVE_APPLYING_REMOTE, 0) ||
            !wait_for_state(clientWindow, WCQ_LIVE_APPLYING_REMOTE, 0)) {
            fwprintf(stderr, L"remote application echoed or did not settle\n");
            result = 16;
            goto cleanup;
        }
    }

    {
        HWND chatDialog;
        HWND chatMessage;
        HWND chatSend;
        HWND chatClose;

        if (!PostMessageW(clientWindow, WM_COMMAND,
                          MAKEWPARAM(IDM_REVIEW_DOCUMENT_CHAT, 0), 0)) {
            fwprintf(stderr, L"could not open collaborative Document Chat\n");
            result = 26;
            goto cleanup;
        }
        chatDialog = wait_for_dialog_with_control(
            clientProcess.dwProcessId, IDC_CHAT_MESSAGE, NULL);
        chatMessage = chatDialog != NULL
                          ? GetDlgItem(chatDialog, IDC_CHAT_MESSAGE)
                          : NULL;
        chatSend = chatDialog != NULL
                       ? GetDlgItem(chatDialog, IDC_CHAT_SEND)
                       : NULL;
        if (chatDialog == NULL || chatMessage == NULL || chatSend == NULL ||
            GetDlgItem(chatDialog, IDC_CHAT_TRANSCRIPT) == NULL ||
            !send_bounded(chatMessage, WM_SETTEXT, 0,
                          (LPARAM)collaborativeChat, NULL) ||
            !send_bounded(chatSend, BM_CLICK, 0, 0, NULL) ||
            !wait_for_local_chat(
                clientWindow, 1, 0,
                probe_hash_text(collaborativeChat)) ||
            !send_bounded(chatMessage, WM_SETTEXT, 0,
                          (LPARAM)collaborativeChatFollowup, NULL) ||
            !send_bounded(chatSend, BM_CLICK, 0, 0, NULL) ||
            !wait_for_local_chat(
                clientWindow, 2, 1,
                probe_hash_text(collaborativeChatFollowup)) ||
            !send_bounded(clientEditor, WM_SETTEXT, 0,
                          (LPARAM)collaborativeContent, NULL) ||
            !wait_for_text(hostEditor, collaborativeContent) ||
            !wait_for_text(clientEditor, collaborativeContent) ||
            !wait_for_chat_convergence(
                hostWindow, clientWindow, 2,
                probe_hash_text(collaborativeChatFollowup)) ||
            !wait_for_state(hostWindow, WCQ_LIVE_DOCUMENT_PENDING, 0) ||
            !wait_for_state(clientWindow, WCQ_LIVE_DOCUMENT_PENDING, 0) ||
            !wait_for_version_author_convergence(
                hostWindow, clientWindow)) {
            LRESULT hostChats = 0;
            LRESULT clientChats = 0;
            LRESULT hostPending = 0;
            LRESULT clientPending = 0;
            LRESULT hostLiveRevision = 0;
            LRESULT clientLiveRevision = 0;
            (void)query_state(hostWindow, WCQ_CHAT_COUNT,
                              &hostChats);
            (void)query_state(clientWindow, WCQ_CHAT_COUNT,
                              &clientChats);
            (void)query_state(hostWindow,
                              WCQ_LIVE_DOCUMENT_PENDING,
                              &hostPending);
            (void)query_state(clientWindow,
                              WCQ_LIVE_DOCUMENT_PENDING,
                              &clientPending);
            (void)query_state(hostWindow, WCQ_LIVE_REVISION_LOW,
                              &hostLiveRevision);
            (void)query_state(clientWindow, WCQ_LIVE_REVISION_LOW,
                              &clientLiveRevision);
            fwprintf(
                stderr,
                L"client chat/content update did not converge together "
                L"(chat=%ld/%ld pending=%ld/%ld rev=%ld/%ld)\n",
                (long)hostChats, (long)clientChats,
                (long)hostPending, (long)clientPending,
                (long)hostLiveRevision, (long)clientLiveRevision);
            result = 27;
            goto cleanup;
        }
        chatClose = GetDlgItem(chatDialog, IDCANCEL);
        if (chatClose == NULL ||
            !send_bounded(chatClose, BM_CLICK, 0, 0, NULL) ||
            !wait_for_window_destroyed(chatDialog)) {
            fwprintf(stderr,
                     L"collaborative Document Chat did not close promptly\n");
            result = 28;
            goto cleanup;
        }
    }

    {
        HWND paperSizeCombo = find_control(hostWindow,
                                           IDC_PAPER_SIZE_COMBO);
        LRESULT selection = CB_ERR;

        if (paperSizeCombo == NULL ||
            !send_bounded(paperSizeCombo, CB_SETCURSEL,
                          (WPARAM)PAPER_SIZE_A4, 0, &selection) ||
            selection != (LRESULT)PAPER_SIZE_A4 ||
            !send_bounded(hostWindow, WM_COMMAND,
                          MAKEWPARAM(IDC_PAPER_SIZE_COMBO,
                                    CBN_SELENDOK),
                          (LPARAM)paperSizeCombo, NULL) ||
            !wait_for_paper_state(clientWindow, PAPER_SIZE_A4,
                                  8268, 11693)) {
            fwprintf(stderr,
                     L"A4 paper size did not synchronize to the client\n");
            result = 17;
            goto cleanup;
        }
    }

    if (!send_bounded(hostEditor, WM_SETTEXT, 0,
                      (LPARAM)L"Final update before leaving", NULL) ||
        !wait_for_state(hostWindow, WCQ_LIVE_DOCUMENT_PENDING, 1) ||
        !send_bounded(hostWindow, WM_COMMAND,
                      MAKEWPARAM(IDM_LIVE_LEAVE_SESSION, 0), 0, NULL) ||
        !wait_for_text(clientEditor, L"Final update before leaving") ||
        !wait_for_state(hostWindow, WCQ_LIVE_ROLE, 0) ||
        !wait_for_state(clientWindow, WCQ_LIVE_ROLE, 0)) {
        fwprintf(stderr, L"host leave discarded its pending final edit\n");
        result = 18;
        goto cleanup;
    }

    {
        static const WCHAR preservedText[] = L"Keep this local document";
        static const WCHAR invalidInvitation[] = L"not-a-wordcraft-invitation";
        HWND liveDialog;
        HWND invitationEdit;
        HWND joinButton;
        HWND statusLabel;
        HWND feedbackDialog = NULL;
        HWND savePrompt = NULL;
        HWND closeButton;
        BOOL rejected = FALSE;
        DWORD start;

        if (!send_bounded(clientEditor, WM_SETTEXT, 0,
                          (LPARAM)preservedText, NULL) ||
            !PostMessageW(clientWindow, WM_COMMAND,
                          MAKEWPARAM(IDM_REVIEW_LIVE_SHARE, 0), 0)) {
            fwprintf(stderr, L"could not open invalid-invitation test UI\n");
            result = 19;
            goto cleanup;
        }
        liveDialog = wait_for_dialog_with_control(
            clientProcess.dwProcessId, IDC_LIVE_JOIN_INVITATION, NULL);
        invitationEdit = liveDialog != NULL
                             ? GetDlgItem(liveDialog,
                                          IDC_LIVE_JOIN_INVITATION)
                             : NULL;
        joinButton = liveDialog != NULL
                         ? GetDlgItem(liveDialog, IDC_LIVE_JOIN_SESSION)
                         : NULL;
        statusLabel = liveDialog != NULL
                          ? GetDlgItem(liveDialog, IDC_LIVE_STATUS)
                          : NULL;
        if (liveDialog == NULL || invitationEdit == NULL ||
            joinButton == NULL || statusLabel == NULL ||
            !send_bounded(invitationEdit, WM_SETTEXT, 0,
                          (LPARAM)invalidInvitation, NULL) ||
            !PostMessageW(joinButton, BM_CLICK, 0, 0)) {
            fwprintf(stderr, L"invalid invitation could not be submitted\n");
            result = 20;
            goto cleanup;
        }

        start = GetTickCount();
        do {
            savePrompt = find_dialog_with_control(
                clientProcess.dwProcessId, IDNO, liveDialog);
            feedbackDialog = find_dialog_with_control(
                clientProcess.dwProcessId, IDOK, liveDialog);
            {
                WCHAR statusText[192];
                statusText[0] = L'\0';
                if (get_text(statusLabel, statusText,
                             ARRAYSIZE(statusText)) &&
                    wcscmp(statusText,
                           L"That live invitation is not valid") == 0) {
                    rejected = TRUE;
                }
            }
            if (savePrompt != NULL || feedbackDialog != NULL || rejected) {
                break;
            }
            Sleep(25);
        } while (GetTickCount() - start < LIVE_GUI_TIMEOUT_MS);

        if (savePrompt != NULL) {
            HWND cancelButton = GetDlgItem(savePrompt, IDCANCEL);
            if (cancelButton != NULL) {
                (void)send_bounded(cancelButton, BM_CLICK, 0, 0, NULL);
            }
            fwprintf(stderr,
                     L"invalid invitation prompted to replace/save the document\n");
            result = 21;
            goto cleanup;
        }
        if (feedbackDialog != NULL) {
            fwprintf(stderr,
                     L"invalid invitation opened a blocking feedback dialog\n");
            result = 22;
            goto cleanup;
        }
        if (!rejected || !wait_for_text(clientEditor, preservedText) ||
            !wait_for_state(clientWindow, WCQ_LIVE_ROLE, 0)) {
            WCHAR actualText[512];
            LRESULT actualRole = -1;
            actualText[0] = L'\0';
            (void)get_text(clientEditor, actualText, ARRAYSIZE(actualText));
            (void)query_state(clientWindow, WCQ_LIVE_ROLE, &actualRole);
            fwprintf(stderr,
                     L"invalid invitation changed state (text='%ls', role=%ld)\n",
                     actualText, (long)actualRole);
            result = 22;
            goto cleanup;
        }
        closeButton = GetDlgItem(liveDialog, IDCANCEL);
        if (closeButton == NULL ||
            !PostMessageW(closeButton, BM_CLICK, 0, 0) ||
            !wait_for_window_destroyed(liveDialog)) {
            fwprintf(stderr, L"live sharing dialog did not close\n");
            result = 23;
            goto cleanup;
        }
    }

    if (!close_discarding_changes(clientWindow, &clientProcess)) {
        fwprintf(stderr, L"client did not close cleanly\n");
        result = 24;
        goto cleanup;
    }
    if (!close_discarding_changes(hostWindow, &hostProcess)) {
        fwprintf(stderr, L"host did not close cleanly\n");
        result = 25;
        goto cleanup;
    }

    printf("gui_initial_rtf=ok gui_comments=ok gui_bidirectional_sync=ok "
           "no_echo=ok chat_content_sync=ok canonical_authors=ok "
           "paper_size_sync=ok "
           "clients=1 leave_flush=ok "
           "invalid_invite=ok "
           "clean_exit=ok\n");
    result = 0;

cleanup:
    clear_live_environment();
    stop_process(&clientProcess);
    stop_process(&hostProcess);
    DeleteFileW(sample);
    return result;
}
