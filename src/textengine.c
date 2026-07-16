#include "editor.h"

#include <limits.h>

/*
 * WordCraft deliberately keeps Microsoft Rich Edit as the single owner of
 * text storage, hit testing, selection, IME input, RTF, and pagination.  This
 * module is the typography policy layer above it.  Using one layout authority
 * keeps the live page, cached page previews, and printed pages identical.
 */
struct TextEngineContext {
    BOOL enabled;
    int backend;
    UINT typographyOptions;
    BYTE lineSpacingRule;
    LONG lineSpacing;
    LONG paragraphSpaceAfter;
    UINT layoutGeneration;
};

static UINT text_engine_get_typography_options(HWND editor)
{
    if (editor == NULL || !IsWindow(editor)) {
        return 0;
    }
    return (UINT)SendMessageW(editor, EM_GETTYPOGRAPHYOPTIONS, 0, 0);
}

BOOL text_engine_initialize(AppState *app)
{
    TextEngineContext *engine;
    CHARRANGE selection;
    BOOL wasModified;
    UINT options;

    if (app == NULL || app->editor == NULL || !IsWindow(app->editor)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (app->textEngine != NULL) {
        return TRUE;
    }

    engine = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*engine));
    if (engine == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    /*
     * Advanced typography selects Rich Edit's full line breaker and formatter.
     * Explicitly masking TO_SIMPLELINEBREAK off prevents the faster, reduced
     * line-breaking path from being selected later.
     */
    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
    wasModified = (BOOL)SendMessageW(app->editor, EM_GETMODIFY, 0, 0);
    SendMessageW(app->editor, EM_SETTYPOGRAPHYOPTIONS,
                 TO_ADVANCEDTYPOGRAPHY,
                 TO_ADVANCEDTYPOGRAPHY | TO_SIMPLELINEBREAK);
    SendMessageW(app->editor, EM_EXSETSEL, 0, (LPARAM)&selection);
    SendMessageW(app->editor, EM_SETMODIFY, wasModified, 0);
    options = text_engine_get_typography_options(app->editor);

    engine->enabled = TRUE;
    engine->typographyOptions = options;
    engine->backend = (options & TO_ADVANCEDTYPOGRAPHY) != 0 &&
                              (options & TO_SIMPLELINEBREAK) == 0
                          ? TEXT_ENGINE_BACKEND_RICHEDIT_ADVANCED
                          : TEXT_ENGINE_BACKEND_RICHEDIT_COMPATIBLE;
    engine->lineSpacingRule = WORDCRAFT_DEFAULT_LINE_SPACING_RULE;
    engine->lineSpacing = WORDCRAFT_DEFAULT_LINE_SPACING;
    engine->paragraphSpaceAfter =
        WORDCRAFT_DEFAULT_PARAGRAPH_SPACE_AFTER_TWIPS;
    engine->layoutGeneration = 1;
    app->textEngine = engine;

    InvalidateRect(app->editor, NULL, FALSE);
    return TRUE;
}

void text_engine_apply_document_defaults(AppState *app)
{
    TextEngineContext *engine;
    PARAFORMAT2 paragraph;
    CHARRANGE selection;
    BOOL wasModified;

    if (app == NULL || app->editor == NULL || app->textEngine == NULL) {
        return;
    }
    engine = app->textEngine;
    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
    wasModified = (BOOL)SendMessageW(app->editor, EM_GETMODIFY, 0, 0);

    ZeroMemory(&paragraph, sizeof(paragraph));
    paragraph.cbSize = sizeof(paragraph);
    paragraph.dwMask = PFM_SPACEAFTER | PFM_LINESPACING;
    paragraph.dySpaceAfter = engine->paragraphSpaceAfter;
    paragraph.bLineSpacingRule = engine->lineSpacingRule;
    paragraph.dyLineSpacing = engine->lineSpacing;
    SendMessageW(app->editor, EM_SETPARAFORMAT, 0, (LPARAM)&paragraph);

    SendMessageW(app->editor, EM_EXSETSEL, 0, (LPARAM)&selection);
    SendMessageW(app->editor, EM_SETMODIFY, wasModified, 0);
    text_engine_note_layout_change(app);
}

void text_engine_note_layout_change(AppState *app)
{
    TextEngineContext *engine;

    if (app == NULL || app->textEngine == NULL) {
        return;
    }
    engine = app->textEngine;
    if (engine->layoutGeneration == UINT_MAX) {
        engine->layoutGeneration = 1;
    } else {
        ++engine->layoutGeneration;
    }
}

LRESULT text_engine_query_state(const AppState *app, UINT query)
{
    const TextEngineContext *engine;

    if (app == NULL || app->textEngine == NULL) {
        return 0;
    }
    engine = app->textEngine;
    switch (query) {
    case WCQ_TEXT_ENGINE_ENABLED:
        return engine->enabled;
    case WCQ_TEXT_ENGINE_BACKEND:
        return engine->backend;
    case WCQ_TEXT_ENGINE_TYPOGRAPHY_OPTIONS:
        return text_engine_get_typography_options(app->editor);
    case WCQ_TEXT_ENGINE_LINE_SPACING_RULE:
        return engine->lineSpacingRule;
    case WCQ_TEXT_ENGINE_LINE_SPACING:
        return engine->lineSpacing;
    case WCQ_TEXT_ENGINE_PARAGRAPH_SPACE_AFTER:
        return engine->paragraphSpaceAfter;
    case WCQ_TEXT_ENGINE_LAYOUT_GENERATION:
        return engine->layoutGeneration;
    default:
        return 0;
    }
}

void text_engine_shutdown(AppState *app)
{
    if (app == NULL || app->textEngine == NULL) {
        return;
    }
    HeapFree(GetProcessHeap(), 0, app->textEngine);
    app->textEngine = NULL;
}
