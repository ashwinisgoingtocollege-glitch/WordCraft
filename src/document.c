#include "editor.h"
#include "rendereditor.h"

#include <limits.h>
#include <string.h>

#define DOCUMENT_LIVE_SNAPSHOT_LIMIT ((SIZE_T)16u * 1024u * 1024u)
#define DOCUMENT_LIVE_ENVELOPE_SIZE 64u
#define DOCUMENT_LIVE_RTF_WIRE_LIMIT \
    (DOCUMENT_LIVE_SNAPSHOT_LIMIT - DOCUMENT_LIVE_ENVELOPE_SIZE)
#define DOCUMENT_LIVE_RTF_MAX_DEPTH 256

typedef enum DocumentHistoryMode {
    DOCUMENT_HISTORY_KEEP = 0,
    DOCUMENT_HISTORY_REPLACE,
    DOCUMENT_HISTORY_MERGE,
    DOCUMENT_HISTORY_RECONCILE,
    DOCUMENT_HISTORY_CHAT_ACK
} DocumentHistoryMode;

typedef enum RtfSafetyMode {
    RTF_SAFETY_STRUCTURE_ONLY = 0,
    RTF_SAFETY_LIVE,
    RTF_SAFETY_HISTORY
} RtfSafetyMode;

typedef struct FileStreamContext {
    HANDLE file;
    DWORD error;
} FileStreamContext;

typedef struct MemoryStreamContext {
    BYTE *data;
    SIZE_T size;
    SIZE_T capacity;
    SIZE_T position;
    DWORD error;
} MemoryStreamContext;

static const WCHAR openFilter[] =
    L"Rich Text Format (*.rtf)\0*.rtf\0"
    L"Text Documents (*.txt)\0*.txt\0"
    L"All Files (*.*)\0*.*\0\0";

static BOOL identity_from_handle(HANDLE file, DocumentIdentity *identity)
{
    BY_HANDLE_FILE_INFORMATION information;
    ZeroMemory(identity, sizeof(*identity));
    if (!GetFileInformationByHandle(file, &information)) {
        return FALSE;
    }
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return FALSE;
    }
    identity->valid = TRUE;
    identity->volumeSerialNumber = information.dwVolumeSerialNumber;
    identity->fileIndexHigh = information.nFileIndexHigh;
    identity->fileIndexLow = information.nFileIndexLow;
    identity->fileSizeHigh = information.nFileSizeHigh;
    identity->fileSizeLow = information.nFileSizeLow;
    identity->lastWriteTime = information.ftLastWriteTime;
    return TRUE;
}

static BOOL query_file_identity(const WCHAR *path, DocumentIdentity *identity)
{
    HANDLE file;
    BOOL success;
    ZeroMemory(identity, sizeof(*identity));
    file = CreateFileW(path, FILE_READ_ATTRIBUTES,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    success = identity_from_handle(file, identity);
    CloseHandle(file);
    return success;
}

static BOOL identities_match(const DocumentIdentity *left,
                             const DocumentIdentity *right)
{
    return left->valid && right->valid &&
           left->volumeSerialNumber == right->volumeSerialNumber &&
           left->fileIndexHigh == right->fileIndexHigh &&
           left->fileIndexLow == right->fileIndexLow &&
           left->fileSizeHigh == right->fileSizeHigh &&
           left->fileSizeLow == right->fileSizeLow &&
           CompareFileTime(&left->lastWriteTime, &right->lastWriteTime) == 0;
}

static DWORD CALLBACK file_read_callback(DWORD_PTR cookie, LPBYTE buffer,
                                         LONG requested, LONG *transferred)
{
    FileStreamContext *context = (FileStreamContext *)cookie;
    DWORD bytesRead = 0;
    if (!ReadFile(context->file, buffer, (DWORD)requested, &bytesRead, NULL)) {
        context->error = GetLastError();
        *transferred = 0;
        return 1;
    }
    *transferred = (LONG)bytesRead;
    return 0;
}

static DWORD CALLBACK memory_read_callback(DWORD_PTR cookie, LPBYTE buffer,
                                           LONG requested, LONG *transferred)
{
    MemoryStreamContext *context = (MemoryStreamContext *)cookie;
    SIZE_T available = context->size - context->position;
    SIZE_T amount = available < (SIZE_T)requested ? available : (SIZE_T)requested;
    if (amount > 0) {
        CopyMemory(buffer, context->data + context->position, amount);
        context->position += amount;
    }
    *transferred = (LONG)amount;
    return 0;
}

static DWORD CALLBACK memory_write_callback(DWORD_PTR cookie, LPBYTE buffer,
                                            LONG requested, LONG *transferred)
{
    MemoryStreamContext *context = (MemoryStreamContext *)cookie;
    SIZE_T needed;
    SIZE_T newCapacity;
    BYTE *newData;

    if (requested < 0 || context->size > SIZE_MAX - (SIZE_T)requested) {
        context->error = ERROR_NOT_ENOUGH_MEMORY;
        *transferred = 0;
        return 1;
    }
    needed = context->size + (SIZE_T)requested;
    if (needed > context->capacity) {
        newCapacity = context->capacity == 0 ? 65536 : context->capacity;
        while (newCapacity < needed) {
            if (newCapacity > SIZE_MAX / 2) {
                newCapacity = needed;
                break;
            }
            newCapacity *= 2;
        }
        if (context->data == NULL) {
            newData = HeapAlloc(GetProcessHeap(), 0, newCapacity);
        } else {
            newData = HeapReAlloc(GetProcessHeap(), 0, context->data, newCapacity);
        }
        if (newData == NULL) {
            context->error = ERROR_NOT_ENOUGH_MEMORY;
            *transferred = 0;
            return 1;
        }
        context->data = newData;
        context->capacity = newCapacity;
    }
    if (requested > 0) {
        CopyMemory(context->data + context->size, buffer, (SIZE_T)requested);
        context->size += (SIZE_T)requested;
    }
    *transferred = requested;
    return 0;
}

static void free_memory_stream(MemoryStreamContext *stream)
{
    if (stream->data != NULL) {
        HeapFree(GetProcessHeap(), 0, stream->data);
    }
    ZeroMemory(stream, sizeof(*stream));
}

static BOOL capture_rtf(HWND editor, MemoryStreamContext *output, DWORD *error)
{
    EDITSTREAM stream;
    ZeroMemory(output, sizeof(*output));
    ZeroMemory(&stream, sizeof(stream));
    stream.dwCookie = (DWORD_PTR)output;
    stream.pfnCallback = memory_write_callback;
    SendMessageW(editor, EM_STREAMOUT, SF_RTF, (LPARAM)&stream);
    if (stream.dwError != 0 || output->error != ERROR_SUCCESS) {
        *error = output->error != ERROR_SUCCESS ? output->error : stream.dwError;
        free_memory_stream(output);
        return FALSE;
    }
    return TRUE;
}

static BOOL restore_rtf(HWND editor, MemoryStreamContext *input)
{
    EDITSTREAM stream;
    DWORD pictureError = ERROR_SUCCESS;

    if (!render_editor_begin_static_picture_stream(editor)) {
        return FALSE;
    }
    input->position = 0;
    ZeroMemory(&stream, sizeof(stream));
    stream.dwCookie = (DWORD_PTR)input;
    stream.pfnCallback = memory_read_callback;
    SendMessageW(editor, EM_STREAMIN, SF_RTF, (LPARAM)&stream);
    if (!render_editor_end_static_picture_stream(
            editor, &pictureError)) {
        SetLastError(pictureError);
        return FALSE;
    }
    return stream.dwError == 0;
}

static BOOL is_rtf_path(const WCHAR *path)
{
    const WCHAR *slash = wcsrchr(path, L'\\');
    const WCHAR *forwardSlash = wcsrchr(path, L'/');
    const WCHAR *dot = wcsrchr(path, L'.');
    const WCHAR *lastSlash = slash;
    if (forwardSlash != NULL && (lastSlash == NULL || forwardSlash > lastSlash)) {
        lastSlash = forwardSlash;
    }
    return dot != NULL && (lastSlash == NULL || dot > lastSlash) &&
           _wcsicmp(dot, L".rtf") == 0;
}

static BOOL is_txt_path(const WCHAR *path)
{
    const WCHAR *slash = wcsrchr(path, L'\\');
    const WCHAR *forwardSlash = wcsrchr(path, L'/');
    const WCHAR *dot = wcsrchr(path, L'.');
    const WCHAR *lastSlash = slash;
    if (forwardSlash != NULL && (lastSlash == NULL || forwardSlash > lastSlash)) {
        lastSlash = forwardSlash;
    }
    return dot != NULL && (lastSlash == NULL || dot > lastSlash) &&
           _wcsicmp(dot, L".txt") == 0;
}

static BOOL path_has_extension(const WCHAR *path)
{
    const WCHAR *slash = wcsrchr(path, L'\\');
    const WCHAR *forwardSlash = wcsrchr(path, L'/');
    const WCHAR *dot = wcsrchr(path, L'.');
    const WCHAR *lastSlash = slash;
    if (forwardSlash != NULL && (lastSlash == NULL || forwardSlash > lastSlash)) {
        lastSlash = forwardSlash;
    }
    return dot != NULL && (lastSlash == NULL || dot > lastSlash);
}

static BOOL read_entire_file(const WCHAR *path, BYTE **data, SIZE_T *size,
                             DocumentIdentity *identity, DWORD *error)
{
    HANDLE file;
    LARGE_INTEGER fileSize;
    BYTE *buffer = NULL;
    SIZE_T total = 0;

    *data = NULL;
    *size = 0;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ,
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
    if (!identity_from_handle(file, identity)) {
        *error = GetLastError();
        CloseHandle(file);
        return FALSE;
    }
    if (fileSize.QuadPart < 0 || (ULONGLONG)fileSize.QuadPart > INT_MAX) {
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
        DWORD chunk = (DWORD)(((SIZE_T)fileSize.QuadPart - total) > (SIZE_T)(16 * 1024 * 1024)
                                  ? (16 * 1024 * 1024)
                                  : ((SIZE_T)fileSize.QuadPart - total));
        DWORD bytesRead = 0;
        if (!ReadFile(file, buffer + total, chunk, &bytesRead, NULL)) {
            *error = GetLastError();
            HeapFree(GetProcessHeap(), 0, buffer);
            CloseHandle(file);
            return FALSE;
        }
        if (bytesRead == 0) {
            *error = ERROR_HANDLE_EOF;
            HeapFree(GetProcessHeap(), 0, buffer);
            CloseHandle(file);
            return FALSE;
        }
        total += bytesRead;
    }
    if (!CloseHandle(file)) {
        *error = GetLastError();
        if (buffer != NULL) {
            HeapFree(GetProcessHeap(), 0, buffer);
        }
        return FALSE;
    }
    *data = buffer;
    *size = total;
    return TRUE;
}

static BOOL sniff_rtf_header(const WCHAR *path, BOOL *isRtf, DWORD *error)
{
    HANDLE file;
    BYTE header[5];
    DWORD bytesRead = 0;

    *isRtf = FALSE;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        *error = GetLastError();
        return FALSE;
    }
    if (!ReadFile(file, header, sizeof(header), &bytesRead, NULL)) {
        *error = GetLastError();
        CloseHandle(file);
        return FALSE;
    }
    *isRtf = bytesRead == sizeof(header) && header[0] == '{' && header[1] == '\\' &&
             header[2] == 'r' && header[3] == 't' && header[4] == 'f';
    if (!CloseHandle(file)) {
        *error = GetLastError();
        return FALSE;
    }
    return TRUE;
}

