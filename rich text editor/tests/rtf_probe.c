#include "editor.h"
#include "rendereditor.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef struct FixedBuffer {
    BYTE *data;
    SIZE_T capacity;
    SIZE_T size;
    SIZE_T position;
} FixedBuffer;

typedef struct ParagraphExpectation {
    const WCHAR *marker;
    WORD alignment;
    BOOL bullets;
} ParagraphExpectation;

/* document.c is linked into this probe so the live-snapshot gate is tested
 * through its real public apply path.  These no-op collaborators keep the
 * probe focused on RTF streaming and validation rather than the full UI. */
BOOL render_editor_install_static_picture_callback(HWND richEdit)
{
    (void)richEdit;
    return TRUE;
}

BOOL render_editor_begin_static_picture_stream(HWND editor)
{
    (void)editor;
    return TRUE;
}

BOOL render_editor_end_static_picture_stream(HWND editor, DWORD *error)
{
    (void)editor;
    if (error != NULL) {
        *error = ERROR_SUCCESS;
    }
    return TRUE;
}

void app_update_status(AppState *app, BOOL recountWords)
{
    (void)app;
    (void)recountWords;
}

void app_set_status_message(AppState *app, const WCHAR *message)
{
    (void)app;
    (void)message;
}

void app_update_command_ui(AppState *app)
{
    (void)app;
}

void app_show_error(HWND owner, const WCHAR *action, DWORD errorCode)
{
    (void)owner;
    (void)action;
    (void)errorCode;
}

BOOL editor_get_all_text(HWND editor, BOOL useCrlf, WCHAR **text,
                         SIZE_T *length, DWORD *error)
{
    (void)editor;
    (void)useCrlf;
    (void)text;
    (void)length;
    if (error != NULL) {
        *error = ERROR_CALL_NOT_IMPLEMENTED;
    }
    return FALSE;
}

void assist_document_changed(AppState *app)
{
    (void)app;
}

void comments_clear(AppState *app)
{
    (void)app;
}

void comments_dismiss_highlight(AppState *app)
{
    (void)app;
}

SIZE_T comments_count(const AppState *app)
{
    (void)app;
    return 0;
}

BOOL comments_embed_rtf(AppState *app, const BYTE *rtf, SIZE_T rtfSize,
                        BYTE **output, SIZE_T *outputSize, DWORD *error)
{
    BYTE *copy;

    (void)app;
    if (output == NULL || outputSize == NULL || error == NULL ||
        (rtf == NULL && rtfSize != 0)) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return FALSE;
    }
    copy = HeapAlloc(GetProcessHeap(), 0, rtfSize == 0 ? 1 : rtfSize);
    if (copy == NULL) {
        *error = ERROR_NOT_ENOUGH_MEMORY;
        return FALSE;
    }
    if (rtfSize != 0) {
        CopyMemory(copy, rtf, rtfSize);
    }
    *output = copy;
    *outputSize = rtfSize;
    *error = ERROR_SUCCESS;
    return TRUE;
}

BOOL comments_load_rtf_memory(AppState *app, const BYTE *data, SIZE_T size,
                              DWORD *error)
{
    (void)app;
    (void)data;
    (void)size;
    if (error != NULL) {
        *error = ERROR_SUCCESS;
    }
    return TRUE;
}

BOOL comments_load_rtf_file(AppState *app, const WCHAR *path, DWORD *error)
{
    (void)app;
    (void)path;
    if (error != NULL) {
        *error = ERROR_CALL_NOT_IMPLEMENTED;
    }
    return FALSE;
}

void history_clear(AppState *app)
{
    (void)app;
}

void history_seed_if_empty(AppState *app)
{
    (void)app;
}

void history_note_document_changed(AppState *app)
{
    (void)app;
}

BOOL history_flush_pending(AppState *app)
{
    (void)app;
    return TRUE;
}

SIZE_T history_chat_count(const AppState *app)
{
    (void)app;
    return 0;
}

SIZE_T history_version_count(const AppState *app)
{
    (void)app;
    return 0;
}

