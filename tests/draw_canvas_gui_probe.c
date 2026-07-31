#include "editor.h"

#include <stdio.h>
#include <string.h>

#define DRAW_PROBE_TIMEOUT_MS 15000u
#define DRAW_PROBE_MESSAGE_TIMEOUT_MS 5000u
#define DRAW_PROBE_CANVAS_CLASS L"WordCraftDrawingCanvas"
#define DRAW_PROBE_INSERT_ID 0x7202
#define DRAW_PROBE_CANCEL_ID 0x7203
#define DRAW_PROBE_MAX_RTF_BYTES ((SIZE_T)64u * 1024u * 1024u)
#define DRAW_PROBE_ANY_OBJECT_COUNT ((UINT)-1)

typedef struct WindowSearch {
    DWORD processId;
    const WCHAR *className;
    HWND window;
} WindowSearch;

typedef struct ControlSearch {
    int id;
    HWND window;
} ControlSearch;

typedef struct DocumentSnapshot {
    LRESULT textLength;
    UINT objectCount;
    LRESULT modified;
    LRESULT canUndo;
    LRESULT canRedo;
} DocumentSnapshot;

static BOOL CALLBACK find_process_window(HWND window, LPARAM parameter)
{
    WindowSearch *search = (WindowSearch *)parameter;
    DWORD processId = 0;
    WCHAR className[128];

    GetWindowThreadProcessId(window, &processId);
    if (processId != search->processId ||
        GetClassNameW(window, className, ARRAYSIZE(className)) <= 0 ||
        lstrcmpW(className, search->className) != 0) {
        return TRUE;
    }
    search->window = window;
    return FALSE;
}

static HWND find_process_window_now(DWORD processId,
                                    const WCHAR *className)
{
    WindowSearch search;

    search.processId = processId;
    search.className = className;
    search.window = NULL;
    EnumWindows(find_process_window, (LPARAM)&search);
    return search.window;
}

static HWND wait_for_process_window(DWORD processId,
                                    const WCHAR *className)
{
    DWORD start = GetTickCount();

    do {
        HWND window = find_process_window_now(processId, className);
        if (window != NULL) {
            return window;
        }
        Sleep(25u);
    } while (GetTickCount() - start < DRAW_PROBE_TIMEOUT_MS);
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
    } while (GetTickCount() - start < DRAW_PROBE_TIMEOUT_MS);
    return FALSE;
}

static BOOL CALLBACK find_descendant_control(HWND window, LPARAM parameter)
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
    EnumChildWindows(parent, find_descendant_control, (LPARAM)&search);
    return search.window;
}

static BOOL send_bounded(HWND window, UINT message, WPARAM wParam,
                         LPARAM lParam, LRESULT *result)
{
    DWORD_PTR raw = 0;

    if (window == NULL ||
        SendMessageTimeoutW(window, message, wParam, lParam,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK,
                            DRAW_PROBE_MESSAGE_TIMEOUT_MS, &raw) == 0) {
        return FALSE;
    }
    if (result != NULL) {
        *result = (LRESULT)raw;
    }
    return TRUE;
}

static BOOL write_initial_rtf(const WCHAR *path)
{
    static const CHAR rtf[] =
        "{\\rtf1\\ansi\\deff0"
        "{\\fonttbl{\\f0\\fnil Times New Roman;}}"
        "\\f0\\fs24 Canvas baseline}";
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
              written == (DWORD)(sizeof(rtf) - 1u) &&
              FlushFileBuffers(file);
    if (!CloseHandle(file)) {
        success = FALSE;
    }
    return success;
}