static BOOL decode_text(const BYTE *data, SIZE_T size, WCHAR **text, DWORD *error)
{
    SIZE_T offset = 0;
    BOOL utf16LittleEndian = FALSE;
    BOOL utf16BigEndian = FALSE;
    WCHAR *result;
    int count;

    *text = NULL;
    if (size >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        utf16LittleEndian = TRUE;
        offset = 2;
    } else if (size >= 2 && data[0] == 0xFE && data[1] == 0xFF) {
        utf16BigEndian = TRUE;
        offset = 2;
    } else if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        offset = 3;
    }

    if (utf16LittleEndian || utf16BigEndian) {
        SIZE_T bytes = size - offset;
        SIZE_T characters;
        SIZE_T i;
        if ((bytes & 1u) != 0) {
            *error = ERROR_NO_UNICODE_TRANSLATION;
            return FALSE;
        }
        characters = bytes / 2;
        result = HeapAlloc(GetProcessHeap(), 0, (characters + 1) * sizeof(WCHAR));
        if (result == NULL) {
            *error = ERROR_NOT_ENOUGH_MEMORY;
            return FALSE;
        }
        for (i = 0; i < characters; ++i) {
            BYTE first = data[offset + i * 2];
            BYTE second = data[offset + i * 2 + 1];
            result[i] = utf16LittleEndian
                            ? (WCHAR)((WORD)first | ((WORD)second << 8))
                            : (WCHAR)((WORD)second | ((WORD)first << 8));
            if (result[i] == L'\0') {
                *error = ERROR_INVALID_DATA;
                HeapFree(GetProcessHeap(), 0, result);
                return FALSE;
            }
        }
        for (i = 0; i < characters; ++i) {
            if (result[i] >= 0xD800 && result[i] <= 0xDBFF) {
                if (i + 1 >= characters ||
                    result[i + 1] < 0xDC00 || result[i + 1] > 0xDFFF) {
                    *error = ERROR_NO_UNICODE_TRANSLATION;
                    HeapFree(GetProcessHeap(), 0, result);
                    return FALSE;
                }
                ++i;
            } else if (result[i] >= 0xDC00 && result[i] <= 0xDFFF) {
                *error = ERROR_NO_UNICODE_TRANSLATION;
                HeapFree(GetProcessHeap(), 0, result);
                return FALSE;
            }
        }
        result[characters] = L'\0';
        *text = result;
        return TRUE;
    }

    if (size == offset) {
        result = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WCHAR));
        if (result == NULL) {
            *error = ERROR_NOT_ENOUGH_MEMORY;
            return FALSE;
        }
        *text = result;
        return TRUE;
    }
    if (size - offset > INT_MAX) {
        *error = ERROR_FILE_TOO_LARGE;
        return FALSE;
    }
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                (LPCCH)(data + offset), (int)(size - offset), NULL, 0);
    if (count <= 0) {
        *error = GetLastError();
        return FALSE;
    }
    result = HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)count + 1) * sizeof(WCHAR));
    if (result == NULL) {
        *error = ERROR_NOT_ENOUGH_MEMORY;
        return FALSE;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            (LPCCH)(data + offset), (int)(size - offset),
                            result, count) != count) {
        *error = GetLastError();
        HeapFree(GetProcessHeap(), 0, result);
        return FALSE;
    }
    {
        int i;
        for (i = 0; i < count; ++i) {
            if (result[i] == L'\0') {
                *error = ERROR_INVALID_DATA;
                HeapFree(GetProcessHeap(), 0, result);
                return FALSE;
            }
        }
    }
    result[count] = L'\0';
    *text = result;
    return TRUE;
}