BOOL history_embed_rtf(AppState *app, const BYTE *rtf, SIZE_T rtfSize,
                       BYTE **output, SIZE_T *outputSize, DWORD *error)
{
    BYTE *copy;

    (void)app;
    if (output == NULL || outputSize == NULL || error == NULL ||
        (rtf == NULL && rtfSize != 0)) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return FALSE;
    }
    copy = HeapAlloc(GetProcessHeap(), 0, rtfSize == 0 ? 1 : rtfSize);
    if (copy == NULL) {
        *error = ERROR_NOT_ENOUGH_MEMORY;
        return FALSE;
    }
    if (rtfSize != 0) {
        CopyMemory(copy, rtf, rtfSize);
    }
    *output = copy;
    *outputSize = rtfSize;
    *error = ERROR_SUCCESS;
    return TRUE;
}

BOOL history_embed_rtf_bounded(
    AppState *app, const BYTE *rtf, SIZE_T rtfSize,
    SIZE_T maximumOutputSize,
    const HistoryChatToken *requiredChats,
    SIZE_T requiredChatCount, BYTE **output, SIZE_T *outputSize,
    DWORD *error)
{
    (void)requiredChats;
    (void)requiredChatCount;
    if (rtfSize > maximumOutputSize) {
        if (error != NULL) {
            *error = ERROR_FILE_TOO_LARGE;
        }
        return FALSE;
    }
    return history_embed_rtf(app, rtf, rtfSize, output, outputSize,
                             error);
}

BOOL history_load_rtf_memory(AppState *app, const BYTE *data, SIZE_T size,
                             DWORD *error)
{
    (void)app;
    (void)data;
    (void)size;
    if (error != NULL) {
        *error = ERROR_SUCCESS;
    }
    return TRUE;
}

BOOL history_merge_rtf_memory(AppState *app, const BYTE *data, SIZE_T size,
                              DWORD *error)
{
    (void)app;
    (void)data;
    (void)size;
    if (error != NULL) {
        *error = ERROR_SUCCESS;
    }
    return TRUE;
}

BOOL history_reconcile_rtf_memory(AppState *app, const BYTE *data,
                                  SIZE_T size, DWORD *error)
{
    return history_merge_rtf_memory(app, data, size, error);
}

BOOL history_reconcile_chat_ack_rtf_memory(
    AppState *app, const BYTE *data, SIZE_T size,
    const HistoryChatToken *acknowledgedChats,
    SIZE_T acknowledgedChatCount, DWORD *error)
{
    (void)acknowledgedChats;
    (void)acknowledgedChatCount;
    return history_merge_rtf_memory(app, data, size, error);
}

void history_cancel_pending_revision(AppState *app)
{
    (void)app;
}

BOOL history_load_rtf_file(AppState *app, const WCHAR *path, DWORD *error)
{
    (void)app;
    (void)path;
    if (error != NULL) {
        *error = ERROR_CALL_NOT_IMPLEMENTED;
    }
    return FALSE;
}

void format_initialize_document(AppState *app)
{
    (void)app;
}

void format_sync_controls(AppState *app)
{
    (void)app;
}

void live_share_document_changed(AppState *app)
{
    (void)app;
}

void pageview_mark_dirty(AppState *app)
{
    (void)app;
}

void ribbon_set_active_style(AppState *app, int style)
{
    (void)app;
    (void)style;
}

void text_engine_note_layout_change(AppState *app)
{
    (void)app;
}

static DWORD CALLBACK write_buffer(DWORD_PTR cookie, LPBYTE data,
                                   LONG requested, LONG *written)
{
    FixedBuffer *buffer = (FixedBuffer *)cookie;
    if (requested < 0 || buffer->size + (SIZE_T)requested > buffer->capacity) {
        *written = 0;
        return 1;
    }
    CopyMemory(buffer->data + buffer->size, data, (SIZE_T)requested);
    buffer->size += (SIZE_T)requested;
    *written = requested;
    return 0;
}

