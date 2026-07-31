#include "editor.h"

#include <ole2.h>
#include <stdio.h>
#include <string.h>

static WCHAR probeSummary[192];

typedef struct ProbeRtfSnapshot {
    BYTE data[32768];
    SIZE_T size;
    BOOL overflow;
} ProbeRtfSnapshot;

static DWORD CALLBACK probe_stream_out(DWORD_PTR cookie, LPBYTE buffer,
                                       LONG byteCount, LONG *written)
{
    ProbeRtfSnapshot *snapshot = (ProbeRtfSnapshot *)cookie;

    *written = 0;
    if (snapshot == NULL || byteCount < 0 ||
        (SIZE_T)byteCount > sizeof(snapshot->data) - snapshot->size) {
        if (snapshot != NULL) {
            snapshot->overflow = TRUE;
        }
        return ERROR_INSUFFICIENT_BUFFER;
    }
    CopyMemory(snapshot->data + snapshot->size, buffer, (SIZE_T)byteCount);
    snapshot->size += (SIZE_T)byteCount;
    *written = byteCount;
    return ERROR_SUCCESS;
}

static BOOL probe_capture_rtf(HWND editor, ProbeRtfSnapshot *snapshot)
{
    EDITSTREAM stream;

    ZeroMemory(snapshot, sizeof(*snapshot));
    ZeroMemory(&stream, sizeof(stream));
    stream.dwCookie = (DWORD_PTR)snapshot;
    stream.pfnCallback = probe_stream_out;
    SendMessageW(editor, EM_STREAMOUT, SF_RTF, (LPARAM)&stream);
    return stream.dwError == ERROR_SUCCESS && !snapshot->overflow &&
           snapshot->size > 0;
}

int app_scale(HWND hwnd, int value)
{
    (void)hwnd;
    return value;
}

void pageview_layout(AppState *app)
{
    (void)app;
}

void pageview_sync_to_caret(AppState *app, BOOL ensureVisible)
{
    (void)app;
    (void)ensureVisible;
}

LONG pageview_character_page(AppState *app, LONG character)
{
    (void)app;
    (void)character;
    return 1;
}

BOOL pageview_get_comment_margin_rect(AppState *app, LONG page, RECT *rect)
{
    (void)app;
    (void)page;
    if (rect != NULL) {
        SetRectEmpty(rect);
    }
    return FALSE;
}