static BOOL load_rtf_candidate(AppState *app, const WCHAR *path,
                               MemoryStreamContext *normalized,
                               DocumentIdentity *identity, DWORD *error)
{
    HANDLE file;
    HWND temporaryEditor;
    FileStreamContext fileContext;
    EDITSTREAM input;
    DWORD pictureError = ERROR_SUCCESS;
    BOOL result = FALSE;
    BYTE header[5];
    DWORD headerBytes = 0;
    LARGE_INTEGER beginning;

    ZeroMemory(normalized, sizeof(*normalized));
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        *error = GetLastError();
        return FALSE;
    }
    if (!identity_from_handle(file, identity)) {
        *error = GetLastError();
        CloseHandle(file);
        return FALSE;
    }
    if (!ReadFile(file, header, sizeof(header), &headerBytes, NULL)) {
        *error = GetLastError();
        CloseHandle(file);
        return FALSE;
    }
    if (headerBytes != sizeof(header) || header[0] != '{' || header[1] != '\\' ||
        header[2] != 'r' || header[3] != 't' || header[4] != 'f') {
        *error = ERROR_INVALID_DATA;
        CloseHandle(file);
        return FALSE;
    }
    beginning.QuadPart = 0;
    if (!SetFilePointerEx(file, beginning, NULL, FILE_BEGIN)) {
        *error = GetLastError();
        CloseHandle(file);
        return FALSE;
    }
    temporaryEditor = CreateWindowExW(
        0, MSFTEDIT_CLASS, NULL, ES_MULTILINE,
        0, 0, 0, 0, app->mainWindow, NULL, app->instance, NULL);
    if (temporaryEditor == NULL) {
        *error = GetLastError();
        CloseHandle(file);
        return FALSE;
    }
    if (!render_editor_install_static_picture_callback(temporaryEditor)) {
        *error = GetLastError();
        DestroyWindow(temporaryEditor);
        CloseHandle(file);
        return FALSE;
    }
    SendMessageW(temporaryEditor, EM_EXLIMITTEXT, 0, 0x7FFFFFFE);
    ZeroMemory(&fileContext, sizeof(fileContext));
    fileContext.file = file;
    ZeroMemory(&input, sizeof(input));
    input.dwCookie = (DWORD_PTR)&fileContext;
    input.pfnCallback = file_read_callback;
    if (!render_editor_begin_static_picture_stream(temporaryEditor)) {
        *error = GetLastError();
        DestroyWindow(temporaryEditor);
        CloseHandle(file);
        return FALSE;
    }
    SendMessageW(temporaryEditor, EM_STREAMIN, SF_RTF, (LPARAM)&input);
    if (!render_editor_end_static_picture_stream(
            temporaryEditor, &pictureError)) {
        *error = pictureError;
    } else if (input.dwError != 0 ||
               fileContext.error != ERROR_SUCCESS) {
        *error = fileContext.error != ERROR_SUCCESS ? fileContext.error : input.dwError;
    } else if (!capture_rtf(temporaryEditor, normalized, error)) {
        /* capture_rtf supplied the error */
    } else {
        result = TRUE;
    }
    DestroyWindow(temporaryEditor);
    if (!CloseHandle(file) && result) {
        *error = GetLastError();
        free_memory_stream(normalized);
        result = FALSE;
    }
    return result;
}

static BOOL rtf_ascii_letter(BYTE value)
{
    return (value >= 'a' && value <= 'z') ||
           (value >= 'A' && value <= 'Z');
}