static DWORD CALLBACK read_buffer(DWORD_PTR cookie, LPBYTE data,
                                  LONG requested, LONG *read)
{
    FixedBuffer *buffer = (FixedBuffer *)cookie;
    SIZE_T remaining = buffer->size - buffer->position;
    SIZE_T amount = remaining < (SIZE_T)requested ? remaining : (SIZE_T)requested;
    CopyMemory(data, buffer->data + buffer->position, amount);
    buffer->position += amount;
    *read = (LONG)amount;
    return 0;
}

static LONG find_marker(const WCHAR *text, const WCHAR *marker)
{
    const WCHAR *match = wcsstr(text, marker);
    ptrdiff_t offset;

    if (match == NULL) {
        return -1;
    }
    offset = match - text;
    if (offset < 0 || offset > LONG_MAX - 1) {
        return -1;
    }
    return (LONG)offset;
}

static BOOL set_paragraph_alignment(HWND editor, LONG position, WORD alignment)
{
    PARAFORMAT2 paragraph;

    if (position < 0) {
        return FALSE;
    }
    SendMessageW(editor, EM_SETSEL, (WPARAM)position, (LPARAM)(position + 1));
    ZeroMemory(&paragraph, sizeof(paragraph));
    paragraph.cbSize = sizeof(paragraph);
    paragraph.dwMask = PFM_ALIGNMENT;
    paragraph.wAlignment = alignment;
    return SendMessageW(editor, EM_SETPARAFORMAT, 0,
                        (LPARAM)&paragraph) != 0;
}

static BOOL set_paragraph_bullets(HWND editor, LONG first, LONG last)
{
    PARAFORMAT2 paragraph;

    if (first < 0 || last < first || last == LONG_MAX) {
        return FALSE;
    }
    SendMessageW(editor, EM_SETSEL, (WPARAM)first, (LPARAM)(last + 1));
    ZeroMemory(&paragraph, sizeof(paragraph));
    paragraph.cbSize = sizeof(paragraph);
    paragraph.dwMask = PFM_NUMBERING | PFM_OFFSET;
    paragraph.wNumbering = PFN_BULLET;
    paragraph.dxOffset = 360;
    return SendMessageW(editor, EM_SETPARAFORMAT, 0,
                        (LPARAM)&paragraph) != 0;
}

static BOOL paragraph_matches(HWND editor, LONG position,
                              const ParagraphExpectation *expected)
{
    PARAFORMAT2 paragraph;
    BOOL hasBullets;

    if (position < 0 || expected == NULL) {
        return FALSE;
    }
    SendMessageW(editor, EM_SETSEL, (WPARAM)position, (LPARAM)(position + 1));
    ZeroMemory(&paragraph, sizeof(paragraph));
    paragraph.cbSize = sizeof(paragraph);
    SendMessageW(editor, EM_GETPARAFORMAT, 0, (LPARAM)&paragraph);
    if ((paragraph.dwMask & PFM_ALIGNMENT) == 0 ||
        paragraph.wAlignment != expected->alignment) {
        return FALSE;
    }
    hasBullets = (paragraph.dwMask & PFM_NUMBERING) != 0 &&
                 paragraph.wNumbering == PFN_BULLET;
    return hasBullets == expected->bullets;
}

