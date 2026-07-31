#include "editor.h"
#include "live.h"
#include "rendereditor.h"

#include <limits.h>
#include <objbase.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HISTORY_MAX_CHATS HISTORY_CHAT_RETENTION_LIMIT
#define HISTORY_MAX_REMOTE_CHAT_IMPORT 64u
#define HISTORY_MAX_VERSIONS 40u
#define HISTORY_MAX_SNAPSHOT_BYTES ((SIZE_T)12u * 1024u * 1024u)
#define HISTORY_METADATA_LIMIT ((SIZE_T)48u * 1024u * 1024u)
#define HISTORY_TEXT_SNAPSHOT_LIMIT ((SIZE_T)16u * 1024u * 1024u)
#define HISTORY_DEBOUNCE_MS 1200u
#define HISTORY_VERSION_STATE_TIMER_ID 1u
#define HISTORY_VERSION_STATE_REFRESH_MS 250u
#define HISTORY_BINARY_HEADER_SIZE 32u
#define HISTORY_BINARY_VERSION 1u
#define HISTORY_DEFAULT_AUTHOR L"WordCraft user"
#define HISTORY_UNKNOWN_AUTHOR L"Unknown author"

typedef struct HistoryId {
    BYTE bytes[16];
} HistoryId;

typedef struct ChatEntry {
    HistoryId id;
    FILETIME timestamp;
    WCHAR author[HISTORY_AUTHOR_CAPACITY + 1];
    WCHAR *text;
} ChatEntry;

typedef struct VersionEntry {
    HistoryId id;
    FILETIME timestamp;
    WCHAR author[HISTORY_AUTHOR_CAPACITY + 1];
    BYTE *rtf;
    SIZE_T rtfSize;
    WCHAR *plainText;
    SIZE_T plainLength;
    LONG changeStart;
    LONG changeEnd;
    SIZE_T insertedCount;
    SIZE_T deletedCount;
    ULONGLONG contentHash;
    PaperSizeId paperSizeId;
    LONG pageWidth;
    LONG pageHeight;
    RECT margins;
} VersionEntry;

struct HistoryContext {
    AppState *app;
    ChatEntry *chats;
    SIZE_T chatCount;
    SIZE_T chatCapacity;
    VersionEntry *versions;
    SIZE_T versionCount;
    SIZE_T versionCapacity;
    SIZE_T snapshotBytes;
    BOOL revisionPending;
    HWND chatDialog;
    HWND versionDialog;
};

typedef struct ByteBuilder {
    BYTE *data;
    SIZE_T size;
    SIZE_T capacity;
} ByteBuilder;

typedef struct WideBuilder {
    WCHAR *data;
    SIZE_T length;
    SIZE_T capacity;
} WideBuilder;

typedef struct MemoryReadContext {
    const BYTE *data;
    SIZE_T size;
    SIZE_T position;
} MemoryReadContext;

static const BYTE historyBinaryMagic[8] = {
    'W', 'C', 'H', 'I', 'S', 'T', '1', 0
};

static const BYTE historyDestinationPrefix[] =
    "{\\*\\wordcrafthistory\\bin";

static BOOL history_valid(const AppState *app)
{
    return app != NULL && app->history != NULL &&
           app->history->app == app;
}

static ULONGLONG history_filetime_value(const FILETIME *time)
{
    ULARGE_INTEGER value;
    value.LowPart = time->dwLowDateTime;
    value.HighPart = time->dwHighDateTime;
    return value.QuadPart;
}

static FILETIME history_filetime_from_value(ULONGLONG raw)
{
    ULARGE_INTEGER value;
    FILETIME result;
    value.QuadPart = raw;
    result.dwLowDateTime = value.LowPart;
    result.dwHighDateTime = value.HighPart;
    return result;
}

static int history_id_compare(const HistoryId *left, const HistoryId *right)
{
    return memcmp(left->bytes, right->bytes, sizeof(left->bytes));
}

static BOOL history_id_equal(const HistoryId *left, const HistoryId *right)
{
    return history_id_compare(left, right) == 0;
}

static BOOL history_chat_token_matches_id(
    const HistoryChatToken *token, const HistoryId *id)
{
    return token != NULL && id != NULL &&
           memcmp(token->bytes, id->bytes, sizeof(id->bytes)) == 0;
}

static BOOL history_chat_tokens_contain(
    const HistoryChatToken *tokens, SIZE_T count, const HistoryId *id)
{
    SIZE_T index;

    for (index = 0; index < count; ++index) {
        if (history_chat_token_matches_id(&tokens[index], id)) {
            return TRUE;
        }
    }
    return FALSE;
}

static void history_create_id(HistoryId *id)
{
    GUID guid;

    ZeroMemory(id, sizeof(*id));
    if (SUCCEEDED(CoCreateGuid(&guid))) {
        CopyMemory(id->bytes, &guid, sizeof(id->bytes));
    } else {
        LARGE_INTEGER counter;
        FILETIME time;
        DWORD process = GetCurrentProcessId();
        DWORD thread = GetCurrentThreadId();
        QueryPerformanceCounter(&counter);
        GetSystemTimeAsFileTime(&time);
        CopyMemory(id->bytes, &time, sizeof(time));
        CopyMemory(id->bytes + 8, &counter.LowPart, sizeof(counter.LowPart));
        CopyMemory(id->bytes + 12, &process, sizeof(process));
        id->bytes[0] ^= (BYTE)thread;
        id->bytes[1] ^= (BYTE)(thread >> 8);
    }
}

static UINT history_hash_text(const WCHAR *text)
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