static BOOL count_file_ascii_before(const WCHAR *path, const CHAR *needle,
                                    const CHAR *boundary, UINT *count)
{
    HANDLE file;
    LARGE_INTEGER length;
    BYTE *data = NULL;
    SIZE_T needleLength;
    SIZE_T boundaryLength = 0;
    SIZE_T total = 0;
    SIZE_T scanLength;
    SIZE_T index;
    UINT matches = 0;
    BOOL success = FALSE;

    if (path == NULL || needle == NULL || count == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    needleLength = strlen(needle);
    if (boundary != NULL) {
        boundaryLength = strlen(boundary);
    }
    if (needleLength == 0 || (boundary != NULL && boundaryLength == 0)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    file = CreateFileW(path, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE |
                           FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    if (!GetFileSizeEx(file, &length) || length.QuadPart < 0 ||
        (ULONGLONG)length.QuadPart > DRAW_PROBE_MAX_RTF_BYTES) {
        goto cleanup;
    }
    if (length.QuadPart != 0) {
        data = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)length.QuadPart);
        if (data == NULL) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            goto cleanup;
        }
    }
    while (total < (SIZE_T)length.QuadPart) {
        DWORD read = 0;
        DWORD request = (DWORD)min(
            (SIZE_T)1024u * 1024u,
            (SIZE_T)length.QuadPart - total);
        if (!ReadFile(file, data + total, request, &read, NULL) ||
            read == 0) {
            goto cleanup;
        }
        total += read;
    }
    scanLength = total;
    if (boundaryLength <= total && boundaryLength != 0) {
        for (index = 0; index <= total - boundaryLength; ++index) {
            if (memcmp(data + index, boundary, boundaryLength) == 0) {
                scanLength = index;
                break;
            }
        }
    }
    if (needleLength <= scanLength) {
        for (index = 0; index <= scanLength - needleLength; ++index) {
            if (memcmp(data + index, needle, needleLength) == 0) {
                ++matches;
            }
        }
    }
    *count = matches;
    success = TRUE;

cleanup:
    if (data != NULL) {
        HeapFree(GetProcessHeap(), 0, data);
    }
    CloseHandle(file);
    return success;
}

static BOOL count_file_ascii(const WCHAR *path, const CHAR *needle,
                             UINT *count)
{
    return count_file_ascii_before(path, needle, NULL, count);
}

static BOOL wait_for_file_ascii_count_before(const WCHAR *path,
                                             const CHAR *needle,
                                             const CHAR *boundary,
                                             UINT expected)
{
    DWORD start = GetTickCount();

    do {
        UINT count = 0;
        if (count_file_ascii_before(path, needle, boundary, &count) &&
            count == expected) {
            return TRUE;
        }
        Sleep(25u);
    } while (GetTickCount() - start < DRAW_PROBE_TIMEOUT_MS);
    return FALSE;
}

static BOOL launch_hidden(const WCHAR *executable, const WCHAR *path,
                          PROCESS_INFORMATION *process, HWND *mainWindow)
{
    WCHAR commandLine[PATH_CAPACITY * 2];
    STARTUPINFOW startup;

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
    (void)WaitForInputIdle(process->hProcess, 5000u);
    *mainWindow = wait_for_process_window(process->dwProcessId,
                                          APP_CLASS_NAME);
    return *mainWindow != NULL;
}

static BOOL capture_document_snapshot(HWND editor,
                                      DocumentSnapshot *snapshot)
{
    WCHAR *text = NULL;
    LRESULT copied = 0;
    LRESULT length = 0;
    UINT index;
    BOOL success = FALSE;

    if (editor == NULL || snapshot == NULL ||
        !send_bounded(editor, WM_GETTEXTLENGTH, 0, 0, &length) ||
        length < 0 || length > 1024 * 1024) {
        return FALSE;
    }
    text = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                     ((SIZE_T)length + 1u) * sizeof(*text));
    if (text == NULL) {
        return FALSE;
    }
    if (!send_bounded(editor, WM_GETTEXT, (WPARAM)(length + 1),
                      (LPARAM)text, &copied) ||
        copied < 0 || copied > length) {
        goto cleanup;
    }
    ZeroMemory(snapshot, sizeof(*snapshot));
    snapshot->textLength = length;
    for (index = 0; index < (UINT)copied; ++index) {
        if (text[index] == L'\xFFFC') {
            ++snapshot->objectCount;
        }
    }
    if (!send_bounded(editor, EM_GETMODIFY, 0, 0,
                      &snapshot->modified) ||
        !send_bounded(editor, EM_CANUNDO, 0, 0,
                      &snapshot->canUndo) ||
        !send_bounded(editor, EM_CANREDO, 0, 0,
                      &snapshot->canRedo)) {
        goto cleanup;
    }
    success = TRUE;