static BOOL all_paragraphs_match(HWND editor, const WCHAR *text,
                                 const ParagraphExpectation *expected,
                                 SIZE_T count)
{
    SIZE_T index;

    for (index = 0; index < count; ++index) {
        LONG position = find_marker(text, expected[index].marker);
        if (!paragraph_matches(editor, position, &expected[index])) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL live_snapshot_rejected_without_mutation(AppState *app,
                                                    const char *rtf,
                                                    SIZE_T size)
{
    static const WCHAR sentinel[] = L"unchanged sentinel";
    WCHAR actual[64];
    DWORD error = ERROR_SUCCESS;

    if (app == NULL || rtf == NULL ||
        !SetWindowTextW(app->editor, sentinel) ||
        document_apply_live_snapshot(app, (const BYTE *)rtf, size, &error) ||
        error != ERROR_INVALID_DATA) {
        return FALSE;
    }
    actual[0] = L'\0';
    GetWindowTextW(app->editor, actual, ARRAYSIZE(actual));
    return wcscmp(actual, sentinel) == 0;
}

static BOOL build_excessively_nested_rtf(char *buffer, SIZE_T capacity,
                                         SIZE_T *size)
{
    static const char prefix[] = "{\\rtf1\\ansi ";
    SIZE_T cursor = 0;
    SIZE_T index;
    const SIZE_T nestedGroups = 300;

    if (buffer == NULL || size == NULL ||
        capacity < sizeof(prefix) - 1 + nestedGroups * 2 + 2) {
        return FALSE;
    }
    CopyMemory(buffer, prefix, sizeof(prefix) - 1);
    cursor = sizeof(prefix) - 1;
    for (index = 0; index < nestedGroups; ++index) {
        buffer[cursor++] = '{';
    }
    buffer[cursor++] = 'x';
    for (index = 0; index < nestedGroups; ++index) {
        buffer[cursor++] = '}';
    }
    buffer[cursor++] = '}';
    *size = cursor;
    return TRUE;
}

int main(void)
{
    static const WCHAR source[] = L"Bold caf\x00E9 \x6F22\x5B57 plain";
    static const WCHAR paragraphSource[] =
        L"Left paragraph.\r"
        L"Center paragraph.\r"
        L"Right paragraph.\r"
        L"Justified paragraph has enough words to wrap across multiple lines "
        L"inside the narrow probe control.\r"
        L"First bullet paragraph.\r"
        L"Second bullet paragraph.\r"
        L"Plain paragraph after bullets.";
    static const ParagraphExpectation paragraphExpectations[] = {
        {L"Left paragraph", PFA_LEFT, FALSE},
        {L"Center paragraph", PFA_CENTER, FALSE},
        {L"Right paragraph", PFA_RIGHT, FALSE},
        {L"Justified paragraph", PFA_JUSTIFY, FALSE},
        {L"First bullet paragraph", PFA_LEFT, TRUE},
        {L"Second bullet paragraph", PFA_LEFT, TRUE},
        {L"Plain paragraph after bullets", PFA_LEFT, FALSE},
    };
    static const char safeLiveRtf[] =
        "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Times New Roman;}}"
        "\\f0\\fs24\\b Safe\\b0  live text}";
    static const char escapedControlTextRtf[] =
        "{\\rtf1\\ansi Escaped \\\\object text}";
    static const char objectRtf[] =
        "{\\rtf1\\ansi Safe{\\ObJeCt\\objemb"
        "{\\*\\objclass Package}{\\*\\objdata 0102}}}";
    static const char fieldRtf[] =
        "{\\rtf1\\ansi {\\field{\\*\\fldinst INCLUDETEXT "
        "\"C:\\\\secret.txt\"}{\\fldrslt linked text}}}";
    static const char pictureRtf[] =
        "{\\rtf1\\ansi {\\pict\\pngblip 89504E47}}";
    HMODULE rich = LoadLibraryW(L"Msftedit.dll");
    HWND parent;
    HWND editor;
    FixedBuffer buffer;
    EDITSTREAM stream;
    CHARFORMAT2W format;
    WCHAR restored[1024];
    char nestedRtf[1024];
    SIZE_T nestedRtfSize = 0;
    AppState app;
    DWORD liveError = ERROR_SUCCESS;
    DWORD historyError = ERROR_SUCCESS;
    LONG positions[ARRAYSIZE(paragraphExpectations)];
    SIZE_T index;
    int result = 1;

    if (rich == NULL) {
        return 10;
    }
    parent = CreateWindowExW(0, L"STATIC", L"probe", WS_OVERLAPPED,
                             0, 0, 320, 200, NULL, NULL, GetModuleHandleW(NULL), NULL);
    editor = CreateWindowExW(0, MSFTEDIT_CLASS, NULL, WS_CHILD | ES_MULTILINE,
                             0, 0, 300, 180, parent, NULL, GetModuleHandleW(NULL), NULL);
    if (parent == NULL || editor == NULL) {
        result = 11;
        goto cleanup;
    }
    ZeroMemory(&app, sizeof(app));
    app.instance = GetModuleHandleW(NULL);
    app.mainWindow = parent;
    app.pageView = parent;
    app.editor = editor;
    SendMessageW(editor, EM_EXLIMITTEXT, 0, 0x7FFFFFFE);
    SetWindowTextW(editor, source);
    SendMessageW(editor, EM_SETSEL, 0, 4);
    ZeroMemory(&format, sizeof(format));
    format.cbSize = sizeof(format);
    format.dwMask = CFM_BOLD;
    format.dwEffects = CFE_BOLD;
    if (!SendMessageW(editor, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&format)) {
        result = 12;
        goto cleanup;
    }

    ZeroMemory(&buffer, sizeof(buffer));
    buffer.capacity = 1024 * 1024;
    buffer.data = HeapAlloc(GetProcessHeap(), 0, buffer.capacity);
    if (buffer.data == NULL) {
        result = 13;
        goto cleanup;
    }
    ZeroMemory(&stream, sizeof(stream));
    stream.dwCookie = (DWORD_PTR)&buffer;
    stream.pfnCallback = write_buffer;
    SendMessageW(editor, EM_STREAMOUT, SF_RTF, (LPARAM)&stream);
    if (stream.dwError != 0 || buffer.size < 5 ||
        memcmp(buffer.data, "{\\rtf", 5) != 0) {
        result = 14;
        goto cleanup_buffer;
    }

    SetWindowTextW(editor, L"");
    buffer.position = 0;
    ZeroMemory(&stream, sizeof(stream));
    stream.dwCookie = (DWORD_PTR)&buffer;
    stream.pfnCallback = read_buffer;
    SendMessageW(editor, EM_STREAMIN, SF_RTF, (LPARAM)&stream);
    GetWindowTextW(editor, restored, ARRAYSIZE(restored));
    if (stream.dwError != 0 || wcscmp(restored, source) != 0) {
        result = 15;
        goto cleanup_buffer;
    }
    SendMessageW(editor, EM_SETSEL, 0, 4);
    ZeroMemory(&format, sizeof(format));
    format.cbSize = sizeof(format);
    SendMessageW(editor, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&format);
    if ((format.dwMask & CFM_BOLD) == 0 || (format.dwEffects & CFE_BOLD) == 0) {
        result = 16;
        goto cleanup_buffer;
    }
    SendMessageW(editor, EM_SETSEL, 5, -1);
    ZeroMemory(&format, sizeof(format));
    format.cbSize = sizeof(format);
    SendMessageW(editor, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&format);
    if ((format.dwMask & CFM_BOLD) == 0 || (format.dwEffects & CFE_BOLD) != 0) {
        result = 17;
        goto cleanup_buffer;
    }

    if (!SetWindowTextW(editor, paragraphSource) ||
        GetWindowTextW(editor, restored, ARRAYSIZE(restored)) <= 0) {
        result = 18;
        goto cleanup_buffer;
    }
    for (index = 0; index < ARRAYSIZE(paragraphExpectations); ++index) {
        positions[index] = find_marker(restored, paragraphExpectations[index].marker);
        if (positions[index] < 0) {
            result = 19;
            goto cleanup_buffer;
        }
    }
    if (!set_paragraph_alignment(editor, positions[0], PFA_LEFT) ||
        !set_paragraph_alignment(editor, positions[1], PFA_CENTER) ||
        !set_paragraph_alignment(editor, positions[2], PFA_RIGHT) ||
        !set_paragraph_alignment(editor, positions[3], PFA_JUSTIFY) ||
        !set_paragraph_bullets(editor, positions[4], positions[5])) {
        result = 20;
        goto cleanup_buffer;
    }
    if (!all_paragraphs_match(editor, restored, paragraphExpectations,
                              ARRAYSIZE(paragraphExpectations))) {
        result = 21;
        goto cleanup_buffer;
    }

    buffer.size = 0;
    buffer.position = 0;
    ZeroMemory(&stream, sizeof(stream));
    stream.dwCookie = (DWORD_PTR)&buffer;
    stream.pfnCallback = write_buffer;
    SendMessageW(editor, EM_STREAMOUT, SF_RTF, (LPARAM)&stream);
    if (stream.dwError != 0 || buffer.size < 5 ||
        memcmp(buffer.data, "{\\rtf", 5) != 0) {
        result = 22;
        goto cleanup_buffer;
    }

    SetWindowTextW(editor, L"");
    buffer.position = 0;
    ZeroMemory(&stream, sizeof(stream));
    stream.dwCookie = (DWORD_PTR)&buffer;
    stream.pfnCallback = read_buffer;
    SendMessageW(editor, EM_STREAMIN, SF_RTF, (LPARAM)&stream);
    ZeroMemory(restored, sizeof(restored));
    if (stream.dwError != 0 ||
        GetWindowTextW(editor, restored, ARRAYSIZE(restored)) <= 0 ||
        !all_paragraphs_match(editor, restored, paragraphExpectations,
                               ARRAYSIZE(paragraphExpectations))) {
        result = 23;
        goto cleanup_buffer;
    }

    liveError = ERROR_SUCCESS;
    ZeroMemory(restored, sizeof(restored));
    if (!document_apply_live_snapshot(
            &app, (const BYTE *)safeLiveRtf, sizeof(safeLiveRtf) - 1,
            &liveError) || liveError != ERROR_SUCCESS ||
        GetWindowTextW(editor, restored, ARRAYSIZE(restored)) <= 0 ||
        wcscmp(restored, L"Safe live text") != 0) {
        fprintf(stderr, "safe live RTF was rejected (error=%lu)\n",
                (unsigned long)liveError);
        result = 24;
        goto cleanup_buffer;
    }
    liveError = ERROR_SUCCESS;
    ZeroMemory(restored, sizeof(restored));
    if (!document_apply_live_snapshot(
            &app, (const BYTE *)escapedControlTextRtf,
            sizeof(escapedControlTextRtf) - 1, &liveError) ||
        liveError != ERROR_SUCCESS ||
        GetWindowTextW(editor, restored, ARRAYSIZE(restored)) <= 0 ||
        wcscmp(restored, L"Escaped \\object text") != 0) {
        fprintf(stderr, "escaped control-like text was rejected (error=%lu)\n",
                (unsigned long)liveError);
        result = 25;
        goto cleanup_buffer;
    }
    if (!live_snapshot_rejected_without_mutation(
            &app, objectRtf, sizeof(objectRtf) - 1) ||
        !live_snapshot_rejected_without_mutation(
            &app, fieldRtf, sizeof(fieldRtf) - 1) ||
        !live_snapshot_rejected_without_mutation(
            &app, pictureRtf, sizeof(pictureRtf) - 1)) {
        fprintf(stderr, "active or embedded live RTF was accepted\n");
        result = 26;
        goto cleanup_buffer;
    }
    if (!document_validate_history_snapshot(
            (const BYTE *)pictureRtf, sizeof(pictureRtf) - 1,
            &historyError) ||
        historyError != ERROR_SUCCESS ||
        document_validate_history_snapshot(
            (const BYTE *)objectRtf, sizeof(objectRtf) - 1,
            &historyError)) {
        fprintf(stderr,
                "history picture/object safety policy was incorrect\n");
        result = 27;
        goto cleanup_buffer;
    }
    if (!build_excessively_nested_rtf(nestedRtf, ARRAYSIZE(nestedRtf),
                                      &nestedRtfSize) ||
        !live_snapshot_rejected_without_mutation(
            &app, nestedRtf, nestedRtfSize)) {
        fprintf(stderr, "excessively nested live RTF was accepted\n");
        result = 28;
        goto cleanup_buffer;
    }
    printf("rtf_unicode=ok formatting_round_trip=ok "
           "live_safe_subset=ok history_static_picture=ok "
           "live_depth_limit=ok\n");
    result = 0;

cleanup_buffer:
    HeapFree(GetProcessHeap(), 0, buffer.data);
cleanup:
    if (parent != NULL) {
        DestroyWindow(parent);
    }
    FreeLibrary(rich);
    return result;
}
