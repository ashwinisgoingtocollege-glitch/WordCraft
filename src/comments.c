#ifndef COBJMACROS
#define COBJMACROS
#endif

#include "editor.h"

#include <limits.h>
#include <stdint.h>
#include <richole.h>
#include <string.h>
#include <tom.h>

#define COMMENT_COUNT_LIMIT 1024
#define COMMENT_METADATA_LIMIT (16u * 1024u * 1024u)
#define COMMENT_FILE_LIMIT ((ULONGLONG)512u * 1024u * 1024u)
#define COMMENT_NO_ACTIVE ((SIZE_T)-1)

typedef struct CommentEntry {
    ITextRange *anchor;
    WCHAR text[COMMENT_TEXT_CAPACITY + 1];
} CommentEntry;

struct CommentStore {
    ITextDocument *document;
    CommentEntry *entries;
    SIZE_T count;
    SIZE_T capacity;
    SIZE_T active;
};

typedef struct ParsedComment {
    LONG start;
    LONG end;
    WCHAR text[COMMENT_TEXT_CAPACITY + 1];
} ParsedComment;

typedef struct ByteBuilder {
    BYTE *data;
    SIZE_T size;
    SIZE_T capacity;
} ByteBuilder;

static const IID wordcraftIidTextDocument = {
    0x8CC497C0, 0xA1DF, 0x11CE,
    {0x80, 0x98, 0x00, 0xAA, 0x00, 0x47, 0xBE, 0x5D}
};

static const BYTE commentMetadataPrefix[] =
    "{\\*\\wordcraftcomments v1;";

static ITextDocument *comments_get_document(HWND editor)
{
    IRichEditOle *richEditOle = NULL;
    ITextDocument *document = NULL;

    if (editor == NULL ||
        !SendMessageW(editor, EM_GETOLEINTERFACE, 0,
                      (LPARAM)&richEditOle) || richEditOle == NULL) {
        return NULL;
    }
    richEditOle->lpVtbl->QueryInterface(richEditOle,
                                       &wordcraftIidTextDocument,
                                       (void **)&document);
    richEditOle->lpVtbl->Release(richEditOle);
    return document;
}

static BOOL comments_valid(const AppState *app)
{
    return app != NULL && app->comments != NULL &&
           app->comments->document != NULL;
}

static BOOL comment_text_length(const WCHAR *text, SIZE_T *length)
{
    SIZE_T index;

    if (text == NULL || length == NULL) {
        return FALSE;
    }
    for (index = 0; index <= COMMENT_TEXT_CAPACITY; ++index) {
        if (text[index] == L'\0') {
            *length = index;
            return index > 0;
        }
    }
    return FALSE;
}

static void comments_refresh_summary(AppState *app)
{
    CommentStore *store;
    WCHAR summary[192];
    WCHAR excerpt[81];
    SIZE_T index;
    SIZE_T excerptLength;

    if (!comments_valid(app)) {
        if (app != NULL) {
            ribbon_set_comment_summary(app, L"No comments");
        }
        return;
    }
    store = app->comments;
    if (store->count == 0) {
        store->active = COMMENT_NO_ACTIVE;
        ribbon_set_comment_summary(app, L"No comments");
        return;
    }
    if (store->active >= store->count) {
        store->active = 0;
    }
    excerptLength = 0;
    while (excerptLength < ARRAYSIZE(excerpt) - 1 &&
           store->entries[store->active].text[excerptLength] != L'\0') {
        WCHAR character = store->entries[store->active].text[excerptLength];
        excerpt[excerptLength] = character < L' ' ? L' ' : character;
        ++excerptLength;
    }
    excerpt[excerptLength] = L'\0';
    for (index = excerptLength; index > 0 && excerpt[index - 1] == L' '; --index) {
        excerpt[index - 1] = L'\0';
    }
    if (FAILED(StringCchPrintfW(summary, ARRAYSIZE(summary),
                                L"Comment %llu of %llu: %s",
                                (unsigned long long)(store->active + 1),
                                (unsigned long long)store->count, excerpt))) {
        StringCchCopyW(summary, ARRAYSIZE(summary), L"Comments");
    }
    ribbon_set_comment_summary(app, summary);
}

static void comments_invalidate(AppState *app)
{
    if (app != NULL && app->editor != NULL) {
        InvalidateRect(app->editor, NULL, FALSE);
    }
}