static BOOL rtf_hex_digit(BYTE value)
{
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

static BOOL rtf_word_is_bin(const BYTE *word, SIZE_T length)
{
    return length == 3 && (word[0] == 'b' || word[0] == 'B') &&
           (word[1] == 'i' || word[1] == 'I') &&
           (word[2] == 'n' || word[2] == 'N');
}

static BYTE rtf_ascii_lower(BYTE value)
{
    return value >= 'A' && value <= 'Z'
               ? (BYTE)(value + ('a' - 'A'))
               : value;
}

static BOOL rtf_word_equals(const BYTE *word, SIZE_T length,
                            const char *expected)
{
    SIZE_T index;
    SIZE_T expectedLength = strlen(expected);

    if (length != expectedLength) {
        return FALSE;
    }
    for (index = 0; index < length; ++index) {
        if (rtf_ascii_lower(word[index]) != (BYTE)expected[index]) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL rtf_word_has_prefix(const BYTE *word, SIZE_T length,
                                const char *prefix)
{
    SIZE_T index;
    SIZE_T prefixLength = strlen(prefix);

    if (length < prefixLength) {
        return FALSE;
    }
    for (index = 0; index < prefixLength; ++index) {
        if (rtf_ascii_lower(word[index]) != (BYTE)prefix[index]) {
            return FALSE;
        }
    }
    return TRUE;
}

/*
 * Live snapshots cross a trust boundary. WordCraft shares text, formatting,
 * and its own inert metadata, so live traffic rejects every embedded object.
 * Local version snapshots additionally permit self-contained static picture
 * groups, but still reject OLE objects, fields, drawings, links, embedded
 * fonts, HTML, and opaque application data before RichEdit sees the bytes.
 */
static BOOL rtf_safe_word_is_unsafe(const BYTE *word, SIZE_T length,
                                    BOOL allowStaticPictures)
{
    static const char *const exactWords[] = {
        "field", "datafield", "formfield",
        "nextgraphic",
        "do", "private", "datastore", "themedata",
        "colorschememapping", "htmltag", "mhtmltag",
        "includetext", "includepicture", "import"
    };
    static const char *const pictureWords[] = {
        "pict", "nonshppict", "listpicture"
    };
    SIZE_T index;

    if (rtf_word_has_prefix(word, length, "obj") ||
        rtf_word_has_prefix(word, length, "fld") ||
        (rtf_word_has_prefix(word, length, "shp") &&
         !(allowStaticPictures &&
           (rtf_word_equals(word, length, "shppict") ||
            rtf_word_equals(word, length, "shprslt")))) ||
        rtf_word_has_prefix(word, length, "file") ||
        rtf_word_has_prefix(word, length, "fontemb") ||
        rtf_word_has_prefix(word, length, "fontfile") ||
        rtf_word_has_prefix(word, length, "html")) {
        return TRUE;
    }
    for (index = 0; index < ARRAYSIZE(exactWords); ++index) {
        if (rtf_word_equals(word, length, exactWords[index])) {
            return TRUE;
        }
    }
    if (!allowStaticPictures) {
        for (index = 0; index < ARRAYSIZE(pictureWords); ++index) {
            if (rtf_word_equals(word, length, pictureWords[index])) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

static BOOL validate_rtf_structure(const BYTE *data, SIZE_T size,
                                   RtfSafetyMode safetyMode)
{
    SIZE_T index = 0;
    LONG depth = 0;
    BOOL rootClosed = FALSE;

    if (data == NULL || size < 6 || data[0] != '{' || data[1] != '\\' ||
        data[2] != 'r' || data[3] != 't' || data[4] != 'f' ||
        data[5] < '0' || data[5] > '9') {
        return FALSE;
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
            if (depth == 0 && index != 0) {
                return FALSE;
            }
            if (depth >= DOCUMENT_LIVE_RTF_MAX_DEPTH) {
                return FALSE;
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
        if (index >= size) {
            return FALSE;
        }
        value = data[index];
        if (value == 0) {
            return FALSE;
        }
        if (value == '\'') {
            if (index + 2 >= size || !rtf_hex_digit(data[index + 1]) ||
                !rtf_hex_digit(data[index + 2])) {
                return FALSE;
            }
            index += 3;
            continue;
        }
        if (rtf_ascii_letter(value)) {
            SIZE_T wordStart = index;
            SIZE_T wordLength;
            SIZE_T binaryLength = 0;
            BOOL negative = FALSE;
            BOOL hasNumber = FALSE;
            BOOL binary;

            while (index < size && rtf_ascii_letter(data[index])) {
                ++index;
            }
            wordLength = index - wordStart;
            binary = rtf_word_is_bin(data + wordStart, wordLength);
            if (safetyMode != RTF_SAFETY_STRUCTURE_ONLY &&
                rtf_safe_word_is_unsafe(
                    data + wordStart, wordLength,
                    safetyMode == RTF_SAFETY_HISTORY)) {
                return FALSE;
            }
            if (index < size && data[index] == '-') {
                negative = TRUE;
                ++index;
            }
            while (index < size && data[index] >= '0' && data[index] <= '9') {
                unsigned digit = (unsigned)(data[index] - '0');
                hasNumber = TRUE;
                if (binaryLength > (SIZE_MAX - digit) / 10) {
                    return FALSE;
                }
                binaryLength = binaryLength * 10 + digit;
                ++index;
            }
            if (index < size && data[index] == ' ') {
                ++index;
            }
            if (binary) {
                if (negative || !hasNumber || binaryLength > size - index) {
                    return FALSE;
                }
                index += binaryLength;
            }
            continue;
        }
        ++index;
        if (value == '\r' && index < size && data[index] == '\n') {
            ++index;
        }
    }
    return rootClosed && depth == 0;
}

BOOL document_validate_live_snapshot(const BYTE *data, SIZE_T size,
                                     DWORD *error)
{
    if (error != NULL) {
        *error = ERROR_SUCCESS;
    }
    if (data == NULL || size == 0u) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return FALSE;
    }
    if (size > DOCUMENT_LIVE_SNAPSHOT_LIMIT) {
        if (error != NULL) {
            *error = ERROR_FILE_TOO_LARGE;
        }
        return FALSE;
    }
    if (!validate_rtf_structure(data, size, RTF_SAFETY_LIVE)) {
        if (error != NULL) {
            *error = ERROR_INVALID_DATA;
        }
        return FALSE;
    }
    return TRUE;
}

BOOL document_validate_history_snapshot(const BYTE *data, SIZE_T size,
                                        DWORD *error)
{
    if (error != NULL) {
        *error = ERROR_SUCCESS;
    }
    if (data == NULL || size == 0u) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return FALSE;
    }
    if (size > DOCUMENT_LIVE_SNAPSHOT_LIMIT) {
        if (error != NULL) {
            *error = ERROR_FILE_TOO_LARGE;
        }
        return FALSE;
    }
    if (!validate_rtf_structure(data, size, RTF_SAFETY_HISTORY)) {
        if (error != NULL) {
            *error = ERROR_INVALID_DATA;
        }
        return FALSE;
    }
    return TRUE;
}

static BOOL normalize_rtf_memory(AppState *app, const BYTE *data, SIZE_T size,
                                 RtfSafetyMode safetyMode,
                                 MemoryStreamContext *normalized, DWORD *error)
{
    HWND temporaryEditor;
    MemoryStreamContext source;
    EDITSTREAM input;
    DWORD pictureError = ERROR_SUCCESS;
    BOOL result = FALSE;

    if (normalized == NULL || error == NULL || app == NULL ||
        (data == NULL && size != 0)) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return FALSE;
    }
    ZeroMemory(normalized, sizeof(*normalized));
    *error = ERROR_SUCCESS;
    if (size > DOCUMENT_LIVE_SNAPSHOT_LIMIT) {
        *error = ERROR_FILE_TOO_LARGE;
        return FALSE;
    }
    if (!validate_rtf_structure(data, size, safetyMode)) {
        *error = ERROR_INVALID_DATA;
        return FALSE;
    }

    temporaryEditor = CreateWindowExW(
        0, MSFTEDIT_CLASS, NULL, ES_MULTILINE,
        0, 0, 0, 0, app->mainWindow, NULL, app->instance, NULL);
    if (temporaryEditor == NULL) {
        *error = GetLastError();
        return FALSE;
    }
    if (!render_editor_install_static_picture_callback(temporaryEditor)) {
        *error = GetLastError();
        DestroyWindow(temporaryEditor);
        return FALSE;
    }
    SendMessageW(temporaryEditor, EM_EXLIMITTEXT, 0, 0x7FFFFFFE);
    ZeroMemory(&source, sizeof(source));
    source.data = (BYTE *)data;
    source.size = size;
    source.capacity = size;
    ZeroMemory(&input, sizeof(input));
    input.dwCookie = (DWORD_PTR)&source;
    input.pfnCallback = memory_read_callback;
    if (!render_editor_begin_static_picture_stream(temporaryEditor)) {
        *error = GetLastError();
        DestroyWindow(temporaryEditor);
        return FALSE;
    }
    SendMessageW(temporaryEditor, EM_STREAMIN, SF_RTF, (LPARAM)&input);
    if (!render_editor_end_static_picture_stream(
            temporaryEditor, &pictureError)) {
        *error = pictureError;
    } else if (input.dwError != 0) {
        *error = input.dwError;
    } else if (!capture_rtf(temporaryEditor, normalized, error)) {
        /* capture_rtf supplied the error */
    } else if (normalized->size > DOCUMENT_LIVE_SNAPSHOT_LIMIT) {
        *error = ERROR_FILE_TOO_LARGE;
        free_memory_stream(normalized);
    } else {
        result = TRUE;
    }
    DestroyWindow(temporaryEditor);
    return result;
}

static BOOL apply_rtf_candidate(AppState *app, MemoryStreamContext *candidate, DWORD *error)
{
    MemoryStreamContext backup;
    EDITSTREAM input;
    DWORD pictureError = ERROR_SUCCESS;
    BOOL success;
    BOOL restoreFailed = FALSE;
    BOOL wasLoading;

    if (!capture_rtf(app->editor, &backup, error)) {
        return FALSE;
    }
    if (!render_editor_begin_static_picture_stream(app->editor)) {
        *error = GetLastError();
        free_memory_stream(&backup);
        return FALSE;
    }
    wasLoading = app->loading;
    app->loading = TRUE;
    SetWindowTextW(app->editor, L"");
    candidate->position = 0;
    ZeroMemory(&input, sizeof(input));
    input.dwCookie = (DWORD_PTR)candidate;
    input.pfnCallback = memory_read_callback;
    SendMessageW(app->editor, EM_STREAMIN, SF_RTF, (LPARAM)&input);
    success = render_editor_end_static_picture_stream(
                  app->editor, &pictureError) &&
              input.dwError == 0;
    if (pictureError != ERROR_SUCCESS) {
        *error = pictureError;
    }
    if (!success) {
        if (*error == ERROR_SUCCESS) {
            *error = input.dwError != 0
                         ? input.dwError : ERROR_CAN_NOT_COMPLETE;
        }
        SetWindowTextW(app->editor, L"");
        if (!restore_rtf(app->editor, &backup)) {
            *error = ERROR_NOT_ENOUGH_MEMORY;
            restoreFailed = TRUE;
        }
    }
    app->loading = wasLoading;
    if (restoreFailed) {
        document_mark_modified(app, TRUE);
    }
    free_memory_stream(&backup);
    return success;
}

static BOOL apply_text_candidate(AppState *app, const WCHAR *text, DWORD *error)
{
    MemoryStreamContext backup;
    BOOL result;
    BOOL wasLoading;
    SetLastError(ERROR_SUCCESS);
    if (!capture_rtf(app->editor, &backup, error)) {
        return FALSE;
    }
    wasLoading = app->loading;
    app->loading = TRUE;
    SetWindowTextW(app->editor, L"");
    format_initialize_document(app);
    SetLastError(ERROR_SUCCESS);
    result = SetWindowTextW(app->editor, text);
    if (!result) {
        DWORD setTextError = GetLastError();
        *error = setTextError != ERROR_SUCCESS ? setTextError : ERROR_NOT_ENOUGH_MEMORY;
        SetWindowTextW(app->editor, L"");
        if (!restore_rtf(app->editor, &backup)) {
            *error = ERROR_INVALID_DATA;
        }
    }
    app->loading = wasLoading;
    free_memory_stream(&backup);
    return result;
}

static BOOL write_all(HANDLE file, const BYTE *data, SIZE_T size, DWORD *error)
{
    SIZE_T total = 0;
    while (total < size) {
        DWORD amount = (DWORD)((size - total) > (SIZE_T)(16 * 1024 * 1024)
                                   ? (16 * 1024 * 1024)
                                   : (size - total));
        DWORD written = 0;
        if (!WriteFile(file, data + total, amount, &written, NULL)) {
            *error = GetLastError();
            return FALSE;
        }
        if (written == 0) {
            *error = ERROR_WRITE_FAULT;
            return FALSE;
        }
        total += written;
    }
    return TRUE;
}

static BOOL write_text_file(AppState *app, const WCHAR *path, DWORD *error)
{
    HANDLE file;
    SIZE_T length = 0;
    WCHAR *wideText = NULL;
    BYTE *utf8 = NULL;
    int utf8Size = 0;
    static const BYTE bom[] = {0xEF, 0xBB, 0xBF};
    BOOL success = FALSE;

    if (!editor_get_all_text(app->editor, TRUE, &wideText, &length, error)) {
        return FALSE;
    }
    if (length > INT_MAX) {
        *error = ERROR_FILE_TOO_LARGE;
        HeapFree(GetProcessHeap(), 0, wideText);
        return FALSE;
    }
    if (length > 0) {
        utf8Size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                      wideText, (int)length, NULL, 0, NULL, NULL);
        if (utf8Size <= 0) {
            *error = GetLastError();
            HeapFree(GetProcessHeap(), 0, wideText);
            return FALSE;
        }
        utf8 = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)utf8Size);
        if (utf8 == NULL) {
            *error = ERROR_NOT_ENOUGH_MEMORY;
            HeapFree(GetProcessHeap(), 0, wideText);
            return FALSE;
        }
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                wideText, (int)length, (LPSTR)utf8, utf8Size,
                                NULL, NULL) != utf8Size) {
            *error = GetLastError();
            HeapFree(GetProcessHeap(), 0, utf8);
            HeapFree(GetProcessHeap(), 0, wideText);
            return FALSE;
        }
    }
    HeapFree(GetProcessHeap(), 0, wideText);

    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        *error = GetLastError();
    } else if (!write_all(file, bom, sizeof(bom), error)) {
        /* write_all supplied the error */
    } else if (utf8Size > 0 && !write_all(file, utf8, (SIZE_T)utf8Size, error)) {
        /* write_all supplied the error */
    } else if (!FlushFileBuffers(file)) {
        *error = GetLastError();
    } else {
        success = TRUE;
    }
    if (file != INVALID_HANDLE_VALUE && !CloseHandle(file) && success) {
        *error = GetLastError();
        success = FALSE;
    }
    if (utf8 != NULL) {
        HeapFree(GetProcessHeap(), 0, utf8);
    }
    return success;
}