cleanup:
    HeapFree(GetProcessHeap(), 0, text);
    return success;
}

static BOOL wait_for_document(HWND editor, LRESULT textLength,
                              UINT objectCount, int canUndo,
                              int canRedo, DocumentSnapshot *actual)
{
    DWORD start = GetTickCount();

    do {
        DocumentSnapshot snapshot;
        if (capture_document_snapshot(editor, &snapshot)) {
            if (actual != NULL) {
                *actual = snapshot;
            }
            if (snapshot.textLength == textLength &&
                (objectCount == DRAW_PROBE_ANY_OBJECT_COUNT ||
                 snapshot.objectCount == objectCount) &&
                (canUndo < 0 ||
                 (snapshot.canUndo != 0) == (canUndo != 0)) &&
                (canRedo < 0 ||
                 (snapshot.canRedo != 0) == (canRedo != 0))) {
                return TRUE;
            }
        }
        Sleep(25u);
    } while (GetTickCount() - start < DRAW_PROBE_TIMEOUT_MS);
    return FALSE;
}

static BOOL map_child_rect_to_parent(HWND child, HWND parent, RECT *rect)
{
    if (child == NULL || parent == NULL || rect == NULL ||
        !GetWindowRect(child, rect)) {
        return FALSE;
    }
    return MapWindowPoints(NULL, parent, (POINT *)rect, 2) != 0;
}

static BOOL simulate_canvas_stroke(HWND canvas)
{
    HWND blackPen;
    HWND insertButton;
    RECT client;
    RECT toolbarRect;
    RECT insertRect;
    POINT start;
    POINT finish;
    int top;
    int bottom;
    int step;

    blackPen = GetDlgItem(canvas, IDM_DRAW_PEN_BLACK);
    insertButton = GetDlgItem(canvas, DRAW_PROBE_INSERT_ID);
    if (blackPen == NULL || insertButton == NULL ||
        !GetClientRect(canvas, &client) ||
        !map_child_rect_to_parent(blackPen, canvas, &toolbarRect) ||
        !map_child_rect_to_parent(insertButton, canvas, &insertRect)) {
        return FALSE;
    }
    top = toolbarRect.bottom + 20;
    bottom = insertRect.top - 20;
    if (client.right - client.left < 240 || bottom - top < 120) {
        return FALSE;
    }
    start.x = client.left + (client.right - client.left) / 4;
    start.y = top + (bottom - top) / 3;
    finish.x = client.left + (client.right - client.left) * 3 / 4;
    finish.y = top + (bottom - top) * 2 / 3;

    if (!send_bounded(canvas, WM_LBUTTONDOWN, MK_LBUTTON,
                      MAKELPARAM((WORD)start.x, (WORD)start.y), NULL)) {
        return FALSE;
    }
    for (step = 1; step <= 12; ++step) {
        int x = start.x + (finish.x - start.x) * step / 12;
        int y = start.y + (finish.y - start.y) * step / 12;
        if (!send_bounded(canvas, WM_MOUSEMOVE, MK_LBUTTON,
                          MAKELPARAM((WORD)x, (WORD)y), NULL)) {
            return FALSE;
        }
    }
    return send_bounded(
        canvas, WM_LBUTTONUP, 0,
        MAKELPARAM((WORD)finish.x, (WORD)finish.y), NULL);
}