static BOOL comments_reserve(CommentStore *store, SIZE_T needed)
{
    SIZE_T capacity;
    CommentEntry *entries;

    if (needed <= store->capacity) {
        return TRUE;
    }
    if (needed > COMMENT_COUNT_LIMIT) {
        SetLastError(ERROR_TOO_MANY_NAMES);
        return FALSE;
    }
    capacity = store->capacity == 0 ? 8 : store->capacity;
    while (capacity < needed) {
        if (capacity >= COMMENT_COUNT_LIMIT / 2) {
            capacity = COMMENT_COUNT_LIMIT;
            break;
        }
        capacity *= 2;
    }
    if (store->entries == NULL) {
        entries = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                            capacity * sizeof(*entries));
    } else {
        entries = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                              store->entries, capacity * sizeof(*entries));
    }
    if (entries == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    store->entries = entries;
    store->capacity = capacity;
    return TRUE;
}

static BOOL comments_create_anchor(CommentStore *store, LONG start, LONG end,
                                   ITextRange **anchor)
{
    ITextRange *range = NULL;
    long actualStart = -1;
    long actualEnd = -1;

    *anchor = NULL;
    if (start < 0 || end < start ||
        FAILED(ITextDocument_Range(store->document, start, end, &range)) ||
        range == NULL || FAILED(ITextRange_GetStart(range, &actualStart)) ||
        FAILED(ITextRange_GetEnd(range, &actualEnd)) ||
        actualStart != start || actualEnd != end) {
        if (range != NULL) {
            ITextRange_Release(range);
        }
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    *anchor = range;
    return TRUE;
}

static BOOL comments_append_at(CommentStore *store, LONG start, LONG end,
                               const WCHAR *text)
{
    CommentEntry *entry;
    ITextRange *anchor;
    SIZE_T length;

    if (!comment_text_length(text, &length) ||
        !comments_reserve(store, store->count + 1) ||
        !comments_create_anchor(store, start, end, &anchor)) {
        return FALSE;
    }
    entry = &store->entries[store->count];
    ZeroMemory(entry, sizeof(*entry));
    entry->anchor = anchor;
    CopyMemory(entry->text, text, (length + 1) * sizeof(WCHAR));
    ++store->count;
    return TRUE;
}

static BOOL comments_anchor_bounds(const CommentEntry *entry,
                                   LONG *start, LONG *end)
{
    long rangeStart;
    long rangeEnd;

    if (entry == NULL || entry->anchor == NULL ||
        FAILED(ITextRange_GetStart(entry->anchor, &rangeStart)) ||
        FAILED(ITextRange_GetEnd(entry->anchor, &rangeEnd)) ||
        rangeStart < 0 || rangeEnd < rangeStart) {
        return FALSE;
    }
    *start = rangeStart;
    *end = rangeEnd;
    return TRUE;
}

static UINT comments_text_hash(const WCHAR *text)
{
    UINT hash = 2166136261u;

    while (*text != L'\0') {
        hash ^= (UINT)*text++;
        hash *= 16777619u;
    }
    return hash;
}

BOOL comments_initialize(AppState *app)
{
    CommentStore *store;

    if (app == NULL || app->editor == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (app->comments != NULL) {
        return comments_valid(app);
    }
    store = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*store));
    if (store == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    store->document = comments_get_document(app->editor);
    store->active = COMMENT_NO_ACTIVE;
    if (store->document == NULL) {
        HeapFree(GetProcessHeap(), 0, store);
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    app->comments = store;
    comments_refresh_summary(app);
    return TRUE;
}

void comments_clear(AppState *app)
{
    CommentStore *store;
    SIZE_T index;

    if (app == NULL || app->comments == NULL) {
        return;
    }
    store = app->comments;
    for (index = 0; index < store->count; ++index) {
        if (store->entries[index].anchor != NULL) {
            ITextRange_Release(store->entries[index].anchor);
            store->entries[index].anchor = NULL;
        }
    }
    store->count = 0;
    store->active = COMMENT_NO_ACTIVE;
    comments_refresh_summary(app);
    comments_invalidate(app);
}

BOOL comments_add(AppState *app, const WCHAR *text)
{
    CHARRANGE selection;
    CommentStore *store;
    SIZE_T length;

    if (!comments_valid(app) || !comment_text_length(text, &length)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    store = app->comments;
    if (store->count >= COMMENT_COUNT_LIMIT) {
        app_set_status_message(app, L"This document has reached the comment limit.");
        SetLastError(ERROR_TOO_MANY_NAMES);
        return FALSE;
    }
    ZeroMemory(&selection, sizeof(selection));
    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
    if (selection.cpMin < 0 || selection.cpMax < selection.cpMin ||
        !comments_append_at(store, selection.cpMin, selection.cpMax, text)) {
        app_set_status_message(app, L"The comment could not be added.");
        return FALSE;
    }
    store->active = store->count - 1;
    comments_refresh_summary(app);
    comments_invalidate(app);
    document_mark_modified(app, TRUE);
    app_set_status_message(app, L"Comment added");
    (void)length;
    return TRUE;
}

static void comments_select_active(AppState *app)
{
    CommentStore *store;
    LONG start;
    LONG end;

    if (!comments_valid(app)) {
        return;
    }
    store = app->comments;
    if (store->count == 0 || store->active >= store->count ||
        !comments_anchor_bounds(&store->entries[store->active], &start, &end)) {
        comments_refresh_summary(app);
        return;
    }
    SendMessageW(app->editor, EM_SETSEL, (WPARAM)start, (LPARAM)end);
    SendMessageW(app->editor, EM_SCROLLCARET, 0, 0);
    SetFocus(app->editor);
    comments_refresh_summary(app);
    comments_invalidate(app);
}

void comments_previous(AppState *app)
{
    CommentStore *store;

    if (!comments_valid(app) || app->comments->count == 0) {
        return;
    }
    store = app->comments;
    if (store->active == COMMENT_NO_ACTIVE || store->active == 0) {
        store->active = store->count - 1;
    } else {
        --store->active;
    }
    comments_select_active(app);
}

void comments_next(AppState *app)
{
    CommentStore *store;

    if (!comments_valid(app) || app->comments->count == 0) {
        return;
    }
    store = app->comments;
    if (store->active == COMMENT_NO_ACTIVE || store->active + 1 >= store->count) {
        store->active = 0;
    } else {
        ++store->active;
    }
    comments_select_active(app);
}

void comments_delete_active(AppState *app)
{
    CommentStore *store;
    SIZE_T active;

    if (!comments_valid(app) || app->comments->count == 0) {
        return;
    }
    store = app->comments;
    if (store->active >= store->count) {
        store->active = 0;
    }
    active = store->active;
    if (store->entries[active].anchor != NULL) {
        ITextRange_Release(store->entries[active].anchor);
    }
    if (active + 1 < store->count) {
        MoveMemory(&store->entries[active], &store->entries[active + 1],
                   (store->count - active - 1) * sizeof(*store->entries));
    }
    --store->count;
    ZeroMemory(&store->entries[store->count], sizeof(*store->entries));
    if (store->count == 0) {
        store->active = COMMENT_NO_ACTIVE;
    } else if (active >= store->count) {
        store->active = store->count - 1;
    } else {
        store->active = active;
    }
    comments_refresh_summary(app);
    comments_invalidate(app);
    document_mark_modified(app, TRUE);
    app_set_status_message(app, L"Comment deleted");
}

void comments_selection_changed(AppState *app)
{
    CHARRANGE selection;
    CommentStore *store;
    SIZE_T index;

    if (app != NULL && app->loading) {
        return;
    }
    if (!comments_valid(app) || app->comments->count == 0) {
        comments_refresh_summary(app);
        return;
    }
    store = app->comments;
    ZeroMemory(&selection, sizeof(selection));
    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
    for (index = 0; index < store->count; ++index) {
        LONG start;
        LONG end;
        BOOL matches;
        if (!comments_anchor_bounds(&store->entries[index], &start, &end)) {
            continue;
        }
        if (selection.cpMin == selection.cpMax) {
            matches = selection.cpMin >= start && selection.cpMin <= end;
        } else if (start == end) {
            matches = start >= selection.cpMin && start <= selection.cpMax;
        } else {
            matches = selection.cpMin < end && selection.cpMax > start;
        }
        if (matches) {
            store->active = index;
            break;
        }
    }
    comments_refresh_summary(app);
    comments_invalidate(app);
}

void comments_paint_overlays(AppState *app, HWND editor)
{
    /* Comment anchors are deliberately kept out of the Rich Edit backing
     * store, so they cannot alter copied text or saved formatting.  The
     * Review ribbon provides the visible comment state; this hook remains a
     * safe no-op until margin balloons are introduced. */
    (void)app;
    (void)editor;
}

SIZE_T comments_count(const AppState *app)
{
    return comments_valid(app) ? app->comments->count : 0;
}

static void builder_free(ByteBuilder *builder)
{
    if (builder->data != NULL) {
        HeapFree(GetProcessHeap(), 0, builder->data);
    }
    ZeroMemory(builder, sizeof(*builder));
}

static BOOL builder_reserve(ByteBuilder *builder, SIZE_T additional)
{
    SIZE_T needed;
    SIZE_T capacity;
    BYTE *data;

    if (additional > COMMENT_METADATA_LIMIT ||
        builder->size > COMMENT_METADATA_LIMIT - additional) {
        SetLastError(ERROR_FILE_TOO_LARGE);
        return FALSE;
    }
    needed = builder->size + additional;
    if (needed <= builder->capacity) {
        return TRUE;
    }
    capacity = builder->capacity == 0 ? 256 : builder->capacity;
    while (capacity < needed) {
        if (capacity > COMMENT_METADATA_LIMIT / 2) {
            capacity = COMMENT_METADATA_LIMIT;
            break;
        }
        capacity *= 2;
    }
    data = builder->data == NULL
               ? HeapAlloc(GetProcessHeap(), 0, capacity)
               : HeapReAlloc(GetProcessHeap(), 0, builder->data, capacity);
    if (data == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    builder->data = data;
    builder->capacity = capacity;
    return TRUE;
}

static BOOL builder_append(ByteBuilder *builder, const void *data, SIZE_T size)
{
    if (!builder_reserve(builder, size)) {
        return FALSE;
    }
    if (size > 0) {
        CopyMemory(builder->data + builder->size, data, size);
        builder->size += size;
    }
    return TRUE;
}

static BOOL builder_append_number(ByteBuilder *builder, ULONGLONG value)
{
    CHAR number[32];
    HRESULT result = StringCchPrintfA(number, ARRAYSIZE(number), "%llu", value);
    return SUCCEEDED(result) &&
           builder_append(builder, number, strlen(number));
}

static BOOL builder_append_byte(ByteBuilder *builder, BYTE value)
{
    return builder_append(builder, &value, 1);
}

static BOOL comments_build_metadata(const CommentStore *store,
                                    ByteBuilder *builder)
{
    static const BYTE hexDigits[] = "0123456789ABCDEF";
    SIZE_T index;

    ZeroMemory(builder, sizeof(*builder));
    if (!builder_append(builder, commentMetadataPrefix,
                        sizeof(commentMetadataPrefix) - 1) ||
        !builder_append_number(builder, store->count) ||
        !builder_append_byte(builder, ';')) {
        return FALSE;
    }
    for (index = 0; index < store->count; ++index) {
        LONG start;
        LONG end;
        SIZE_T length;
        SIZE_T textIndex;
        if (!comments_anchor_bounds(&store->entries[index], &start, &end) ||
            !comment_text_length(store->entries[index].text, &length) ||
            !builder_append_number(builder, (ULONGLONG)start) ||
            !builder_append_byte(builder, ',') ||
            !builder_append_number(builder, (ULONGLONG)end) ||
            !builder_append_byte(builder, ',') ||
            !builder_append_number(builder, length) ||
            !builder_append_byte(builder, ',')) {
            return FALSE;
        }
        for (textIndex = 0; textIndex < length; ++textIndex) {
            unsigned value = (unsigned)store->entries[index].text[textIndex];
            BYTE encoded[4];
            encoded[0] = hexDigits[(value >> 12) & 0x0F];
            encoded[1] = hexDigits[(value >> 8) & 0x0F];
            encoded[2] = hexDigits[(value >> 4) & 0x0F];
            encoded[3] = hexDigits[value & 0x0F];
            if (!builder_append(builder, encoded, sizeof(encoded))) {
                return FALSE;
            }
        }
        if (!builder_append_byte(builder, ';')) {
            return FALSE;
        }
    }
    return builder_append_byte(builder, '}');
}

BOOL comments_embed_rtf(AppState *app, const BYTE *rtf, SIZE_T rtfSize,
                        BYTE **output, SIZE_T *outputSize, DWORD *error)
{
    ByteBuilder metadata;
    SIZE_T insertion;
    SIZE_T total;
    BYTE *result;

    if (output == NULL || outputSize == NULL || error == NULL ||
        (rtf == NULL && rtfSize != 0) || !comments_valid(app)) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return FALSE;
    }
    *output = NULL;
    *outputSize = 0;
    *error = ERROR_SUCCESS;
    if (app->comments->count == 0) {
        result = HeapAlloc(GetProcessHeap(), 0, rtfSize == 0 ? 1 : rtfSize);
        if (result == NULL) {
            *error = ERROR_NOT_ENOUGH_MEMORY;
            return FALSE;
        }
        if (rtfSize > 0) {
            CopyMemory(result, rtf, rtfSize);
        }
        *output = result;
        *outputSize = rtfSize;
        return TRUE;
    }
    insertion = rtfSize;
    while (insertion > 0 &&
           (rtf[insertion - 1] == 0 || rtf[insertion - 1] == ' ' ||
            rtf[insertion - 1] == '\t' || rtf[insertion - 1] == '\r' ||
            rtf[insertion - 1] == '\n')) {
        --insertion;
    }
    if (insertion == 0 || rtf[insertion - 1] != '}') {
        *error = ERROR_INVALID_DATA;
        return FALSE;
    }
    --insertion;
    if (!comments_build_metadata(app->comments, &metadata)) {
        *error = GetLastError() != ERROR_SUCCESS
                     ? GetLastError() : ERROR_INVALID_DATA;
        builder_free(&metadata);
        return FALSE;
    }
    if (rtfSize > SIZE_MAX - metadata.size) {
        *error = ERROR_NOT_ENOUGH_MEMORY;
        builder_free(&metadata);
        return FALSE;
    }
    total = rtfSize + metadata.size;
    result = HeapAlloc(GetProcessHeap(), 0, total == 0 ? 1 : total);
    if (result == NULL) {
        *error = ERROR_NOT_ENOUGH_MEMORY;
        builder_free(&metadata);
        return FALSE;
    }
    CopyMemory(result, rtf, insertion);
    CopyMemory(result + insertion, metadata.data, metadata.size);
    CopyMemory(result + insertion + metadata.size,
               rtf + insertion, rtfSize - insertion);
    builder_free(&metadata);
    *output = result;
    *outputSize = total;
    return TRUE;
}

static const BYTE *comments_find_bytes(const BYTE *data, SIZE_T size,
                                       const BYTE *needle, SIZE_T needleSize,
                                       SIZE_T offset)
{
    SIZE_T index;

    if (needleSize == 0 || offset > size || needleSize > size - offset) {
        return NULL;
    }
    for (index = offset; index <= size - needleSize; ++index) {
        if (data[index] == needle[0] &&
            memcmp(data + index, needle, needleSize) == 0) {
            return data + index;
        }
    }
    return NULL;
}

static BOOL comments_parse_number(const BYTE **cursor, const BYTE *end,
                                  BYTE delimiter, ULONGLONG maximum,
                                  ULONGLONG *value)
{
    ULONGLONG parsed = 0;
    BOOL sawDigit = FALSE;

    while (*cursor < end && **cursor >= '0' && **cursor <= '9') {
        unsigned digit = (unsigned)(**cursor - '0');
        sawDigit = TRUE;
        if (parsed > (maximum - digit) / 10) {
            return FALSE;
        }
        parsed = parsed * 10 + digit;
        ++*cursor;
    }
    if (!sawDigit || *cursor >= end || **cursor != delimiter) {
        return FALSE;
    }
    ++*cursor;
    *value = parsed;
    return TRUE;
}

static int comments_hex_value(BYTE character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    return -1;
}

static BOOL comments_parse_metadata(const BYTE *data, SIZE_T size,
                                    ParsedComment **parsedOutput,
                                    SIZE_T *countOutput, BOOL *found)
{
    const SIZE_T prefixSize = sizeof(commentMetadataPrefix) - 1;
    const BYTE *group;
    const BYTE *cursor;
    const BYTE *end;
    ParsedComment *parsed = NULL;
    ULONGLONG countValue;
    SIZE_T count;
    SIZE_T index;

    *parsedOutput = NULL;
    *countOutput = 0;
    *found = FALSE;
    if (size == 0) {
        return TRUE;
    }
    if (data == NULL) {
        return FALSE;
    }
    end = data + size;
    group = comments_find_bytes(data, size, commentMetadataPrefix,
                                prefixSize, 0);
    if (group == NULL) {
        return TRUE;
    }
    *found = TRUE;
    cursor = group + prefixSize;
    if (!comments_parse_number(&cursor, end, ';', COMMENT_COUNT_LIMIT,
                               &countValue)) {
        return FALSE;
    }
    count = (SIZE_T)countValue;
    if (count > 0) {
        parsed = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                           count * sizeof(*parsed));
        if (parsed == NULL) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
    }
    for (index = 0; index < count; ++index) {
        ULONGLONG start;
        ULONGLONG finish;
        ULONGLONG lengthValue;
        SIZE_T length;
        SIZE_T textIndex;
        if (!comments_parse_number(&cursor, end, ',', LONG_MAX, &start) ||
            !comments_parse_number(&cursor, end, ',', LONG_MAX, &finish) ||
            finish < start ||
            !comments_parse_number(&cursor, end, ',', COMMENT_TEXT_CAPACITY,
                                   &lengthValue)) {
            HeapFree(GetProcessHeap(), 0, parsed);
            return FALSE;
        }
        length = (SIZE_T)lengthValue;
        if (length == 0 || length > (SIZE_T)(end - cursor) / 4) {
            HeapFree(GetProcessHeap(), 0, parsed);
            return FALSE;
        }
        parsed[index].start = (LONG)start;
        parsed[index].end = (LONG)finish;
        for (textIndex = 0; textIndex < length; ++textIndex) {
            int a = comments_hex_value(cursor[0]);
            int b = comments_hex_value(cursor[1]);
            int c = comments_hex_value(cursor[2]);
            int d = comments_hex_value(cursor[3]);
            unsigned value;
            if (a < 0 || b < 0 || c < 0 || d < 0) {
                HeapFree(GetProcessHeap(), 0, parsed);
                return FALSE;
            }
            value = ((unsigned)a << 12) | ((unsigned)b << 8) |
                    ((unsigned)c << 4) | (unsigned)d;
            if (value == 0) {
                HeapFree(GetProcessHeap(), 0, parsed);
                return FALSE;
            }
            parsed[index].text[textIndex] = (WCHAR)value;
            cursor += 4;
        }
        parsed[index].text[length] = L'\0';
        if (cursor >= end || *cursor != ';') {
            HeapFree(GetProcessHeap(), 0, parsed);
            return FALSE;
        }
        ++cursor;
    }
    if (cursor >= end || *cursor != '}' ||
        (SIZE_T)(cursor - group + 1) > COMMENT_METADATA_LIMIT ||
        comments_find_bytes(data, size, commentMetadataPrefix, prefixSize,
                            (SIZE_T)(cursor - data + 1)) != NULL) {
        HeapFree(GetProcessHeap(), 0, parsed);
        return FALSE;
    }
    *parsedOutput = parsed;
    *countOutput = count;
    return TRUE;
}

static BOOL comments_read_file(const WCHAR *path, BYTE **data, SIZE_T *size,
                               DWORD *error)
{
    HANDLE file;
    LARGE_INTEGER fileSize;
    BYTE *buffer = NULL;
    SIZE_T total = 0;

    *data = NULL;
    *size = 0;
    file = CreateFileW(path, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        *error = GetLastError();
        return FALSE;
    }
    if (!GetFileSizeEx(file, &fileSize)) {
        *error = GetLastError();
        CloseHandle(file);
        return FALSE;
    }
    if (fileSize.QuadPart < 0 ||
        (ULONGLONG)fileSize.QuadPart > COMMENT_FILE_LIMIT ||
        (ULONGLONG)fileSize.QuadPart > SIZE_MAX) {
        *error = ERROR_FILE_TOO_LARGE;
        CloseHandle(file);
        return FALSE;
    }
    if (fileSize.QuadPart > 0) {
        buffer = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)fileSize.QuadPart);
        if (buffer == NULL) {
            *error = ERROR_NOT_ENOUGH_MEMORY;
            CloseHandle(file);
            return FALSE;
        }
    }
    while (total < (SIZE_T)fileSize.QuadPart) {
        DWORD amount = (DWORD)(((SIZE_T)fileSize.QuadPart - total) > 0x40000000u
                                   ? 0x40000000u
                                   : ((SIZE_T)fileSize.QuadPart - total));
        DWORD read = 0;
        if (!ReadFile(file, buffer + total, amount, &read, NULL) || read == 0) {
            *error = GetLastError() != ERROR_SUCCESS
                         ? GetLastError() : ERROR_HANDLE_EOF;
            HeapFree(GetProcessHeap(), 0, buffer);
            CloseHandle(file);
            return FALSE;
        }
        total += read;
    }
    if (!CloseHandle(file)) {
        *error = GetLastError();
        HeapFree(GetProcessHeap(), 0, buffer);
        return FALSE;
    }
    *data = buffer;
    *size = total;
    return TRUE;
}