static BOOL write_rtf_file(AppState *app, const WCHAR *path, DWORD *error)
{
    HANDLE file;
    MemoryStreamContext rtf;
    BYTE *withHistory = NULL;
    SIZE_T withHistorySize = 0;
    BYTE *withComments = NULL;
    SIZE_T withCommentsSize = 0;
    BOOL success = FALSE;

    if (!capture_rtf(app->editor, &rtf, error)) {
        return FALSE;
    }
    if (!history_embed_rtf(app, rtf.data, rtf.size, &withHistory,
                           &withHistorySize, error)) {
        free_memory_stream(&rtf);
        return FALSE;
    }
    free_memory_stream(&rtf);
    /*
     * Comments deliberately remain the final direct child of the RTF root;
     * their legacy parser enforces that placement.  History is inserted
     * first, then comments are appended after it.
     */
    if (!comments_embed_rtf(app, withHistory, withHistorySize, &withComments,
                            &withCommentsSize, error)) {
        HeapFree(GetProcessHeap(), 0, withHistory);
        return FALSE;
    }
    HeapFree(GetProcessHeap(), 0, withHistory);

    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        *error = GetLastError();
    } else if (!write_all(file, withComments, withCommentsSize, error)) {
        /* write_all supplied the error */
    } else if (!FlushFileBuffers(file)) {
        *error = GetLastError();
    } else {
        success = TRUE;
    }
    if (file != INVALID_HANDLE_VALUE && !CloseHandle(file) && success) {
        *error = GetLastError();
        success = FALSE;
    }
    HeapFree(GetProcessHeap(), 0, withComments);
    return success;
}

BOOL document_capture_live_snapshot(AppState *app,
                                    const HistoryChatToken *requiredChats,
                                    SIZE_T requiredChatCount, BYTE **data,
                                    SIZE_T *size, DWORD *error)
{
    MemoryStreamContext rtf;
    BYTE *commentsOnly = NULL;
    SIZE_T commentsOnlySize = 0;
    SIZE_T commentsOverhead;
    SIZE_T historyOutputLimit;
    BYTE *withHistory = NULL;
    SIZE_T withHistorySize = 0;
    BYTE *withComments = NULL;
    SIZE_T withCommentsSize = 0;

    if (app == NULL || app->editor == NULL || data == NULL || size == NULL ||
        error == NULL) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return FALSE;
    }
    *data = NULL;
    *size = 0;
    *error = ERROR_SUCCESS;
    if (!capture_rtf(app->editor, &rtf, error)) {
        return FALSE;
    }
    /*
     * Comments must remain the final direct child of the RTF root. Measure
     * their fixed encoded overhead first, then give history the exact
     * remaining wire budget. The bounded history serializer only trims the
     * transmitted view; the document's permanent in-memory history is
     * untouched and a normal RTF save still writes it in full.
     */
    if (!comments_embed_rtf(app, rtf.data, rtf.size, &commentsOnly,
                            &commentsOnlySize, error)) {
        free_memory_stream(&rtf);
        return FALSE;
    }
    if (commentsOnlySize < rtf.size ||
        commentsOnlySize > DOCUMENT_LIVE_RTF_WIRE_LIMIT) {
        HeapFree(GetProcessHeap(), 0, commentsOnly);
        free_memory_stream(&rtf);
        *error = commentsOnlySize < rtf.size
                     ? ERROR_INVALID_DATA : ERROR_FILE_TOO_LARGE;
        return FALSE;
    }
    commentsOverhead = commentsOnlySize - rtf.size;
    historyOutputLimit =
        DOCUMENT_LIVE_RTF_WIRE_LIMIT - commentsOverhead;
    HeapFree(GetProcessHeap(), 0, commentsOnly);
    if (!history_embed_rtf_bounded(
            app, rtf.data, rtf.size, historyOutputLimit,
            requiredChats, requiredChatCount, &withHistory,
            &withHistorySize, error)) {
        free_memory_stream(&rtf);
        return FALSE;
    }
    free_memory_stream(&rtf);
    if (!comments_embed_rtf(app, withHistory, withHistorySize, &withComments,
                            &withCommentsSize, error)) {
        HeapFree(GetProcessHeap(), 0, withHistory);
        return FALSE;
    }
    HeapFree(GetProcessHeap(), 0, withHistory);
    if (withCommentsSize > DOCUMENT_LIVE_RTF_WIRE_LIMIT) {
        HeapFree(GetProcessHeap(), 0, withComments);
        *error = ERROR_FILE_TOO_LARGE;
        return FALSE;
    }
    *data = withComments;
    *size = withCommentsSize;
    return TRUE;
}

BOOL document_capture_revision_snapshot(AppState *app, BYTE **data,
                                        SIZE_T *size, DWORD *error)
{
    MemoryStreamContext rtf;
    BYTE *withComments = NULL;
    SIZE_T withCommentsSize = 0;

    if (app == NULL || app->editor == NULL || data == NULL || size == NULL ||
        error == NULL) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return FALSE;
    }
    *data = NULL;
    *size = 0;
    *error = ERROR_SUCCESS;
    if (!capture_rtf(app->editor, &rtf, error)) {
        return FALSE;
    }
    if (!comments_embed_rtf(app, rtf.data, rtf.size, &withComments,
                            &withCommentsSize, error)) {
        free_memory_stream(&rtf);
        return FALSE;
    }
    free_memory_stream(&rtf);
    if (withCommentsSize > DOCUMENT_LIVE_SNAPSHOT_LIMIT) {
        HeapFree(GetProcessHeap(), 0, withComments);
        *error = ERROR_FILE_TOO_LARGE;
        return FALSE;
    }
    *data = withComments;
    *size = withCommentsSize;
    return TRUE;
}