static ULONGLONG history_hash_bytes(ULONGLONG hash, const void *data,
                                    SIZE_T size)
{
    const BYTE *bytes = (const BYTE *)data;
    SIZE_T index;

    for (index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static ULONGLONG history_content_hash(const BYTE *rtf, SIZE_T rtfSize,
                                      PaperSizeId paperSizeId,
                                      LONG pageWidth, LONG pageHeight,
                                      const RECT *margins)
{
    ULONGLONG hash = UINT64_C(1469598103934665603);

    hash = history_hash_bytes(hash, rtf, rtfSize);
    hash = history_hash_bytes(hash, &paperSizeId, sizeof(paperSizeId));
    hash = history_hash_bytes(hash, &pageWidth, sizeof(pageWidth));
    hash = history_hash_bytes(hash, &pageHeight, sizeof(pageHeight));
    hash = history_hash_bytes(hash, margins, sizeof(*margins));
    return hash;
}

static BOOL history_wide_length(const WCHAR *text, SIZE_T maximum,
                                SIZE_T *length)
{
    SIZE_T index;

    if (text == NULL || length == NULL) {
        return FALSE;
    }
    for (index = 0; index <= maximum; ++index) {
        if (text[index] == L'\0') {
            *length = index;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL history_valid_utf16(const WCHAR *text, SIZE_T length,
                                BOOL allowNewlines)
{
    SIZE_T index;

    if (text == NULL && length != 0) {
        return FALSE;
    }
    for (index = 0; index < length; ++index) {
        WCHAR value = text[index];
        if (value == L'\0' ||
            (!allowNewlines && (value == L'\r' || value == L'\n'))) {
            return FALSE;
        }
        if (value >= 0xD800 && value <= 0xDBFF) {
            if (index + 1 >= length ||
                text[index + 1] < 0xDC00 ||
                text[index + 1] > 0xDFFF) {
                return FALSE;
            }
            ++index;
        } else if (value >= 0xDC00 && value <= 0xDFFF) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL history_author_is_valid(const WCHAR *author, SIZE_T *lengthOutput)
{
    SIZE_T length;
    SIZE_T index;
    BOOL hasVisibleCharacter = FALSE;

    if (!history_wide_length(author, HISTORY_AUTHOR_CAPACITY, &length) ||
        length == 0 ||
        !history_valid_utf16(author, length, FALSE)) {
        return FALSE;
    }
    for (index = 0; index < length; ++index) {
        WCHAR value = author[index];
        if (value < 0x20 ||
            (value >= 0x7F && value <= 0x9F) ||
            value == 0x2028 || value == 0x2029 ||
            (value >= 0x202A && value <= 0x202E) ||
            (value >= 0x2066 && value <= 0x2069)) {
            return FALSE;
        }
        if (!iswspace(value)) {
            hasVisibleCharacter = TRUE;
        }
    }
    if (!hasVisibleCharacter) {
        return FALSE;
    }
    if (lengthOutput != NULL) {
        *lengthOutput = length;
    }
    return TRUE;
}

BOOL history_author_is_acceptable(const WCHAR *author)
{
    return history_author_is_valid(author, NULL);
}

static void history_copy_author_or_fallback(
    const WCHAR *requested, const WCHAR *fallback,
    WCHAR author[HISTORY_AUTHOR_CAPACITY + 1])
{
    if (history_author_is_valid(requested, NULL) &&
        SUCCEEDED(StringCchCopyW(author, HISTORY_AUTHOR_CAPACITY + 1,
                                 requested))) {
        return;
    }
    if (fallback == NULL ||
        FAILED(StringCchCopyW(author, HISTORY_AUTHOR_CAPACITY + 1,
                              fallback))) {
        StringCchCopyW(author, HISTORY_AUTHOR_CAPACITY + 1,
                       HISTORY_UNKNOWN_AUTHOR);
    }
}

static BOOL history_version_storage_size_values(SIZE_T rtfSize,
                                                SIZE_T plainLength,
                                                SIZE_T *sizeOutput)
{
    SIZE_T plainBytes;

    if (sizeOutput == NULL ||
        plainLength > SIZE_MAX / sizeof(WCHAR)) {
        return FALSE;
    }
    plainBytes = plainLength * sizeof(WCHAR);
    if (rtfSize > SIZE_MAX - plainBytes) {
        return FALSE;
    }
    *sizeOutput = rtfSize + plainBytes;
    return TRUE;
}

static BOOL history_version_storage_size(const VersionEntry *entry,
                                         SIZE_T *sizeOutput)
{
    return entry != NULL &&
           history_version_storage_size_values(
               entry->rtfSize, entry->plainLength, sizeOutput);
}

static BOOL history_validate_version_entry(const VersionEntry *entry,
                                           DWORD *error)
{
    const PaperSizePreset *preset;
    SIZE_T storageSize;
    DWORD validationError = ERROR_SUCCESS;

    if (error != NULL) {
        *error = ERROR_SUCCESS;
    }
    if (entry == NULL || entry->rtf == NULL || entry->rtfSize == 0 ||
        entry->plainText == NULL ||
        entry->plainLength >
            HISTORY_TEXT_SNAPSHOT_LIMIT / sizeof(WCHAR) ||
        !history_author_is_valid(entry->author, NULL) ||
        !history_valid_utf16(entry->plainText, entry->plainLength, TRUE) ||
        entry->paperSizeId < 0 ||
        entry->paperSizeId >= PAPER_SIZE_COUNT ||
        entry->changeStart < 0 ||
        entry->changeEnd < entry->changeStart ||
        (SIZE_T)entry->changeEnd > entry->plainLength ||
        entry->insertedCount !=
            (SIZE_T)(entry->changeEnd - entry->changeStart) ||
        !paper_size_validate_dimensions(
            entry->pageWidth, entry->pageHeight, &entry->margins) ||
        !history_version_storage_size(entry, &storageSize)) {
        if (error != NULL) {
            *error = ERROR_INVALID_DATA;
        }
        return FALSE;
    }
    if (storageSize > HISTORY_MAX_SNAPSHOT_BYTES) {
        if (error != NULL) {
            *error = ERROR_FILE_TOO_LARGE;
        }
        return FALSE;
    }
    preset = paper_size_by_id(entry->paperSizeId);
    if (preset == NULL ||
        (entry->paperSizeId != PAPER_SIZE_CUSTOM &&
         (preset->widthThousandths != entry->pageWidth ||
          preset->heightThousandths != entry->pageHeight)) ||
        history_content_hash(entry->rtf, entry->rtfSize,
                             entry->paperSizeId, entry->pageWidth,
                             entry->pageHeight, &entry->margins) !=
            entry->contentHash) {
        if (error != NULL) {
            *error = ERROR_INVALID_DATA;
        }
        return FALSE;
    }
    if (!document_validate_history_snapshot(entry->rtf, entry->rtfSize,
                                            &validationError)) {
        if (error != NULL) {
            *error = validationError != ERROR_SUCCESS
                         ? validationError : ERROR_INVALID_DATA;
        }
        return FALSE;
    }
    return TRUE;
}

void history_get_local_author(WCHAR *author, SIZE_T capacity)
{
    WCHAR candidate[HISTORY_AUTHOR_CAPACITY + 1];
    WCHAR normalized[HISTORY_AUTHOR_CAPACITY + 1];
    DWORD count;

    if (author == NULL || capacity == 0 || capacity > MAXDWORD) {
        return;
    }
    author[0] = L'\0';
    candidate[0] = L'\0';
    count = GetEnvironmentVariableW(L"USERNAME", candidate,
                                    ARRAYSIZE(candidate));
    if (count == 0 || count >= ARRAYSIZE(candidate)) {
        candidate[0] = L'\0';
    }
    history_copy_author_or_fallback(
        candidate, HISTORY_DEFAULT_AUTHOR, normalized);
    if (FAILED(StringCchCopyW(author, capacity, normalized))) {
        author[0] = L'\0';
    }
}

static void history_chat_release(ChatEntry *entry)
{
    if (entry != NULL && entry->text != NULL) {
        HeapFree(GetProcessHeap(), 0, entry->text);
        entry->text = NULL;
    }
}

static void history_version_release(VersionEntry *entry)
{
    if (entry == NULL) {
        return;
    }
    if (entry->rtf != NULL) {
        HeapFree(GetProcessHeap(), 0, entry->rtf);
        entry->rtf = NULL;
    }
    if (entry->plainText != NULL) {
        HeapFree(GetProcessHeap(), 0, entry->plainText);
        entry->plainText = NULL;
    }
    entry->rtfSize = 0;
    entry->plainLength = 0;
}

static void history_release_entries(HistoryContext *history)
{
    SIZE_T index;

    if (history == NULL) {
        return;
    }
    for (index = 0; index < history->chatCount; ++index) {
        history_chat_release(&history->chats[index]);
    }
    for (index = 0; index < history->versionCount; ++index) {
        history_version_release(&history->versions[index]);
    }
    HeapFree(GetProcessHeap(), 0, history->chats);
    HeapFree(GetProcessHeap(), 0, history->versions);
    history->chats = NULL;
    history->versions = NULL;
    history->chatCount = 0;
    history->chatCapacity = 0;
    history->versionCount = 0;
    history->versionCapacity = 0;
    history->snapshotBytes = 0;
}

static BOOL history_reserve_chats(HistoryContext *history, SIZE_T needed)
{
    SIZE_T capacity;
    ChatEntry *entries;

    if (needed <= history->chatCapacity) {
        return TRUE;
    }
    if (needed > HISTORY_MAX_CHATS) {
        SetLastError(ERROR_TOO_MANY_NAMES);
        return FALSE;
    }
    capacity = history->chatCapacity == 0 ? 8 : history->chatCapacity;
    while (capacity < needed) {
        capacity *= 2;
        if (capacity > HISTORY_MAX_CHATS) {
            capacity = HISTORY_MAX_CHATS;
        }
    }
    entries = history->chats == NULL
                  ? HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                              capacity * sizeof(*entries))
                  : HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                history->chats,
                                capacity * sizeof(*entries));
    if (entries == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    history->chats = entries;
    history->chatCapacity = capacity;
    return TRUE;
}

static BOOL history_reserve_versions(HistoryContext *history, SIZE_T needed)
{
    SIZE_T capacity;
    VersionEntry *entries;

    if (needed <= history->versionCapacity) {
        return TRUE;
    }
    if (needed > HISTORY_MAX_VERSIONS) {
        SetLastError(ERROR_TOO_MANY_NAMES);
        return FALSE;
    }
    capacity = history->versionCapacity == 0 ? 8 : history->versionCapacity;
    while (capacity < needed) {
        capacity *= 2;
        if (capacity > HISTORY_MAX_VERSIONS) {
            capacity = HISTORY_MAX_VERSIONS;
        }
    }
    entries = history->versions == NULL
                  ? HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                              capacity * sizeof(*entries))
                  : HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                history->versions,
                                capacity * sizeof(*entries));
    if (entries == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    history->versions = entries;
    history->versionCapacity = capacity;
    return TRUE;
}

static void history_remove_oldest_chat(HistoryContext *history)
{
    if (history == NULL || history->chatCount == 0) {
        return;
    }
    history_chat_release(&history->chats[0]);
    if (history->chatCount > 1) {
        MoveMemory(history->chats, history->chats + 1,
                   (history->chatCount - 1) * sizeof(*history->chats));
    }
    --history->chatCount;
    ZeroMemory(&history->chats[history->chatCount],
               sizeof(*history->chats));
}

static void history_remove_oldest_version(HistoryContext *history)
{
    SIZE_T bytes = 0;

    if (history == NULL || history->versionCount == 0) {
        return;
    }
    (void)history_version_storage_size(&history->versions[0], &bytes);
    history_version_release(&history->versions[0]);
    if (history->versionCount > 1) {
        MoveMemory(history->versions, history->versions + 1,
                   (history->versionCount - 1) *
                       sizeof(*history->versions));
    }
    --history->versionCount;
    ZeroMemory(&history->versions[history->versionCount],
               sizeof(*history->versions));
    history->snapshotBytes =
        bytes <= history->snapshotBytes
            ? history->snapshotBytes - bytes
            : 0;
}

static BOOL history_copy_wide(const WCHAR *source, SIZE_T length,
                              WCHAR **output)
{
    WCHAR *copy;

    if (output == NULL || (source == NULL && length != 0) ||
        length > (SIZE_MAX / sizeof(WCHAR)) - 1) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    copy = HeapAlloc(GetProcessHeap(), 0,
                     (length + 1) * sizeof(WCHAR));
    if (copy == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    if (length != 0) {
        CopyMemory(copy, source, length * sizeof(WCHAR));
    }
    copy[length] = L'\0';
    *output = copy;
    return TRUE;
}

static BOOL history_clone_chat(const ChatEntry *source, ChatEntry *target)
{
    SIZE_T length;

    if (source == NULL || target == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ZeroMemory(target, sizeof(*target));
    if (!history_wide_length(source->text, HISTORY_CHAT_TEXT_CAPACITY,
                             &length) ||
        length == 0 ||
        !history_valid_utf16(source->text, length, TRUE)) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    if (!history_copy_wide(source->text, length, &target->text)) {
        return FALSE;
    }
    target->id = source->id;
    target->timestamp = source->timestamp;
    history_copy_author_or_fallback(
        source->author, HISTORY_UNKNOWN_AUTHOR, target->author);
    return TRUE;
}

static BOOL history_clone_version(const VersionEntry *source,
                                  VersionEntry *target)
{
    BYTE *rtfCopy = NULL;
    WCHAR *textCopy = NULL;
    DWORD validationError = ERROR_SUCCESS;

    if (target == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ZeroMemory(target, sizeof(*target));
    if (!history_validate_version_entry(source, &validationError)) {
        SetLastError(validationError != ERROR_SUCCESS
                         ? validationError : ERROR_INVALID_DATA);
        return FALSE;
    }
    if (source->rtfSize != 0) {
        rtfCopy = HeapAlloc(GetProcessHeap(), 0, source->rtfSize);
        if (rtfCopy == NULL) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        CopyMemory(rtfCopy, source->rtf, source->rtfSize);
    }
    if (!history_copy_wide(source->plainText, source->plainLength,
                           &textCopy)) {
        HeapFree(GetProcessHeap(), 0, rtfCopy);
        return FALSE;
    }
    *target = *source;
    target->rtf = rtfCopy;
    target->plainText = textCopy;
    return TRUE;
}

static void byte_builder_free(ByteBuilder *builder)
{
    if (builder != NULL && builder->data != NULL) {
        HeapFree(GetProcessHeap(), 0, builder->data);
    }
    if (builder != NULL) {
        ZeroMemory(builder, sizeof(*builder));
    }
}

static BOOL byte_builder_reserve(ByteBuilder *builder, SIZE_T additional)
{
    SIZE_T needed;
    SIZE_T capacity;
    BYTE *data;

    if (builder == NULL || additional > HISTORY_METADATA_LIMIT ||
        builder->size > HISTORY_METADATA_LIMIT - additional) {
        SetLastError(ERROR_FILE_TOO_LARGE);
        return FALSE;
    }
    needed = builder->size + additional;
    if (needed <= builder->capacity) {
        return TRUE;
    }
    capacity = builder->capacity == 0 ? 4096 : builder->capacity;
    while (capacity < needed) {
        if (capacity > HISTORY_METADATA_LIMIT / 2) {
            capacity = HISTORY_METADATA_LIMIT;
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

static BOOL byte_builder_append(ByteBuilder *builder, const void *data,
                                SIZE_T size)
{
    if ((data == NULL && size != 0) ||
        !byte_builder_reserve(builder, size)) {
        return FALSE;
    }
    if (size != 0) {
        CopyMemory(builder->data + builder->size, data, size);
    }
    builder->size += size;
    return TRUE;
}

static BOOL byte_builder_u32(ByteBuilder *builder, uint32_t value)
{
    BYTE bytes[4];
    bytes[0] = (BYTE)value;
    bytes[1] = (BYTE)(value >> 8);
    bytes[2] = (BYTE)(value >> 16);
    bytes[3] = (BYTE)(value >> 24);
    return byte_builder_append(builder, bytes, sizeof(bytes));
}

static BOOL byte_builder_u64(ByteBuilder *builder, uint64_t value)
{
    BYTE bytes[8];
    size_t index;
    for (index = 0; index < ARRAYSIZE(bytes); ++index) {
        bytes[index] = (BYTE)(value >> (index * 8));
    }
    return byte_builder_append(builder, bytes, sizeof(bytes));
}

static BOOL byte_builder_i32(ByteBuilder *builder, LONG value)
{
    return byte_builder_u32(builder, (uint32_t)(int32_t)value);
}

static BOOL history_build_payload_range(
    const HistoryContext *history, SIZE_T chatStart, SIZE_T chatCount,
    SIZE_T versionStart, SIZE_T versionCount, ByteBuilder *builder)
{
    SIZE_T index;

    ZeroMemory(builder, sizeof(*builder));
    if (history == NULL ||
        chatStart > history->chatCount ||
        chatCount > history->chatCount - chatStart ||
        versionStart > history->versionCount ||
        versionCount > history->versionCount - versionStart ||
        chatCount > UINT32_MAX ||
        versionCount > UINT32_MAX ||
        !byte_builder_append(builder, historyBinaryMagic,
                             sizeof(historyBinaryMagic)) ||
        !byte_builder_u32(builder, HISTORY_BINARY_VERSION) ||
        !byte_builder_u32(builder, HISTORY_BINARY_HEADER_SIZE) ||
        !byte_builder_u32(builder, (uint32_t)chatCount) ||
        !byte_builder_u32(builder, (uint32_t)versionCount) ||
        !byte_builder_u32(builder, 0u) ||
        !byte_builder_u32(builder, 0u)) {
        byte_builder_free(builder);
        return FALSE;
    }
    for (index = chatStart; index < chatStart + chatCount; ++index) {
        const ChatEntry *entry = &history->chats[index];
        SIZE_T authorLength;
        SIZE_T textLength;

        if (!history_author_is_valid(entry->author, &authorLength) ||
            !history_wide_length(entry->text, HISTORY_CHAT_TEXT_CAPACITY,
                                 &textLength) ||
            textLength == 0 ||
            !history_valid_utf16(entry->text, textLength, TRUE) ||
            authorLength > UINT32_MAX || textLength > UINT32_MAX) {
            SetLastError(ERROR_INVALID_DATA);
            byte_builder_free(builder);
            return FALSE;
        }
        if (!byte_builder_append(builder, entry->id.bytes,
                                 sizeof(entry->id.bytes)) ||
            !byte_builder_u64(builder,
                              history_filetime_value(&entry->timestamp)) ||
            !byte_builder_u32(builder, (uint32_t)authorLength) ||
            !byte_builder_u32(builder, (uint32_t)textLength) ||
            !byte_builder_append(builder, entry->author,
                                 authorLength * sizeof(WCHAR)) ||
            !byte_builder_append(builder, entry->text,
                                 textLength * sizeof(WCHAR))) {
            byte_builder_free(builder);
            return FALSE;
        }
    }
    for (index = versionStart;
         index < versionStart + versionCount; ++index) {
        const VersionEntry *entry = &history->versions[index];
        SIZE_T authorLength;
        DWORD validationError = ERROR_SUCCESS;

        if (!history_validate_version_entry(entry, &validationError) ||
            !history_author_is_valid(entry->author, &authorLength) ||
            authorLength > UINT32_MAX ||
            entry->plainLength > UINT32_MAX ||
            entry->rtfSize > UINT32_MAX ||
            entry->insertedCount > UINT32_MAX ||
            entry->deletedCount > UINT32_MAX) {
            SetLastError(validationError != ERROR_SUCCESS
                             ? validationError : ERROR_INVALID_DATA);
            byte_builder_free(builder);
            return FALSE;
        }
        if (!byte_builder_append(builder, entry->id.bytes,
                                 sizeof(entry->id.bytes)) ||
            !byte_builder_u64(builder,
                              history_filetime_value(&entry->timestamp)) ||
            !byte_builder_u32(builder, (uint32_t)authorLength) ||
            !byte_builder_u32(builder, (uint32_t)entry->plainLength) ||
            !byte_builder_u32(builder, (uint32_t)entry->rtfSize) ||
            !byte_builder_i32(builder, (LONG)entry->paperSizeId) ||
            !byte_builder_i32(builder, entry->pageWidth) ||
            !byte_builder_i32(builder, entry->pageHeight) ||
            !byte_builder_i32(builder, entry->margins.left) ||
            !byte_builder_i32(builder, entry->margins.top) ||
            !byte_builder_i32(builder, entry->margins.right) ||
            !byte_builder_i32(builder, entry->margins.bottom) ||
            !byte_builder_i32(builder, entry->changeStart) ||
            !byte_builder_i32(builder, entry->changeEnd) ||
            !byte_builder_u32(builder, (uint32_t)entry->insertedCount) ||
            !byte_builder_u32(builder, (uint32_t)entry->deletedCount) ||
            !byte_builder_u64(builder, entry->contentHash) ||
            !byte_builder_append(builder, entry->author,
                                 authorLength * sizeof(WCHAR)) ||
            !byte_builder_append(builder, entry->plainText,
                                 entry->plainLength * sizeof(WCHAR)) ||
            !byte_builder_append(builder, entry->rtf, entry->rtfSize)) {
            byte_builder_free(builder);
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL history_build_payload(const HistoryContext *history,
                                  ByteBuilder *builder)
{
    if (history == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        ZeroMemory(builder, sizeof(*builder));
        return FALSE;
    }
    return history_build_payload_range(
        history, 0, history->chatCount, 0, history->versionCount,
        builder);
}

static BOOL history_build_payload_selection(
    const HistoryContext *history, const BYTE *selectedChats,
    SIZE_T selectedChatCount, const BYTE *selectedVersions,
    SIZE_T selectedVersionCount, ByteBuilder *builder)
{
    HistoryContext selection;
    SIZE_T index;
    BOOL result;

    ZeroMemory(&selection, sizeof(selection));
    ZeroMemory(builder, sizeof(*builder));
    if (history == NULL ||
        (history->chatCount != 0 && selectedChats == NULL) ||
        (history->versionCount != 0 && selectedVersions == NULL) ||
        selectedChatCount > history->chatCount ||
        selectedVersionCount > history->versionCount) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (selectedChatCount != 0) {
        selection.chats = HeapAlloc(
            GetProcessHeap(), 0,
            selectedChatCount * sizeof(*selection.chats));
        if (selection.chats == NULL) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
    }
    if (selectedVersionCount != 0) {
        selection.versions = HeapAlloc(
            GetProcessHeap(), 0,
            selectedVersionCount * sizeof(*selection.versions));
        if (selection.versions == NULL) {
            HeapFree(GetProcessHeap(), 0, selection.chats);
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
    }
    for (index = 0; index < history->chatCount; ++index) {
        if (selectedChats[index] != 0) {
            selection.chats[selection.chatCount++] =
                history->chats[index];
        }
    }
    for (index = 0; index < history->versionCount; ++index) {
        if (selectedVersions[index] != 0) {
            selection.versions[selection.versionCount++] =
                history->versions[index];
        }
    }
    if (selection.chatCount != selectedChatCount ||
        selection.versionCount != selectedVersionCount) {
        HeapFree(GetProcessHeap(), 0, selection.versions);
        HeapFree(GetProcessHeap(), 0, selection.chats);
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    result = history_build_payload_range(
        &selection, 0, selection.chatCount, 0,
        selection.versionCount, builder);
    HeapFree(GetProcessHeap(), 0, selection.versions);
    HeapFree(GetProcessHeap(), 0, selection.chats);
    return result;
}

static BOOL history_chat_record_size(const ChatEntry *entry,
                                     SIZE_T *recordSize)
{
    SIZE_T authorLength;
    SIZE_T textLength;
    SIZE_T characterCount;

    if (entry == NULL || recordSize == NULL ||
        !history_author_is_valid(entry->author, &authorLength) ||
        !history_wide_length(entry->text, HISTORY_CHAT_TEXT_CAPACITY,
                             &textLength) ||
        textLength == 0 ||
        !history_valid_utf16(entry->text, textLength, TRUE) ||
        authorLength > SIZE_MAX - textLength) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    characterCount = authorLength + textLength;
    if (characterCount > (SIZE_MAX - 32u) / sizeof(WCHAR)) {
        SetLastError(ERROR_FILE_TOO_LARGE);
        return FALSE;
    }
    *recordSize = 32u + characterCount * sizeof(WCHAR);
    return TRUE;
}

static BOOL history_version_record_size(const VersionEntry *entry,
                                        SIZE_T *recordSize)
{
    SIZE_T authorLength;
    SIZE_T snapshotSize;
    SIZE_T authorBytes;
    DWORD validationError = ERROR_SUCCESS;

    if (recordSize == NULL ||
        !history_validate_version_entry(entry, &validationError) ||
        !history_author_is_valid(entry->author, &authorLength) ||
        !history_version_storage_size(entry, &snapshotSize) ||
        authorLength > SIZE_MAX / sizeof(WCHAR)) {
        SetLastError(validationError != ERROR_SUCCESS
                         ? validationError : ERROR_INVALID_DATA);
        return FALSE;
    }
    authorBytes = authorLength * sizeof(WCHAR);
    if (snapshotSize > SIZE_MAX - authorBytes ||
        snapshotSize + authorBytes > SIZE_MAX - 88u) {
        SetLastError(ERROR_FILE_TOO_LARGE);
        return FALSE;
    }
    *recordSize = 88u + authorBytes + snapshotSize;
    return TRUE;
}

static BOOL history_embedded_output_size(SIZE_T rtfSize,
                                         SIZE_T payloadSize,
                                         SIZE_T *outputSize)
{
    CHAR prefix[96];
    SIZE_T prefixSize;

    if (outputSize == NULL ||
        FAILED(StringCchPrintfA(
            prefix, ARRAYSIZE(prefix),
            "{\\*\\wordcrafthistory\\bin%llu ",
            (unsigned long long)payloadSize))) {
        return FALSE;
    }
    prefixSize = strlen(prefix);
    if (rtfSize > SIZE_MAX - prefixSize ||
        rtfSize + prefixSize > SIZE_MAX - payloadSize ||
        rtfSize + prefixSize + payloadSize > SIZE_MAX - 1u) {
        return FALSE;
    }
    *outputSize = rtfSize + prefixSize + payloadSize + 1u;
    return TRUE;
}

static BOOL history_copy_bare_rtf(const BYTE *rtf, SIZE_T rtfSize,
                                  BYTE **output, SIZE_T *outputSize,
                                  DWORD *error)
{
    BYTE *copy;

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
    return TRUE;
}

static uint32_t history_read_u32(const BYTE *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static uint64_t history_read_u64(const BYTE *bytes)
{
    uint64_t value = 0;
    size_t index;
    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static LONG history_read_i32(const BYTE *bytes)
{
    return (LONG)(int32_t)history_read_u32(bytes);
}

static BOOL history_take_bytes(const BYTE **cursor, const BYTE *end,
                               SIZE_T size, const BYTE **output)
{
    if (cursor == NULL || *cursor == NULL || end == NULL ||
        *cursor > end || size > (SIZE_T)(end - *cursor)) {
        return FALSE;
    }
    if (output != NULL) {
        *output = *cursor;
    }
    *cursor += size;
    return TRUE;
}

static BOOL history_take_u32(const BYTE **cursor, const BYTE *end,
                             uint32_t *output)
{
    const BYTE *bytes;
    if (!history_take_bytes(cursor, end, 4, &bytes)) {
        return FALSE;
    }
    *output = history_read_u32(bytes);
    return TRUE;
}

static BOOL history_take_u64(const BYTE **cursor, const BYTE *end,
                             uint64_t *output)
{
    const BYTE *bytes;
    if (!history_take_bytes(cursor, end, 8, &bytes)) {
        return FALSE;
    }
    *output = history_read_u64(bytes);
    return TRUE;
}

static BOOL history_ascii_letter(BYTE value)
{
    return (value >= 'a' && value <= 'z') ||
           (value >= 'A' && value <= 'Z');
}

static int history_hex_value(BYTE value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static BOOL history_find_payload(const BYTE *data, SIZE_T size,
                                 const BYTE **payloadOutput,
                                 SIZE_T *payloadSizeOutput, BOOL *foundOutput)
{
    const SIZE_T prefixSize = sizeof(historyDestinationPrefix) - 1;
    SIZE_T index = 0;
    LONG depth = 0;
    BOOL rootClosed = FALSE;
    BOOL found = FALSE;
    const BYTE *payload = NULL;
    SIZE_T payloadSize = 0;

    if (payloadOutput == NULL || payloadSizeOutput == NULL ||
        foundOutput == NULL || (data == NULL && size != 0)) {
        return FALSE;
    }
    *payloadOutput = NULL;
    *payloadSizeOutput = 0;
    *foundOutput = FALSE;
    if (size == 0 || data[0] != '{') {
        return size == 0;
    }
    while (index < size) {
        BYTE value = data[index];

        if (rootClosed) {
            if (value != 0 && value != ' ' && value != '\t' &&
                value != '\r' && value != '\n') {
                return FALSE;
            }
            ++index;
            continue;
        }
        if (value == 0) {
            return FALSE;
        }
        if (value == '{') {
            if (depth == 1 && prefixSize <= size - index &&
                memcmp(data + index, historyDestinationPrefix,
                       prefixSize) == 0) {
                SIZE_T cursor = index + prefixSize;
                SIZE_T encodedSize = 0;
                BOOL sawDigit = FALSE;

                if (found) {
                    return FALSE;
                }
                while (cursor < size && data[cursor] >= '0' &&
                       data[cursor] <= '9') {
                    unsigned digit = (unsigned)(data[cursor] - '0');
                    sawDigit = TRUE;
                    if (encodedSize > (HISTORY_METADATA_LIMIT - digit) / 10) {
                        return FALSE;
                    }
                    encodedSize = encodedSize * 10 + digit;
                    ++cursor;
                }
                if (!sawDigit || cursor >= size || data[cursor] != ' ' ||
                    encodedSize > size - cursor - 1) {
                    return FALSE;
                }
                ++cursor;
                if (encodedSize > size - cursor ||
                    cursor + encodedSize >= size ||
                    data[cursor + encodedSize] != '}') {
                    return FALSE;
                }
                payload = data + cursor;
                payloadSize = encodedSize;
                found = TRUE;
            }
            ++depth;
            ++index;
            continue;
        }
        if (value == '}') {
            if (depth <= 0) {
                return FALSE;
            }
            --depth;
            ++index;
            if (depth == 0) {
                rootClosed = TRUE;
            }
            continue;
        }
        if (value != '\\') {
            ++index;
            continue;
        }
        ++index;
        if (index >= size || data[index] == 0) {
            return FALSE;
        }
        value = data[index];
        if (value == '\'') {
            if (index + 2 >= size ||
                history_hex_value(data[index + 1]) < 0 ||
                history_hex_value(data[index + 2]) < 0) {
                return FALSE;
            }
            index += 3;
            continue;
        }
        if (history_ascii_letter(value)) {
            SIZE_T wordStart = index;
            SIZE_T binarySize = 0;
            BOOL hasNumber = FALSE;
            BOOL negative = FALSE;
            BOOL binary;

            while (index < size && history_ascii_letter(data[index])) {
                ++index;
            }
            binary = index - wordStart == 3 &&
                     data[wordStart] == 'b' &&
                     data[wordStart + 1] == 'i' &&
                     data[wordStart + 2] == 'n';
            if (index < size && data[index] == '-') {
                negative = TRUE;
                ++index;
            }
            while (index < size && data[index] >= '0' &&
                   data[index] <= '9') {
                unsigned digit = (unsigned)(data[index] - '0');
                hasNumber = TRUE;
                if (binarySize > (SIZE_MAX - digit) / 10) {
                    return FALSE;
                }
                binarySize = binarySize * 10 + digit;
                ++index;
            }
            if (index < size && data[index] == ' ') {
                ++index;
            }
            if (binary) {
                if (!hasNumber || negative || binarySize > size - index) {
                    return FALSE;
                }
                index += binarySize;
            }
            continue;
        }
        ++index;
        if (value == '\r' && index < size && data[index] == '\n') {
            ++index;
        }
    }
    if (!rootClosed || depth != 0) {
        return FALSE;
    }
    *payloadOutput = payload;
    *payloadSizeOutput = payloadSize;
    *foundOutput = found;
    return TRUE;
}

static BOOL history_contains_chat_id(const HistoryContext *history,
                                     const HistoryId *id)
{
    SIZE_T index;
    for (index = 0; index < history->chatCount; ++index) {
        if (history_id_equal(&history->chats[index].id, id)) {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL history_find_version_index(const HistoryContext *history,
                                       const HistoryId *id,
                                       SIZE_T *indexOutput)
{
    SIZE_T index;

    if (history == NULL || id == NULL) {
        return FALSE;
    }
    for (index = 0; index < history->versionCount; ++index) {
        if (history_id_equal(&history->versions[index].id, id)) {
            if (indexOutput != NULL) {
                *indexOutput = index;
            }
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL history_contains_version_id(const HistoryContext *history,
                                        const HistoryId *id)
{
    return history_find_version_index(history, id, NULL);
}

static BOOL history_parse_payload(const BYTE *payload, SIZE_T payloadSize,
                                  AppState *app, HistoryContext *parsed)
{
    const BYTE *cursor = payload;
    const BYTE *end = payload + payloadSize;
    const BYTE *bytes;
    uint32_t version;
    uint32_t headerSize;
    uint32_t chatCount;
    uint32_t versionCount;
    uint32_t reserved;
    SIZE_T index;

    ZeroMemory(parsed, sizeof(*parsed));
    parsed->app = app;
    if (payload == NULL || payloadSize < HISTORY_BINARY_HEADER_SIZE ||
        !history_take_bytes(&cursor, end, sizeof(historyBinaryMagic),
                            &bytes) ||
        memcmp(bytes, historyBinaryMagic,
               sizeof(historyBinaryMagic)) != 0 ||
        !history_take_u32(&cursor, end, &version) ||
        !history_take_u32(&cursor, end, &headerSize) ||
        !history_take_u32(&cursor, end, &chatCount) ||
        !history_take_u32(&cursor, end, &versionCount) ||
        !history_take_u32(&cursor, end, &reserved) ||
        reserved != 0u ||
        !history_take_u32(&cursor, end, &reserved) ||
        reserved != 0u ||
        version != HISTORY_BINARY_VERSION ||
        headerSize != HISTORY_BINARY_HEADER_SIZE ||
        chatCount > HISTORY_MAX_CHATS ||
        versionCount > HISTORY_MAX_VERSIONS) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    if (chatCount != 0 &&
        !history_reserve_chats(parsed, (SIZE_T)chatCount)) {
        history_release_entries(parsed);
        return FALSE;
    }
    for (index = 0; index < chatCount; ++index) {
        ChatEntry *entry = &parsed->chats[parsed->chatCount];
        uint64_t timestamp;
        uint32_t authorLength;
        uint32_t textLength;
        const BYTE *authorBytes;
        const BYTE *textBytes;
        WCHAR normalizedAuthor[HISTORY_AUTHOR_CAPACITY + 1];

        if (!history_take_bytes(&cursor, end, sizeof(entry->id.bytes),
                                &bytes) ||
            !history_take_u64(&cursor, end, &timestamp) ||
            !history_take_u32(&cursor, end, &authorLength) ||
            !history_take_u32(&cursor, end, &textLength) ||
            authorLength > HISTORY_AUTHOR_CAPACITY ||
            textLength == 0 ||
            textLength > HISTORY_CHAT_TEXT_CAPACITY ||
            authorLength > (SIZE_T)(end - cursor) / sizeof(WCHAR) ||
            !history_take_bytes(&cursor, end,
                                (SIZE_T)authorLength * sizeof(WCHAR),
                                &authorBytes) ||
            textLength > (SIZE_T)(end - cursor) / sizeof(WCHAR) ||
            !history_take_bytes(&cursor, end,
                                (SIZE_T)textLength * sizeof(WCHAR),
                                &textBytes)) {
            SetLastError(ERROR_INVALID_DATA);
            history_release_entries(parsed);
            return FALSE;
        }
        CopyMemory(entry->id.bytes, bytes, sizeof(entry->id.bytes));
        if (history_contains_chat_id(parsed, &entry->id)) {
            SetLastError(ERROR_INVALID_DATA);
            history_release_entries(parsed);
            return FALSE;
        }
        entry->timestamp = history_filetime_from_value(timestamp);
        CopyMemory(entry->author, authorBytes,
                   (SIZE_T)authorLength * sizeof(WCHAR));
        entry->author[authorLength] = L'\0';
        if (!history_copy_wide((const WCHAR *)textBytes, textLength,
                               &entry->text)) {
            history_chat_release(entry);
            history_release_entries(parsed);
            return FALSE;
        }
        if (!history_valid_utf16(entry->text, textLength, TRUE)) {
            SetLastError(ERROR_INVALID_DATA);
            history_chat_release(entry);
            history_release_entries(parsed);
            return FALSE;
        }
        if (history_valid_utf16(entry->author, authorLength, FALSE)) {
            history_copy_author_or_fallback(
                entry->author, HISTORY_UNKNOWN_AUTHOR,
                normalizedAuthor);
        } else {
            StringCchCopyW(normalizedAuthor, ARRAYSIZE(normalizedAuthor),
                           HISTORY_UNKNOWN_AUTHOR);
        }
        StringCchCopyW(entry->author, ARRAYSIZE(entry->author),
                       normalizedAuthor);
        ++parsed->chatCount;
    }
    if (versionCount != 0 &&
        !history_reserve_versions(parsed, (SIZE_T)versionCount)) {
        history_release_entries(parsed);
        return FALSE;
    }
    for (index = 0; index < versionCount; ++index) {
        VersionEntry *entry = &parsed->versions[parsed->versionCount];
        uint64_t timestamp;
        uint64_t contentHash;
        uint32_t authorLength;
        uint32_t plainLength;
        uint32_t rtfSize;
        uint32_t insertedCount;
        uint32_t deletedCount;
        const BYTE *authorBytes;
        const BYTE *plainBytes;
        const BYTE *rtfBytes;
        LONG values[9];
        size_t valueIndex;
        const PaperSizePreset *preset;
        SIZE_T entryBytes;
        DWORD validationError = ERROR_INVALID_DATA;
        WCHAR normalizedAuthor[HISTORY_AUTHOR_CAPACITY + 1];

        if (!history_take_bytes(&cursor, end, sizeof(entry->id.bytes),
                                &bytes) ||
            !history_take_u64(&cursor, end, &timestamp) ||
            !history_take_u32(&cursor, end, &authorLength) ||
            !history_take_u32(&cursor, end, &plainLength) ||
            !history_take_u32(&cursor, end, &rtfSize)) {
            goto invalid_version;
        }
        CopyMemory(entry->id.bytes, bytes, sizeof(entry->id.bytes));
        if (authorLength > HISTORY_AUTHOR_CAPACITY ||
            plainLength >
                HISTORY_TEXT_SNAPSHOT_LIMIT / sizeof(WCHAR) ||
            rtfSize == 0) {
            goto invalid_version;
        }
        if (rtfSize > HISTORY_MAX_SNAPSHOT_BYTES) {
            validationError = ERROR_FILE_TOO_LARGE;
            goto invalid_version;
        }
        for (valueIndex = 0; valueIndex < ARRAYSIZE(values); ++valueIndex) {
            if (!history_take_bytes(&cursor, end, 4, &bytes)) {
                goto invalid_version;
            }
            values[valueIndex] = history_read_i32(bytes);
        }
        if (!history_take_u32(&cursor, end, &insertedCount) ||
            !history_take_u32(&cursor, end, &deletedCount) ||
            !history_take_u64(&cursor, end, &contentHash) ||
            authorLength > (SIZE_T)(end - cursor) / sizeof(WCHAR) ||
            !history_take_bytes(&cursor, end,
                                (SIZE_T)authorLength * sizeof(WCHAR),
                                &authorBytes) ||
            plainLength > (SIZE_T)(end - cursor) / sizeof(WCHAR) ||
            !history_take_bytes(&cursor, end,
                                (SIZE_T)plainLength * sizeof(WCHAR),
                                &plainBytes) ||
            !history_take_bytes(&cursor, end, rtfSize, &rtfBytes)) {
            goto invalid_version;
        }
        entry->timestamp = history_filetime_from_value(timestamp);
        entry->paperSizeId = (PaperSizeId)values[0];
        entry->pageWidth = values[1];
        entry->pageHeight = values[2];
        entry->margins.left = values[3];
        entry->margins.top = values[4];
        entry->margins.right = values[5];
        entry->margins.bottom = values[6];
        entry->changeStart = values[7];
        entry->changeEnd = values[8];
        entry->insertedCount = insertedCount;
        entry->deletedCount = deletedCount;
        entry->contentHash = contentHash;
        entry->rtfSize = rtfSize;
        entry->plainLength = plainLength;
        if (entry->paperSizeId < 0 ||
            entry->paperSizeId >= PAPER_SIZE_COUNT ||
            entry->changeStart < 0 ||
            entry->changeEnd < entry->changeStart ||
            (SIZE_T)entry->changeEnd > entry->plainLength ||
            entry->insertedCount !=
                (SIZE_T)(entry->changeEnd - entry->changeStart) ||
            !paper_size_validate_dimensions(
                entry->pageWidth, entry->pageHeight, &entry->margins)) {
            goto invalid_version;
        }
        preset = paper_size_by_id(entry->paperSizeId);
        if (preset == NULL ||
            (entry->paperSizeId != PAPER_SIZE_CUSTOM &&
             (preset->widthThousandths != entry->pageWidth ||
              preset->heightThousandths != entry->pageHeight))) {
            goto invalid_version;
        }
        CopyMemory(entry->author, authorBytes,
                   (SIZE_T)authorLength * sizeof(WCHAR));
        entry->author[authorLength] = L'\0';
        if (history_valid_utf16(entry->author, authorLength, FALSE)) {
            history_copy_author_or_fallback(
                entry->author, HISTORY_UNKNOWN_AUTHOR,
                normalizedAuthor);
        } else {
            StringCchCopyW(normalizedAuthor, ARRAYSIZE(normalizedAuthor),
                           HISTORY_UNKNOWN_AUTHOR);
        }
        StringCchCopyW(entry->author, ARRAYSIZE(entry->author),
                       normalizedAuthor);
        if (!history_copy_wide((const WCHAR *)plainBytes, plainLength,
                               &entry->plainText)) {
            history_version_release(entry);
            history_release_entries(parsed);
            return FALSE;
        }
        entry->rtf = HeapAlloc(GetProcessHeap(), 0, rtfSize);
        if (entry->rtf == NULL) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            history_version_release(entry);
            history_release_entries(parsed);
            return FALSE;
        }
        CopyMemory(entry->rtf, rtfBytes, rtfSize);
        if (!history_validate_version_entry(entry, &validationError)) {
            goto invalid_version;
        }
        if (history_contains_version_id(parsed, &entry->id)) {
            validationError = ERROR_INVALID_DATA;
            goto invalid_version;
        }
        if (!history_version_storage_size(entry, &entryBytes) ||
            entryBytes >
                HISTORY_MAX_SNAPSHOT_BYTES - parsed->snapshotBytes) {
            validationError = ERROR_FILE_TOO_LARGE;
            goto invalid_version;
        }
        parsed->snapshotBytes += entryBytes;
        ++parsed->versionCount;
        continue;

invalid_version:
        SetLastError(validationError != ERROR_SUCCESS
                         ? validationError : ERROR_INVALID_DATA);
        history_version_release(entry);
        history_release_entries(parsed);
        return FALSE;
    }
    if (cursor != end) {
        SetLastError(ERROR_INVALID_DATA);
        history_release_entries(parsed);
        return FALSE;
    }
    return TRUE;
}

static int __cdecl history_chat_compare(const void *left, const void *right)
{
    const ChatEntry *a = (const ChatEntry *)left;
    const ChatEntry *b = (const ChatEntry *)right;
    LONG timeOrder = CompareFileTime(&a->timestamp, &b->timestamp);
    return timeOrder != 0 ? (timeOrder < 0 ? -1 : 1)
                          : history_id_compare(&a->id, &b->id);
}

static int __cdecl history_version_compare(const void *left,
                                           const void *right)
{
    const VersionEntry *a = (const VersionEntry *)left;
    const VersionEntry *b = (const VersionEntry *)right;
    LONG timeOrder = CompareFileTime(&a->timestamp, &b->timestamp);
    return timeOrder != 0 ? (timeOrder < 0 ? -1 : 1)
                          : history_id_compare(&a->id, &b->id);
}

static void history_sort(HistoryContext *history)
{
    if (history->chatCount > 1) {
        qsort(history->chats, history->chatCount,
              sizeof(*history->chats), history_chat_compare);
    }
    if (history->versionCount > 1) {
        qsort(history->versions, history->versionCount,
              sizeof(*history->versions), history_version_compare);
    }
}

static BOOL history_clone_store(const HistoryContext *source,
                                HistoryContext *target)
{
    SIZE_T index;

    ZeroMemory(target, sizeof(*target));
    target->app = source->app;
    if (source->chatCount != 0 &&
        !history_reserve_chats(target, source->chatCount)) {
        return FALSE;
    }
    for (index = 0; index < source->chatCount; ++index) {
        if (!history_clone_chat(&source->chats[index],
                                &target->chats[target->chatCount])) {
            history_release_entries(target);
            return FALSE;
        }
        ++target->chatCount;
    }
    if (source->versionCount != 0 &&
        !history_reserve_versions(target, source->versionCount)) {
        history_release_entries(target);
        return FALSE;
    }
    for (index = 0; index < source->versionCount; ++index) {
        SIZE_T entryBytes;

        if (!history_version_storage_size(
                &source->versions[index], &entryBytes)) {
            SetLastError(ERROR_INVALID_DATA);
            history_release_entries(target);
            return FALSE;
        }
        if (target->snapshotBytes > HISTORY_MAX_SNAPSHOT_BYTES ||
            entryBytes >
                HISTORY_MAX_SNAPSHOT_BYTES - target->snapshotBytes) {
            SetLastError(ERROR_FILE_TOO_LARGE);
            history_release_entries(target);
            return FALSE;
        }
        if (!history_clone_version(
                &source->versions[index],
                &target->versions[target->versionCount])) {
            history_release_entries(target);
            return FALSE;
        }
        target->snapshotBytes += entryBytes;
        ++target->versionCount;
    }
    return TRUE;
}

static void history_replace_entries(HistoryContext *target,
                                    HistoryContext *source)
{
    history_release_entries(target);
    target->chats = source->chats;
    target->chatCount = source->chatCount;
    target->chatCapacity = source->chatCapacity;
    target->versions = source->versions;
    target->versionCount = source->versionCount;
    target->versionCapacity = source->versionCapacity;
    target->snapshotBytes = source->snapshotBytes;
    source->chats = NULL;
    source->versions = NULL;
    source->chatCount = source->chatCapacity = 0;
    source->versionCount = source->versionCapacity = 0;
    source->snapshotBytes = 0;
}

static BOOL history_parse_rtf(const BYTE *data, SIZE_T size, AppState *app,
                              HistoryContext *parsed, BOOL *found)
{
    const BYTE *payload;
    SIZE_T payloadSize;

    ZeroMemory(parsed, sizeof(*parsed));
    parsed->app = app;
    if (!history_find_payload(data, size, &payload, &payloadSize, found)) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    if (!*found) {
        return TRUE;
    }
    return history_parse_payload(payload, payloadSize, app, parsed);
}

BOOL history_embed_rtf(AppState *app, const BYTE *rtf, SIZE_T rtfSize,
                       BYTE **output, SIZE_T *outputSize, DWORD *error)
{
    HistoryContext *history;
    ByteBuilder payload;
    CHAR prefix[96];
    SIZE_T prefixSize;
    SIZE_T insertion;
    SIZE_T total;
    BYTE *result;
    const BYTE *existingPayload;
    SIZE_T existingSize;
    BOOL existingFound;

    if (!history_valid(app) || output == NULL || outputSize == NULL ||
        error == NULL || (rtf == NULL && rtfSize != 0)) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return FALSE;
    }
    *output = NULL;
    *outputSize = 0;
    *error = ERROR_SUCCESS;
    history = app->history;
    if (history->chatCount == 0 && history->versionCount == 0) {
        result = HeapAlloc(GetProcessHeap(), 0, rtfSize == 0 ? 1 : rtfSize);
        if (result == NULL) {
            *error = ERROR_NOT_ENOUGH_MEMORY;
            return FALSE;
        }
        if (rtfSize != 0) {
            CopyMemory(result, rtf, rtfSize);
        }
        *output = result;
        *outputSize = rtfSize;
        return TRUE;
    }
    if (!history_find_payload(rtf, rtfSize, &existingPayload,
                              &existingSize, &existingFound) ||
        existingFound) {
        (void)existingPayload;
        (void)existingSize;
        *error = ERROR_INVALID_DATA;
        return FALSE;
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
    if (!history_build_payload(history, &payload)) {
        *error = GetLastError() != ERROR_SUCCESS
                     ? GetLastError() : ERROR_INVALID_DATA;
        return FALSE;
    }
    if (FAILED(StringCchPrintfA(prefix, ARRAYSIZE(prefix),
                                "{\\*\\wordcrafthistory\\bin%llu ",
                                (unsigned long long)payload.size))) {
        byte_builder_free(&payload);
        *error = ERROR_INSUFFICIENT_BUFFER;
        return FALSE;
    }
    prefixSize = strlen(prefix);
    if (rtfSize > SIZE_MAX - prefixSize - payload.size - 1) {
        byte_builder_free(&payload);
        *error = ERROR_NOT_ENOUGH_MEMORY;
        return FALSE;
    }
    total = rtfSize + prefixSize + payload.size + 1;
    result = HeapAlloc(GetProcessHeap(), 0, total == 0 ? 1 : total);
    if (result == NULL) {
        byte_builder_free(&payload);
        *error = ERROR_NOT_ENOUGH_MEMORY;
        return FALSE;
    }
    CopyMemory(result, rtf, insertion);
    CopyMemory(result + insertion, prefix, prefixSize);
    CopyMemory(result + insertion + prefixSize,
               payload.data, payload.size);
    result[insertion + prefixSize + payload.size] = '}';
    CopyMemory(result + insertion + prefixSize + payload.size + 1,
               rtf + insertion, rtfSize - insertion);
    byte_builder_free(&payload);
    *output = result;
    *outputSize = total;
    return TRUE;
}

BOOL history_embed_rtf_bounded(
    AppState *app, const BYTE *rtf, SIZE_T rtfSize,
    SIZE_T maximumOutputSize,
    const HistoryChatToken *requiredChats,
    SIZE_T requiredChatCount, BYTE **output, SIZE_T *outputSize,
    DWORD *error)
{
    HistoryContext *history;
    ByteBuilder payload;
    BYTE selectedChats[HISTORY_MAX_CHATS];
    BYTE selectedVersions[HISTORY_MAX_VERSIONS];
    SIZE_T selectedChatCount = 0;
    SIZE_T selectedVersionCount = 0;
    SIZE_T payloadSize = HISTORY_BINARY_HEADER_SIZE;
    SIZE_T index;
    SIZE_T requiredIndex;
    SIZE_T recordSize;
    SIZE_T candidatePayloadSize;
    SIZE_T candidateOutputSize;
    SIZE_T insertion;
    SIZE_T total;
    const BYTE *existingPayload;
    SIZE_T existingPayloadSize;
    BOOL existingFound;
    CHAR prefix[96];
    SIZE_T prefixSize;
    BYTE *result;
    BOOL newestVersionFits = TRUE;

    if (!history_valid(app) || output == NULL || outputSize == NULL ||
        error == NULL || (rtf == NULL && rtfSize != 0) ||
        (requiredChats == NULL && requiredChatCount != 0) ||
        requiredChatCount > HISTORY_MAX_CHATS) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return FALSE;
    }
    *output = NULL;
    *outputSize = 0;
    *error = ERROR_SUCCESS;
    if (rtfSize > maximumOutputSize) {
        *error = ERROR_FILE_TOO_LARGE;
        return FALSE;
    }
    if (!history_find_payload(rtf, rtfSize, &existingPayload,
                              &existingPayloadSize, &existingFound) ||
        existingFound) {
        (void)existingPayload;
        (void)existingPayloadSize;
        *error = ERROR_INVALID_DATA;
        return FALSE;
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

    history = app->history;
    if (history->chatCount > HISTORY_MAX_CHATS ||
        history->versionCount > HISTORY_MAX_VERSIONS) {
        *error = ERROR_INVALID_DATA;
        return FALSE;
    }
    ZeroMemory(selectedChats, sizeof(selectedChats));
    ZeroMemory(selectedVersions, sizeof(selectedVersions));

    /*
     * Reserve the exact locally pending or just-accepted chat IDs first.
     * Timestamp order can differ across peers, so a mere newest-N suffix
     * cannot guarantee delivery of the messages awaiting acknowledgement.
     */
    for (requiredIndex = 0; requiredIndex < requiredChatCount;
         ++requiredIndex) {
        BOOL found = FALSE;

        for (index = 0; index < history->chatCount; ++index) {
            if (!history_chat_token_matches_id(
                    &requiredChats[requiredIndex],
                    &history->chats[index].id)) {
                continue;
            }
            found = TRUE;
            if (selectedChats[index] != 0) {
                break;
            }
            SetLastError(ERROR_SUCCESS);
            if (!history_chat_record_size(
                    &history->chats[index], &recordSize)) {
                *error = GetLastError() != ERROR_SUCCESS
                             ? GetLastError() : ERROR_INVALID_DATA;
                return FALSE;
            }
            if (recordSize > HISTORY_METADATA_LIMIT - payloadSize) {
                *error = ERROR_FILE_TOO_LARGE;
                return FALSE;
            }
            candidatePayloadSize = payloadSize + recordSize;
            if (!history_embedded_output_size(
                    rtfSize, candidatePayloadSize,
                    &candidateOutputSize) ||
                candidateOutputSize > maximumOutputSize) {
                *error = ERROR_FILE_TOO_LARGE;
                return FALSE;
            }
            selectedChats[index] = 1;
            ++selectedChatCount;
            payloadSize = candidatePayloadSize;
            break;
        }
        if (!found) {
            *error = ERROR_INVALID_DATA;
            return FALSE;
        }
    }

    if (requiredChatCount == 0 && history->chatCount != 0) {
        index = history->chatCount - 1u;
        if (selectedChats[index] == 0) {
            SetLastError(ERROR_SUCCESS);
            if (!history_chat_record_size(
                    &history->chats[index], &recordSize)) {
                *error = GetLastError() != ERROR_SUCCESS
                             ? GetLastError() : ERROR_INVALID_DATA;
                return FALSE;
            }
            if (recordSize <= HISTORY_METADATA_LIMIT - payloadSize) {
                candidatePayloadSize = payloadSize + recordSize;
                if (history_embedded_output_size(
                        rtfSize, candidatePayloadSize,
                        &candidateOutputSize) &&
                    candidateOutputSize <= maximumOutputSize) {
                    selectedChats[index] = 1;
                    ++selectedChatCount;
                    payloadSize = candidatePayloadSize;
                }
            }
        }
    }

    if (history->versionCount != 0) {
        index = history->versionCount - 1u;
        SetLastError(ERROR_SUCCESS);
        if (!history_version_record_size(
                &history->versions[index], &recordSize)) {
            *error = GetLastError() != ERROR_SUCCESS
                         ? GetLastError() : ERROR_INVALID_DATA;
            return FALSE;
        }
        if (recordSize <= HISTORY_METADATA_LIMIT - payloadSize) {
            candidatePayloadSize = payloadSize + recordSize;
            if (history_embedded_output_size(
                rtfSize, candidatePayloadSize,
                &candidateOutputSize) &&
                candidateOutputSize <= maximumOutputSize) {
                selectedVersions[index] = 1;
                ++selectedVersionCount;
                payloadSize = candidatePayloadSize;
            } else {
                newestVersionFits = FALSE;
            }
        } else {
            newestVersionFits = FALSE;
        }
    }

    for (index = requiredChatCount == 0
                     ? history->chatCount : 0;
         index != 0; --index) {
        SIZE_T chatIndex = index - 1u;
        if (selectedChats[chatIndex] != 0) {
            continue;
        }
        SetLastError(ERROR_SUCCESS);
        if (!history_chat_record_size(
                &history->chats[chatIndex], &recordSize)) {
            *error = GetLastError() != ERROR_SUCCESS
                         ? GetLastError() : ERROR_INVALID_DATA;
            return FALSE;
        }
        if (recordSize <= HISTORY_METADATA_LIMIT - payloadSize) {
            candidatePayloadSize = payloadSize + recordSize;
            if (history_embedded_output_size(
                rtfSize, candidatePayloadSize,
                &candidateOutputSize) &&
                candidateOutputSize <= maximumOutputSize) {
                selectedChats[chatIndex] = 1;
                ++selectedChatCount;
                payloadSize = candidatePayloadSize;
            }
        }
    }

    for (index = newestVersionFits ? history->versionCount : 0;
         index != 0; --index) {
        SIZE_T versionIndex = index - 1u;
        if (selectedVersions[versionIndex] != 0) {
            continue;
        }
        SetLastError(ERROR_SUCCESS);
        if (!history_version_record_size(
                &history->versions[versionIndex], &recordSize)) {
            *error = GetLastError() != ERROR_SUCCESS
                         ? GetLastError() : ERROR_INVALID_DATA;
            return FALSE;
        }
        if (recordSize > HISTORY_METADATA_LIMIT - payloadSize) {
            break;
        }
        candidatePayloadSize = payloadSize + recordSize;
        if (!history_embedded_output_size(
                rtfSize, candidatePayloadSize,
                &candidateOutputSize) ||
            candidateOutputSize > maximumOutputSize) {
            continue;
        }
        selectedVersions[versionIndex] = 1;
        ++selectedVersionCount;
        payloadSize = candidatePayloadSize;
    }

    if (selectedChatCount == 0 && selectedVersionCount == 0) {
        return history_copy_bare_rtf(
            rtf, rtfSize, output, outputSize, error);
    }
    if (!history_build_payload_selection(
            history, selectedChats, selectedChatCount,
            selectedVersions, selectedVersionCount, &payload)) {
        *error = GetLastError() != ERROR_SUCCESS
                     ? GetLastError() : ERROR_INVALID_DATA;
        return FALSE;
    }
    if (payload.size != payloadSize ||
        !history_embedded_output_size(rtfSize, payload.size, &total) ||
        total > maximumOutputSize ||
        FAILED(StringCchPrintfA(
            prefix, ARRAYSIZE(prefix),
            "{\\*\\wordcrafthistory\\bin%llu ",
            (unsigned long long)payload.size))) {
        byte_builder_free(&payload);
        *error = ERROR_INVALID_DATA;
        return FALSE;
    }
    prefixSize = strlen(prefix);
    result = HeapAlloc(GetProcessHeap(), 0, total);
    if (result == NULL) {
        byte_builder_free(&payload);
        *error = ERROR_NOT_ENOUGH_MEMORY;
        return FALSE;
    }
    CopyMemory(result, rtf, insertion);
    CopyMemory(result + insertion, prefix, prefixSize);
    CopyMemory(result + insertion + prefixSize,
               payload.data, payload.size);
    result[insertion + prefixSize + payload.size] = '}';
    CopyMemory(result + insertion + prefixSize + payload.size + 1,
               rtf + insertion, rtfSize - insertion);
    byte_builder_free(&payload);
    *output = result;
    *outputSize = total;
    return TRUE;
}

BOOL history_load_rtf_memory(AppState *app, const BYTE *data, SIZE_T size,
                             DWORD *error)
{
    HistoryContext parsed;
    BOOL found = FALSE;

    if (!history_valid(app) || error == NULL ||
        (data == NULL && size != 0)) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return FALSE;
    }
    *error = ERROR_SUCCESS;
    SetLastError(ERROR_SUCCESS);
    if (!history_parse_rtf(data, size, app, &parsed, &found)) {
        *error = GetLastError() != ERROR_SUCCESS
                     ? GetLastError() : ERROR_INVALID_DATA;
        return FALSE;
    }
    KillTimer(app->mainWindow, HISTORY_TIMER_ID);
    app->history->revisionPending = FALSE;
    if (found) {
        history_sort(&parsed);
        history_replace_entries(app->history, &parsed);
    } else {
        history_release_entries(app->history);
    }
    history_release_entries(&parsed);
    history_refresh_dialogs(app);
    return TRUE;
}

static BOOL history_append_chat_clone(HistoryContext *target,
                                      const ChatEntry *entry)
{
    if (target->chatCount >= HISTORY_MAX_CHATS) {
        history_sort(target);
        if (CompareFileTime(&entry->timestamp,
                            &target->chats[0].timestamp) <= 0) {
            return TRUE;
        }
        history_remove_oldest_chat(target);
    }
    if (!history_reserve_chats(target, target->chatCount + 1) ||
        !history_clone_chat(entry, &target->chats[target->chatCount])) {
        return FALSE;
    }
    ++target->chatCount;
    return TRUE;
}

static BOOL history_append_version_clone(HistoryContext *target,
                                         const VersionEntry *entry)
{
    SIZE_T entryBytes;
    DWORD validationError = ERROR_SUCCESS;

    if (!history_validate_version_entry(entry, &validationError) ||
        !history_version_storage_size(entry, &entryBytes)) {
        SetLastError(validationError != ERROR_SUCCESS
                         ? validationError : ERROR_INVALID_DATA);
        return FALSE;
    }
    if (entryBytes > HISTORY_MAX_SNAPSHOT_BYTES) {
        SetLastError(ERROR_FILE_TOO_LARGE);
        return FALSE;
    }
    if (target->versionCount >= HISTORY_MAX_VERSIONS) {
        history_sort(target);
        if (CompareFileTime(&entry->timestamp,
                            &target->versions[0].timestamp) <= 0) {
            return TRUE;
        }
        history_remove_oldest_version(target);
    }
    while (target->versionCount != 0 &&
           entryBytes >
               HISTORY_MAX_SNAPSHOT_BYTES - target->snapshotBytes) {
        history_sort(target);
        if (CompareFileTime(&entry->timestamp,
                            &target->versions[0].timestamp) <= 0) {
            return TRUE;
        }
        history_remove_oldest_version(target);
    }
    if (!history_reserve_versions(target, target->versionCount + 1) ||
        !history_clone_version(
            entry, &target->versions[target->versionCount])) {
        return FALSE;
    }
    target->snapshotBytes += entryBytes;
    ++target->versionCount;
    return TRUE;
}

BOOL history_merge_rtf_memory(AppState *app, const BYTE *data, SIZE_T size,
                              DWORD *error)
{
    HistoryContext parsed;
    HistoryContext combined;
    BOOL found = FALSE;
    SIZE_T index;

    if (!history_valid(app) || error == NULL ||
        (data == NULL && size != 0)) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return FALSE;
    }
    *error = ERROR_SUCCESS;
    SetLastError(ERROR_SUCCESS);
    if (!history_parse_rtf(data, size, app, &parsed, &found)) {
        *error = GetLastError() != ERROR_SUCCESS
                     ? GetLastError() : ERROR_INVALID_DATA;
        return FALSE;
    }
    if (!found) {
        history_release_entries(&parsed);
        return TRUE;
    }
    if (!history_clone_store(app->history, &combined)) {
        *error = GetLastError() != ERROR_SUCCESS
                     ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY;
        history_release_entries(&parsed);
        return FALSE;
    }
    for (index = 0; index < parsed.chatCount; ++index) {
        if (!history_contains_chat_id(&combined, &parsed.chats[index].id) &&
            !history_append_chat_clone(&combined, &parsed.chats[index])) {
            goto merge_failed;
        }
    }
    for (index = 0; index < parsed.versionCount; ++index) {
        if (!history_contains_version_id(
                &combined, &parsed.versions[index].id) &&
            !history_append_version_clone(
                &combined, &parsed.versions[index])) {
            goto merge_failed;
        }
    }
    history_sort(&combined);
    history_replace_entries(app->history, &combined);
    history_release_entries(&combined);
    history_release_entries(&parsed);
    history_refresh_dialogs(app);
    return TRUE;

merge_failed:
    *error = GetLastError() != ERROR_SUCCESS
                 ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY;
    history_release_entries(&combined);
    history_release_entries(&parsed);
    return FALSE;
}

static BOOL history_reconcile_rtf_memory_internal(
    AppState *app, const BYTE *data, SIZE_T size,
    const HistoryChatToken *acknowledgedChats,
    SIZE_T acknowledgedChatCount, DWORD *error)
{
    HistoryContext parsed;
    HistoryContext combined;
    BOOL found = FALSE;
    SIZE_T index;

    if (!history_valid(app) || error == NULL ||
        (data == NULL && size != 0) ||
        (acknowledgedChats == NULL && acknowledgedChatCount != 0) ||
        acknowledgedChatCount > HISTORY_MAX_CHATS) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return FALSE;
    }
    *error = ERROR_SUCCESS;
    SetLastError(ERROR_SUCCESS);
    if (!history_parse_rtf(data, size, app, &parsed, &found)) {
        *error = GetLastError() != ERROR_SUCCESS
                     ? GetLastError() : ERROR_INVALID_DATA;
        return FALSE;
    }
    if (!found && acknowledgedChatCount == 0) {
        history_release_entries(&parsed);
        return TRUE;
    }

    /*
     * Canonical entries win ID collisions so the host's authenticated
     * author/timestamp replaces a client's provisional copy. Entries omitted
     * from a bounded wire snapshot remain available locally.
     */
    if (found) {
        if (!history_clone_store(&parsed, &combined)) {
            *error = GetLastError() != ERROR_SUCCESS
                         ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY;
            history_release_entries(&parsed);
            return FALSE;
        }
    } else {
        ZeroMemory(&combined, sizeof(combined));
        combined.app = app;
    }
    for (index = 0; index < app->history->chatCount; ++index) {
        if (!history_contains_chat_id(
                &combined, &app->history->chats[index].id) &&
            !history_chat_tokens_contain(
                acknowledgedChats, acknowledgedChatCount,
                &app->history->chats[index].id) &&
            !history_append_chat_clone(
                &combined, &app->history->chats[index])) {
            goto reconcile_failed;
        }
    }
    for (index = 0; index < app->history->versionCount; ++index) {
        if (!history_contains_version_id(
                &combined, &app->history->versions[index].id) &&
            !history_append_version_clone(
                &combined, &app->history->versions[index])) {
            goto reconcile_failed;
        }
    }
    history_sort(&combined);
    history_replace_entries(app->history, &combined);
    history_release_entries(&combined);
    history_release_entries(&parsed);
    history_refresh_dialogs(app);
    return TRUE;

reconcile_failed:
    *error = GetLastError() != ERROR_SUCCESS
                 ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY;
    history_release_entries(&combined);
    history_release_entries(&parsed);
    return FALSE;
}

BOOL history_reconcile_rtf_memory(AppState *app, const BYTE *data,
                                  SIZE_T size, DWORD *error)
{
    return history_reconcile_rtf_memory_internal(
        app, data, size, NULL, 0, error);
}

BOOL history_reconcile_chat_ack_rtf_memory(
    AppState *app, const BYTE *data, SIZE_T size,
    const HistoryChatToken *acknowledgedChats,
    SIZE_T acknowledgedChatCount, DWORD *error)
{
    return history_reconcile_rtf_memory_internal(
        app, data, size, acknowledgedChats,
        acknowledgedChatCount, error);
}

BOOL history_merge_chat_rtf_memory(
    AppState *app, const BYTE *data, SIZE_T size,
    const WCHAR *authenticatedAuthor, BOOL includeKnownChats,
    HistoryChatToken *acceptedChats, SIZE_T acceptedChatCapacity,
    SIZE_T *acceptedChatCount, DWORD *error)
{
    HistoryContext parsed;
    HistoryContext combined;
    BYTE unseenChats[HISTORY_MAX_CHATS];
    BOOL found = FALSE;
    SIZE_T index;
    SIZE_T unseenCount = 0;
    SIZE_T importedCount = 0;
    DWORD validationError = ERROR_SUCCESS;
    FILETIME hostTimestamp;
    ULONGLONG hostTimestampValue;
    WCHAR trustedAuthor[HISTORY_AUTHOR_CAPACITY + 1];

    if (acceptedChatCount != NULL) {
        *acceptedChatCount = 0;
    }
    if (!history_valid(app) || data == NULL || size == 0 ||
        error == NULL || acceptedChatCount == NULL ||
        (acceptedChats == NULL && acceptedChatCapacity != 0) ||
        !history_author_is_valid(authenticatedAuthor, NULL)) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return FALSE;
    }
    *error = ERROR_SUCCESS;
    if (!document_validate_live_snapshot(data, size, &validationError)) {
        *error = validationError != ERROR_SUCCESS
                     ? validationError : ERROR_INVALID_DATA;
        return FALSE;
    }
    SetLastError(ERROR_SUCCESS);
    if (!history_parse_rtf(data, size, app, &parsed, &found)) {
        *error = GetLastError() != ERROR_SUCCESS
                     ? GetLastError() : ERROR_INVALID_DATA;
        return FALSE;
    }
    if (!found || parsed.chatCount == 0) {
        history_release_entries(&parsed);
        *error = ERROR_INVALID_DATA;
        return FALSE;
    }
    if (includeKnownChats &&
        parsed.chatCount > HISTORY_MAX_REMOTE_CHAT_IMPORT) {
        history_release_entries(&parsed);
        *error = ERROR_TOO_MANY_NAMES;
        return FALSE;
    }
    ZeroMemory(unseenChats, sizeof(unseenChats));
    for (index = 0; index < parsed.chatCount; ++index) {
        if (!history_contains_chat_id(
                app->history, &parsed.chats[index].id)) {
            unseenChats[index] = 1;
            ++unseenCount;
            if (unseenCount > HISTORY_MAX_REMOTE_CHAT_IMPORT) {
                history_release_entries(&parsed);
                *error = ERROR_TOO_MANY_NAMES;
                return FALSE;
            }
        }
    }
    if (!history_clone_store(app->history, &combined)) {
        *error = GetLastError() != ERROR_SUCCESS
                     ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY;
        history_release_entries(&parsed);
        return FALSE;
    }
    history_sort(&combined);
    while (combined.chatCount >
           HISTORY_MAX_CHATS - unseenCount) {
        history_remove_oldest_chat(&combined);
    }
    StringCchCopyW(trustedAuthor, ARRAYSIZE(trustedAuthor),
                   authenticatedAuthor);
    GetSystemTimeAsFileTime(&hostTimestamp);
    hostTimestampValue = history_filetime_value(&hostTimestamp);
    if (combined.chatCount != 0) {
        ULONGLONG latestTimestamp = history_filetime_value(
            &combined.chats[combined.chatCount - 1].timestamp);
        if (hostTimestampValue <= latestTimestamp &&
            latestTimestamp != UINT64_MAX) {
            hostTimestampValue = latestTimestamp + 1u;
        }
    }
    for (index = 0; index < parsed.chatCount; ++index) {
        ChatEntry authenticatedEntry;

        if (history_contains_chat_id(app->history,
                                     &parsed.chats[index].id)) {
            continue;
        }
        authenticatedEntry = parsed.chats[index];
        authenticatedEntry.timestamp = history_filetime_from_value(
            hostTimestampValue <= UINT64_MAX - importedCount
                ? hostTimestampValue + importedCount
                : hostTimestampValue);
        ++importedCount;
        StringCchCopyW(authenticatedEntry.author,
                       ARRAYSIZE(authenticatedEntry.author),
                       trustedAuthor);
        if (!history_reserve_chats(
                &combined, combined.chatCount + 1) ||
            !history_clone_chat(
                &authenticatedEntry,
                &combined.chats[combined.chatCount])) {
            *error = GetLastError() != ERROR_SUCCESS
                         ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY;
            history_release_entries(&combined);
            history_release_entries(&parsed);
            return FALSE;
        }
        ++combined.chatCount;
    }
    history_sort(&combined);
    for (index = 0; index < parsed.chatCount; ++index) {
        if ((includeKnownChats || unseenChats[index] != 0) &&
            history_contains_chat_id(
                &combined, &parsed.chats[index].id)) {
            if (*acceptedChatCount >= acceptedChatCapacity) {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                goto merge_failed;
            }
            CopyMemory(
                acceptedChats[*acceptedChatCount].bytes,
                parsed.chats[index].id.bytes,
                sizeof(acceptedChats[*acceptedChatCount].bytes));
            ++*acceptedChatCount;
        }
    }
    history_replace_entries(app->history, &combined);
    history_release_entries(&combined);
    history_release_entries(&parsed);
    history_refresh_dialogs(app);
    return TRUE;

merge_failed:
    *acceptedChatCount = 0;
    *error = GetLastError() != ERROR_SUCCESS
                 ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY;
    history_release_entries(&combined);
    history_release_entries(&parsed);
    return FALSE;
}

BOOL history_load_rtf_file(AppState *app, const WCHAR *path, DWORD *error)
{
    HANDLE file;
    HANDLE mapping = NULL;
    LARGE_INTEGER fileSize;
    const BYTE *data = NULL;
    BOOL result;

    if (!history_valid(app) || path == NULL || error == NULL) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return FALSE;
    }
    *error = ERROR_SUCCESS;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
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
        (ULONGLONG)fileSize.QuadPart > SIZE_MAX) {
        *error = ERROR_FILE_TOO_LARGE;
        CloseHandle(file);
        return FALSE;
    }
    if (fileSize.QuadPart != 0) {
        mapping = CreateFileMappingW(file, NULL, PAGE_READONLY, 0, 0, NULL);
        if (mapping == NULL) {
            *error = GetLastError();
            CloseHandle(file);
            return FALSE;
        }
        data = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0,
                             (SIZE_T)fileSize.QuadPart);
        if (data == NULL) {
            *error = GetLastError();
            CloseHandle(mapping);
            CloseHandle(file);
            return FALSE;
        }
    }
    result = history_load_rtf_memory(
        app, data, (SIZE_T)fileSize.QuadPart, error);
    if (data != NULL) {
        UnmapViewOfFile(data);
    }
    if (mapping != NULL) {
        CloseHandle(mapping);
    }
    CloseHandle(file);
    return result;
}

BOOL history_initialize(AppState *app)
{
    HistoryContext *history;

    if (app == NULL || app->mainWindow == NULL || app->editor == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (app->history != NULL) {
        return history_valid(app);
    }
    history = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                        sizeof(*history));
    if (history == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    history->app = app;
    app->history = history;
    return TRUE;
}

void history_clear(AppState *app)
{
    if (!history_valid(app)) {
        return;
    }
    KillTimer(app->mainWindow, HISTORY_TIMER_ID);
    app->history->revisionPending = FALSE;
    history_release_entries(app->history);
    history_refresh_dialogs(app);
}

static BOOL history_same_layout(const VersionEntry *entry,
                                const AppState *app)
{
    return entry->paperSizeId == app->paperSizeId &&
           entry->pageWidth == app->pageSize.x &&
           entry->pageHeight == app->pageSize.y &&
           EqualRect(&entry->margins, &app->pageMargins);
}

static void history_normalize_author(const WCHAR *requested,
                                     WCHAR author[HISTORY_AUTHOR_CAPACITY + 1])
{
    WCHAR localAuthor[HISTORY_AUTHOR_CAPACITY + 1];

    if (history_author_is_valid(requested, NULL)) {
        StringCchCopyW(author, HISTORY_AUTHOR_CAPACITY + 1, requested);
        return;
    }
    history_get_local_author(localAuthor, ARRAYSIZE(localAuthor));
    history_copy_author_or_fallback(
        localAuthor, HISTORY_DEFAULT_AUTHOR, author);
}

BOOL history_record_revision(AppState *app, const WCHAR *authorRequested,
                             BOOL retagMatching)
{
    HistoryContext *history;
    VersionEntry *latest;
    VersionEntry *entry;
    BYTE *rtf = NULL;
    SIZE_T rtfSize = 0;
    WCHAR *plainText = NULL;
    SIZE_T plainLength = 0;
    DWORD error = ERROR_SUCCESS;
    WCHAR author[HISTORY_AUTHOR_CAPACITY + 1];
    ULONGLONG hash;
    SIZE_T prefix = 0;
    SIZE_T suffix = 0;
    SIZE_T previousLength = 0;
    SIZE_T entryBytes = 0;
    BOOL hadLatest;
    ULONGLONG latestTimestampFloor = 0u;

    if (!history_valid(app) || app->editor == NULL ||
        (authorRequested != NULL &&
         !history_author_is_valid(authorRequested, NULL))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    history = app->history;
    history_normalize_author(authorRequested, author);
    if (!document_capture_revision_snapshot(app, &rtf, &rtfSize, &error)) {
        SetLastError(error != ERROR_SUCCESS ? error
                                            : ERROR_CAN_NOT_COMPLETE);
        return FALSE;
    }
    if (rtfSize > HISTORY_MAX_SNAPSHOT_BYTES) {
        HeapFree(GetProcessHeap(), 0, rtf);
        history->revisionPending = FALSE;
        KillTimer(app->mainWindow, HISTORY_TIMER_ID);
        app_set_status_message(
            app, L"Version history skipped a document and text snapshot larger than 12 MiB");
        SetLastError(ERROR_SUCCESS);
        return TRUE;
    }
    if (!document_validate_history_snapshot(rtf, rtfSize, &error)) {
        HeapFree(GetProcessHeap(), 0, rtf);
        history->revisionPending = FALSE;
        KillTimer(app->mainWindow, HISTORY_TIMER_ID);
        app_set_status_message(
            app,
            L"Version history skipped content that is unsafe to preview");
        SetLastError(ERROR_SUCCESS);
        return TRUE;
    }
    if (!editor_get_all_text(app->editor, FALSE, &plainText,
                             &plainLength, &error)) {
        HeapFree(GetProcessHeap(), 0, rtf);
        SetLastError(error != ERROR_SUCCESS ? error
                                            : ERROR_CAN_NOT_COMPLETE);
        return FALSE;
    }
    if (plainLength > HISTORY_TEXT_SNAPSHOT_LIMIT / sizeof(WCHAR)) {
        HeapFree(GetProcessHeap(), 0, rtf);
        HeapFree(GetProcessHeap(), 0, plainText);
        history->revisionPending = FALSE;
        KillTimer(app->mainWindow, HISTORY_TIMER_ID);
        app_set_status_message(
            app, L"Version history skipped a text snapshot that exceeded its limit");
        SetLastError(ERROR_SUCCESS);
        return TRUE;
    }
    if (!history_version_storage_size_values(
            rtfSize, plainLength, &entryBytes) ||
        entryBytes > HISTORY_MAX_SNAPSHOT_BYTES) {
        HeapFree(GetProcessHeap(), 0, rtf);
        HeapFree(GetProcessHeap(), 0, plainText);
        history->revisionPending = FALSE;
        KillTimer(app->mainWindow, HISTORY_TIMER_ID);
        app_set_status_message(
            app, L"Version history skipped a document and text snapshot larger than 12 MiB");
        SetLastError(ERROR_SUCCESS);
        return TRUE;
    }
    hash = history_content_hash(rtf, rtfSize, app->paperSizeId,
                                app->pageSize.x, app->pageSize.y,
                                &app->pageMargins);
    latest = history->versionCount != 0
                 ? &history->versions[history->versionCount - 1]
                 : NULL;
    hadLatest = latest != NULL;
    if (latest != NULL) {
        previousLength = latest->plainLength;
        latestTimestampFloor =
            history_filetime_value(&latest->timestamp);
    }
    if (latest != NULL && latest->contentHash == hash &&
        latest->rtfSize == rtfSize &&
        history_same_layout(latest, app) &&
        memcmp(latest->rtf, rtf, rtfSize) == 0) {
        if (retagMatching &&
            lstrcmpW(latest->author, author) != 0) {
            StringCchCopyW(latest->author, ARRAYSIZE(latest->author),
                           author);
        }
        HeapFree(GetProcessHeap(), 0, rtf);
        HeapFree(GetProcessHeap(), 0, plainText);
        history->revisionPending = FALSE;
        KillTimer(app->mainWindow, HISTORY_TIMER_ID);
        history_refresh_dialogs(app);
        return TRUE;
    }
    if (latest != NULL) {
        SIZE_T shared = min(latest->plainLength, plainLength);
        while (prefix < shared &&
               latest->plainText[prefix] == plainText[prefix]) {
            ++prefix;
        }
        while (suffix < latest->plainLength - prefix &&
               suffix < plainLength - prefix &&
               latest->plainText[latest->plainLength - suffix - 1] ==
                   plainText[plainLength - suffix - 1]) {
            ++suffix;
        }
    }
    while (history->versionCount >= HISTORY_MAX_VERSIONS ||
           (history->versionCount != 0 &&
            (history->snapshotBytes > HISTORY_MAX_SNAPSHOT_BYTES ||
             entryBytes > HISTORY_MAX_SNAPSHOT_BYTES -
                              history->snapshotBytes))) {
        history_remove_oldest_version(history);
    }
    if (history->versionCount == 0) {
        history->snapshotBytes = 0;
    }
    if (!history_reserve_versions(history, history->versionCount + 1)) {
        HeapFree(GetProcessHeap(), 0, rtf);
        HeapFree(GetProcessHeap(), 0, plainText);
        return FALSE;
    }
    entry = &history->versions[history->versionCount];
    ZeroMemory(entry, sizeof(*entry));
    history_create_id(&entry->id);
    GetSystemTimeAsFileTime(&entry->timestamp);
    if (history_filetime_value(&entry->timestamp) <=
            latestTimestampFloor &&
        latestTimestampFloor != UINT64_MAX) {
        entry->timestamp = history_filetime_from_value(
            latestTimestampFloor + 1u);
    }
    StringCchCopyW(entry->author, ARRAYSIZE(entry->author), author);
    entry->rtf = rtf;
    entry->rtfSize = rtfSize;
    entry->plainText = plainText;
    entry->plainLength = plainLength;
    entry->changeStart = hadLatest ? (LONG)prefix : 0;
    entry->insertedCount =
        hadLatest ? plainLength - prefix - suffix : plainLength;
    entry->deletedCount =
        hadLatest
            ? previousLength - prefix - suffix
            : 0;
    entry->changeEnd =
        entry->changeStart + (LONG)entry->insertedCount;
    entry->contentHash = hash;
    entry->paperSizeId = app->paperSizeId;
    entry->pageWidth = app->pageSize.x;
    entry->pageHeight = app->pageSize.y;
    entry->margins = app->pageMargins;
    if (!history_validate_version_entry(entry, &error)) {
        history_version_release(entry);
        SetLastError(error != ERROR_SUCCESS ? error
                                            : ERROR_INVALID_DATA);
        return FALSE;
    }
    ++history->versionCount;
    history->snapshotBytes += entryBytes;
    history->revisionPending = FALSE;
    KillTimer(app->mainWindow, HISTORY_TIMER_ID);
    history_refresh_dialogs(app);
    app_update_command_ui(app);
    return TRUE;
}

void history_seed_if_empty(AppState *app)
{
    if (history_valid(app) && app->history->versionCount == 0) {
        (void)history_record_revision(app, NULL, FALSE);
    }
}

void history_note_document_changed(AppState *app)
{
    if (!history_valid(app) || app->loading) {
        return;
    }
    app->history->revisionPending = TRUE;
    SetTimer(app->mainWindow, HISTORY_TIMER_ID,
             HISTORY_DEBOUNCE_MS, NULL);
}

void history_handle_timer(AppState *app, UINT_PTR timerId)
{
    if (!history_valid(app) || timerId != HISTORY_TIMER_ID) {
        return;
    }
    KillTimer(app->mainWindow, HISTORY_TIMER_ID);
    if (app->history->revisionPending &&
        live_share_query_state(app, WCQ_LIVE_ROLE) ==
            LIVE_ROLE_CLIENT &&
        live_share_query_state(app, WCQ_LIVE_STATE) ==
            LIVE_STATE_CONNECTED) {
        /*
         * Client edits become canonical only after the host accepts them.
         * Keep the local debounce pending instead of creating a speculative
         * author/version ID that a bounded canonical frame cannot retract.
         */
        SetTimer(app->mainWindow, HISTORY_TIMER_ID,
                 HISTORY_DEBOUNCE_MS, NULL);
        return;
    }
    if (app->history->revisionPending &&
        !history_record_revision(app, NULL, FALSE)) {
        app->history->revisionPending = TRUE;
        SetTimer(app->mainWindow, HISTORY_TIMER_ID,
                 HISTORY_DEBOUNCE_MS, NULL);
    }
}

BOOL history_flush_pending(AppState *app)
{
    if (!history_valid(app)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!app->history->revisionPending) {
        return TRUE;
    }
    if (live_share_query_state(app, WCQ_LIVE_ROLE) ==
            LIVE_ROLE_CLIENT &&
        live_share_query_state(app, WCQ_LIVE_STATE) ==
            LIVE_STATE_CONNECTED) {
        /*
         * Saving or opening the history viewer must not create a speculative
         * client-authored checkpoint while the host's canonical revision is
         * still pending.
         */
        SetTimer(app->mainWindow, HISTORY_TIMER_ID,
                 HISTORY_DEBOUNCE_MS, NULL);
        return TRUE;
    }
    return history_record_revision(app, NULL, FALSE);
}

void history_cancel_pending_revision(AppState *app)
{
    if (!history_valid(app)) {
        return;
    }
    KillTimer(app->mainWindow, HISTORY_TIMER_ID);
    app->history->revisionPending = FALSE;
}

BOOL history_post_chat(AppState *app, const WCHAR *text)
{
    HistoryContext *history;
    ChatEntry *entry;
    ChatEntry pending;
    SIZE_T length;
    SIZE_T start = 0;
    SIZE_T finish;
    WCHAR author[HISTORY_AUTHOR_CAPACITY + 1];
    HistoryChatToken chatToken;

    if (!history_valid(app) || text == NULL ||
        !history_wide_length(text, HISTORY_CHAT_TEXT_CAPACITY, &length)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    finish = length;
    while (start < finish && iswspace(text[start])) {
        ++start;
    }
    while (finish > start && iswspace(text[finish - 1])) {
        --finish;
    }
    if (finish == start ||
        !history_valid_utf16(text + start, finish - start, TRUE)) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    history = app->history;
    ZeroMemory(&pending, sizeof(pending));
    history_create_id(&pending.id);
    CopyMemory(chatToken.bytes, pending.id.bytes,
               sizeof(chatToken.bytes));
    GetSystemTimeAsFileTime(&pending.timestamp);
    if (history->chatCount != 0) {
        ULONGLONG latestTimestamp = history_filetime_value(
            &history->chats[history->chatCount - 1].timestamp);
        ULONGLONG pendingTimestamp =
            history_filetime_value(&pending.timestamp);

        /*
         * Keep locally posted messages at the chronological tail even when
         * the peer's system clock is ahead. Live snapshots reserve that tail
         * so every unsent message remains in the bounded wire frame.
         */
        if (pendingTimestamp <= latestTimestamp &&
            latestTimestamp != UINT64_MAX) {
            pending.timestamp = history_filetime_from_value(
                latestTimestamp + 1u);
        }
    }
    history_get_local_author(author, ARRAYSIZE(author));
    StringCchCopyW(pending.author, ARRAYSIZE(pending.author), author);
    if (!history_copy_wide(text + start, finish - start,
                           &pending.text)) {
        return FALSE;
    }
    if (history->chatCount >= HISTORY_MAX_CHATS) {
        history_remove_oldest_chat(history);
    }
    if (!history_reserve_chats(history, history->chatCount + 1)) {
        history_chat_release(&pending);
        return FALSE;
    }
    entry = &history->chats[history->chatCount];
    *entry = pending;
    ++history->chatCount;
    history_sort(history);
    document_mark_metadata_modified(app);
    live_share_chat_changed(app, &chatToken);
    history_refresh_dialogs(app);
    app_update_command_ui(app);
    app_set_status_message(app, L"Chat message saved with the document");
    return TRUE;
}

SIZE_T history_chat_count(const AppState *app)
{
    return history_valid(app) ? app->history->chatCount : 0;
}

BOOL history_contains_chat_token(
    const AppState *app, const HistoryChatToken *token)
{
    SIZE_T index;

    if (!history_valid(app) || token == NULL) {
        return FALSE;
    }
    for (index = 0; index < app->history->chatCount; ++index) {
        if (history_chat_token_matches_id(
                token, &app->history->chats[index].id)) {
            return TRUE;
        }
    }
    return FALSE;
}

SIZE_T history_version_count(const AppState *app)
{
    return history_valid(app) ? app->history->versionCount : 0;
}

LRESULT history_query_state(const AppState *app, UINT query, LPARAM index)
{
    const HistoryContext *history;
    SIZE_T position = index >= 0 ? (SIZE_T)index : SIZE_MAX;

    if (!history_valid(app)) {
        return 0;
    }
    history = app->history;
    switch (query) {
    case WCQ_CHAT_COUNT:
        return (LRESULT)history->chatCount;
    case WCQ_CHAT_AUTHOR_HASH:
        return position < history->chatCount
                   ? (LRESULT)(DWORD_PTR)history_hash_text(
                         history->chats[position].author)
                   : 0;
    case WCQ_CHAT_TEXT_HASH:
        return position < history->chatCount
                   ? (LRESULT)(DWORD_PTR)history_hash_text(
                         history->chats[position].text)
                   : 0;
    case WCQ_VERSION_COUNT:
        return (LRESULT)history->versionCount;
    case WCQ_VERSION_AUTHOR_HASH:
        return position < history->versionCount
                   ? (LRESULT)(DWORD_PTR)history_hash_text(
                         history->versions[position].author)
                   : 0;
    case WCQ_VERSION_CHANGE_START:
        return position < history->versionCount
                   ? history->versions[position].changeStart : 0;
    case WCQ_VERSION_CHANGE_END:
        return position < history->versionCount
                   ? history->versions[position].changeEnd : 0;
    case WCQ_VERSION_INSERTED_COUNT:
        return position < history->versionCount
                   ? (LRESULT)min(history->versions[position].insertedCount,
                                  (SIZE_T)LONG_MAX)
                   : 0;
    case WCQ_VERSION_DELETED_COUNT:
        return position < history->versionCount
                   ? (LRESULT)min(history->versions[position].deletedCount,
                                  (SIZE_T)LONG_MAX)
                   : 0;
    case WCQ_CHAT_DIALOG_VISIBLE:
        return history->chatDialog != NULL &&
               IsWindow(history->chatDialog);
    case WCQ_VERSION_DIALOG_VISIBLE:
        return history->versionDialog != NULL &&
               IsWindow(history->versionDialog);
    default:
        return 0;
    }
}

static void wide_builder_free(WideBuilder *builder)
{
    if (builder != NULL && builder->data != NULL) {
        HeapFree(GetProcessHeap(), 0, builder->data);
    }
    if (builder != NULL) {
        ZeroMemory(builder, sizeof(*builder));
    }
}

static BOOL wide_builder_reserve(WideBuilder *builder, SIZE_T additional)
{
    SIZE_T needed;
    SIZE_T capacity;
    WCHAR *data;

    if (builder == NULL ||
        additional > SIZE_MAX - builder->length - 1) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    needed = builder->length + additional + 1;
    if (needed <= builder->capacity) {
        return TRUE;
    }
    capacity = builder->capacity == 0 ? 1024 : builder->capacity;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof(WCHAR)) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    data = builder->data == NULL
               ? HeapAlloc(GetProcessHeap(), 0,
                           capacity * sizeof(WCHAR))
               : HeapReAlloc(GetProcessHeap(), 0, builder->data,
                             capacity * sizeof(WCHAR));
    if (data == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    builder->data = data;
    builder->capacity = capacity;
    if (builder->length == 0) {
        builder->data[0] = L'\0';
    }
    return TRUE;
}

static BOOL wide_builder_append_n(WideBuilder *builder, const WCHAR *text,
                                  SIZE_T length)
{
    if ((text == NULL && length != 0) ||
        !wide_builder_reserve(builder, length)) {
        return FALSE;
    }
    if (length != 0) {
        CopyMemory(builder->data + builder->length, text,
                   length * sizeof(WCHAR));
        builder->length += length;
    }
    builder->data[builder->length] = L'\0';
    return TRUE;
}

static BOOL wide_builder_append(WideBuilder *builder, const WCHAR *text)
{
    SIZE_T length;
    return history_wide_length(text, SIZE_MAX / sizeof(WCHAR) - 1,
                               &length) &&
           wide_builder_append_n(builder, text, length);
}

static void history_format_timestamp(const FILETIME *time, WCHAR *output,
                                     SIZE_T capacity)
{
    FILETIME localTime;
    SYSTEMTIME systemTime;
    WCHAR date[64];
    WCHAR clock[64];

    if (output == NULL || capacity == 0) {
        return;
    }
    output[0] = L'\0';
    date[0] = L'\0';
    clock[0] = L'\0';
    if (FileTimeToLocalFileTime(time, &localTime) &&
        FileTimeToSystemTime(&localTime, &systemTime)) {
        (void)GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE,
                             &systemTime, NULL, date, ARRAYSIZE(date));
        (void)GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS,
                             &systemTime, NULL, clock,
                             ARRAYSIZE(clock));
    }
    if (date[0] != L'\0' && clock[0] != L'\0') {
        StringCchPrintfW(output, capacity, L"%s, %s", date, clock);
    } else {
        StringCchCopyW(output, capacity, L"Unknown time");
    }
}

static void history_refresh_chat_dialog(AppState *app)
{
    HistoryContext *history;
    WideBuilder transcript;
    SIZE_T index;
    WCHAR timestamp[144];
    WCHAR header[320];
    WCHAR status[192];
    const WCHAR *storageStatus;
    LRESULT role;

    if (!history_valid(app) || app->history->chatDialog == NULL ||
        !IsWindow(app->history->chatDialog)) {
        return;
    }
    history = app->history;
    ZeroMemory(&transcript, sizeof(transcript));
    if (history->chatCount == 0) {
        (void)wide_builder_append(
            &transcript,
            L"No messages yet.\r\n\r\nStart a conversation about this document.");
    }
    for (index = 0; index < history->chatCount; ++index) {
        history_format_timestamp(&history->chats[index].timestamp,
                                 timestamp, ARRAYSIZE(timestamp));
        if (FAILED(StringCchPrintfW(
                header, ARRAYSIZE(header), L"%s  •  %s\r\n",
                history->chats[index].author, timestamp)) ||
            !wide_builder_append(&transcript, header) ||
            !wide_builder_append(&transcript,
                                 history->chats[index].text) ||
            !wide_builder_append(&transcript, L"\r\n\r\n")) {
            wide_builder_free(&transcript);
            return;
        }
    }
    SetDlgItemTextW(history->chatDialog, IDC_CHAT_TRANSCRIPT,
                    transcript.data != NULL ? transcript.data : L"");
    SendDlgItemMessageW(history->chatDialog, IDC_CHAT_TRANSCRIPT,
                        EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendDlgItemMessageW(history->chatDialog, IDC_CHAT_TRANSCRIPT,
                        EM_SCROLLCARET, 0, 0);
    wide_builder_free(&transcript);

    storageStatus =
        app->currentPath[0] != L'\0' && app->currentIsRtf &&
                !app->modified
            ? L"Saved in this document"
            : L"Save as RTF to keep after closing";
    role = live_share_query_state(app, WCQ_LIVE_ROLE);
    if (role == LIVE_ROLE_HOST) {
        StringCchPrintfW(status, ARRAYSIZE(status),
                         L"%llu message%s • %s • Hosting live",
                         (unsigned long long)history->chatCount,
                         history->chatCount == 1 ? L"" : L"s",
                         storageStatus);
    } else if (role == LIVE_ROLE_CLIENT) {
        StringCchPrintfW(status, ARRAYSIZE(status),
                         L"%llu message%s • %s • Connected live",
                         (unsigned long long)history->chatCount,
                         history->chatCount == 1 ? L"" : L"s",
                         storageStatus);
    } else {
        StringCchPrintfW(status, ARRAYSIZE(status),
                         L"%llu message%s • %s • Offline",
                         (unsigned long long)history->chatCount,
                         history->chatCount == 1 ? L"" : L"s",
                         storageStatus);
    }
    SetDlgItemTextW(history->chatDialog, IDC_CHAT_STATUS, status);
}

static void history_layout_chat_dialog(HWND dialog)
{
    RECT client;
    int margin = app_scale(dialog, 12);
    int gap = app_scale(dialog, 8);
    int statusHeight = app_scale(dialog, 22);
    int labelHeight = app_scale(dialog, 20);
    int composerHeight = app_scale(dialog, 58);
    int buttonWidth = app_scale(dialog, 82);
    int buttonHeight = app_scale(dialog, 27);
    int footerHeight = app_scale(dialog, 20);
    int width;
    int transcriptBottom;
    int composerTop;

    GetClientRect(dialog, &client);
    width = max(0, client.right - client.left - margin * 2);
    MoveWindow(GetDlgItem(dialog, IDC_CHAT_STATUS), margin, margin,
               width, statusHeight, TRUE);
    composerTop = max(margin + statusHeight + gap,
                      client.bottom - margin - footerHeight - gap -
                          labelHeight - composerHeight);
    transcriptBottom = composerTop - gap;
    MoveWindow(GetDlgItem(dialog, IDC_CHAT_TRANSCRIPT), margin,
               margin + statusHeight,
               width,
               max(0, transcriptBottom - (margin + statusHeight)), TRUE);
    MoveWindow(GetDlgItem(dialog, IDC_CHAT_MESSAGE_LABEL), margin,
               composerTop, max(0, width - buttonWidth - gap),
               labelHeight, TRUE);
    MoveWindow(GetDlgItem(dialog, IDC_CHAT_MESSAGE), margin,
               composerTop + labelHeight,
               max(0, width - buttonWidth - gap), composerHeight, TRUE);
    MoveWindow(GetDlgItem(dialog, IDC_CHAT_SEND),
               client.right - margin - buttonWidth,
               composerTop + labelHeight,
               buttonWidth, buttonHeight, TRUE);
    MoveWindow(GetDlgItem(dialog, IDCANCEL),
               client.right - margin - buttonWidth,
               composerTop + labelHeight + buttonHeight + gap / 2,
               buttonWidth, buttonHeight, TRUE);
    MoveWindow(GetDlgItem(dialog, IDC_CHAT_FOOTER), margin,
               max(margin, client.bottom - margin - footerHeight),
               width, footerHeight, TRUE);
}

static INT_PTR CALLBACK history_chat_dialog_proc(HWND dialog, UINT message,
                                                 WPARAM wParam,
                                                 LPARAM lParam)
{
    AppState *app =
        (AppState *)GetWindowLongPtrW(dialog, DWLP_USER);

    switch (message) {
    case WM_GETMINMAXINFO: {
        MINMAXINFO *limits = (MINMAXINFO *)lParam;
        limits->ptMinTrackSize.x = app_scale(dialog, 520);
        limits->ptMinTrackSize.y = app_scale(dialog, 420);
        return TRUE;
    }
    case WM_INITDIALOG:
        app = (AppState *)lParam;
        SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)app);
        if (!history_valid(app)) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        app->history->chatDialog = dialog;
        SendDlgItemMessageW(dialog, IDC_CHAT_MESSAGE, EM_SETLIMITTEXT,
                            HISTORY_CHAT_TEXT_CAPACITY, 0);
        SendDlgItemMessageW(dialog, IDC_CHAT_TRANSCRIPT, EM_SETREADONLY,
                            TRUE, 0);
        history_layout_chat_dialog(dialog);
        history_refresh_chat_dialog(app);
        SetFocus(GetDlgItem(dialog, IDC_CHAT_MESSAGE));
        return FALSE;
    case WM_SIZE:
        history_layout_chat_dialog(dialog);
        return TRUE;
    case WM_COMMAND:
        if (!history_valid(app)) {
            break;
        }
        switch (LOWORD(wParam)) {
        case IDC_CHAT_SEND: {
            WCHAR messageText[HISTORY_CHAT_TEXT_CAPACITY + 1];
            if (GetDlgItemTextW(dialog, IDC_CHAT_MESSAGE, messageText,
                                ARRAYSIZE(messageText)) <= 0 ||
                !history_post_chat(app, messageText)) {
                MessageBeep(MB_ICONWARNING);
            } else {
                SetDlgItemTextW(dialog, IDC_CHAT_MESSAGE, L"");
                SetFocus(GetDlgItem(dialog, IDC_CHAT_MESSAGE));
            }
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        default:
            break;
        }
        break;
    case WM_DESTROY:
        if (history_valid(app) &&
            app->history->chatDialog == dialog) {
            app->history->chatDialog = NULL;
        }
        break;
    default:
        break;
    }
    return FALSE;
}

static DWORD CALLBACK history_stream_read(DWORD_PTR cookie, LPBYTE buffer,
                                          LONG requested, LONG *transferred)
{
    MemoryReadContext *context = (MemoryReadContext *)cookie;
    SIZE_T available;
    SIZE_T amount;

    if (context == NULL || buffer == NULL || transferred == NULL ||
        requested < 0 || context->position > context->size) {
        return ERROR_INVALID_PARAMETER;
    }
    available = context->size - context->position;
    amount = min(available, (SIZE_T)requested);
    if (amount != 0) {
        CopyMemory(buffer, context->data + context->position, amount);
        context->position += amount;
    }
    *transferred = (LONG)amount;
    return ERROR_SUCCESS;
}

static COLORREF history_author_color(const WCHAR *author)
{
    static const COLORREF colors[] = {
        RGB(255, 213, 128), RGB(174, 221, 255), RGB(190, 232, 191),
        RGB(226, 197, 255), RGB(255, 190, 205), RGB(255, 235, 130)
    };
    return colors[history_hash_text(author) % ARRAYSIZE(colors)];
}

static BOOL history_restore_is_allowed(const AppState *app)
{
    return history_valid(app) &&
           live_share_query_state(app, WCQ_LIVE_ROLE) !=
               LIVE_ROLE_CLIENT;
}

static void history_preview_version(AppState *app, SIZE_T index)
{
    static const WCHAR deletionMarker[] = L"[deleted here]";
    HistoryContext *history;
    VersionEntry *entry;
    HWND preview;
    EDITSTREAM stream;
    MemoryReadContext input;
    CHARFORMAT2W format;
    WCHAR timestamp[144];
    WCHAR details[512];
    CHARRANGE changed;
    LONG focusPosition = 0;
    DWORD validationError = ERROR_SUCCESS;

    if (!history_valid(app) || index >= app->history->versionCount ||
        app->history->versionDialog == NULL) {
        return;
    }
    history = app->history;
    entry = &history->versions[index];
    preview = GetDlgItem(history->versionDialog, IDC_VERSION_PREVIEW);
    if (preview == NULL) {
        return;
    }
    if (!history_validate_version_entry(entry, &validationError)) {
        SetWindowTextW(preview, L"");
        SetDlgItemTextW(
            history->versionDialog, IDC_VERSION_DETAILS,
            L"This saved version is invalid and cannot be previewed.");
        EnableWindow(GetDlgItem(history->versionDialog,
                                IDC_VERSION_RESTORE),
                     FALSE);
        return;
    }
    EnableWindow(GetDlgItem(history->versionDialog,
                            IDC_VERSION_RESTORE),
                 history_restore_is_allowed(app));
    ZeroMemory(&input, sizeof(input));
    input.data = entry->rtf;
    input.size = entry->rtfSize;
    ZeroMemory(&stream, sizeof(stream));
    stream.dwCookie = (DWORD_PTR)&input;
    stream.pfnCallback = history_stream_read;
    if (!render_editor_begin_static_picture_stream(preview)) {
        SetDlgItemTextW(
            history->versionDialog, IDC_VERSION_DETAILS,
            L"This saved version could not initialize its safe preview.");
        EnableWindow(GetDlgItem(history->versionDialog,
                                IDC_VERSION_RESTORE),
                     FALSE);
        return;
    }
    SendMessageW(preview, WM_SETREDRAW, FALSE, 0);
    SendMessageW(preview, EM_SETREADONLY, FALSE, 0);
    SendMessageW(preview, EM_STREAMIN, SF_RTF, (LPARAM)&stream);
    {
        DWORD pictureError = ERROR_SUCCESS;
        if (!render_editor_end_static_picture_stream(
                preview, &pictureError)) {
            stream.dwError = pictureError != ERROR_SUCCESS
                                 ? pictureError : ERROR_CAN_NOT_COMPLETE;
        }
    }
    if (stream.dwError != 0u) {
        SetWindowTextW(preview, L"");
        SendMessageW(preview, EM_SETREADONLY, TRUE, 0);
        SendMessageW(preview, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(preview, NULL, TRUE);
        SetDlgItemTextW(
            history->versionDialog, IDC_VERSION_DETAILS,
            L"This saved version could not be loaded into the preview.");
        EnableWindow(GetDlgItem(history->versionDialog,
                                IDC_VERSION_RESTORE),
                     FALSE);
        return;
    }
    SendMessageW(preview, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));
    if (entry->insertedCount != 0) {
        focusPosition = entry->changeStart;
        changed.cpMin = entry->changeStart;
        changed.cpMax = entry->changeEnd;
        SendMessageW(preview, EM_EXSETSEL, 0, (LPARAM)&changed);
        ZeroMemory(&format, sizeof(format));
        format.cbSize = sizeof(format);
        format.dwMask = CFM_BACKCOLOR;
        format.crBackColor = history_author_color(entry->author);
        SendMessageW(preview, EM_SETCHARFORMAT, SCF_SELECTION,
                     (LPARAM)&format);
    } else if (entry->deletedCount != 0) {
        focusPosition = entry->changeStart;
        changed.cpMin = entry->changeStart;
        changed.cpMax = entry->changeStart;
        SendMessageW(preview, EM_EXSETSEL, 0, (LPARAM)&changed);
        SendMessageW(preview, EM_REPLACESEL, FALSE,
                     (LPARAM)deletionMarker);
        changed.cpMax =
            changed.cpMin + (LONG)(ARRAYSIZE(deletionMarker) - 1u);
        SendMessageW(preview, EM_EXSETSEL, 0, (LPARAM)&changed);
        ZeroMemory(&format, sizeof(format));
        format.cbSize = sizeof(format);
        format.dwMask = CFM_BACKCOLOR | CFM_COLOR | CFM_ITALIC;
        format.dwEffects = CFE_ITALIC;
        format.crBackColor = history_author_color(entry->author);
        format.crTextColor = RGB(80, 55, 20);
        SendMessageW(preview, EM_SETCHARFORMAT, SCF_SELECTION,
                     (LPARAM)&format);
    }
    changed.cpMin = changed.cpMax = focusPosition;
    SendMessageW(preview, EM_EXSETSEL, 0, (LPARAM)&changed);
    SendMessageW(preview, EM_SCROLLCARET, 0, 0);
    SendMessageW(preview, EM_SETREADONLY, TRUE, 0);
    SendMessageW(preview, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(preview, NULL, TRUE);

    history_format_timestamp(&entry->timestamp, timestamp,
                             ARRAYSIZE(timestamp));
    if (entry->insertedCount == 0 && entry->deletedCount == 0) {
        StringCchPrintfW(
            details, ARRAYSIZE(details),
            L"Edited by %s • %s • Formatting changed",
            entry->author, timestamp);
    } else if (entry->insertedCount == 0) {
        StringCchPrintfW(
            details, ARRAYSIZE(details),
            L"Edited by %s • %s • -%llu characters • deletion point marked",
            entry->author, timestamp,
            (unsigned long long)entry->deletedCount);
    } else {
        StringCchPrintfW(
            details, ARRAYSIZE(details),
            L"Edited by %s • %s • +%llu / -%llu characters",
            entry->author, timestamp,
            (unsigned long long)entry->insertedCount,
            (unsigned long long)entry->deletedCount);
    }
    SetDlgItemTextW(history->versionDialog, IDC_VERSION_DETAILS,
                    details);
}

static BOOL history_selected_version(AppState *app, SIZE_T *index)
{
    HWND list;
    LRESULT selection;
    LRESULT itemData;

    if (!history_valid(app) || index == NULL ||
        app->history->versionDialog == NULL) {
        return FALSE;
    }
    list = GetDlgItem(app->history->versionDialog, IDC_VERSION_LIST);
    selection = SendMessageW(list, LB_GETCURSEL, 0, 0);
    if (selection == LB_ERR) {
        return FALSE;
    }
    itemData = SendMessageW(list, LB_GETITEMDATA, (WPARAM)selection, 0);
    if (itemData == LB_ERR || (SIZE_T)itemData >=
                                  app->history->versionCount) {
        return FALSE;
    }
    *index = (SIZE_T)itemData;
    return TRUE;
}

static void history_refresh_version_restore_state(AppState *app)
{
    SIZE_T index;
    DWORD validationError = ERROR_SUCCESS;
    BOOL enabled = FALSE;

    if (!history_valid(app) || app->history->versionDialog == NULL ||
        !IsWindow(app->history->versionDialog)) {
        return;
    }
    if (history_selected_version(app, &index) &&
        history_validate_version_entry(
            &app->history->versions[index], &validationError) &&
        history_restore_is_allowed(app)) {
        enabled = TRUE;
    }
    EnableWindow(GetDlgItem(app->history->versionDialog,
                            IDC_VERSION_RESTORE),
                 enabled);
}

static void history_refresh_version_dialog(AppState *app)
{
    HistoryContext *history;
    HWND list;
    HistoryId selectedId;
    BOOL preserveSelection = FALSE;
    SIZE_T selectedIndex;
    SIZE_T index;
    LRESULT selectedRow = LB_ERR;
    WCHAR timestamp[144];
    WCHAR rowText[512];

    if (!history_valid(app) || app->history->versionDialog == NULL ||
        !IsWindow(app->history->versionDialog)) {
        return;
    }
    history = app->history;
    list = GetDlgItem(history->versionDialog, IDC_VERSION_LIST);
    if (history_selected_version(app, &selectedIndex)) {
        selectedId = history->versions[selectedIndex].id;
        preserveSelection = TRUE;
    }
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (index = history->versionCount; index != 0; --index) {
        SIZE_T storageIndex = index - 1;
        VersionEntry *entry = &history->versions[storageIndex];
        LRESULT row;

        history_format_timestamp(&entry->timestamp, timestamp,
                                 ARRAYSIZE(timestamp));
        if (entry->insertedCount == 0 && entry->deletedCount == 0) {
            StringCchPrintfW(rowText, ARRAYSIZE(rowText),
                             L"%s — %s — formatting",
                             entry->author, timestamp);
        } else {
            StringCchPrintfW(
                rowText, ARRAYSIZE(rowText),
                L"%s — %s — +%llu / -%llu",
                entry->author, timestamp,
                (unsigned long long)entry->insertedCount,
                (unsigned long long)entry->deletedCount);
        }
        row = SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)rowText);
        if (row == LB_ERR || row == LB_ERRSPACE) {
            continue;
        }
        SendMessageW(list, LB_SETITEMDATA, (WPARAM)row,
                     (LPARAM)storageIndex);
        if (preserveSelection &&
            history_id_equal(&selectedId, &entry->id)) {
            selectedRow = row;
        }
    }
    if (selectedRow == LB_ERR && history->versionCount != 0) {
        selectedRow = 0;
    }
    if (selectedRow != LB_ERR) {
        SendMessageW(list, LB_SETCURSEL, (WPARAM)selectedRow, 0);
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, NULL, TRUE);

    history_refresh_version_restore_state(app);
    if (history_selected_version(app, &selectedIndex)) {
        history_preview_version(app, selectedIndex);
    } else {
        SetDlgItemTextW(history->versionDialog, IDC_VERSION_DETAILS,
                        L"No saved versions are available.");
        SetWindowTextW(GetDlgItem(history->versionDialog,
                                 IDC_VERSION_PREVIEW),
                       L"");
    }
}

static void history_layout_version_dialog(HWND dialog)
{
    RECT client;
    int margin = app_scale(dialog, 12);
    int gap = app_scale(dialog, 10);
    int headerHeight = app_scale(dialog, 28);
    int titleWidth = app_scale(dialog, 112);
    int buttonHeight = app_scale(dialog, 28);
    int restoreWidth = app_scale(dialog, 142);
    int closeWidth = app_scale(dialog, 76);
    int rightMinimum = app_scale(dialog, 210);
    int contentTop;
    int contentBottom;
    int rightWidth;
    int previewWidth;
    int buttonTop;

    GetClientRect(dialog, &client);
    contentTop = margin + headerHeight;
    buttonTop = max(contentTop,
                    client.bottom - margin - buttonHeight);
    contentBottom = max(contentTop, buttonTop - gap);
    rightWidth = max(rightMinimum,
                     (client.right - margin * 2 - gap) * 31 / 100);
    previewWidth = max(0, client.right - margin * 2 - gap -
                             rightWidth);

    MoveWindow(GetDlgItem(dialog, IDC_VERSION_TITLE), margin,
               margin, titleWidth, headerHeight, TRUE);
    MoveWindow(GetDlgItem(dialog, IDC_VERSION_DETAILS),
               margin + titleWidth + gap, margin,
               max(0, client.right - margin * 2 - titleWidth - gap),
               headerHeight, TRUE);
    MoveWindow(GetDlgItem(dialog, IDC_VERSION_PREVIEW), margin,
               contentTop, previewWidth,
               max(0, contentBottom - contentTop), TRUE);
    MoveWindow(GetDlgItem(dialog, IDC_VERSION_LIST),
               margin + previewWidth + gap, contentTop, rightWidth,
               max(0, contentBottom - contentTop), TRUE);
    MoveWindow(GetDlgItem(dialog, IDC_VERSION_RESTORE),
               max(margin, client.right - margin - closeWidth - gap -
                               restoreWidth),
               buttonTop, restoreWidth, buttonHeight, TRUE);
    MoveWindow(GetDlgItem(dialog, IDCANCEL),
               max(margin, client.right - margin - closeWidth),
               buttonTop, closeWidth, buttonHeight, TRUE);
}

static void history_restore_store_rollback(
    AppState *app, HistoryContext *backup, BOOL revisionPending)
{
    if (!history_valid(app) || backup == NULL) {
        return;
    }
    KillTimer(app->mainWindow, HISTORY_TIMER_ID);
    history_replace_entries(app->history, backup);
    app->history->revisionPending = revisionPending;
    if (revisionPending) {
        SetTimer(app->mainWindow, HISTORY_TIMER_ID,
                 HISTORY_DEBOUNCE_MS, NULL);
    }
    history_refresh_dialogs(app);
    app_update_command_ui(app);
}

static BOOL history_restore_version(AppState *app,
                                    const HistoryId *selectedId)
{
    HistoryContext storeBefore;
    VersionEntry selected;
    VersionEntry currentSnapshot;
    VersionEntry *current;
    SIZE_T index;
    SIZE_T currentBytes;
    SIZE_T selectedBytes;
    DWORD error = ERROR_SUCCESS;
    BOOL revisionPendingBefore;
    BOOL restoreAttempted = FALSE;
    BOOL wasModified;
    BOOL result = FALSE;

    if (!history_valid(app) || selectedId == NULL ||
        !history_find_version_index(app->history, selectedId, &index) ||
        !history_restore_is_allowed(app)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!history_validate_version_entry(
            &app->history->versions[index], &error)) {
        SetLastError(error != ERROR_SUCCESS ? error
                                            : ERROR_INVALID_DATA);
        return FALSE;
    }
    ZeroMemory(&storeBefore, sizeof(storeBefore));
    ZeroMemory(&selected, sizeof(selected));
    ZeroMemory(&currentSnapshot, sizeof(currentSnapshot));
    if (!history_clone_version(&app->history->versions[index],
                               &selected)) {
        return FALSE;
    }
    revisionPendingBefore = app->history->revisionPending;
    wasModified = app->modified;
    if (!history_clone_store(app->history, &storeBefore)) {
        history_version_release(&selected);
        return FALSE;
    }

    /*
     * Capture the document currently on screen before restoring. This makes
     * Restore recoverable from the same history list, including when the
     * selected item is about to be pruned by the bounded store.
     */
    if (!history_flush_pending(app) ||
        !history_record_revision(app, NULL, FALSE)) {
        error = GetLastError();
        goto complete;
    }
    current = app->history->versionCount != 0
                  ? &app->history->versions[
                        app->history->versionCount - 1u]
                  : NULL;
    if (current == NULL ||
        !history_version_storage_size(current, &currentBytes) ||
        !history_version_storage_size(&selected, &selectedBytes) ||
        currentBytes > HISTORY_MAX_SNAPSHOT_BYTES ||
        selectedBytes >
            HISTORY_MAX_SNAPSHOT_BYTES - currentBytes) {
        error = ERROR_FILE_TOO_LARGE;
        goto complete;
    }
    if (!history_clone_version(current, &currentSnapshot)) {
        error = GetLastError();
        goto complete;
    }
    restoreAttempted = TRUE;
    if (!document_apply_history_snapshot(app, selected.rtf,
                                         selected.rtfSize, &error)) {
        goto complete;
    }
    if (!paper_size_apply_shared_layout(
            app, selected.paperSizeId, selected.pageWidth,
            selected.pageHeight, &selected.margins)) {
        error = GetLastError();
        goto complete;
    }

    document_mark_modified(app, TRUE);
    if (!history_record_revision(app, NULL, FALSE)) {
        error = GetLastError();
        goto complete;
    }
    app_set_status_message(app, L"Selected version restored");
    history_refresh_dialogs(app);
    result = TRUE;

complete:
    if (!result && restoreAttempted && currentSnapshot.rtf != NULL) {
        DWORD rollbackError = ERROR_SUCCESS;

        if (!document_apply_history_snapshot(
                app, currentSnapshot.rtf, currentSnapshot.rtfSize,
                &rollbackError) ||
            !paper_size_apply_shared_layout(
                app, currentSnapshot.paperSizeId,
                currentSnapshot.pageWidth, currentSnapshot.pageHeight,
                &currentSnapshot.margins)) {
            error = rollbackError != ERROR_SUCCESS
                        ? rollbackError : GetLastError();
        } else {
            document_mark_modified(app, wasModified);
        }
    }
    if (!result) {
        history_restore_store_rollback(
            app, &storeBefore, revisionPendingBefore);
    } else {
        history_release_entries(&storeBefore);
    }
    history_version_release(&currentSnapshot);
    history_version_release(&selected);
    if (!result) {
        SetLastError(error != ERROR_SUCCESS ? error
                                            : ERROR_CAN_NOT_COMPLETE);
    }
    return result;
}

static INT_PTR CALLBACK history_version_dialog_proc(HWND dialog,
                                                    UINT message,
                                                    WPARAM wParam,
                                                    LPARAM lParam)
{
    AppState *app =
        (AppState *)GetWindowLongPtrW(dialog, DWLP_USER);

    switch (message) {
    case WM_GETMINMAXINFO: {
        MINMAXINFO *limits = (MINMAXINFO *)lParam;
        limits->ptMinTrackSize.x = app_scale(dialog, 760);
        limits->ptMinTrackSize.y = app_scale(dialog, 520);
        return TRUE;
    }
    case WM_INITDIALOG:
        app = (AppState *)lParam;
        SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)app);
        if (!history_valid(app)) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        app->history->versionDialog = dialog;
        if (!render_editor_install_static_picture_callback(
                GetDlgItem(dialog, IDC_VERSION_PREVIEW))) {
            app->history->versionDialog = NULL;
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        SendDlgItemMessageW(dialog, IDC_VERSION_PREVIEW,
                            EM_EXLIMITTEXT, 0, 0x7FFFFFFE);
        SendDlgItemMessageW(dialog, IDC_VERSION_PREVIEW,
                            EM_SETREADONLY, TRUE, 0);
        SetTimer(dialog, HISTORY_VERSION_STATE_TIMER_ID,
                 HISTORY_VERSION_STATE_REFRESH_MS, NULL);
        history_layout_version_dialog(dialog);
        history_refresh_version_dialog(app);
        return TRUE;
    case WM_SIZE:
        history_layout_version_dialog(dialog);
        return TRUE;
    case WM_TIMER:
        if ((UINT_PTR)wParam == HISTORY_VERSION_STATE_TIMER_ID) {
            history_refresh_version_restore_state(app);
            return TRUE;
        }
        break;
    case WM_COMMAND:
        if (!history_valid(app)) {
            break;
        }
        switch (LOWORD(wParam)) {
        case IDC_VERSION_LIST:
            if (HIWORD(wParam) == LBN_SELCHANGE) {
                SIZE_T index;
                if (history_selected_version(app, &index)) {
                    history_preview_version(app, index);
                }
                return TRUE;
            }
            break;
        case IDC_VERSION_RESTORE: {
            SIZE_T index;
            HistoryId selectedId;
            if (!history_restore_is_allowed(app)) {
                MessageBoxW(
                    dialog,
                    L"Finish or leave the live sharing session before "
                    L"restoring a version.",
                    L"Version history",
                    MB_OK | MB_ICONINFORMATION);
                return TRUE;
            }
            if (!history_selected_version(app, &index)) {
                MessageBeep(MB_ICONWARNING);
                return TRUE;
            }
            selectedId = app->history->versions[index].id;
            if (MessageBoxW(
                    dialog,
                    L"Restore this version?\n\n"
                    L"Your current document will remain available as "
                    L"a newer version.",
                    L"Restore version",
                    MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1) !=
                IDOK) {
                return TRUE;
            }
            if (!history_restore_version(app, &selectedId)) {
                app_show_error(dialog,
                               L"The selected version could not be restored.",
                               GetLastError());
            }
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        default:
            break;
        }
        break;
    case WM_DESTROY:
        KillTimer(dialog, HISTORY_VERSION_STATE_TIMER_ID);
        if (history_valid(app) &&
            app->history->versionDialog == dialog) {
            app->history->versionDialog = NULL;
        }
        break;
    default:
        break;
    }
    return FALSE;
}

void history_show_chat(AppState *app)
{
    INT_PTR result;

    if (!history_valid(app)) {
        return;
    }
    if (app->history->chatDialog != NULL &&
        IsWindow(app->history->chatDialog)) {
        SetForegroundWindow(app->history->chatDialog);
        return;
    }
    result = DialogBoxParamW(app->instance,
                             MAKEINTRESOURCEW(IDD_DOCUMENT_CHAT),
                             app->mainWindow,
                             history_chat_dialog_proc,
                             (LPARAM)app);
    if (result == -1) {
        app_show_error(app->mainWindow,
                       L"The document chat could not be opened.",
                       GetLastError());
    }
}

void history_show_versions(AppState *app)
{
    INT_PTR result;

    if (!history_valid(app)) {
        return;
    }
    if (!history_flush_pending(app)) {
        app_show_error(app->mainWindow,
                       L"The latest document version could not be saved.",
                       GetLastError());
        return;
    }
    if (app->history->versionDialog != NULL &&
        IsWindow(app->history->versionDialog)) {
        SetForegroundWindow(app->history->versionDialog);
        return;
    }
    result = DialogBoxParamW(app->instance,
                             MAKEINTRESOURCEW(IDD_VERSION_HISTORY),
                             app->mainWindow,
                             history_version_dialog_proc,
                             (LPARAM)app);
    if (result == -1) {
        app_show_error(app->mainWindow,
                       L"The version history could not be opened.",
                       GetLastError());
    }
}

void history_refresh_dialogs(AppState *app)
{
    if (!history_valid(app)) {
        return;
    }
    history_refresh_chat_dialog(app);
    history_refresh_version_dialog(app);
}

void history_shutdown(AppState *app)
{
    HistoryContext *history;

    if (!history_valid(app)) {
        return;
    }
    history = app->history;
    KillTimer(app->mainWindow, HISTORY_TIMER_ID);
    history->revisionPending = FALSE;
    if (history->chatDialog != NULL &&
        IsWindow(history->chatDialog)) {
        EndDialog(history->chatDialog, IDCANCEL);
    }
    if (history->versionDialog != NULL &&
        IsWindow(history->versionDialog)) {
        EndDialog(history->versionDialog, IDCANCEL);
    }
    history_release_entries(history);
    app->history = NULL;
    history->app = NULL;
    HeapFree(GetProcessHeap(), 0, history);
}