BOOL comments_load_rtf_file(AppState *app, const WCHAR *path, DWORD *error)
{
    BYTE *data = NULL;
    SIZE_T size = 0;
    ParsedComment *parsed = NULL;
    SIZE_T count = 0;
    SIZE_T index;
    BOOL found = FALSE;

    if (!comments_valid(app) || path == NULL || error == NULL) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return FALSE;
    }
    *error = ERROR_SUCCESS;
    if (!comments_read_file(path, &data, &size, error)) {
        return FALSE;
    }
    if (!comments_parse_metadata(data, size, &parsed, &count, &found)) {
        *error = GetLastError() != ERROR_SUCCESS
                     ? GetLastError() : ERROR_INVALID_DATA;
        HeapFree(GetProcessHeap(), 0, data);
        return FALSE;
    }
    HeapFree(GetProcessHeap(), 0, data);
    comments_clear(app);
    if (!found) {
        return TRUE;
    }
    for (index = 0; index < count; ++index) {
        if (!comments_append_at(app->comments, parsed[index].start,
                                parsed[index].end, parsed[index].text)) {
            *error = GetLastError() != ERROR_SUCCESS
                         ? GetLastError() : ERROR_INVALID_DATA;
            HeapFree(GetProcessHeap(), 0, parsed);
            comments_clear(app);
            return FALSE;
        }
    }
    HeapFree(GetProcessHeap(), 0, parsed);
    app->comments->active = count > 0 ? 0 : COMMENT_NO_ACTIVE;
    comments_refresh_summary(app);
    comments_invalidate(app);
    return TRUE;
}