static BOOL document_apply_snapshot(AppState *app, const BYTE *data,
                                    SIZE_T size, DWORD *error,
                                    DocumentHistoryMode historyMode,
                                    const HistoryChatToken *acknowledgedChats,
                                    SIZE_T acknowledgedChatCount)
{
    MemoryStreamContext normalized;
    MemoryStreamContext restoreCandidate;
    BYTE *backup = NULL;
    SIZE_T backupSize = 0;
    CHARRANGE selection;
    GETTEXTLENGTHEX lengthQuery;
    LRESULT textLength;
    DWORD originalError = ERROR_SUCCESS;
    DWORD rollbackError = ERROR_SUCCESS;
    BOOL wasModified;
    BOOL editorChanged = FALSE;
    BOOL rollbackFailed = FALSE;
    BOOL priorLoading;
    LRESULT eventMask;
    HWND oldFocus;

    if (app == NULL || app->editor == NULL || error == NULL ||
        (data == NULL && size != 0)) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return FALSE;
    }
    *error = ERROR_SUCCESS;
    ZeroMemory(&normalized, sizeof(normalized));
    ZeroMemory(&restoreCandidate, sizeof(restoreCandidate));
    if (!normalize_rtf_memory(
            app, data, size,
            historyMode == DOCUMENT_HISTORY_KEEP
                ? RTF_SAFETY_HISTORY : RTF_SAFETY_LIVE,
            &normalized, error)) {
        return FALSE;
    }
    /*
     * History loading/merging parses transactionally before swapping state,
     * so only the body and comments need a rollback snapshot here. This also
     * keeps a bounded live frame from expanding its own backup recursively.
     */
    if (!document_capture_revision_snapshot(app, &backup, &backupSize,
                                            error)) {
        free_memory_stream(&normalized);
        return FALSE;
    }

    ZeroMemory(&selection, sizeof(selection));
    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
    wasModified = app->modified;
    oldFocus = GetFocus();
    priorLoading = app->loading;
    eventMask = SendMessageW(app->editor, EM_GETEVENTMASK, 0, 0);
    app->loading = TRUE;
    SendMessageW(app->editor, EM_SETEVENTMASK, 0,
                 (LPARAM)(eventMask &
                          ~(ENM_CHANGE | ENM_UPDATE | ENM_SELCHANGE |
                            ENM_SCROLL | ENM_PAGECHANGE)));
    SendMessageW(app->editor, WM_SETREDRAW, FALSE, 0);
    comments_dismiss_highlight(app);
    comments_clear(app);
    if (!apply_rtf_candidate(app, &normalized, error)) {
        originalError = *error;
        if (!normalize_rtf_memory(app, backup, backupSize,
                                  RTF_SAFETY_STRUCTURE_ONLY,
                                  &restoreCandidate, &rollbackError) ||
            !apply_rtf_candidate(app, &restoreCandidate, &rollbackError) ||
            !comments_load_rtf_memory(app, backup, backupSize,
                                      &rollbackError)) {
            originalError = rollbackError != ERROR_SUCCESS
                                ? rollbackError : ERROR_CAN_NOT_COMPLETE;
            rollbackFailed = TRUE;
        }
        editorChanged = rollbackFailed;
        goto rollback_complete;
    }
    editorChanged = TRUE;
    if (!comments_load_rtf_memory(app, data, size, error)) {
        originalError = *error;
        if (!normalize_rtf_memory(app, backup, backupSize,
                                  RTF_SAFETY_STRUCTURE_ONLY,
                                  &restoreCandidate, &rollbackError) ||
            !apply_rtf_candidate(app, &restoreCandidate, &rollbackError) ||
            !comments_load_rtf_memory(app, backup, backupSize,
                                      &rollbackError)) {
            originalError = rollbackError != ERROR_SUCCESS
                                ? rollbackError : ERROR_CAN_NOT_COMPLETE;
            rollbackFailed = TRUE;
        }
        editorChanged = rollbackFailed;
        goto rollback_complete;
    }
    if ((historyMode == DOCUMENT_HISTORY_REPLACE &&
         !history_load_rtf_memory(app, data, size, error)) ||
        (historyMode == DOCUMENT_HISTORY_MERGE &&
         !history_merge_rtf_memory(app, data, size, error)) ||
        (historyMode == DOCUMENT_HISTORY_RECONCILE &&
         !history_reconcile_rtf_memory(app, data, size, error)) ||
        (historyMode == DOCUMENT_HISTORY_CHAT_ACK &&
         !history_reconcile_chat_ack_rtf_memory(
             app, data, size, acknowledgedChats,
             acknowledgedChatCount, error))) {
        originalError = *error;
        if (!normalize_rtf_memory(app, backup, backupSize,
                                  RTF_SAFETY_STRUCTURE_ONLY,
                                  &restoreCandidate, &rollbackError) ||
            !apply_rtf_candidate(app, &restoreCandidate, &rollbackError) ||
            !comments_load_rtf_memory(app, backup, backupSize,
                                     &rollbackError)) {
            originalError = rollbackError != ERROR_SUCCESS
                                ? rollbackError : ERROR_CAN_NOT_COMPLETE;
            rollbackFailed = TRUE;
        }
        editorChanged = rollbackFailed;
        goto rollback_complete;
    }

    ZeroMemory(&lengthQuery, sizeof(lengthQuery));
    lengthQuery.flags = GTL_NUMCHARS | GTL_PRECISE;
    lengthQuery.codepage = 1200;
    textLength = SendMessageW(app->editor, EM_GETTEXTLENGTHEX,
                              (WPARAM)&lengthQuery, 0);
    if (textLength < 0) {
        textLength = 0;
    }
    if (selection.cpMin < 0) {
        selection.cpMin = 0;
    }
    if (selection.cpMax < selection.cpMin) {
        selection.cpMax = selection.cpMin;
    }
    if (selection.cpMin > textLength) {
        selection.cpMin = (LONG)textLength;
    }
    if (selection.cpMax > textLength) {
        selection.cpMax = (LONG)textLength;
    }
    SendMessageW(app->editor, EM_EXSETSEL, 0, (LPARAM)&selection);
    SendMessageW(app->editor, EM_EMPTYUNDOBUFFER, 0, 0);
    document_mark_modified(app, TRUE);
    app->currentIsRtf = TRUE;
    app->richFormattingUsed = TRUE;
    app->wordCountDirty = TRUE;
    text_engine_note_layout_change(app);
    pageview_mark_dirty(app);
    format_sync_controls(app);
    ribbon_set_active_style(app, -1);
    assist_document_changed(app);
    SendMessageW(app->editor, EM_SETEVENTMASK, 0, eventMask);
    SendMessageW(app->editor, WM_SETREDRAW, TRUE, 0);
    app->loading = priorLoading;
    if (historyMode == DOCUMENT_HISTORY_RECONCILE ||
        historyMode == DOCUMENT_HISTORY_CHAT_ACK) {
        history_cancel_pending_revision(app);
    }
    if (historyMode == DOCUMENT_HISTORY_REPLACE) {
        history_seed_if_empty(app);
    }
    app_update_status(app, TRUE);
    app_update_command_ui(app);
    InvalidateRect(app->editor, NULL, FALSE);
    InvalidateRect(app->pageView, NULL, FALSE);
    if (oldFocus != NULL && IsWindow(oldFocus)) {
        SetFocus(oldFocus);
    }
    free_memory_stream(&normalized);
    free_memory_stream(&restoreCandidate);
    HeapFree(GetProcessHeap(), 0, backup);
    return TRUE;

rollback_complete:
    SendMessageW(app->editor, EM_EMPTYUNDOBUFFER, 0, 0);
    document_mark_modified(app, wasModified || editorChanged);
    SendMessageW(app->editor, EM_EXSETSEL, 0, (LPARAM)&selection);
    app->wordCountDirty = TRUE;
    text_engine_note_layout_change(app);
    pageview_mark_dirty(app);
    format_sync_controls(app);
    assist_document_changed(app);
    SendMessageW(app->editor, EM_SETEVENTMASK, 0, eventMask);
    SendMessageW(app->editor, WM_SETREDRAW, TRUE, 0);
    app->loading = priorLoading;
    app_update_status(app, TRUE);
    app_update_command_ui(app);
    InvalidateRect(app->editor, NULL, FALSE);
    InvalidateRect(app->pageView, NULL, FALSE);
    if (oldFocus != NULL && IsWindow(oldFocus)) {
        SetFocus(oldFocus);
    }
    free_memory_stream(&normalized);
    free_memory_stream(&restoreCandidate);
    HeapFree(GetProcessHeap(), 0, backup);
    *error = originalError != ERROR_SUCCESS ? originalError : ERROR_INVALID_DATA;
    return FALSE;
}

BOOL document_apply_live_snapshot(AppState *app, const BYTE *data,
                                  SIZE_T size, DWORD *error)
{
    return document_apply_snapshot(app, data, size, error,
                                   DOCUMENT_HISTORY_REPLACE, NULL, 0u);
}

BOOL document_apply_merged_live_snapshot(AppState *app, const BYTE *data,
                                         SIZE_T size, DWORD *error)
{
    return document_apply_snapshot(app, data, size, error,
                                   DOCUMENT_HISTORY_MERGE, NULL, 0u);
}

BOOL document_apply_reconciled_live_snapshot(AppState *app,
                                             const BYTE *data, SIZE_T size,
                                             DWORD *error)
{
    return document_apply_snapshot(app, data, size, error,
                                   DOCUMENT_HISTORY_RECONCILE, NULL, 0u);
}

BOOL document_apply_acknowledged_live_snapshot(
    AppState *app, const BYTE *data, SIZE_T size,
    const HistoryChatToken *acknowledgedChats,
    SIZE_T acknowledgedChatCount, DWORD *error)
{
    return document_apply_snapshot(
        app, data, size, error, DOCUMENT_HISTORY_CHAT_ACK,
        acknowledgedChats, acknowledgedChatCount);
}

BOOL document_apply_history_snapshot(AppState *app, const BYTE *data,
                                     SIZE_T size, DWORD *error)
{
    return document_apply_snapshot(app, data, size, error,
                                   DOCUMENT_HISTORY_KEEP, NULL, 0u);
}

static BOOL create_temp_path(const WCHAR *target, WCHAR *temporary)
{
    DWORD attempt;
    for (attempt = 0; attempt < 100; ++attempt) {
        DWORD attributeError;
        HRESULT result = StringCchPrintfW(
            temporary, PATH_CAPACITY, L"%s.wordcraft-%08lX-%08lX-%02lu.tmp",
            target, GetCurrentProcessId(), GetTickCount(), attempt);
        if (FAILED(result)) {
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            return FALSE;
        }
        if (GetFileAttributesW(temporary) == INVALID_FILE_ATTRIBUTES) {
            attributeError = GetLastError();
            if (attributeError == ERROR_FILE_NOT_FOUND) {
                return TRUE;
            }
            SetLastError(attributeError);
            return FALSE;
        }
    }
    SetLastError(ERROR_FILE_EXISTS);
    return FALSE;
}

