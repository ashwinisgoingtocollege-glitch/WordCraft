#include "editor.h"

#include <stdio.h>
#include <string.h>

typedef struct FixedBuffer {
    BYTE *data;
    SIZE_T capacity;
    SIZE_T size;
    SIZE_T position;
} FixedBuffer;

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

int main(void)
{
    static const WCHAR source[] = L"Bold caf\x00E9 \x6F22\x5B57 plain";
    HMODULE rich = LoadLibraryW(L"Msftedit.dll");
    HWND parent;
    HWND editor;
    FixedBuffer buffer;
    EDITSTREAM stream;
    CHARFORMAT2W format;
    WCHAR restored[128];
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
    printf("rtf_unicode=ok formatting_round_trip=ok\n");
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