LRESULT comments_query_state(const AppState *app, UINT query, LPARAM index)
{
    const CommentStore *store;
    LONG start;
    LONG end;

    if (!comments_valid(app)) {
        return query == WCQ_COMMENT_ACTIVE_INDEX ||
                       query == WCQ_COMMENT_ANCHOR_START ||
                       query == WCQ_COMMENT_ANCHOR_END
                   ? -1 : 0;
    }
    store = app->comments;
    switch (query) {
    case WCQ_COMMENT_COUNT:
        return (LRESULT)store->count;
    case WCQ_COMMENT_ACTIVE_INDEX:
        return store->active < store->count ? (LRESULT)store->active : -1;
    case WCQ_COMMENT_ANCHOR_START:
    case WCQ_COMMENT_ANCHOR_END:
        if (index < 0 || (SIZE_T)index >= store->count ||
            !comments_anchor_bounds(&store->entries[(SIZE_T)index],
                                    &start, &end)) {
            return -1;
        }
        return query == WCQ_COMMENT_ANCHOR_START ? start : end;
    case WCQ_COMMENT_TEXT_HASH:
        if (index < 0 || (SIZE_T)index >= store->count) {
            return 0;
        }
        return (LRESULT)(UINT_PTR)
            comments_text_hash(store->entries[(SIZE_T)index].text);
    default:
        return 0;
    }
}

void comments_shutdown(AppState *app)
{
    CommentStore *store;

    if (app == NULL || app->comments == NULL) {
        return;
    }
    store = app->comments;
    comments_clear(app);
    if (store->entries != NULL) {
        HeapFree(GetProcessHeap(), 0, store->entries);
    }
    if (store->document != NULL) {
        ITextDocument_Release(store->document);
    }
    HeapFree(GetProcessHeap(), 0, store);
    app->comments = NULL;
}
