#ifndef COBJMACROS
#define COBJMACROS
#endif
#include "editor.h"
#include <richole.h>
#include <tom.h>

static const IID wordcraftIidTextDocument = {
    0x8CC497C0, 0xA1DF, 0x11CE,
    {0x80, 0x98, 0x00, 0xAA, 0x00, 0x47, 0xBE, 0x5D}
};

static void seed_find_text_from_selection(AppState *app)
{
    CHARRANGE selection;
    TEXTRANGEW range;
    LONG length;

    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
    length = selection.cpMax - selection.cpMin;
    if (length <= 0 || length >= FIND_CAPACITY) {
        return;
    }
    range.chrg = selection;
    range.lpstrText = app->findText;
    app->findText[0] = L'\0';
    SendMessageW(app->editor, EM_GETTEXTRANGE, 0, (LPARAM)&range);
    app->findText[FIND_CAPACITY - 1] = L'\0';
}

void dialogs_show_find(AppState *app, BOOL replace)
{
    if (app->findDialog != NULL && IsWindow(app->findDialog)) {
        if (app->findDialogIsReplace == replace) {
            ShowWindow(app->findDialog, SW_RESTORE);
            SetForegroundWindow(app->findDialog);
            return;
        }
        SendMessageW(app->findDialog, WM_CLOSE, 0, 0);
        if (app->findDialog != NULL && IsWindow(app->findDialog)) {
            SetForegroundWindow(app->findDialog);
            return;
        }
    }

    seed_find_text_from_selection(app);
    ZeroMemory(&app->findReplace, sizeof(app->findReplace));
    app->findReplace.lStructSize = sizeof(app->findReplace);
    app->findReplace.hwndOwner = app->mainWindow;
    app->findReplace.lpstrFindWhat = app->findText;
    app->findReplace.wFindWhatLen = FIND_CAPACITY;
    app->findReplace.lpstrReplaceWith = app->replaceText;
    app->findReplace.wReplaceWithLen = FIND_CAPACITY;
    app->findReplace.Flags = FR_DOWN;
    app->findDialogIsReplace = replace;
    app->findDialog = replace ? ReplaceTextW(&app->findReplace)
                              : FindTextW(&app->findReplace);
    if (app->findDialog == NULL) {
        DWORD error = CommDlgExtendedError();
        app_show_error(app->mainWindow,
                       replace ? L"The Replace dialog could not be opened."
                               : L"The Find dialog could not be opened.",
                       error);
    }
}

static DWORD rich_edit_find_flags(const FINDREPLACEW *request)
{
    DWORD flags = 0;
    if ((request->Flags & FR_DOWN) != 0) {
        flags |= FR_DOWN;
    }
    if ((request->Flags & FR_MATCHCASE) != 0) {
        flags |= FR_MATCHCASE;
    }
    if ((request->Flags & FR_WHOLEWORD) != 0) {
        flags |= FR_WHOLEWORD;
    }
    return flags;
}

static BOOL select_next_match(AppState *app, const FINDREPLACEW *request, BOOL allowWrap)
{
    CHARRANGE selection;
    FINDTEXTEXW search;
    LONG textLength = (LONG)GetWindowTextLengthW(app->editor);
    LONG result;
    BOOL down = (request->Flags & FR_DOWN) != 0;
    BOOL wrapped = FALSE;

    if (request->lpstrFindWhat == NULL || request->lpstrFindWhat[0] == L'\0') {
        MessageBeep(MB_ICONWARNING);
        app_set_status_message(app, L"Enter text to find");
        return FALSE;
    }
    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
    ZeroMemory(&search, sizeof(search));
    search.lpstrText = request->lpstrFindWhat;
    if (down) {
        search.chrg.cpMin = selection.cpMax;
        search.chrg.cpMax = -1;
    } else {
        search.chrg.cpMin = selection.cpMin;
        search.chrg.cpMax = 0;
    }
    result = (LONG)SendMessageW(app->editor, EM_FINDTEXTEXW,
                                rich_edit_find_flags(request), (LPARAM)&search);
    if (result == -1 && allowWrap && textLength > 0) {
        wrapped = TRUE;
        if (down) {
            search.chrg.cpMin = 0;
            search.chrg.cpMax = selection.cpMin;
        } else {
            search.chrg.cpMin = textLength;
            search.chrg.cpMax = selection.cpMax;
        }
        result = (LONG)SendMessageW(app->editor, EM_FINDTEXTEXW,
                                    rich_edit_find_flags(request), (LPARAM)&search);
    }
    if (result == -1) {
        MessageBeep(MB_ICONINFORMATION);
        app_set_status_message(app, L"No match found");
        return FALSE;
    }
    SendMessageW(app->editor, EM_EXSETSEL, 0, (LPARAM)&search.chrgText);
    SendMessageW(app->editor, EM_SCROLLCARET, 0, 0);
    SetFocus(app->editor);
    app_set_status_message(app, wrapped ? L"Match found (search wrapped)" : L"Match found");
    return TRUE;
}

