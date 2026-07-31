#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _RICHEDIT_VER 0x0500
#include <windows.h>
#include <richedit.h>
#include <stdio.h>

int main(void)
{
    HMODULE rich = LoadLibraryW(L"Msftedit.dll");
    HWND parent;
    HWND editor;
    LONG_PTR style;
    LRESULT noWrapLines;
    LRESULT wrapLines;
    LRESULT restoredLines;
    if (rich == NULL) {
        return 10;
    }
    parent = CreateWindowExW(0, L"STATIC", L"probe", WS_OVERLAPPED,
                             0, 0, 240, 200, NULL, NULL, GetModuleHandleW(NULL), NULL);
    editor = CreateWindowExW(0, MSFTEDIT_CLASS, NULL,
                             WS_CHILD | WS_VISIBLE | WS_HSCROLL | ES_MULTILINE |
                                 ES_AUTOHSCROLL,
                             0, 0, 200, 150, parent, NULL, GetModuleHandleW(NULL), NULL);
    if (parent == NULL || editor == NULL) {
        return 11;
    }
    SetWindowTextW(editor,
                   L"one two three four five six seven eight nine ten eleven twelve "
                   L"thirteen fourteen fifteen sixteen seventeen eighteen");
    noWrapLines = SendMessageW(editor, EM_GETLINECOUNT, 0, 0);
    style = GetWindowLongPtrW(editor, GWL_STYLE);
    style &= ~(LONG_PTR)(WS_HSCROLL | ES_AUTOHSCROLL);
    SetWindowLongPtrW(editor, GWL_STYLE, style);
    SendMessageW(editor, EM_SETTARGETDEVICE, 0, 0);
    SetWindowPos(editor, NULL, 0, 0, 200, 150,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    wrapLines = SendMessageW(editor, EM_GETLINECOUNT, 0, 0);
    style = GetWindowLongPtrW(editor, GWL_STYLE);
    style |= WS_HSCROLL | ES_AUTOHSCROLL;
    SetWindowLongPtrW(editor, GWL_STYLE, style);
    SendMessageW(editor, EM_SETTARGETDEVICE, 0, 1);
    SetWindowPos(editor, NULL, 0, 0, 200, 150,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    restoredLines = SendMessageW(editor, EM_GETLINECOUNT, 0, 0);
    printf("nowrap=%lld wrap=%lld restored=%lld\n",
           (long long)noWrapLines, (long long)wrapLines, (long long)restoredLines);
    DestroyWindow(parent);
    FreeLibrary(rich);
    return noWrapLines == 1 && wrapLines > 1 && restoredLines == 1 ? 0 : 1;
}
