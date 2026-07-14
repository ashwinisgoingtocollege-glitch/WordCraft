#include "editor.h"

#include <limits.h>

BOOL editor_get_text_length(HWND editor, BOOL useCrlf, SIZE_T *length, DWORD *error)
{
    GETTEXTLENGTHEX options;
    LRESULT result;

    if (editor == NULL || length == NULL || error == NULL) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return FALSE;
    }
    ZeroMemory(&options, sizeof(options));
    options.flags = GTL_PRECISE | GTL_NUMCHARS | (useCrlf ? GTL_USECRLF : 0);
    options.codepage = 1200;
    result = SendMessageW(editor, EM_GETTEXTLENGTHEX, (WPARAM)&options, 0);
    if (result < 0) {
        *error = ERROR_INVALID_DATA;
        return FALSE;
    }
    *length = (SIZE_T)result;
    return TRUE;
}

BOOL editor_get_all_text(HWND editor, BOOL useCrlf, WCHAR **text,
                         SIZE_T *length, DWORD *error)
{
    GETTEXTEX options;
    WCHAR *buffer;
    SIZE_T expected;
    SIZE_T bytes;
    LRESULT copied;

    if (text == NULL || length == NULL || error == NULL) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return FALSE;
    }
    *text = NULL;
    *length = 0;
    if (!editor_get_text_length(editor, useCrlf, &expected, error)) {
        return FALSE;
    }
    if (expected > ((SIZE_T)MAXDWORD / sizeof(WCHAR)) - 1 ||
        expected > INT_MAX) {
        *error = ERROR_FILE_TOO_LARGE;
        return FALSE;
    }
    bytes = (expected + 1) * sizeof(WCHAR);
    buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
    if (buffer == NULL) {
        *error = ERROR_NOT_ENOUGH_MEMORY;
        return FALSE;
    }

    ZeroMemory(&options, sizeof(options));
    options.cb = (DWORD)bytes;
    options.flags = GT_DEFAULT | (useCrlf ? GT_USECRLF : 0);
    options.codepage = 1200;
    options.lpDefaultChar = NULL;
    options.lpUsedDefChar = NULL;
    copied = SendMessageW(editor, EM_GETTEXTEX, (WPARAM)&options, (LPARAM)buffer);
    if (copied < 0 || (SIZE_T)copied != expected) {
        HeapFree(GetProcessHeap(), 0, buffer);
        *error = ERROR_INVALID_DATA;
        return FALSE;
    }
    buffer[expected] = L'\0';
    *text = buffer;
    *length = expected;
    return TRUE;
}