static BOOL selection_matches(AppState *app, const FINDREPLACEW *request)
{
    CHARRANGE selection;
    TEXTRANGEW range;
    WCHAR *selected;
    LONG length;
    BOOL equal;
    LRESULT copied;

    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
    length = selection.cpMax - selection.cpMin;
    if (length <= 0 || length != lstrlenW(request->lpstrFindWhat)) {
        return FALSE;
    }
    selected = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                         ((SIZE_T)length + 1) * sizeof(WCHAR));
    if (selected == NULL) {
        return FALSE;
    }
    range.chrg = selection;
    range.lpstrText = selected;
    copied = SendMessageW(app->editor, EM_GETTEXTRANGE, 0, (LPARAM)&range);
    if (copied != length) {
        HeapFree(GetProcessHeap(), 0, selected);
        return FALSE;
    }
    selected[length] = L'\0';
    equal = CompareStringOrdinal(selected, length, request->lpstrFindWhat, length,
                                 (request->Flags & FR_MATCHCASE) == 0) == CSTR_EQUAL;
    HeapFree(GetProcessHeap(), 0, selected);
    if (equal && (request->Flags & FR_WHOLEWORD) != 0) {
        FINDTEXTEXW wholeWordSearch;
        DWORD flags = FR_DOWN | FR_WHOLEWORD;
        LONG found;
        if ((request->Flags & FR_MATCHCASE) != 0) {
            flags |= FR_MATCHCASE;
        }
        ZeroMemory(&wholeWordSearch, sizeof(wholeWordSearch));
        wholeWordSearch.chrg = selection;
        wholeWordSearch.lpstrText = request->lpstrFindWhat;
        found = (LONG)SendMessageW(app->editor, EM_FINDTEXTEXW, flags,
                                   (LPARAM)&wholeWordSearch);
        equal = found == selection.cpMin &&
                wholeWordSearch.chrgText.cpMin == selection.cpMin &&
                wholeWordSearch.chrgText.cpMax == selection.cpMax;
    }
    return equal;
}

static ITextDocument *begin_edit_collection(HWND editor)
{
    IRichEditOle *richEditOle = NULL;
    ITextDocument *document = NULL;
    if (!SendMessageW(editor, EM_GETOLEINTERFACE, 0, (LPARAM)&richEditOle) ||
        richEditOle == NULL) {
        return NULL;
    }
    if (FAILED(richEditOle->lpVtbl->QueryInterface(
            richEditOle, &wordcraftIidTextDocument, (void **)&document))) {
        document = NULL;
    }
    richEditOle->lpVtbl->Release(richEditOle);
    if (document != NULL && FAILED(ITextDocument_BeginEditCollection(document))) {
        ITextDocument_Release(document);
        document = NULL;
    }
    return document;
}

static void end_edit_collection(ITextDocument *document)
{
    if (document != NULL) {
        ITextDocument_EndEditCollection(document);
        ITextDocument_Release(document);
    }
}

static void replace_current(AppState *app, FINDREPLACEW *request)
{
    if (request->lpstrFindWhat == NULL || request->lpstrFindWhat[0] == L'\0') {
        MessageBeep(MB_ICONWARNING);
        app_set_status_message(app, L"Enter text to find");
        return;
    }
    if (!selection_matches(app, request)) {
        select_next_match(app, request, TRUE);
        return;
    }
    SendMessageW(app->editor, EM_REPLACESEL, TRUE, (LPARAM)request->lpstrReplaceWith);
    select_next_match(app, request, TRUE);
}

