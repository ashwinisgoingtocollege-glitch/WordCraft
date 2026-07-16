#include "editor.h"

#include <ole2.h>
#include <stdio.h>
#include <string.h>

static WCHAR probeSummary[192];

void ribbon_set_comment_summary(AppState *app, const WCHAR *summary)
{
    (void)app;
    StringCchCopyW(probeSummary, ARRAYSIZE(probeSummary),
                   summary != NULL ? summary : L"");
}

void app_set_status_message(AppState *app, const WCHAR *message)
{
    (void)app;
    (void)message;
}

void document_mark_modified(AppState *app, BOOL modified)
{
    app->modified = modified;
}

static UINT probe_hash(const WCHAR *text)
{
    UINT hash = 2166136261u;
    while (*text != L'\0') {
        hash ^= (UINT)*text++;
        hash *= 16777619u;
    }
    return hash;
}

static BOOL probe_write_file(const WCHAR *path, const BYTE *data, SIZE_T size)
{
    HANDLE file;
    SIZE_T total = 0;

    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    while (total < size) {
        DWORD amount = (DWORD)((size - total) > 0x40000000u
                                   ? 0x40000000u : size - total);
        DWORD written = 0;
        if (!WriteFile(file, data + total, amount, &written, NULL) ||
            written == 0) {
            CloseHandle(file);
            return FALSE;
        }
        total += written;
    }
    return CloseHandle(file);
}

