#include "editor.h"

#include <stdio.h>

static BOOL probe_text_and_selection_unchanged(HWND editor,
                                               const WCHAR *expectedText,
                                               LONG expectedStart,
                                               LONG expectedEnd,
                                               BOOL expectedModified)
{
    WCHAR text[256];
    CHARRANGE selection;

    ZeroMemory(text, sizeof(text));
    ZeroMemory(&selection, sizeof(selection));
    GetWindowTextW(editor, text, ARRAYSIZE(text));
    SendMessageW(editor, EM_EXGETSEL, 0, (LPARAM)&selection);
    return wcscmp(text, expectedText) == 0 &&
           selection.cpMin == expectedStart &&
           selection.cpMax == expectedEnd &&
           (SendMessageW(editor, EM_GETMODIFY, 0, 0) != 0) ==
               (expectedModified != FALSE);
}

int wmain(void)
{
    static const WCHAR sample[] =
        L"office affinity caf\x00E9 cafe\x0301 \xD83D\xDE00 "
        L"\x0627\x0644\x0639\x0631\x0628\x064A\x0629";
    HMODULE richEditModule = NULL;
    HWND editor = NULL;
    AppState app;
    PARAFORMAT2 paragraph;
    LRESULT generation;
    LRESULT nextGeneration;
    UINT options;
    int result = 1;

    ZeroMemory(&app, sizeof(app));
    richEditModule = LoadLibraryW(L"Msftedit.dll");
    if (richEditModule == NULL) {
        fwprintf(stderr, L"could not load Msftedit.dll\n");
        goto cleanup;
    }
    editor = CreateWindowExW(0, MSFTEDIT_CLASS, NULL,
                             WS_OVERLAPPED | ES_MULTILINE,
                             0, 0, 640, 480, NULL, NULL,
                             GetModuleHandleW(NULL), NULL);
    if (editor == NULL) {
        fwprintf(stderr, L"could not create the typography probe editor\n");
        goto cleanup;
    }
    app.editor = editor;
    app.richEditModule = richEditModule;

    SetWindowTextW(editor, sample);
    SendMessageW(editor, EM_SETSEL, 3, 11);
    SendMessageW(editor, EM_SETMODIFY, TRUE, 0);
    if (!text_engine_initialize(&app) ||
        !probe_text_and_selection_unchanged(editor, sample, 3, 11, TRUE)) {
        fwprintf(stderr,
                 L"initializing the text engine changed document state\n");
        goto cleanup;
    }

    options = (UINT)text_engine_query_state(
        &app, WCQ_TEXT_ENGINE_TYPOGRAPHY_OPTIONS);
    if (text_engine_query_state(&app, WCQ_TEXT_ENGINE_ENABLED) != 1 ||
        text_engine_query_state(&app, WCQ_TEXT_ENGINE_BACKEND) !=
            TEXT_ENGINE_BACKEND_RICHEDIT_ADVANCED ||
        (options & TO_ADVANCEDTYPOGRAPHY) == 0 ||
        (options & TO_SIMPLELINEBREAK) != 0) {
        fwprintf(stderr,
                 L"advanced typography was not enabled cleanly (options=0x%X)\n",
                 options);
        goto cleanup;
    }

    SetWindowTextW(editor, L"");
    SendMessageW(editor, EM_SETSEL, 0, 0);
    SendMessageW(editor, EM_SETMODIFY, FALSE, 0);
    text_engine_apply_document_defaults(&app);
    ZeroMemory(&paragraph, sizeof(paragraph));
    paragraph.cbSize = sizeof(paragraph);
    SendMessageW(editor, EM_GETPARAFORMAT, 0, (LPARAM)&paragraph);
    if ((paragraph.dwMask & (PFM_LINESPACING | PFM_SPACEAFTER)) !=
            (PFM_LINESPACING | PFM_SPACEAFTER) ||
        paragraph.bLineSpacingRule != WORDCRAFT_DEFAULT_LINE_SPACING_RULE ||
        paragraph.dyLineSpacing != WORDCRAFT_DEFAULT_LINE_SPACING ||
        paragraph.dySpaceAfter !=
            WORDCRAFT_DEFAULT_PARAGRAPH_SPACE_AFTER_TWIPS ||
        SendMessageW(editor, EM_GETMODIFY, 0, 0) != FALSE) {
        fwprintf(stderr,
                 L"Word-like paragraph defaults were not applied without modifying the document\n");
        goto cleanup;
    }

    generation = text_engine_query_state(
        &app, WCQ_TEXT_ENGINE_LAYOUT_GENERATION);
    text_engine_note_layout_change(&app);
    nextGeneration = text_engine_query_state(
        &app, WCQ_TEXT_ENGINE_LAYOUT_GENERATION);
    if (generation < 1 || nextGeneration != generation + 1 ||
        text_engine_query_state(&app, WCQ_TEXT_ENGINE_LINE_SPACING_RULE) !=
            WORDCRAFT_DEFAULT_LINE_SPACING_RULE ||
        text_engine_query_state(&app, WCQ_TEXT_ENGINE_LINE_SPACING) !=
            WORDCRAFT_DEFAULT_LINE_SPACING ||
        text_engine_query_state(
            &app, WCQ_TEXT_ENGINE_PARAGRAPH_SPACE_AFTER) !=
            WORDCRAFT_DEFAULT_PARAGRAPH_SPACE_AFTER_TWIPS) {
        fwprintf(stderr, L"text-engine state queries were inconsistent\n");
        goto cleanup;
    }

    wprintf(L"text_engine=ok advanced_typography=ok simple_breaker=off "
            L"unicode_state=preserved line_spacing=1.10 paragraph_after=6pt\n");
    result = 0;

cleanup:
    text_engine_shutdown(&app);
    text_engine_shutdown(&app);
    if (editor != NULL) {
        DestroyWindow(editor);
    }
    if (richEditModule != NULL) {
        FreeLibrary(richEditModule);
    }
    return result;
}