static void replace_all(AppState *app, FINDREPLACEW *request)
{
    FINDTEXTEXW search;
    CHARRANGE originalSelection;
    LONG position = 0;
    LONG result;
    LONG replacementLength;
    LONG count = 0;
    WCHAR status[128];
    DWORD flags;
    ITextDocument *undoCollection;

    if (request->lpstrFindWhat == NULL || request->lpstrFindWhat[0] == L'\0') {
        MessageBeep(MB_ICONWARNING);
        app_set_status_message(app, L"Enter text to find");
        return;
    }
    replacementLength = lstrlenW(request->lpstrReplaceWith);
    flags = rich_edit_find_flags(request) | FR_DOWN;
    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&originalSelection);
    SendMessageW(app->editor, WM_SETREDRAW, FALSE, 0);
    undoCollection = begin_edit_collection(app->editor);
    if (undoCollection == NULL) {
        SendMessageW(app->editor, EM_STOPGROUPTYPING, 0, 0);
    }

    for (;;) {
        ZeroMemory(&search, sizeof(search));
        search.chrg.cpMin = position;
        search.chrg.cpMax = -1;
        search.lpstrText = request->lpstrFindWhat;
        result = (LONG)SendMessageW(app->editor, EM_FINDTEXTEXW, flags, (LPARAM)&search);
        if (result == -1) {
            break;
        }
        SendMessageW(app->editor, EM_EXSETSEL, 0, (LPARAM)&search.chrgText);
        SendMessageW(app->editor, EM_REPLACESEL, TRUE, (LPARAM)request->lpstrReplaceWith);
        position = search.chrgText.cpMin + replacementLength;
        ++count;
    }
    if (undoCollection != NULL) {
        end_edit_collection(undoCollection);
    } else {
        SendMessageW(app->editor, EM_STOPGROUPTYPING, 0, 0);
    }
    if (count == 0) {
        SendMessageW(app->editor, EM_EXSETSEL, 0, (LPARAM)&originalSelection);
    }
    SendMessageW(app->editor, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(app->editor, NULL, NULL,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    app->wordCountDirty = TRUE;
    app_update_status(app, TRUE);
    StringCchPrintfW(status, ARRAYSIZE(status),
                     count == 1 ? L"Replaced 1 match" : L"Replaced %ld matches", count);
    app_set_status_message(app, status);
    SetFocus(app->editor);
}

void dialogs_handle_find_replace(AppState *app, FINDREPLACEW *request)
{
    if (request == NULL) {
        return;
    }
    if ((request->Flags & FR_DIALOGTERM) != 0) {
        app->findDialog = NULL;
        SetFocus(app->editor);
        return;
    }
    if ((request->Flags & FR_FINDNEXT) != 0) {
        select_next_match(app, request, TRUE);
    } else if ((request->Flags & FR_REPLACE) != 0) {
        replace_current(app, request);
    } else if ((request->Flags & FR_REPLACEALL) != 0) {
        replace_all(app, request);
    }
}

void dialogs_insert_datetime(AppState *app)
{
    SYSTEMTIME time;
    WCHAR date[128];
    WCHAR clock[128];
    WCHAR combined[260];

    GetLocalTime(&time);
    if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, &time,
                        NULL, date, ARRAYSIZE(date), NULL) == 0 ||
        GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &time,
                        NULL, clock, ARRAYSIZE(clock)) == 0) {
        app_show_error(app->mainWindow, L"The current date and time could not be formatted.",
                       GetLastError());
        return;
    }
    StringCchPrintfW(combined, ARRAYSIZE(combined), L"%s %s", date, clock);
    SendMessageW(app->editor, EM_REPLACESEL, TRUE, (LPARAM)combined);
    SetFocus(app->editor);
}

void dialogs_show_about(AppState *app)
{
    MessageBoxW(
        app->mainWindow,
        L"WordCraft\n\n"
        L"A native Windows rich-text editor written in C.\n\n"
        L"It supports formatting-preserving RTF documents and Unicode text files, "
        L"plus printing, find and replace, zoom, and paragraph tools.\n\n"
        L"WordCraft is an independent sample application and is not affiliated with Microsoft.",
        L"About WordCraft", MB_OK | MB_ICONINFORMATION);
}
