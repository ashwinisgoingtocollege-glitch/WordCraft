#ifndef COBJMACROS
#define COBJMACROS
#endif
#include "editor.h"
#include <richole.h>
#include <tom.h>

#include <stdio.h>
#include <string.h>

static const IID probeIidTextDocument = {
    0x8CC497C0, 0xA1DF, 0x11CE,
    {0x80, 0x98, 0x00, 0xAA, 0x00, 0x47, 0xBE, 0x5D}
};

static BOOL probe_grouped_undo(HWND editor)
{
    IRichEditOle *richEditOle = NULL;
    ITextDocument *document = NULL;
    WCHAR text[32];
    HRESULT queryResult;

    SetWindowTextW(editor, L"one one");
    SendMessageW(editor, EM_EMPTYUNDOBUFFER, 0, 0);
    if (!SendMessageW(editor, EM_GETOLEINTERFACE, 0, (LPARAM)&richEditOle) ||
        richEditOle == NULL) {
        return FALSE;
    }
    queryResult = richEditOle->lpVtbl->QueryInterface(
        richEditOle, &probeIidTextDocument, (void **)&document);
    richEditOle->lpVtbl->Release(richEditOle);
    if (FAILED(queryResult) || document == NULL) {
        return FALSE;
    }
    if (FAILED(ITextDocument_BeginEditCollection(document))) {
        ITextDocument_Release(document);
        return FALSE;
    }
    SendMessageW(editor, EM_SETSEL, 0, 3);
    SendMessageW(editor, EM_REPLACESEL, TRUE, (LPARAM)L"two");
    SendMessageW(editor, EM_SETSEL, 4, 7);
    SendMessageW(editor, EM_REPLACESEL, TRUE, (LPARAM)L"two");
    ITextDocument_EndEditCollection(document);
    ITextDocument_Release(document);
    SendMessageW(editor, EM_UNDO, 0, 0);
    GetWindowTextW(editor, text, ARRAYSIZE(text));
    return wcscmp(text, L"one one") == 0;
}

static BOOL probe_whole_word(HWND editor)
{
    FINDTEXTEXW search;
    LONG result;
    SetWindowTextW(editor, L"foobar foo");
    ZeroMemory(&search, sizeof(search));
    search.chrg.cpMin = 0;
    search.chrg.cpMax = 3;
    search.lpstrText = L"foo";
    result = (LONG)SendMessageW(editor, EM_FINDTEXTEXW,
                                FR_DOWN | FR_WHOLEWORD, (LPARAM)&search);
    if (result != -1) {
        return FALSE;
    }
    ZeroMemory(&search, sizeof(search));
    search.chrg.cpMin = 7;
    search.chrg.cpMax = 10;
    search.lpstrText = L"foo";
    result = (LONG)SendMessageW(editor, EM_FINDTEXTEXW,
                                FR_DOWN | FR_WHOLEWORD, (LPARAM)&search);
    return result == 7 && search.chrgText.cpMin == 7 && search.chrgText.cpMax == 10;
}

int main(void)
{
    const SIZE_T sourceLength = 120000;
    HMODULE rich = LoadLibraryW(L"Msftedit.dll");
    HWND parent;
    HWND editor;
    WCHAR *source;
    WCHAR *copy = NULL;
    SIZE_T copiedLength = 0;
    DWORD error = ERROR_SUCCESS;
    SIZE_T i;
    int result = 1;

    if (rich == NULL) {
        return 10;
    }
    parent = CreateWindowExW(0, L"STATIC", L"probe", WS_OVERLAPPED,
                             0, 0, 320, 200, NULL, NULL, GetModuleHandleW(NULL), NULL);
    editor = CreateWindowExW(0, MSFTEDIT_CLASS, NULL,
                             WS_CHILD | ES_MULTILINE,
                             0, 0, 300, 180, parent, NULL, GetModuleHandleW(NULL), NULL);
    if (parent == NULL || editor == NULL) {
        result = 11;
        goto cleanup;
    }
    SendMessageW(editor, EM_EXLIMITTEXT, 0, 0x7FFFFFFE);
    source = HeapAlloc(GetProcessHeap(), 0, (sourceLength + 1) * sizeof(WCHAR));
    if (source == NULL) {
        result = 12;
        goto cleanup;
    }
    for (i = 0; i < sourceLength; ++i) {
        source[i] = (i % 17 == 16) ? L' ' : (WCHAR)(L'a' + (i % 26));
    }
    source[sourceLength] = L'\0';
    if (!SetWindowTextW(editor, source) ||
        !editor_get_all_text(editor, FALSE, &copy, &copiedLength, &error) ||
        copiedLength != sourceLength ||
        memcmp(source, copy, (sourceLength + 1) * sizeof(WCHAR)) != 0) {
        fwprintf(stderr, L"large-text extraction failed (length=%llu, error=%lu)\n",
                 (unsigned long long)copiedLength, error);
        HeapFree(GetProcessHeap(), 0, source);
        goto cleanup;
    }
    HeapFree(GetProcessHeap(), 0, copy);
    copy = NULL;
    HeapFree(GetProcessHeap(), 0, source);

    if (!SetWindowTextW(editor, L"alpha\r\nbeta") ||
        !editor_get_all_text(editor, TRUE, &copy, &copiedLength, &error) ||
        wcscmp(copy, L"alpha\r\nbeta") != 0) {
        fwprintf(stderr, L"CRLF extraction failed (length=%llu, error=%lu)\n",
                 (unsigned long long)copiedLength, error);
        goto cleanup;
    }
    HeapFree(GetProcessHeap(), 0, copy);
    copy = NULL;
    if (!probe_grouped_undo(editor)) {
        fwprintf(stderr, L"grouped undo probe failed\n");
        goto cleanup;
    }
    if (!probe_whole_word(editor)) {
        fwprintf(stderr, L"whole-word probe failed\n");
        goto cleanup;
    }
    printf("large_text=%llu crlf=ok grouped_undo=ok whole_word=ok\n",
           (unsigned long long)sourceLength);
    result = 0;

cleanup:
    if (copy != NULL) {
        HeapFree(GetProcessHeap(), 0, copy);
    }
    if (parent != NULL) {
        DestroyWindow(parent);
    }
    FreeLibrary(rich);
    return result;
}