static BOOL probe_contains(const BYTE *data, SIZE_T size, const char *needle)
{
    SIZE_T needleSize = strlen(needle);
    SIZE_T index;
    if (needleSize == 0 || needleSize > size) {
        return FALSE;
    }
    for (index = 0; index <= size - needleSize; ++index) {
        if (memcmp(data + index, needle, needleSize) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

int wmain(void)
{
    static const BYTE sourceRtf[] =
        "{\\rtf1\\ansi Xzz alpha beta gamma}";
    HMODULE rich = NULL;
    HWND parent = NULL;
    HWND editor = NULL;
    AppState app;
    WCHAR tempDirectory[MAX_PATH];
    WCHAR metadataPath[MAX_PATH];
    WCHAR plainPath[MAX_PATH];
    WCHAR longComment[COMMENT_TEXT_CAPACITY + 2];
    BYTE *embedded = NULL;
    SIZE_T embeddedSize = 0;
    DWORD error = ERROR_SUCCESS;
    LRESULT firstStart;
    LRESULT firstEnd;
    LRESULT secondStart;
    LRESULT secondEnd;
    HRESULT oleResult;
    int result = 1;

    ZeroMemory(&app, sizeof(app));
    metadataPath[0] = L'\0';
    plainPath[0] = L'\0';
    probeSummary[0] = L'\0';
    oleResult = OleInitialize(NULL);
    if (FAILED(oleResult)) {
        return 10;
    }
    rich = LoadLibraryW(L"Msftedit.dll");
    if (rich == NULL) {
        result = 11;
        goto cleanup;
    }
    parent = CreateWindowExW(0, L"STATIC", L"comments probe", WS_OVERLAPPED,
                             0, 0, 480, 240, NULL, NULL,
                             GetModuleHandleW(NULL), NULL);
    editor = CreateWindowExW(0, MSFTEDIT_CLASS, NULL,
                             WS_CHILD | ES_MULTILINE,
                             0, 0, 460, 220, parent, NULL,
                             GetModuleHandleW(NULL), NULL);
    if (parent == NULL || editor == NULL) {
        result = 12;
        goto cleanup;
    }
    app.mainWindow = parent;
    app.editor = editor;
    SetWindowTextW(editor, L"zz alpha beta gamma");
    if (!comments_initialize(&app) || comments_count(&app) != 0 ||
        wcscmp(probeSummary, L"No comments") != 0) {
        fwprintf(stderr, L"comment initialization failed (error=%lu)\n",
                 GetLastError());
        result = 13;
        goto cleanup;
    }

    SendMessageW(editor, EM_SETSEL, 3, 8);
    if (!comments_add(&app, L"First comment")) {
        fwprintf(stderr, L"first comment failed (error=%lu)\n", GetLastError());
        result = 14;
        goto cleanup;
    }
    SendMessageW(editor, EM_SETSEL, 9, 13);
    if (!comments_add(&app, L"Second: caf\x00E9 \x4E16\x754C") ||
        comments_query_state(&app, WCQ_COMMENT_COUNT, 0) != 2 ||
        comments_query_state(&app, WCQ_COMMENT_ACTIVE_INDEX, 0) != 1 ||
        (UINT)comments_query_state(&app, WCQ_COMMENT_TEXT_HASH, 0) !=
            probe_hash(L"First comment") ||
        (UINT)comments_query_state(&app, WCQ_COMMENT_TEXT_HASH, 1) !=
            probe_hash(L"Second: caf\x00E9 \x4E16\x754C")) {
        fwprintf(stderr, L"comment add/query failed\n");
        result = 15;
        goto cleanup;
    }

    SendMessageW(editor, EM_SETSEL, 0, 0);
    SendMessageW(editor, EM_REPLACESEL, TRUE, (LPARAM)L"X");
    firstStart = comments_query_state(&app, WCQ_COMMENT_ANCHOR_START, 0);
    firstEnd = comments_query_state(&app, WCQ_COMMENT_ANCHOR_END, 0);
    secondStart = comments_query_state(&app, WCQ_COMMENT_ANCHOR_START, 1);
    secondEnd = comments_query_state(&app, WCQ_COMMENT_ANCHOR_END, 1);
    if (firstStart != 4 || firstEnd != 9 ||
        secondStart != 10 || secondEnd != 14) {
        fwprintf(stderr,
                 L"live anchors failed (%lld,%lld) (%lld,%lld)\n",
                 (long long)firstStart, (long long)firstEnd,
                 (long long)secondStart, (long long)secondEnd);
        result = 16;
        goto cleanup;
    }

    SendMessageW(editor, EM_SETSEL, (WPARAM)firstStart, (LPARAM)firstEnd);
    comments_selection_changed(&app);
    if (comments_query_state(&app, WCQ_COMMENT_ACTIVE_INDEX, 0) != 0) {
        fwprintf(stderr, L"selection did not activate first comment\n");
        result = 17;
        goto cleanup;
    }
    comments_next(&app);
    if (comments_query_state(&app, WCQ_COMMENT_ACTIVE_INDEX, 0) != 1) {
        fwprintf(stderr, L"next comment failed\n");
        result = 18;
        goto cleanup;
    }
    comments_previous(&app);
    if (comments_query_state(&app, WCQ_COMMENT_ACTIVE_INDEX, 0) != 0) {
        fwprintf(stderr, L"previous comment failed\n");
        result = 19;
        goto cleanup;
    }

    if (!comments_embed_rtf(&app, sourceRtf, sizeof(sourceRtf) - 1,
                            &embedded, &embeddedSize, &error) ||
        embeddedSize <= sizeof(sourceRtf) - 1 ||
        !probe_contains(embedded, embeddedSize, "wordcraftcomments")) {
        fwprintf(stderr, L"RTF metadata embed failed (error=%lu)\n", error);
        result = 20;
        goto cleanup;
    }
    if (GetTempPathW(ARRAYSIZE(tempDirectory), tempDirectory) == 0 ||
        FAILED(StringCchPrintfW(metadataPath, ARRAYSIZE(metadataPath),
                                L"%swordcraft-comment-%lu-%lu.rtf",
                                tempDirectory, GetCurrentProcessId(),
                                GetTickCount())) ||
        FAILED(StringCchPrintfW(plainPath, ARRAYSIZE(plainPath),
                                L"%swordcraft-comment-plain-%lu-%lu.rtf",
                                tempDirectory, GetCurrentProcessId(),
                                GetTickCount())) ||
        !probe_write_file(metadataPath, embedded, embeddedSize) ||
        !probe_write_file(plainPath, sourceRtf, sizeof(sourceRtf) - 1)) {
        fwprintf(stderr, L"temporary RTF write failed (error=%lu)\n",
                 GetLastError());
        result = 21;
        goto cleanup;
    }
    comments_clear(&app);
    if (!comments_load_rtf_file(&app, metadataPath, &error) ||
        comments_count(&app) != 2 ||
        comments_query_state(&app, WCQ_COMMENT_ANCHOR_START, 0) != 4 ||
        comments_query_state(&app, WCQ_COMMENT_ANCHOR_END, 1) != 14 ||
        (UINT)comments_query_state(&app, WCQ_COMMENT_TEXT_HASH, 1) !=
            probe_hash(L"Second: caf\x00E9 \x4E16\x754C")) {
        fwprintf(stderr, L"RTF metadata load failed (error=%lu)\n", error);
        result = 22;
        goto cleanup;
    }

    app.modified = FALSE;
    comments_delete_active(&app);
    if (comments_count(&app) != 1 ||
        comments_query_state(&app, WCQ_COMMENT_ACTIVE_INDEX, 0) != 0 ||
        !app.modified) {
        fwprintf(stderr, L"comment delete failed\n");
        result = 23;
        goto cleanup;
    }
    if (!comments_load_rtf_file(&app, plainPath, &error) ||
        comments_count(&app) != 0 ||
        comments_query_state(&app, WCQ_COMMENT_ACTIVE_INDEX, 0) != -1) {
        fwprintf(stderr, L"no-metadata load failed (error=%lu)\n", error);
        result = 24;
        goto cleanup;
    }
    wmemset(longComment, L'x', ARRAYSIZE(longComment));
    longComment[ARRAYSIZE(longComment) - 1] = L'\0';
    if (comments_add(&app, longComment)) {
        fwprintf(stderr, L"oversized comment was accepted\n");
        result = 25;
        goto cleanup;
    }

    printf("comments=ok live_anchor=ok navigation=ok "
           "rtf_round_trip=ok metadata_bounds=ok\n");
    result = 0;

cleanup:
    comments_shutdown(&app);
    if (embedded != NULL) {
        HeapFree(GetProcessHeap(), 0, embedded);
    }
    if (metadataPath[0] != L'\0') {
        DeleteFileW(metadataPath);
    }
    if (plainPath[0] != L'\0') {
        DeleteFileW(plainPath);
    }
    if (parent != NULL) {
        DestroyWindow(parent);
    }
    if (rich != NULL) {
        FreeLibrary(rich);
    }
    OleUninitialize();
    return result;
}