static BOOL wait_for_control_enabled(HWND control)
{
    DWORD start = GetTickCount();

    do {
        if (IsWindow(control) && IsWindowEnabled(control)) {
            return TRUE;
        }
        Sleep(25u);
    } while (GetTickCount() - start < DRAW_PROBE_TIMEOUT_MS);
    return FALSE;
}

static BOOL open_canvas(HWND mainWindow, DWORD processId, HWND *canvas)
{
    if (!PostMessageW(mainWindow, WM_COMMAND,
                      MAKEWPARAM(IDM_DRAW_CANVAS, 0), 0)) {
        return FALSE;
    }
    *canvas = wait_for_process_window(processId,
                                      DRAW_PROBE_CANVAS_CLASS);
    return *canvas != NULL &&
           GetWindow(mainWindow, GW_ENABLEDPOPUP) == *canvas;
}

static BOOL click_canvas_child(HWND canvas, int id)
{
    HWND button = GetDlgItem(canvas, id);

    return button != NULL && IsWindowEnabled(button) &&
           PostMessageW(button, BM_CLICK, 0, 0) &&
           wait_for_window_destroyed(canvas);
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
                               DRAW_PROBE_TIMEOUT_MS);
    if (wait != WAIT_OBJECT_0 ||
        !GetExitCodeProcess(process->hProcess, &exitCode)) {
        return FALSE;
    }
    CloseHandle(process->hProcess);
    process->hProcess = NULL;
    return exitCode == 0;
}