BOOL pageview_map_character_to_client(AppState *app, LONG character,
                                      LONG *page, POINT *point)
{
    (void)app;
    (void)character;
    if (page != NULL) {
        *page = 0;
    }
    if (point != NULL) {
        point->x = 0;
        point->y = 0;
    }
    return FALSE;
}

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
    static const BYTE malformedMetadataRtf[] =
        "{\\rtf1\\ansi Xzz alpha beta gamma"
        "{\\*\\wordcraftcomments v1;1;4,9,5,004600690072007300ZZ;}}";
    static const BYTE nestedMetadataRtf[] =
        "{\\rtf1\\ansi{\\*\\outer{\\*\\wordcraftcomments v1;0;}}}";
    static const BYTE nonterminalMetadataRtf[] =
        "{\\rtf1\\ansi{\\*\\wordcraftcomments v1;0;}X}";
    static const char fakeMetadata[] =
        "{\\*\\wordcraftcomments v1;0;}";
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
    char binaryMetadataRtf[256];
    DWORD error = ERROR_SUCCESS;
    LRESULT firstStart;
    LRESULT firstEnd;
    LRESULT secondStart;
    LRESULT secondEnd;
    LRESULT modifiedBefore;
    LRESULT undoBefore;
    LRESULT redoBefore;
    LRESULT undoNameBefore;
    LRESULT redoNameBefore;
    CHARFORMAT2W formatBefore;
    CHARFORMAT2W formatAfter;
    ProbeRtfSnapshot rtfBeforeHighlight;
    ProbeRtfSnapshot rtfDuringHighlight;
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

    SendMessageW(editor, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(editor, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessageW(editor, EM_REPLACESEL, TRUE, (LPARAM)L"1");
    SendMessageW(editor, EM_STOPGROUPTYPING, 0, 0);
    SendMessageW(editor, EM_REPLACESEL, TRUE, (LPARAM)L"2");
    SendMessageW(editor, EM_STOPGROUPTYPING, 0, 0);
    if (!SendMessageW(editor, EM_UNDO, 0, 0)) {
        fwprintf(stderr, L"could not seed comment undo/redo history\n");
        result = 14;
        goto cleanup;
    }
    SendMessageW(editor, EM_SETSEL, 3, 8);
    SendMessageW(editor, EM_SETMODIFY, FALSE, 0);
    modifiedBefore = SendMessageW(editor, EM_GETMODIFY, 0, 0);
    undoBefore = SendMessageW(editor, EM_CANUNDO, 0, 0);
    redoBefore = SendMessageW(editor, EM_CANREDO, 0, 0);
    undoNameBefore = SendMessageW(editor, EM_GETUNDONAME, 0, 0);
    redoNameBefore = SendMessageW(editor, EM_GETREDONAME, 0, 0);
    if (undoBefore == 0 || redoBefore == 0) {
        fwprintf(stderr, L"comment undo/redo history seed was incomplete\n");
        result = 14;
        goto cleanup;
    }
    ZeroMemory(&formatBefore, sizeof(formatBefore));
    formatBefore.cbSize = sizeof(formatBefore);
    SendMessageW(editor, EM_GETCHARFORMAT, SCF_SELECTION,
                 (LPARAM)&formatBefore);
    if (!probe_capture_rtf(editor, &rtfBeforeHighlight) ||
        !comments_begin_draft(&app) ||
        comments_query_state(&app, WCQ_COMMENT_COMPOSITION_ACTIVE, 0) != 1 ||
        comments_query_state(&app, WCQ_COMMENT_HIGHLIGHT_VISIBLE, 0) != 1 ||
        comments_query_state(&app, WCQ_COMMENT_HIGHLIGHT_START, 0) != 3 ||
        comments_query_state(&app, WCQ_COMMENT_HIGHLIGHT_END, 0) != 8 ||
        (COLORREF)comments_query_state(
            &app, WCQ_COMMENT_HIGHLIGHT_COLOR, 0) !=
            WORDCRAFT_COMMENT_HIGHLIGHT_COLOR) {
        fwprintf(stderr, L"comment draft highlight failed\n");
        result = 14;
        goto cleanup;
    }
    ZeroMemory(&formatAfter, sizeof(formatAfter));
    formatAfter.cbSize = sizeof(formatAfter);
    SendMessageW(editor, EM_GETCHARFORMAT, SCF_SELECTION,
                 (LPARAM)&formatAfter);
    if (!probe_capture_rtf(editor, &rtfDuringHighlight) ||
        rtfDuringHighlight.size != rtfBeforeHighlight.size ||
        memcmp(rtfDuringHighlight.data, rtfBeforeHighlight.data,
               rtfBeforeHighlight.size) != 0 ||
        SendMessageW(editor, EM_GETMODIFY, 0, 0) != modifiedBefore ||
        SendMessageW(editor, EM_CANUNDO, 0, 0) != undoBefore ||
        SendMessageW(editor, EM_CANREDO, 0, 0) != redoBefore ||
        SendMessageW(editor, EM_GETUNDONAME, 0, 0) != undoNameBefore ||
        SendMessageW(editor, EM_GETREDONAME, 0, 0) != redoNameBefore ||
        formatAfter.dwMask != formatBefore.dwMask ||
        formatAfter.dwEffects != formatBefore.dwEffects ||
        formatAfter.crTextColor != formatBefore.crTextColor ||
        formatAfter.crBackColor != formatBefore.crBackColor) {
        fwprintf(stderr,
                 L"temporary comment highlight changed document formatting or undo state "
                 L"(modified=%lld/%lld undo=%lld/%lld mask=0x%lx/0x%lx "
                 L"effects=0x%lx/0x%lx text=0x%lx/0x%lx back=0x%lx/0x%lx)\n",
                 (long long)modifiedBefore,
                 (long long)SendMessageW(editor, EM_GETMODIFY, 0, 0),
                 (long long)undoBefore,
                 (long long)SendMessageW(editor, EM_CANUNDO, 0, 0),
                 formatBefore.dwMask, formatAfter.dwMask,
                 formatBefore.dwEffects, formatAfter.dwEffects,
                 formatBefore.crTextColor, formatAfter.crTextColor,
                 formatBefore.crBackColor, formatAfter.crBackColor);
        result = 14;
        goto cleanup;
    }
    comments_cancel_draft(&app);
    if (comments_query_state(&app, WCQ_COMMENT_COMPOSITION_ACTIVE, 0) != 0 ||
        comments_query_state(&app, WCQ_COMMENT_HIGHLIGHT_VISIBLE, 0) != 0 ||
        SendMessageW(editor, EM_GETMODIFY, 0, 0) != modifiedBefore ||
        SendMessageW(editor, EM_CANUNDO, 0, 0) != undoBefore ||
        SendMessageW(editor, EM_CANREDO, 0, 0) != redoBefore ||
        SendMessageW(editor, EM_GETUNDONAME, 0, 0) != undoNameBefore ||
        SendMessageW(editor, EM_GETREDONAME, 0, 0) != redoNameBefore ||
        !comments_begin_draft(&app)) {
        fwprintf(stderr,
                 L"clearing or restoring a draft changed undo/redo history\n");
        result = 14;
        goto cleanup;
    }
    if (!comments_add(&app, L"First comment") ||
        comments_query_state(&app, WCQ_COMMENT_COMPOSITION_ACTIVE, 0) != 0 ||
        comments_query_state(&app, WCQ_COMMENT_HIGHLIGHT_VISIBLE, 0) != 1 ||
        comments_query_state(&app, WCQ_COMMENT_HIGHLIGHT_START, 0) != 3 ||
        comments_query_state(&app, WCQ_COMMENT_HIGHLIGHT_END, 0) != 8) {
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

    SendMessageW(editor, EM_SETSEL, 0, 0);
    comments_selection_changed(&app);
    if (comments_query_state(&app, WCQ_COMMENT_HIGHLIGHT_VISIBLE, 0) != 0 ||
        comments_query_state(&app, WCQ_COMMENT_HIGHLIGHT_START, 0) != -1 ||
        comments_query_state(&app, WCQ_COMMENT_HIGHLIGHT_END, 0) != -1) {
        fwprintf(stderr, L"comment highlight did not dismiss outside its anchor\n");
        result = 17;
        goto cleanup;
    }

    SendMessageW(editor, EM_SETSEL, (WPARAM)firstStart, (LPARAM)firstEnd);
    comments_selection_changed(&app);
    if (comments_query_state(&app, WCQ_COMMENT_ACTIVE_INDEX, 0) != 0 ||
        comments_query_state(&app, WCQ_COMMENT_HIGHLIGHT_VISIBLE, 0) != 1 ||
        comments_query_state(&app, WCQ_COMMENT_HIGHLIGHT_START, 0) !=
            firstStart ||
        comments_query_state(&app, WCQ_COMMENT_HIGHLIGHT_END, 0) != firstEnd) {
        fwprintf(stderr, L"selection did not activate first comment\n");
        result = 17;
        goto cleanup;
    }
    comments_next(&app);
    if (comments_query_state(&app, WCQ_COMMENT_ACTIVE_INDEX, 0) != 1 ||
        comments_query_state(&app, WCQ_COMMENT_HIGHLIGHT_START, 0) !=
            secondStart ||
        comments_query_state(&app, WCQ_COMMENT_HIGHLIGHT_END, 0) != secondEnd) {
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

    comments_clear(&app);
    if (!comments_load_rtf_memory(&app, embedded, embeddedSize, &error) ||
        error != ERROR_SUCCESS || comments_count(&app) != 2 ||
        comments_query_state(&app, WCQ_COMMENT_ACTIVE_INDEX, 0) != 0 ||
        comments_query_state(&app, WCQ_COMMENT_ANCHOR_START, 0) != 4 ||
        comments_query_state(&app, WCQ_COMMENT_ANCHOR_END, 0) != 9 ||
        comments_query_state(&app, WCQ_COMMENT_ANCHOR_START, 1) != 10 ||
        comments_query_state(&app, WCQ_COMMENT_ANCHOR_END, 1) != 14 ||
        (UINT)comments_query_state(&app, WCQ_COMMENT_TEXT_HASH, 0) !=
            probe_hash(L"First comment") ||
        (UINT)comments_query_state(&app, WCQ_COMMENT_TEXT_HASH, 1) !=
            probe_hash(L"Second: caf\x00E9 \x4E16\x754C")) {
        fwprintf(stderr, L"RTF memory metadata load failed (error=%lu)\n",
                 error);
        result = 21;
        goto cleanup;
    }

    error = ERROR_SUCCESS;
    if (comments_load_rtf_memory(&app, malformedMetadataRtf,
                                 sizeof(malformedMetadataRtf) - 1, &error) ||
        error == ERROR_SUCCESS || comments_count(&app) != 2 ||
        comments_query_state(&app, WCQ_COMMENT_ACTIVE_INDEX, 0) != 0 ||
        comments_query_state(&app, WCQ_COMMENT_ANCHOR_START, 0) != 4 ||
        comments_query_state(&app, WCQ_COMMENT_ANCHOR_END, 0) != 9 ||
        comments_query_state(&app, WCQ_COMMENT_ANCHOR_START, 1) != 10 ||
        comments_query_state(&app, WCQ_COMMENT_ANCHOR_END, 1) != 14 ||
        (UINT)comments_query_state(&app, WCQ_COMMENT_TEXT_HASH, 0) !=
            probe_hash(L"First comment") ||
        (UINT)comments_query_state(&app, WCQ_COMMENT_TEXT_HASH, 1) !=
            probe_hash(L"Second: caf\x00E9 \x4E16\x754C")) {
        fwprintf(stderr,
                 L"malformed RTF memory metadata was not transactional "
                 L"(error=%lu)\n", error);
        result = 22;
        goto cleanup;
    }

    error = ERROR_SUCCESS;
    if (comments_load_rtf_memory(NULL, embedded, embeddedSize, &error) ||
        error != ERROR_INVALID_PARAMETER ||
        comments_load_rtf_memory(&app, NULL, 1, &error) ||
        error != ERROR_INVALID_PARAMETER ||
        comments_load_rtf_memory(&app, embedded, embeddedSize, NULL) ||
        comments_count(&app) != 2 ||
        (UINT)comments_query_state(&app, WCQ_COMMENT_TEXT_HASH, 0) !=
            probe_hash(L"First comment") ||
        (UINT)comments_query_state(&app, WCQ_COMMENT_TEXT_HASH, 1) !=
            probe_hash(L"Second: caf\x00E9 \x4E16\x754C")) {
        fwprintf(stderr, L"RTF memory invalid-argument handling failed\n");
        result = 23;
        goto cleanup;
    }

    if (FAILED(StringCchPrintfA(binaryMetadataRtf,
                                ARRAYSIZE(binaryMetadataRtf),
                                "{\\rtf1\\ansi\\bin%u %s}",
                                (unsigned)(sizeof(fakeMetadata) - 1),
                                fakeMetadata)) ||
        !comments_load_rtf_memory(
            &app, (const BYTE *)binaryMetadataRtf,
            strlen(binaryMetadataRtf), &error) ||
        comments_count(&app) != 0 ||
        !comments_load_rtf_memory(&app, embedded, embeddedSize, &error) ||
        comments_count(&app) != 2 ||
        !comments_load_rtf_memory(&app, nestedMetadataRtf,
                                  sizeof(nestedMetadataRtf) - 1, &error) ||
        comments_count(&app) != 0 ||
        !comments_load_rtf_memory(&app, embedded, embeddedSize, &error) ||
        comments_count(&app) != 2) {
        fwprintf(stderr,
                 L"RTF-aware metadata scanning failed for binary/nested data "
                 L"(error=%lu)\n", error);
        result = 30;
        goto cleanup;
    }
    error = ERROR_SUCCESS;
    if (comments_load_rtf_memory(&app, nonterminalMetadataRtf,
                                 sizeof(nonterminalMetadataRtf) - 1, &error) ||
        error == ERROR_SUCCESS || comments_count(&app) != 2) {
        fwprintf(stderr,
                 L"nonterminal metadata destination was accepted or mutated "
                 L"comments (error=%lu)\n", error);
        result = 31;
        goto cleanup;
    }

    if (!comments_load_rtf_memory(&app, sourceRtf,
                                  sizeof(sourceRtf) - 1, &error) ||
        error != ERROR_SUCCESS || comments_count(&app) != 0 ||
        comments_query_state(&app, WCQ_COMMENT_ACTIVE_INDEX, 0) != -1) {
        fwprintf(stderr, L"plain RTF memory did not clear comments (error=%lu)\n",
                 error);
        result = 24;
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
        result = 25;
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
        result = 26;
        goto cleanup;
    }

    app.modified = FALSE;
    comments_delete_active(&app);
    if (comments_count(&app) != 1 ||
        comments_query_state(&app, WCQ_COMMENT_ACTIVE_INDEX, 0) != 0 ||
        !app.modified) {
        fwprintf(stderr, L"comment delete failed\n");
        result = 27;
        goto cleanup;
    }
    if (!comments_load_rtf_file(&app, plainPath, &error) ||
        comments_count(&app) != 0 ||
        comments_query_state(&app, WCQ_COMMENT_ACTIVE_INDEX, 0) != -1) {
        fwprintf(stderr, L"no-metadata load failed (error=%lu)\n", error);
        result = 28;
        goto cleanup;
    }
    wmemset(longComment, L'x', ARRAYSIZE(longComment));
    longComment[ARRAYSIZE(longComment) - 1] = L'\0';
    if (comments_add(&app, longComment)) {
        fwprintf(stderr, L"oversized comment was accepted\n");
        result = 29;
        goto cleanup;
    }

    printf("comments=ok live_anchor=ok navigation=ok "
           "comment_highlight=ok highlight_nonmutating=ok "
           "highlight_undo_redo_preserved=ok "
           "rtf_round_trip=ok rtf_memory_load=ok "
           "rtf_memory_transaction=ok rtf_memory_invalid_args=ok "
           "metadata_scanner=ok metadata_bounds=ok\n");
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