static const WCHAR *document_basename(const AppState *app)
{
    const WCHAR *slash;
    const WCHAR *forwardSlash;
    if (app->currentPath[0] == L'\0') {
        return L"Untitled";
    }
    slash = wcsrchr(app->currentPath, L'\\');
    forwardSlash = wcsrchr(app->currentPath, L'/');
    if (forwardSlash != NULL && (slash == NULL || forwardSlash > slash)) {
        slash = forwardSlash;
    }
    return slash != NULL ? slash + 1 : app->currentPath;
}

static BOOL commit_temp_file(const WCHAR *temporary, const WCHAR *target, DWORD *error)
{
    DWORD attributes = GetFileAttributesW(target);
    BOOL targetExists = attributes != INVALID_FILE_ATTRIBUTES;

    if (targetExists) {
        if (ReplaceFileW(target, temporary, NULL, REPLACEFILE_IGNORE_MERGE_ERRORS,
                         NULL, NULL)) {
            return TRUE;
        }
        if (MoveFileExW(temporary, target,
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return TRUE;
        }
    } else if (MoveFileExW(temporary, target, MOVEFILE_WRITE_THROUGH)) {
        return TRUE;
    }
    *error = GetLastError();
    return FALSE;
}

static BOOL save_to_path(AppState *app, const WCHAR *path, BOOL rtf)
{
    WCHAR temporary[PATH_CAPACITY];
    DWORD error = ERROR_SUCCESS;
    BOOL success;
    DocumentIdentity identityAtStart;
    DocumentIdentity identityBeforeCommit;
    BOOL existedAtStart;
    BOOL existsBeforeCommit;
    SIZE_T commentCount;
    SIZE_T chatCount;
    SIZE_T versionCount;

    if (!history_flush_pending(app)) {
        app_show_error(app->mainWindow,
                       L"The current version could not be added to history.",
                       GetLastError() != ERROR_SUCCESS
                           ? GetLastError() : ERROR_CAN_NOT_COMPLETE);
        return FALSE;
    }
    commentCount = comments_count(app);
    chatCount = history_chat_count(app);
    versionCount = history_version_count(app);

    if (!rtf && (app->richFormattingUsed || commentCount > 0 ||
                 chatCount > 0 || versionCount > 1)) {
        WCHAR warning[768];
        StringCchPrintfW(
            warning, ARRAYSIZE(warning),
            L"Plain text cannot preserve formatting or WordCraft review "
            L"metadata.\n\nThis document currently has %llu comment%s, "
            L"%llu chat message%s, and %llu saved version%s. Saving as "
            L"plain text will remove that review history from the open "
            L"document.\n\nSave a plain-text copy anyway?",
            (unsigned long long)commentCount,
            commentCount == 1 ? L"" : L"s",
            (unsigned long long)chatCount,
            chatCount == 1 ? L"" : L"s",
            (unsigned long long)versionCount,
            versionCount == 1 ? L"" : L"s");
        int choice = MessageBoxW(
            app->mainWindow, warning,
            APP_NAME, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2);
        if (choice != IDOK) {
            return FALSE;
        }
    }

    existedAtStart = query_file_identity(path, &identityAtStart);

    if (!create_temp_path(path, temporary)) {
        app_show_error(app->mainWindow, L"A temporary file could not be created for saving.",
                       GetLastError());
        return FALSE;
    }
    success = rtf ? write_rtf_file(app, temporary, &error)
                  : write_text_file(app, temporary, &error);
    if (!success) {
        DeleteFileW(temporary);
        app_show_error(app->mainWindow, L"The document could not be written.", error);
        return FALSE;
    }
    existsBeforeCommit = query_file_identity(path, &identityBeforeCommit);
    if (existedAtStart != existsBeforeCommit ||
        (existedAtStart &&
         !identities_match(&identityAtStart, &identityBeforeCommit))) {
        int choice = MessageBoxW(
            app->mainWindow,
            L"The destination changed while WordCraft was preparing this save.\n\n"
            L"Replace the newer external version anyway?",
            APP_NAME, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
        if (choice != IDYES) {
            DeleteFileW(temporary);
            return FALSE;
        }
    }
    if (!commit_temp_file(temporary, path, &error)) {
        DeleteFileW(temporary);
        app_show_error(app->mainWindow, L"The saved document could not replace the destination file.",
                       error);
        return FALSE;
    }
    if (path != app->currentPath &&
        FAILED(StringCchCopyW(app->currentPath, ARRAYSIZE(app->currentPath), path))) {
        app_show_error(app->mainWindow,
                       L"The document was saved, but its full path is too long for this session.",
                       ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }
    app->currentIsRtf = rtf;
    if (!rtf && commentCount > 0) {
        comments_clear(app);
    }
    if (!rtf && (chatCount > 0 || versionCount > 0)) {
        history_clear(app);
        history_seed_if_empty(app);
    }
    query_file_identity(path, &app->fileIdentity);
    document_mark_modified(app, FALSE);
    app_set_status_message(app, L"Document saved");
    return TRUE;
}

void document_update_title(AppState *app)
{
    const WCHAR *name = document_basename(app);
    WCHAR title[1024];

    if (FAILED(StringCchPrintfW(title, ARRAYSIZE(title), L"%.900s%s - %s",
                                name, app->modified ? L" *" : L"", APP_NAME))) {
        StringCchCopyW(title, ARRAYSIZE(title), APP_NAME);
    }
    SetWindowTextW(app->mainWindow, title);
}

void document_mark_modified(AppState *app, BOOL modified)
{
    if (app->modified != modified) {
        app->modified = modified;
        document_update_title(app);
    }
    SendMessageW(app->editor, EM_SETMODIFY, modified, 0);
    if (modified && !app->loading) {
        history_note_document_changed(app);
        live_share_document_changed(app);
    }
}

void document_mark_metadata_modified(AppState *app)
{
    if (app == NULL || app->editor == NULL) {
        return;
    }
    if (!app->modified) {
        app->modified = TRUE;
        document_update_title(app);
    }
    SendMessageW(app->editor, EM_SETMODIFY, TRUE, 0);
}

BOOL document_prompt_save(AppState *app)
{
    int choice;
    WCHAR prompt[512];
    const WCHAR *name = document_basename(app);

    if (!app->modified) {
        return TRUE;
    }
    if (FAILED(StringCchPrintfW(prompt, ARRAYSIZE(prompt),
                                L"Do you want to save changes to:\n%.400s?", name))) {
        StringCchCopyW(prompt, ARRAYSIZE(prompt), L"Do you want to save this document?");
    }
    choice = MessageBoxW(app->mainWindow, prompt, APP_NAME,
                         MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON1);
    if (choice == IDYES) {
        return document_save(app);
    }
    return choice == IDNO;
}

BOOL document_new(AppState *app, BOOL askToSave)
{
    if (askToSave && !document_prompt_save(app)) {
        return FALSE;
    }
    comments_clear(app);
    history_clear(app);
    app->loading = TRUE;
    SetWindowTextW(app->editor, L"");
    format_initialize_document(app);
    app->loading = FALSE;
    app->currentPath[0] = L'\0';
    ZeroMemory(&app->fileIdentity, sizeof(app->fileIdentity));
    app->currentIsRtf = TRUE;
    app->richFormattingUsed = FALSE;
    SendMessageW(app->editor, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(app->editor, EM_SETSEL, 0, 0);
    document_mark_modified(app, FALSE);
    history_seed_if_empty(app);
    app->wordCountDirty = TRUE;
    app_update_status(app, TRUE);
    format_sync_controls(app);
    ribbon_set_active_style(app, WORDCRAFT_STYLE_NORMAL);
    app_update_command_ui(app);
    assist_document_changed(app);
    SetFocus(app->editor);
    return TRUE;
}

BOOL document_open_path(AppState *app, const WCHAR *path, BOOL askToSave)
{
    DWORD attributes;
    DWORD error = ERROR_SUCCESS;
    BOOL rtf;
    BOOL rtfHeader = FALSE;
    BOOL success = FALSE;
    MemoryStreamContext candidate;
    BYTE *bytes = NULL;
    SIZE_T byteCount = 0;
    WCHAR *text = NULL;
    DocumentIdentity loadedIdentity;

    attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        app_show_error(app->mainWindow, L"The selected document could not be opened.",
                       attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_ACCESS_DENIED);
        return FALSE;
    }
    if (askToSave && !document_prompt_save(app)) {
        return FALSE;
    }

    if (!sniff_rtf_header(path, &rtfHeader, &error)) {
        app_show_error(app->mainWindow, L"The selected document could not be read.", error);
        return FALSE;
    }
    rtf = is_rtf_path(path) || rtfHeader;
    /* A candidate load can replace and then roll back the RichEdit backing
     * store.  Dismiss display-only comment state first so its live TOM range
     * can never outlive the text generation it was created from. */
    comments_dismiss_highlight(app);
    ZeroMemory(&candidate, sizeof(candidate));
    if (rtf) {
        if (load_rtf_candidate(app, path, &candidate, &loadedIdentity, &error)) {
            success = apply_rtf_candidate(app, &candidate, &error);
        }
        free_memory_stream(&candidate);
    } else if (read_entire_file(path, &bytes, &byteCount, &loadedIdentity, &error)) {
        if (decode_text(bytes, byteCount, &text, &error)) {
            success = apply_text_candidate(app, text, &error);
            HeapFree(GetProcessHeap(), 0, text);
        }
        if (bytes != NULL) {
            HeapFree(GetProcessHeap(), 0, bytes);
        }
    }
    if (!success) {
        app_show_error(app->mainWindow,
                       rtf ? L"The RTF document could not be read."
                           : L"The text document is not valid UTF-8 or UTF-16, or could not be read.",
                       error);
        return FALSE;
    }
    if (FAILED(StringCchCopyW(app->currentPath, ARRAYSIZE(app->currentPath), path))) {
        app_show_error(app->mainWindow, L"The selected path is too long.",
                       ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }
    if (rtf) {
        DWORD commentError = ERROR_SUCCESS;
        DWORD historyError = ERROR_SUCCESS;
        if (!comments_load_rtf_file(app, path, &commentError)) {
            comments_clear(app);
            MessageBoxW(
                app->mainWindow,
                L"The document text opened successfully, but its WordCraft "
                L"comment metadata was invalid and could not be loaded.",
                APP_NAME, MB_OK | MB_ICONWARNING);
        }
        if (!history_load_rtf_file(app, path, &historyError)) {
            history_clear(app);
            MessageBoxW(
                app->mainWindow,
                L"The document text opened successfully, but its WordCraft "
                L"chat or version-history metadata was invalid and could not "
                L"be loaded.",
                APP_NAME, MB_OK | MB_ICONWARNING);
        }
    } else {
        comments_clear(app);
        history_clear(app);
    }
    app->currentIsRtf = rtf;
    app->richFormattingUsed = rtf;
    app->fileIdentity = loadedIdentity;
    SendMessageW(app->editor, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(app->editor, EM_SETSEL, 0, 0);
    SendMessageW(app->editor, EM_SCROLLCARET, 0, 0);
    document_mark_modified(app, FALSE);
    history_seed_if_empty(app);
    app->wordCountDirty = TRUE;
    text_engine_note_layout_change(app);
    pageview_mark_dirty(app);
    app_update_status(app, TRUE);
    format_sync_controls(app);
    ribbon_set_active_style(app, rtf ? -1 : WORDCRAFT_STYLE_NORMAL);
    app_update_command_ui(app);
    assist_document_changed(app);
    SetFocus(app->editor);
    return TRUE;
}

BOOL document_open_dialog(AppState *app)
{
    OPENFILENAMEW dialog;
    WCHAR *path = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                            PATH_CAPACITY * sizeof(WCHAR));
    BOOL result = FALSE;
    if (path == NULL) {
        app_show_error(app->mainWindow, L"Not enough memory to open a document.",
                       ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    ZeroMemory(&dialog, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = app->mainWindow;
    dialog.lpstrFilter = openFilter;
    dialog.lpstrFile = path;
    dialog.nMaxFile = PATH_CAPACITY;
    dialog.nFilterIndex = 1;
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                   OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog)) {
        result = document_open_path(app, path, TRUE);
    } else if (CommDlgExtendedError() != 0) {
        app_show_error(app->mainWindow, L"The Open dialog failed.", CommDlgExtendedError());
    }
    HeapFree(GetProcessHeap(), 0, path);
    return result;
}

BOOL document_save(AppState *app)
{
    DocumentIdentity currentIdentity;
    if (app->currentPath[0] == L'\0') {
        return document_save_as(app);
    }
    if (!app->modified) {
        return TRUE;
    }
    if (app->fileIdentity.valid &&
        (!query_file_identity(app->currentPath, &currentIdentity) ||
         !identities_match(&app->fileIdentity, &currentIdentity))) {
        int choice = MessageBoxW(
            app->mainWindow,
            L"This file was changed or removed outside WordCraft.\n\n"
            L"Yes: overwrite the external version\n"
            L"No: save your document under a different name\n"
            L"Cancel: return to the editor",
            APP_NAME, MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON3);
        if (choice == IDNO) {
            return document_save_as(app);
        }
        if (choice != IDYES) {
            return FALSE;
        }
    }
    return save_to_path(app, app->currentPath, app->currentIsRtf);
}

BOOL document_save_as(AppState *app)
{
    OPENFILENAMEW dialog;
    WCHAR *path = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                            PATH_CAPACITY * sizeof(WCHAR));
    BOOL result = FALSE;
    UINT filterIndex;
    BOOL saveAsRtf;
    BOOL pathChanged = FALSE;

    if (path == NULL) {
        app_show_error(app->mainWindow, L"Not enough memory to save the document.",
                       ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    if (app->currentPath[0] != L'\0') {
        StringCchCopyW(path, PATH_CAPACITY, app->currentPath);
    }
    filterIndex = app->currentPath[0] == L'\0' || app->currentIsRtf ? 1u : 2u;
    ZeroMemory(&dialog, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = app->mainWindow;
    dialog.lpstrFilter = openFilter;
    dialog.lpstrFile = path;
    dialog.nMaxFile = PATH_CAPACITY;
    dialog.nFilterIndex = filterIndex;
    dialog.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST |
                   OFN_HIDEREADONLY | OFN_NOCHANGEDIR;

    if (GetSaveFileNameW(&dialog)) {
        if (dialog.nFilterIndex == 1) {
            saveAsRtf = TRUE;
            if (!is_rtf_path(path)) {
                if (FAILED(StringCchCatW(path, PATH_CAPACITY, L".rtf"))) {
                    app_show_error(app->mainWindow, L"The selected path is too long.",
                                   ERROR_FILENAME_EXCED_RANGE);
                    HeapFree(GetProcessHeap(), 0, path);
                    return FALSE;
                }
                pathChanged = TRUE;
            }
        } else if (dialog.nFilterIndex == 2) {
            saveAsRtf = FALSE;
            if (!is_txt_path(path)) {
                if (FAILED(StringCchCatW(path, PATH_CAPACITY, L".txt"))) {
                    app_show_error(app->mainWindow, L"The selected path is too long.",
                                   ERROR_FILENAME_EXCED_RANGE);
                    HeapFree(GetProcessHeap(), 0, path);
                    return FALSE;
                }
                pathChanged = TRUE;
            }
        } else {
            saveAsRtf = is_rtf_path(path) || !path_has_extension(path);
        }
        if (!path_has_extension(path)) {
            const WCHAR *extension = saveAsRtf ? L".rtf" : L".txt";
            if (FAILED(StringCchCatW(path, PATH_CAPACITY, extension))) {
                app_show_error(app->mainWindow, L"The selected path is too long.",
                               ERROR_FILENAME_EXCED_RANGE);
                HeapFree(GetProcessHeap(), 0, path);
                return FALSE;
            }
            pathChanged = TRUE;
        }
        if (pathChanged && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
            int overwrite = MessageBoxW(
                app->mainWindow,
                L"A file already exists at the normalized document path.\n\nReplace it?",
                APP_NAME, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
            if (overwrite != IDYES) {
                HeapFree(GetProcessHeap(), 0, path);
                return FALSE;
            }
        }
        result = save_to_path(app, path, saveAsRtf);
    } else if (CommDlgExtendedError() != 0) {
        app_show_error(app->mainWindow, L"The Save As dialog failed.", CommDlgExtendedError());
    }
    HeapFree(GetProcessHeap(), 0, path);
    return result;
}
