#include "editor.h"

#include <limits.h>
#include <string.h>

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
    input->position = 0;
    ZeroMemory(&stream, sizeof(stream));
    stream.dwCookie = (DWORD_PTR)input;
    stream.pfnCallback = memory_read_callback;
    SendMessageW(editor, EM_STREAMIN, SF_RTF, (LPARAM)&stream);
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
    SendMessageW(temporaryEditor, EM_EXLIMITTEXT, 0, 0x7FFFFFFE);
    ZeroMemory(&fileContext, sizeof(fileContext));
    fileContext.file = file;
    ZeroMemory(&input, sizeof(input));
    input.dwCookie = (DWORD_PTR)&fileContext;
    input.pfnCallback = file_read_callback;
    SendMessageW(temporaryEditor, EM_STREAMIN, SF_RTF, (LPARAM)&input);
    if (input.dwError != 0 || fileContext.error != ERROR_SUCCESS) {
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

static BOOL apply_rtf_candidate(AppState *app, MemoryStreamContext *candidate, DWORD *error)
{
    MemoryStreamContext backup;
    EDITSTREAM input;
    BOOL success;
    BOOL restoreFailed = FALSE;

    if (!capture_rtf(app->editor, &backup, error)) {
        return FALSE;
    }
    app->loading = TRUE;
    SetWindowTextW(app->editor, L"");
    candidate->position = 0;
    ZeroMemory(&input, sizeof(input));
    input.dwCookie = (DWORD_PTR)candidate;
    input.pfnCallback = memory_read_callback;
    SendMessageW(app->editor, EM_STREAMIN, SF_RTF, (LPARAM)&input);
    success = input.dwError == 0;
    if (!success) {
        *error = input.dwError;
        SetWindowTextW(app->editor, L"");
        if (!restore_rtf(app->editor, &backup)) {
            *error = ERROR_NOT_ENOUGH_MEMORY;
            restoreFailed = TRUE;
        }
    }
    app->loading = FALSE;
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
    SetLastError(ERROR_SUCCESS);
    if (!capture_rtf(app->editor, &backup, error)) {
        return FALSE;
    }
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
    app->loading = FALSE;
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
    BYTE *withComments = NULL;
    SIZE_T withCommentsSize = 0;
    BOOL success = FALSE;

    if (!capture_rtf(app->editor, &rtf, error)) {
        return FALSE;
    }
    if (!comments_embed_rtf(app, rtf.data, rtf.size, &withComments,
                            &withCommentsSize, error)) {
        free_memory_stream(&rtf);
        return FALSE;
    }
    free_memory_stream(&rtf);

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
    SIZE_T commentCount = comments_count(app);

    if (!rtf && (app->richFormattingUsed || commentCount > 0)) {
        WCHAR warning[512];
        if (commentCount > 0) {
            StringCchPrintfW(
                warning, ARRAYSIZE(warning),
                commentCount == 1
                    ? L"Plain text cannot preserve fonts, colors, alignment, "
                      L"bullets, or the document's 1 comment.\n\n"
                      L"Save a plain-text copy anyway?"
                    : L"Plain text cannot preserve fonts, colors, alignment, "
                      L"bullets, or the document's %llu comments.\n\n"
                      L"Save a plain-text copy anyway?",
                (unsigned long long)commentCount);
        } else {
            StringCchCopyW(
                warning, ARRAYSIZE(warning),
                L"Plain text cannot preserve fonts, colors, alignment, "
                L"bullets, or other formatting.\n\n"
                L"Save a plain-text copy anyway?");
        }
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
    app->wordCountDirty = TRUE;
    app_update_status(app, TRUE);
    format_sync_controls(app);
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
        if (!comments_load_rtf_file(app, path, &commentError)) {
            comments_clear(app);
            MessageBoxW(
                app->mainWindow,
                L"The document text opened successfully, but its WordCraft "
                L"comment metadata was invalid and could not be loaded.",
                APP_NAME, MB_OK | MB_ICONWARNING);
        }
    } else {
        comments_clear(app);
    }
    app->currentIsRtf = rtf;
    app->richFormattingUsed = rtf;
    app->fileIdentity = loadedIdentity;
    SendMessageW(app->editor, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(app->editor, EM_SETSEL, 0, 0);
    SendMessageW(app->editor, EM_SCROLLCARET, 0, 0);
    document_mark_modified(app, FALSE);
    app->wordCountDirty = TRUE;
    text_engine_note_layout_change(app);
    pageview_mark_dirty(app);
    app_update_status(app, TRUE);
    format_sync_controls(app);
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