static void cancel_open_canvas(DWORD processId)
{
    HWND canvas = find_process_window_now(
        processId, DRAW_PROBE_CANVAS_CLASS);

    if (canvas != NULL) {
        HWND cancel = GetDlgItem(canvas, DRAW_PROBE_CANCEL_ID);
        if (cancel != NULL) {
            (void)PostMessageW(cancel, BM_CLICK, 0, 0);
        } else {
            (void)PostMessageW(canvas, WM_CLOSE, 0, 0);
        }
        (void)wait_for_window_destroyed(canvas);
    }
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
    WCHAR executable[PATH_CAPACITY];
    WCHAR tempDirectory[PATH_CAPACITY];
    WCHAR sample[PATH_CAPACITY];
    PROCESS_INFORMATION process;
    HWND mainWindow = NULL;
    HWND editor = NULL;
    HWND canvas = NULL;
    HWND insertButton;
    DocumentSnapshot baseline;
    DocumentSnapshot inserted;
    DocumentSnapshot reopened;
    DocumentSnapshot beforeCancel;
    DocumentSnapshot afterCancel;
    UINT pictureCount = 0;
    UINT emfCount = 0;
    UINT wmfCount = 0;
    UINT shapePictureCount = 0;
    UINT fallbackPictureCount = 0;
    DWORD processId = GetCurrentProcessId();
    int result = 1;

    ZeroMemory(&process, sizeof(process));
    ZeroMemory(&baseline, sizeof(baseline));
    ZeroMemory(&inserted, sizeof(inserted));
    ZeroMemory(&reopened, sizeof(reopened));
    ZeroMemory(&beforeCancel, sizeof(beforeCancel));
    ZeroMemory(&afterCancel, sizeof(afterCancel));
    sample[0] = L'\0';
    SetProcessDPIAware();

    if (!resolve_test_app(L"wordcraft.exe", executable,
                          ARRAYSIZE(executable)) ||
        GetTempPathW(ARRAYSIZE(tempDirectory), tempDirectory) == 0 ||
        FAILED(StringCchPrintfW(
            sample, ARRAYSIZE(sample), L"%swordcraft_draw_%lu_%lu.rtf",
            tempDirectory, (unsigned long)processId,
            (unsigned long)GetTickCount())) ||
        !write_initial_rtf(sample) ||
        !count_file_ascii(sample, "\\pict", &pictureCount) ||
        pictureCount != 0) {
        fwprintf(stderr,
                 L"could not prepare the drawing canvas RTF probe (error=%lu)\n",
                 (unsigned long)GetLastError());
        goto cleanup;
    }

    if (!launch_hidden(executable, sample, &process, &mainWindow)) {
        fwprintf(stderr,
                 L"could not launch WordCraft for the drawing canvas probe (error=%lu)\n",
                 (unsigned long)GetLastError());
        goto cleanup;
    }
    editor = find_control(mainWindow, IDC_EDITOR);
    if (editor == NULL ||
        !send_bounded(editor, EM_SETSEL, (WPARAM)-1, (LPARAM)-1,
                      NULL) ||
        !send_bounded(editor, EM_EMPTYUNDOBUFFER, 0, 0, NULL) ||
        !capture_document_snapshot(editor, &baseline) ||
        baseline.objectCount != 0 || baseline.modified != 0 ||
        baseline.canUndo != 0) {
        fwprintf(stderr,
                 L"the initial RichEdit document state was not clean\n");
        goto cleanup;
    }

    if (!open_canvas(mainWindow, process.dwProcessId, &canvas) ||
        !simulate_canvas_stroke(canvas)) {
        fwprintf(stderr,
                 L"the drawing canvas did not open or accept a bounded stroke\n");
        goto cleanup;
    }
    insertButton = GetDlgItem(canvas, DRAW_PROBE_INSERT_ID);
    if (!wait_for_control_enabled(insertButton) ||
        !click_canvas_child(canvas, DRAW_PROBE_INSERT_ID)) {
        fwprintf(stderr,
                 L"the private drawing canvas Insert button did not commit the stroke\n");
        goto cleanup;
    }
    canvas = NULL;
    /*
     * WM_GETTEXT does not expose RichEdit's object replacement character
     * consistently across a process boundary.  The one-character growth and
     * atomic Undo/Redo checks establish the live mutation here; the exact
     * persisted object type and count are verified from the saved RTF below.
     */
    if (!wait_for_document(editor, baseline.textLength + 1,
                           DRAW_PROBE_ANY_OBJECT_COUNT, 1, 0, &inserted) ||
        inserted.modified == 0) {
        fwprintf(stderr,
                 L"the canvas did not create one undoable embedded-picture "
                 L"mutation (baseline=%lld current=%lld text_objects=%u "
                 L"modified=%lld undo=%lld redo=%lld)\n",
                 (long long)baseline.textLength,
                 (long long)inserted.textLength,
                 inserted.objectCount,
                 (long long)inserted.modified,
                 (long long)inserted.canUndo,
                 (long long)inserted.canRedo);
        goto cleanup;
    }

    if (!send_bounded(mainWindow, WM_COMMAND,
                      MAKEWPARAM(IDM_EDIT_UNDO, 0), 0, NULL) ||
        !wait_for_document(editor, baseline.textLength,
                           DRAW_PROBE_ANY_OBJECT_COUNT, 0, 1,
                           NULL) ||
        !send_bounded(mainWindow, WM_COMMAND,
                      MAKEWPARAM(IDM_EDIT_REDO, 0), 0, NULL) ||
        !wait_for_document(editor, inserted.textLength,
                           DRAW_PROBE_ANY_OBJECT_COUNT, 1, 0,
                           NULL)) {
        fwprintf(stderr,
                 L"Undo/Redo did not treat the inserted drawing as one mutation\n");
        goto cleanup;
    }

    if (!send_bounded(mainWindow, WM_COMMAND,
                      MAKEWPARAM(IDM_FILE_SAVE, 0), 0, NULL) ||
        !wait_for_file_ascii_count_before(
            sample, "\\emfblip", "\\wordcrafthistory", 1u) ||
        !count_file_ascii_before(
            sample, "\\shppict", "\\wordcrafthistory",
            &shapePictureCount) ||
        shapePictureCount != 1u ||
        !count_file_ascii_before(
            sample, "\\nonshppict", "\\wordcrafthistory",
            &fallbackPictureCount) ||
        fallbackPictureCount != 1u ||
        !count_file_ascii_before(
            sample, "\\pict", "\\wordcrafthistory", &pictureCount) ||
        pictureCount != 2u ||
        !count_file_ascii_before(
            sample, "\\emfblip", "\\wordcrafthistory", &emfCount) ||
        emfCount != 1u ||
        !count_file_ascii_before(
            sample, "\\wmetafile8", "\\wordcrafthistory", &wmfCount) ||
        wmfCount != 1u) {
        fwprintf(stderr,
                 L"the saved RTF did not contain exactly one static EMF "
                 L"drawing before history metadata "
                 L"(shape=%u fallback=%u pict=%u emf=%u wmf=%u)\n",
                 shapePictureCount, fallbackPictureCount, pictureCount,
                 emfCount, wmfCount);
        goto cleanup;
    }
    if (!close_process_cleanly(mainWindow, &process)) {
        fwprintf(stderr,
                 L"WordCraft did not close cleanly after saving the drawing\n");
        goto cleanup;
    }
    mainWindow = NULL;
    editor = NULL;

    if (!launch_hidden(executable, sample, &process, &mainWindow)) {
        fwprintf(stderr,
                 L"could not reopen the saved drawing RTF (error=%lu)\n",
                 (unsigned long)GetLastError());
        goto cleanup;
    }
    editor = find_control(mainWindow, IDC_EDITOR);
    if (editor == NULL ||
        !wait_for_document(editor, inserted.textLength,
                           DRAW_PROBE_ANY_OBJECT_COUNT, 0, 0,
                           &reopened) ||
        reopened.modified != 0) {
        fwprintf(stderr,
                 L"the static drawing did not persist after close/reopen "
                 L"(expected_length=%lld current_length=%lld "
                 L"text_objects=%u modified=%lld undo=%lld redo=%lld)\n",
                 (long long)inserted.textLength,
                 (long long)reopened.textLength,
                 reopened.objectCount,
                 (long long)reopened.modified,
                 (long long)reopened.canUndo,
                 (long long)reopened.canRedo);
        goto cleanup;
    }

    if (!send_bounded(editor, EM_EMPTYUNDOBUFFER, 0, 0, NULL) ||
        !capture_document_snapshot(editor, &beforeCancel) ||
        !open_canvas(mainWindow, process.dwProcessId, &canvas) ||
        !click_canvas_child(canvas, DRAW_PROBE_CANCEL_ID)) {
        fwprintf(stderr,
                 L"the second drawing canvas did not cancel through private ID 0x7203\n");
        goto cleanup;
    }
    canvas = NULL;
    if (!capture_document_snapshot(editor, &afterCancel) ||
        afterCancel.textLength != beforeCancel.textLength ||
        afterCancel.objectCount != beforeCancel.objectCount ||
        afterCancel.modified != beforeCancel.modified ||
        afterCancel.canUndo != beforeCancel.canUndo ||
        afterCancel.canRedo != beforeCancel.canRedo ||
        !count_file_ascii_before(
            sample, "\\shppict", "\\wordcrafthistory", &pictureCount) ||
        pictureCount != 1u) {
        fwprintf(stderr,
                 L"canceling a fresh drawing canvas mutated the document\n");
        goto cleanup;
    }

    if (!close_process_cleanly(mainWindow, &process)) {
        fwprintf(stderr,
                 L"WordCraft did not close cleanly after the cancel probe\n");
        goto cleanup;
    }
    mainWindow = NULL;
    result = 0;
    wprintf(L"draw_canvas=ok stroke=bounded static_emf=1 "
            L"undo_redo=ok rtf_emf=ok reopen=ok cancel=nondestructive\n");

cleanup:
    if (process.hProcess != NULL) {
        cancel_open_canvas(process.dwProcessId);
    }
    stop_process(&process);
    if (sample[0] != L'\0') {
        (void)DeleteFileW(sample);
    }
    return result;
}
