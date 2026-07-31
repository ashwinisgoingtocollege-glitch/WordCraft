#ifndef COBJMACROS
#define COBJMACROS
#endif

#include "editor.h"

#include <limits.h>
#include <stdint.h>
#include <richole.h>
#include <stdlib.h>
#include <string.h>
#include <tom.h>

#define COMMENT_COUNT_LIMIT 1024
#define COMMENT_METADATA_LIMIT (16u * 1024u * 1024u)
#define COMMENT_FILE_LIMIT ((ULONGLONG)512u * 1024u * 1024u)
#define COMMENT_NO_ACTIVE ((SIZE_T)-1)

typedef struct CommentEntry {
    ITextRange *anchor;
    WCHAR text[COMMENT_TEXT_CAPACITY + 1];
} CommentEntry;

struct CommentStore {
    ITextDocument *document;
    CommentEntry *entries;
    SIZE_T count;
    SIZE_T capacity;
    SIZE_T active;
    ITextRange *draftAnchor;
    ITextRange *highlightAnchor;
    SIZE_T highlighted;
    BOOL compositionActive;
    BOOL selectionHidden;
    BOOL selectingAnchor;
    BOOL marginVisible;
};

typedef struct CommentCardLayout {
    SIZE_T entryIndex;
    LONG anchorStart;
    LONG anchorEnd;
    LONG page;
    POINT anchorPoint;
    RECT margin;
    RECT rect;
} CommentCardLayout;

typedef struct ParsedComment {
    LONG start;
    LONG end;
    WCHAR text[COMMENT_TEXT_CAPACITY + 1];
} ParsedComment;

typedef struct ByteBuilder {
    BYTE *data;
    SIZE_T size;
    SIZE_T capacity;
} ByteBuilder;

static const IID wordcraftIidTextDocument = {
    0x8CC497C0, 0xA1DF, 0x11CE,
    {0x80, 0x98, 0x00, 0xAA, 0x00, 0x47, 0xBE, 0x5D}
};

static const BYTE commentMetadataPrefix[] =
    "{\\*\\wordcraftcomments v1;";

static ITextDocument *comments_get_document(HWND editor)
{
    IRichEditOle *richEditOle = NULL;
    ITextDocument *document = NULL;

    if (editor == NULL ||
        !SendMessageW(editor, EM_GETOLEINTERFACE, 0,
                      (LPARAM)&richEditOle) || richEditOle == NULL) {
        return NULL;
    }
    richEditOle->lpVtbl->QueryInterface(richEditOle,
                                       &wordcraftIidTextDocument,
                                       (void **)&document);
    richEditOle->lpVtbl->Release(richEditOle);
    return document;
}

static BOOL comments_valid(const AppState *app)
{
    return app != NULL && app->comments != NULL &&
           app->comments->document != NULL;
}

static BOOL comment_text_length(const WCHAR *text, SIZE_T *length)
{
    SIZE_T index;

    if (text == NULL || length == NULL) {
        return FALSE;
    }
    for (index = 0; index <= COMMENT_TEXT_CAPACITY; ++index) {
        if (text[index] == L'\0') {
            *length = index;
            return index > 0;
        }
    }
    return FALSE;
}

static void comments_sync_margin_visibility(AppState *app)
{
    CommentStore *store;
    BOOL visible;

    if (!comments_valid(app)) {
        return;
    }
    store = app->comments;
    visible = store->count > 0;
    if (store->marginVisible == visible) {
        return;
    }
    store->marginVisible = visible;
    if (app->pageView != NULL && IsWindow(app->pageView)) {
        pageview_layout(app);
    }
}

static void comments_refresh_summary(AppState *app)
{
    CommentStore *store;
    WCHAR summary[192];

    if (!comments_valid(app)) {
        if (app != NULL) {
            ribbon_set_comment_summary(app, L"No comments");
        }
        return;
    }
    store = app->comments;
    if (store->count == 0) {
        store->active = COMMENT_NO_ACTIVE;
        ribbon_set_comment_summary(app, L"No comments");
        comments_sync_margin_visibility(app);
        return;
    }
    if (store->active >= store->count) {
        store->active = 0;
    }
    if (FAILED(StringCchPrintfW(summary, ARRAYSIZE(summary),
                                L"%llu comment%s - comment %llu selected beside the page",
                                (unsigned long long)store->count,
                                store->count == 1 ? L"" : L"s",
                                (unsigned long long)(store->active + 1)))) {
        StringCchCopyW(summary, ARRAYSIZE(summary), L"Comments");
    }
    ribbon_set_comment_summary(app, summary);
    comments_sync_margin_visibility(app);
}

static void comments_invalidate(AppState *app)
{
    if (app != NULL && app->editor != NULL) {
        InvalidateRect(app->editor, NULL, FALSE);
    }
    if (app != NULL && app->pageView != NULL) {
        InvalidateRect(app->pageView, NULL, FALSE);
    }
}

static void comments_invalidate_editor(AppState *app)
{
    if (app != NULL && app->editor != NULL) {
        InvalidateRect(app->editor, NULL, FALSE);
    }
}

static BOOL comments_reserve(CommentStore *store, SIZE_T needed)
{
    SIZE_T capacity;
    CommentEntry *entries;

    if (needed <= store->capacity) {
        return TRUE;
    }
    if (needed > COMMENT_COUNT_LIMIT) {
        SetLastError(ERROR_TOO_MANY_NAMES);
        return FALSE;
    }
    capacity = store->capacity == 0 ? 8 : store->capacity;
    while (capacity < needed) {
        if (capacity >= COMMENT_COUNT_LIMIT / 2) {
            capacity = COMMENT_COUNT_LIMIT;
            break;
        }
        capacity *= 2;
    }
    if (store->entries == NULL) {
        entries = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                            capacity * sizeof(*entries));
    } else {
        entries = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                              store->entries, capacity * sizeof(*entries));
    }
    if (entries == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    store->entries = entries;
    store->capacity = capacity;
    return TRUE;
}

static BOOL comments_create_anchor(CommentStore *store, LONG start, LONG end,
                                   ITextRange **anchor)
{
    ITextRange *range = NULL;
    long actualStart = -1;
    long actualEnd = -1;

    *anchor = NULL;
    if (start < 0 || end < start ||
        FAILED(ITextDocument_Range(store->document, start, end, &range)) ||
        range == NULL || FAILED(ITextRange_GetStart(range, &actualStart)) ||
        FAILED(ITextRange_GetEnd(range, &actualEnd)) ||
        actualStart != start || actualEnd != end) {
        if (range != NULL) {
            ITextRange_Release(range);
        }
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    *anchor = range;
    return TRUE;
}

static BOOL comments_append_at(CommentStore *store, LONG start, LONG end,
                               const WCHAR *text)
{
    CommentEntry *entry;
    ITextRange *anchor;
    SIZE_T length;

    if (!comment_text_length(text, &length) ||
        !comments_reserve(store, store->count + 1) ||
        !comments_create_anchor(store, start, end, &anchor)) {
        return FALSE;
    }
    entry = &store->entries[store->count];
    ZeroMemory(entry, sizeof(*entry));
    entry->anchor = anchor;
    CopyMemory(entry->text, text, (length + 1) * sizeof(WCHAR));
    ++store->count;
    return TRUE;
}

static BOOL comments_range_bounds(ITextRange *range, LONG *start, LONG *end);

static BOOL comments_anchor_bounds(const CommentEntry *entry,
                                   LONG *start, LONG *end)
{
    if (entry == NULL) {
        return FALSE;
    }
    return comments_range_bounds(entry->anchor, start, end);
}

static BOOL comments_range_bounds(ITextRange *range, LONG *start, LONG *end)
{
    long rangeStart;
    long rangeEnd;

    if (range == NULL || start == NULL || end == NULL ||
        FAILED(ITextRange_GetStart(range, &rangeStart)) ||
        FAILED(ITextRange_GetEnd(range, &rangeEnd)) ||
        rangeStart < 0 || rangeEnd < rangeStart) {
        return FALSE;
    }
    *start = rangeStart;
    *end = rangeEnd;
    return TRUE;
}

static BOOL comments_set_temporary_highlight(AppState *app, ITextRange *range,
                                             BOOL visible)
{
    CommentStore *store;
    ITextFont *font = NULL;
    HRESULT result;
    HRESULT applyResult;
    HRESULT suspendResult;
    HRESULT resumeResult;
    LRESULT wasModified;
    BOOL wasLoading;
    BOOL temporaryMode;
    LONG start;
    LONG end;

    if (!comments_valid(app) ||
        !comments_range_bounds(range, &start, &end) || start == end ||
        ITextRange_GetFont(range, &font) != S_OK || font == NULL) {
        return FALSE;
    }
    store = app->comments;
    wasModified = SendMessageW(app->editor, EM_GETMODIFY, 0, 0);
    wasLoading = app->loading;
    app->loading = TRUE;
    suspendResult = ITextDocument_Undo(store->document, tomSuspend, NULL);
    result = ITextFont_Reset(font, tomApplyTmp);
    temporaryMode = result == S_OK;
    if (result == S_OK) {
        result = ITextFont_SetBackColor(
            font, visible ? (long)WORDCRAFT_COMMENT_HIGHLIGHT_COLOR
                          : tomAutoColor);
    }
    if (result == S_OK) {
        /* A dark document theme still needs readable glyphs on pale yellow.
         * Both colors are temporary and the original character colors return
         * as soon as the comment highlight is dismissed. */
        result = ITextFont_SetForeColor(
            font, visible ? (long)RGB(32, 32, 32) : tomAutoColor);
    }
    if (temporaryMode) {
        applyResult = ITextFont_Reset(font, tomApplyNow);
        if (result == S_OK && applyResult != S_OK) {
            result = applyResult;
        }
    }
    if (visible && result != S_OK &&
        ITextFont_Reset(font, tomApplyTmp) == S_OK) {
        /* Do not leave a half-applied yellow/foreground pair behind when a
         * protected or deleted range rejects one step of the transaction. */
        (void)ITextFont_SetBackColor(font, tomAutoColor);
        (void)ITextFont_SetForeColor(font, tomAutoColor);
        (void)ITextFont_Reset(font, tomApplyNow);
    }
    if (SUCCEEDED(suspendResult)) {
        resumeResult = ITextDocument_Undo(store->document, tomResume, NULL);
        if (result == S_OK && FAILED(resumeResult)) {
            result = resumeResult;
        }
    }
    if (visible && result != S_OK &&
        ITextFont_Reset(font, tomApplyTmp) == S_OK) {
        (void)ITextFont_SetBackColor(font, tomAutoColor);
        (void)ITextFont_SetForeColor(font, tomAutoColor);
        (void)ITextFont_Reset(font, tomApplyNow);
        if (SUCCEEDED(suspendResult)) {
            (void)ITextDocument_Undo(store->document, tomResume, NULL);
        }
    }
    SendMessageW(app->editor, EM_SETMODIFY, wasModified != 0, 0);
    app->loading = wasLoading;
    ITextFont_Release(font);
    return result == S_OK;
}

static BOOL comments_clear_highlight(AppState *app)
{
    CommentStore *store;

    if (app == NULL || app->comments == NULL) {
        return TRUE;
    }
    store = app->comments;
    if (store->highlightAnchor != NULL) {
        if (!comments_set_temporary_highlight(
                app, store->highlightAnchor, FALSE)) {
            return FALSE;
        }
        ITextRange_Release(store->highlightAnchor);
        store->highlightAnchor = NULL;
    }
    store->highlighted = COMMENT_NO_ACTIVE;
    if (store->selectionHidden && app->editor != NULL) {
        SendMessageW(app->editor, EM_HIDESELECTION, FALSE, 0);
        store->selectionHidden = FALSE;
    }
    comments_invalidate_editor(app);
    return TRUE;
}

static BOOL comments_show_highlight(AppState *app, ITextRange *range,
                                    SIZE_T entryIndex)
{
    CommentStore *store;
    LONG start;
    LONG end;

    if (!comments_valid(app) ||
        !comments_range_bounds(range, &start, &end) || start == end) {
        (void)comments_clear_highlight(app);
        return FALSE;
    }
    store = app->comments;
    if (!comments_clear_highlight(app)) {
        return FALSE;
    }
    if (!comments_set_temporary_highlight(app, range, TRUE)) {
        return FALSE;
    }
    ITextRange_AddRef(range);
    store->highlightAnchor = range;
    store->highlighted = entryIndex;
    SendMessageW(app->editor, EM_HIDESELECTION, TRUE, 0);
    store->selectionHidden = TRUE;
    comments_invalidate_editor(app);
    return TRUE;
}

static BOOL comments_discard_draft(AppState *app)
{
    CommentStore *store;

    if (app == NULL || app->comments == NULL) {
        return TRUE;
    }
    store = app->comments;
    if (store->compositionActive && !comments_clear_highlight(app)) {
        return FALSE;
    }
    if (store->draftAnchor != NULL) {
        ITextRange_Release(store->draftAnchor);
        store->draftAnchor = NULL;
    }
    store->compositionActive = FALSE;
    return TRUE;
}

static void comments_forget_transient_state(AppState *app)
{
    CommentStore *store;

    if (app == NULL || app->comments == NULL) {
        return;
    }
    store = app->comments;
    if (store->highlightAnchor != NULL) {
        ITextRange_Release(store->highlightAnchor);
        store->highlightAnchor = NULL;
    }
    if (store->draftAnchor != NULL) {
        ITextRange_Release(store->draftAnchor);
        store->draftAnchor = NULL;
    }
    store->highlighted = COMMENT_NO_ACTIVE;
    store->compositionActive = FALSE;
    if (store->selectionHidden && app->editor != NULL) {
        SendMessageW(app->editor, EM_HIDESELECTION, FALSE, 0);
        store->selectionHidden = FALSE;
    }
    comments_invalidate_editor(app);
}

static BOOL comments_selection_matches_bounds(const CHARRANGE *selection,
                                              LONG start, LONG end)
{
    if (selection->cpMin == selection->cpMax) {
        if (start == end) {
            return selection->cpMin == start;
        }
        return selection->cpMin >= start && selection->cpMin < end;
    }
    if (start == end) {
        return start >= selection->cpMin && start <= selection->cpMax;
    }
    return selection->cpMin < end && selection->cpMax > start;
}

static int comments_card_compare(const void *leftValue,
                                 const void *rightValue)
{
    const CommentCardLayout *left = (const CommentCardLayout *)leftValue;
    const CommentCardLayout *right = (const CommentCardLayout *)rightValue;

    if (left->page != right->page) {
        return left->page < right->page ? -1 : 1;
    }
    if (left->anchorStart != right->anchorStart) {
        return left->anchorStart < right->anchorStart ? -1 : 1;
    }
    if (left->anchorEnd != right->anchorEnd) {
        return left->anchorEnd < right->anchorEnd ? -1 : 1;
    }
    if (left->entryIndex == right->entryIndex) {
        return 0;
    }
    return left->entryIndex < right->entryIndex ? -1 : 1;
}

static int comments_measure_card_height(AppState *app, HDC dc,
                                        const WCHAR *text, int cardWidth)
{
    RECT measurement;
    TEXTMETRICW metrics;
    int padding = max(1, app_scale(app->pageView, 12));
    int titleHeight = max(1, app_scale(app->pageView, 20));
    int bodyGap = max(1, app_scale(app->pageView, 6));
    int minimumHeight = max(1, app_scale(app->pageView, 82));
    int maximumHeight = max(minimumHeight,
                            app_scale(app->pageView, 260));
    int contentWidth = max(1, cardWidth - padding * 2);
    int bodyHeight;
    int height;

    ZeroMemory(&measurement, sizeof(measurement));
    measurement.right = contentWidth;
    ZeroMemory(&metrics, sizeof(metrics));
    GetTextMetricsW(dc, &metrics);
    DrawTextW(dc, (LPWSTR)text, -1, &measurement,
              DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
    bodyHeight = max(max(1, metrics.tmHeight), measurement.bottom);
    height = padding + titleHeight + bodyGap + bodyHeight + padding;
    return min(maximumHeight, max(minimumHeight, height));
}

static void comments_shift_page_cards(CommentCardLayout *cards,
                                      SIZE_T begin, SIZE_T end,
                                      const RECT *margin, int padding,
                                      SIZE_T activeEntry)
{
    int overflow;
    int availableShift;
    int shift;
    SIZE_T index;

    if (cards == NULL || margin == NULL || begin >= end) {
        return;
    }
    shift = 0;
    for (index = begin; index < end; ++index) {
        if (cards[index].entryIndex == activeEntry &&
            cards[index].rect.bottom > margin->bottom - padding) {
            /* Dense pages behave like a virtualized comment rail: navigating
             * to a comment always brings its complete card into view, while
             * older cards can move above the page's clipped rail. */
            shift = cards[index].rect.bottom - (margin->bottom - padding);
            break;
        }
    }
    overflow = cards[end - 1].rect.bottom - (margin->bottom - padding);
    availableShift = cards[begin].rect.top - (margin->top + padding);
    if (shift == 0) {
        shift = min(max(0, overflow), max(0, availableShift));
    }
    if (shift <= 0) {
        return;
    }
    for (index = begin; index < end; ++index) {
        OffsetRect(&cards[index].rect, 0, -shift);
    }
}

static BOOL comments_build_card_layout(AppState *app, HDC dc,
                                       const RECT *visibleRect,
                                       CommentCardLayout **cardsOutput,
                                       SIZE_T *countOutput)
{
    CommentStore *store;
    CommentCardLayout *cards;
    SIZE_T index;
    SIZE_T count = 0;
    SIZE_T pageBegin = 0;
    LONG currentPage = 0;
    RECT currentMargin;
    int pagePadding;
    int cardInset;
    int cardGap;
    int titleHeight;
    int lastBottom = 0;
    HFONT previousFont = NULL;

    if (cardsOutput == NULL || countOutput == NULL) {
        return FALSE;
    }
    *cardsOutput = NULL;
    *countOutput = 0;
    if (!comments_valid(app) || app->comments->count == 0) {
        return TRUE;
    }
    if (app->pageView == NULL || dc == NULL) {
        return FALSE;
    }
    store = app->comments;
    cards = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                      store->count * sizeof(*cards));
    if (cards == NULL) {
        return FALSE;
    }
    for (index = 0; index < store->count; ++index) {
        RECT margin;
        RECT intersection;
        LONG start;
        LONG end;
        LONG page;
        POINT anchorPoint;

        if (!comments_anchor_bounds(&store->entries[index], &start, &end)) {
            continue;
        }
        page = pageview_character_page(app, start);
        if (!pageview_get_comment_margin_rect(app, page, &margin) ||
            (visibleRect != NULL &&
             !IntersectRect(&intersection, &margin, visibleRect)) ||
            !pageview_map_character_to_client(app, start, &page,
                                              &anchorPoint)) {
            continue;
        }
        cards[count].entryIndex = index;
        cards[count].anchorStart = start;
        cards[count].anchorEnd = end;
        cards[count].page = page;
        cards[count].anchorPoint = anchorPoint;
        cards[count].margin = margin;
        ++count;
    }
    if (count == 0) {
        HeapFree(GetProcessHeap(), 0, cards);
        return TRUE;
    }
    qsort(cards, count, sizeof(*cards), comments_card_compare);
    pagePadding = max(1, app_scale(app->pageView, 8));
    cardInset = max(1, app_scale(app->pageView, 4));
    cardGap = max(1, app_scale(app->pageView, 8));
    titleHeight = max(1, app_scale(app->pageView, 20));
    if (app->uiFont != NULL) {
        previousFont = (HFONT)SelectObject(dc, app->uiFont);
    }
    SetRectEmpty(&currentMargin);
    for (index = 0; index < count; ++index) {
        RECT margin;
        int cardWidth;
        int cardHeight;
        int minimumTop;
        int maximumTop;
        int desiredTop;

        margin = cards[index].margin;
        if (currentPage != cards[index].page) {
            if (currentPage != 0) {
                comments_shift_page_cards(cards, pageBegin, index,
                                          &currentMargin, pagePadding,
                                          store->active);
            }
            currentPage = cards[index].page;
            currentMargin = margin;
            pageBegin = index;
            lastBottom = margin.top + pagePadding - cardGap;
        }
        cards[index].anchorPoint.y = max(
            margin.top, min(margin.bottom - 1, cards[index].anchorPoint.y));
        cardWidth = max(1, margin.right - margin.left - cardInset * 2);
        cardHeight = comments_measure_card_height(
            app, dc, store->entries[cards[index].entryIndex].text,
            cardWidth);
        minimumTop = margin.top + pagePadding;
        maximumTop = margin.bottom - pagePadding - cardHeight;
        desiredTop = cards[index].anchorPoint.y - titleHeight / 2;
        if (maximumTop >= minimumTop) {
            desiredTop = min(desiredTop, maximumTop);
        }
        desiredTop = max(minimumTop, desiredTop);
        desiredTop = max(lastBottom + cardGap, desiredTop);
        SetRect(&cards[index].rect,
                margin.left + cardInset, desiredTop,
                margin.right - cardInset, desiredTop + cardHeight);
        lastBottom = cards[index].rect.bottom;
    }
    comments_shift_page_cards(cards, pageBegin, count, &currentMargin,
                              pagePadding, store->active);
    if (previousFont != NULL) {
        SelectObject(dc, previousFont);
    }
    *cardsOutput = cards;
    *countOutput = count;
    return TRUE;
}

static void comments_free_card_layout(CommentCardLayout *cards)
{
    if (cards != NULL) {
        HeapFree(GetProcessHeap(), 0, cards);
    }
}

static void comments_fill_rect(HDC dc, const RECT *rect, COLORREF color)
{
    HBRUSH brush;

    if (dc == NULL || rect == NULL || IsRectEmpty(rect)) {
        return;
    }
    brush = CreateSolidBrush(color);
    if (brush != NULL) {
        FillRect(dc, rect, brush);
        DeleteObject(brush);
    }
}

static UINT comments_text_hash(const WCHAR *text)
{
    UINT hash = 2166136261u;

    while (*text != L'\0') {
        hash ^= (UINT)*text++;
        hash *= 16777619u;
    }
    return hash;
}

BOOL comments_initialize(AppState *app)
{
    CommentStore *store;

    if (app == NULL || app->editor == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (app->comments != NULL) {
        return comments_valid(app);
    }
    store = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*store));
    if (store == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    store->document = comments_get_document(app->editor);
    store->active = COMMENT_NO_ACTIVE;
    store->highlighted = COMMENT_NO_ACTIVE;
    if (store->document == NULL) {
        HeapFree(GetProcessHeap(), 0, store);
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    app->comments = store;
    comments_refresh_summary(app);
    return TRUE;
}

void comments_clear(AppState *app)
{
    CommentStore *store;
    SIZE_T index;

    if (app == NULL || app->comments == NULL) {
        return;
    }
    store = app->comments;
    if (!comments_discard_draft(app) || !comments_clear_highlight(app)) {
        /* The backing document is about to lose all comment metadata.  A
         * deleted/protected TOM range can reject temp-format cleanup; never
         * let that stale range or hidden selection cross into the next
         * document generation. */
        comments_forget_transient_state(app);
    }
    for (index = 0; index < store->count; ++index) {
        if (store->entries[index].anchor != NULL) {
            ITextRange_Release(store->entries[index].anchor);
            store->entries[index].anchor = NULL;
        }
    }
    store->count = 0;
    store->active = COMMENT_NO_ACTIVE;
    comments_refresh_summary(app);
    comments_invalidate(app);
}

void comments_cancel_draft(AppState *app)
{
    if (!comments_discard_draft(app)) {
        comments_forget_transient_state(app);
    }
}

void comments_dismiss_highlight(AppState *app)
{
    if (!comments_discard_draft(app) || !comments_clear_highlight(app)) {
        comments_forget_transient_state(app);
    }
}

BOOL comments_begin_draft(AppState *app)
{
    CHARRANGE selection;
    CommentStore *store;
    LONG start;
    LONG end;

    if (!comments_valid(app)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    store = app->comments;
    if (store->compositionActive &&
        comments_range_bounds(store->draftAnchor, &start, &end)) {
        return TRUE;
    }
    if (!comments_discard_draft(app)) {
        app_set_status_message(app,
                               L"The previous comment highlight could not be dismissed.");
        return FALSE;
    }
    if (!comments_clear_highlight(app)) {
        app_set_status_message(app,
                               L"The previous comment highlight could not be dismissed.");
        return FALSE;
    }
    ZeroMemory(&selection, sizeof(selection));
    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
    if (selection.cpMin < 0 || selection.cpMax < selection.cpMin ||
        !comments_create_anchor(store, selection.cpMin, selection.cpMax,
                                &store->draftAnchor)) {
        app_set_status_message(app, L"The selected text could not be prepared for a comment.");
        return FALSE;
    }
    store->compositionActive = TRUE;
    if (selection.cpMin != selection.cpMax &&
        !comments_show_highlight(app, store->draftAnchor,
                                 COMMENT_NO_ACTIVE)) {
        ITextRange_Release(store->draftAnchor);
        store->draftAnchor = NULL;
        store->compositionActive = FALSE;
        app_set_status_message(app,
                               L"The selected text could not be highlighted.");
        return FALSE;
    }
    return TRUE;
}

BOOL comments_add(AppState *app, const WCHAR *text)
{
    CHARRANGE selection;
    CommentStore *store;
    SIZE_T length;
    LONG start;
    LONG end;
    BOOL usedDraft;

    if (!comments_valid(app) || !comment_text_length(text, &length)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    store = app->comments;
    if (store->count >= COMMENT_COUNT_LIMIT) {
        app_set_status_message(app, L"This document has reached the comment limit.");
        SetLastError(ERROR_TOO_MANY_NAMES);
        return FALSE;
    }
    usedDraft = store->compositionActive &&
                comments_range_bounds(store->draftAnchor, &start, &end);
    if (!usedDraft) {
        (void)comments_discard_draft(app);
        ZeroMemory(&selection, sizeof(selection));
        SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
        start = selection.cpMin;
        end = selection.cpMax;
    }
    if (start < 0 || end < start ||
        !comments_append_at(store, start, end, text)) {
        app_set_status_message(app, L"The comment could not be added.");
        return FALSE;
    }
    store->active = store->count - 1;
    if (usedDraft) {
        if (store->draftAnchor != NULL) {
            ITextRange_Release(store->draftAnchor);
            store->draftAnchor = NULL;
        }
        store->compositionActive = FALSE;
        if (store->highlightAnchor != NULL) {
            store->highlighted = store->active;
        } else {
            (void)comments_show_highlight(
                app, store->entries[store->active].anchor, store->active);
        }
    } else {
        (void)comments_show_highlight(
            app, store->entries[store->active].anchor, store->active);
    }
    comments_refresh_summary(app);
    comments_invalidate(app);
    document_mark_modified(app, TRUE);
    app_set_status_message(app, L"Comment added");
    (void)length;
    return TRUE;
}

static void comments_select_active(AppState *app)
{
    CommentStore *store;
    LONG start;
    LONG end;

    if (!comments_valid(app)) {
        return;
    }
    store = app->comments;
    if (store->count == 0 || store->active >= store->count ||
        !comments_anchor_bounds(&store->entries[store->active], &start, &end)) {
        comments_refresh_summary(app);
        return;
    }
    if (!comments_discard_draft(app) || !comments_clear_highlight(app)) {
        app_set_status_message(app,
                               L"The previous comment highlight could not be dismissed.");
        return;
    }
    store->selectingAnchor = TRUE;
    SendMessageW(app->editor, EM_SETSEL, (WPARAM)start, (LPARAM)end);
    SendMessageW(app->editor, EM_SCROLLCARET, 0, 0);
    SetFocus(app->editor);
    store->selectingAnchor = FALSE;
    (void)comments_show_highlight(
        app, store->entries[store->active].anchor, store->active);
    pageview_sync_to_caret(app, TRUE);
    comments_refresh_summary(app);
    comments_invalidate(app);
}

void comments_previous(AppState *app)
{
    CommentStore *store;

    if (!comments_valid(app) || app->comments->count == 0) {
        return;
    }
    store = app->comments;
    if (store->active == COMMENT_NO_ACTIVE || store->active == 0) {
        store->active = store->count - 1;
    } else {
        --store->active;
    }
    comments_select_active(app);
}

void comments_next(AppState *app)
{
    CommentStore *store;

    if (!comments_valid(app) || app->comments->count == 0) {
        return;
    }
    store = app->comments;
    if (store->active == COMMENT_NO_ACTIVE || store->active + 1 >= store->count) {
        store->active = 0;
    } else {
        ++store->active;
    }
    comments_select_active(app);
}

void comments_delete_active(AppState *app)
{
    CommentStore *store;
    SIZE_T active;

    if (!comments_valid(app) || app->comments->count == 0) {
        return;
    }
    store = app->comments;
    if (store->active >= store->count) {
        store->active = 0;
    }
    if (!comments_discard_draft(app) || !comments_clear_highlight(app)) {
        app_set_status_message(app,
                               L"The comment highlight could not be dismissed.");
        return;
    }
    active = store->active;
    if (store->entries[active].anchor != NULL) {
        ITextRange_Release(store->entries[active].anchor);
    }
    if (active + 1 < store->count) {
        MoveMemory(&store->entries[active], &store->entries[active + 1],
                   (store->count - active - 1) * sizeof(*store->entries));
    }
    --store->count;
    ZeroMemory(&store->entries[store->count], sizeof(*store->entries));
    if (store->count == 0) {
        store->active = COMMENT_NO_ACTIVE;
    } else if (active >= store->count) {
        store->active = store->count - 1;
    } else {
        store->active = active;
    }
    comments_refresh_summary(app);
    comments_invalidate(app);
    document_mark_modified(app, TRUE);
    app_set_status_message(app, L"Comment deleted");
}

void comments_selection_changed(AppState *app)
{
    CHARRANGE selection;
    CommentStore *store;
    SIZE_T index;
    SIZE_T matched;
    SIZE_T previousActive;

    if (app != NULL && app->loading) {
        return;
    }
    if (!comments_valid(app)) {
        return;
    }
    store = app->comments;
    if (store->selectingAnchor) {
        return;
    }
    previousActive = store->active;
    ZeroMemory(&selection, sizeof(selection));
    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
    if (store->compositionActive) {
        LONG draftStart;
        LONG draftEnd;
        if (comments_range_bounds(store->draftAnchor, &draftStart, &draftEnd) &&
            selection.cpMin == draftStart && selection.cpMax == draftEnd) {
            return;
        }
        if (!comments_discard_draft(app)) {
            return;
        }
    }
    if (store->count == 0) {
        (void)comments_clear_highlight(app);
        return;
    }
    matched = COMMENT_NO_ACTIVE;
    if (store->active < store->count) {
        LONG activeStart;
        LONG activeEnd;
        if (comments_anchor_bounds(&store->entries[store->active],
                                   &activeStart, &activeEnd) &&
            comments_selection_matches_bounds(&selection, activeStart,
                                               activeEnd)) {
            matched = store->active;
        }
    }
    for (index = 0; matched == COMMENT_NO_ACTIVE &&
                    index < store->count; ++index) {
        LONG start;
        LONG end;
        if (!comments_anchor_bounds(&store->entries[index], &start, &end)) {
            continue;
        }
        if (comments_selection_matches_bounds(&selection, start, end)) {
            matched = index;
            break;
        }
    }
    if (matched == COMMENT_NO_ACTIVE) {
        (void)comments_clear_highlight(app);
        return;
    }
    store->active = matched;
    if (store->highlightAnchor == NULL || store->highlighted != matched) {
        (void)comments_show_highlight(
            app, store->entries[matched].anchor, matched);
    }
    if (store->active != previousActive) {
        comments_refresh_summary(app);
        comments_invalidate(app);
    }
}

void comments_paint_overlays(AppState *app, HWND editor)
{
    /* RichEdit renders comment anchors through TOM temporary formatting.
     * Keeping this hook intentionally empty preserves the common editor paint
     * order while ensuring the yellow is display-only and never enters RTF. */
    (void)app;
    (void)editor;
}

void comments_paint_margin(AppState *app, HDC dc, const RECT *dirtyRect)
{
    CommentCardLayout *cards = NULL;
    SIZE_T cardCount = 0;
    SIZE_T index;
    RECT client;
    RECT dirty;
    int padding;
    int titleHeight;
    int bodyGap;
    int cornerRadius;
    int dotRadius;
    int accentWidth;
    HFONT previousFont = NULL;
    COLORREF previousTextColor;
    int previousBkMode;

    if (!comments_valid(app) || app->comments->count == 0 ||
        app->pageView == NULL || dc == NULL) {
        return;
    }
    GetClientRect(app->pageView, &client);
    if (dirtyRect == NULL || IsRectEmpty(dirtyRect) ||
        !IntersectRect(&dirty, &client, dirtyRect)) {
        dirty = client;
    }
    if (!comments_build_card_layout(app, dc, &dirty, &cards, &cardCount)) {
        return;
    }
    padding = max(1, app_scale(app->pageView, 12));
    titleHeight = max(1, app_scale(app->pageView, 20));
    bodyGap = max(1, app_scale(app->pageView, 6));
    cornerRadius = max(2, app_scale(app->pageView, 10));
    dotRadius = max(2, app_scale(app->pageView, 3));
    accentWidth = max(2, app_scale(app->pageView, 4));
    if (app->uiFont != NULL) {
        previousFont = (HFONT)SelectObject(dc, app->uiFont);
    }
    previousTextColor = GetTextColor(dc);
    previousBkMode = SetBkMode(dc, TRANSPARENT);
    for (index = 0; index < cardCount; ++index) {
        const CommentCardLayout *card = &cards[index];
        const CommentEntry *entry =
            &app->comments->entries[card->entryIndex];
        BOOL active = card->entryIndex == app->comments->active;
        COLORREF background = active ? app->palette.formatBackground
                                     : app->palette.controlBackground;
        COLORREF foreground = active ? app->palette.formatText
                                     : app->palette.controlText;
        COLORREF accent = active ? app->palette.toolbarHotBackground
                                 : app->palette.controlBorder;
        RECT margin;
        RECT shadow;
        RECT accentRect;
        RECT titleRect;
        RECT bodyRect;
        POINT connector[3];
        WCHAR title[96];
        HBRUSH cardBrush;
        HBRUSH shadowBrush;
        HBRUSH dotBrush;
        HPEN borderPen;
        HPEN connectorPen;
        int savedDc;
        int headerY;

        margin = card->margin;
        if (IsRectEmpty(&card->rect) || IsRectEmpty(&margin)) {
            continue;
        }
        headerY = card->rect.top + padding + titleHeight / 2;
        connector[0] = card->anchorPoint;
        connector[1].x = card->anchorPoint.x +
                         max(1, (card->rect.left - card->anchorPoint.x) / 2);
        connector[1].y = card->anchorPoint.y;
        connector[2].x = card->rect.left;
        connector[2].y = headerY;
        connectorPen = CreatePen(PS_SOLID,
                                 active ? max(1, app_scale(app->pageView, 2))
                                        : 1,
                                 accent);
        dotBrush = CreateSolidBrush(accent);
        savedDc = SaveDC(dc);
        if (connectorPen != NULL) {
            SelectObject(dc, connectorPen);
            Polyline(dc, connector, ARRAYSIZE(connector));
        }
        if (dotBrush != NULL) {
            SelectObject(dc, dotBrush);
            SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, card->anchorPoint.x - dotRadius,
                    card->anchorPoint.y - dotRadius,
                    card->anchorPoint.x + dotRadius + 1,
                    card->anchorPoint.y + dotRadius + 1);
        }
        RestoreDC(dc, savedDc);
        if (connectorPen != NULL) {
            DeleteObject(connectorPen);
        }
        if (dotBrush != NULL) {
            DeleteObject(dotBrush);
        }

        savedDc = SaveDC(dc);
        IntersectClipRect(dc, margin.left, margin.top,
                          margin.right, margin.bottom);
        shadow = card->rect;
        OffsetRect(&shadow, max(1, app_scale(app->pageView, 2)),
                   max(1, app_scale(app->pageView, 3)));
        shadowBrush = CreateSolidBrush(app->palette.pageShadow);
        if (shadowBrush != NULL) {
            SelectObject(dc, shadowBrush);
            SelectObject(dc, GetStockObject(NULL_PEN));
            RoundRect(dc, shadow.left, shadow.top, shadow.right, shadow.bottom,
                      cornerRadius, cornerRadius);
        }
        cardBrush = CreateSolidBrush(background);
        borderPen = CreatePen(PS_SOLID,
                              active ? max(1, app_scale(app->pageView, 2)) : 1,
                              accent);
        if (cardBrush != NULL) {
            SelectObject(dc, cardBrush);
        }
        if (borderPen != NULL) {
            SelectObject(dc, borderPen);
        } else {
            SelectObject(dc, GetStockObject(NULL_PEN));
        }
        if (cardBrush != NULL) {
            RoundRect(dc, card->rect.left, card->rect.top,
                      card->rect.right, card->rect.bottom,
                      cornerRadius, cornerRadius);
        }
        accentRect = card->rect;
        accentRect.left += max(1, app_scale(app->pageView, 3));
        accentRect.right = min(card->rect.right,
                               accentRect.left + accentWidth);
        accentRect.top += padding;
        accentRect.bottom -= padding;
        comments_fill_rect(dc, &accentRect, accent);
        titleRect = card->rect;
        titleRect.left += padding + accentWidth;
        titleRect.right -= padding;
        titleRect.top += padding;
        titleRect.bottom = titleRect.top + titleHeight;
        if (FAILED(StringCchPrintfW(
                title, ARRAYSIZE(title), L"Comment %llu  |  Page %ld",
                (unsigned long long)(card->entryIndex + 1), card->page))) {
            StringCchCopyW(title, ARRAYSIZE(title), L"Comment");
        }
        SetTextColor(dc, foreground);
        DrawTextW(dc, title, -1, &titleRect,
                  DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        bodyRect = card->rect;
        bodyRect.left += padding + accentWidth;
        bodyRect.right -= padding;
        bodyRect.top = titleRect.bottom + bodyGap;
        bodyRect.bottom -= padding;
        DrawTextW(dc, (LPWSTR)entry->text, -1, &bodyRect,
                  DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX |
                      DT_END_ELLIPSIS);
        RestoreDC(dc, savedDc);
        if (borderPen != NULL) {
            DeleteObject(borderPen);
        }
        if (cardBrush != NULL) {
            DeleteObject(cardBrush);
        }
        if (shadowBrush != NULL) {
            DeleteObject(shadowBrush);
        }
    }
    if (previousFont != NULL) {
        SelectObject(dc, previousFont);
    }
    SetTextColor(dc, previousTextColor);
    SetBkMode(dc, previousBkMode);
    comments_free_card_layout(cards);
}

BOOL comments_handle_margin_click(AppState *app, POINT point)
{
    CommentCardLayout *cards = NULL;
    SIZE_T cardCount = 0;
    SIZE_T index;
    RECT client;
    HDC dc;

    if (!comments_valid(app) || app->comments->count == 0 ||
        app->pageView == NULL) {
        return FALSE;
    }
    dc = GetDC(app->pageView);
    if (dc == NULL) {
        return FALSE;
    }
    GetClientRect(app->pageView, &client);
    if (!comments_build_card_layout(app, dc, &client, &cards, &cardCount)) {
        ReleaseDC(app->pageView, dc);
        return FALSE;
    }
    ReleaseDC(app->pageView, dc);
    for (index = 0; index < cardCount; ++index) {
        RECT margin = cards[index].margin;
        RECT visibleCard;
        if (IntersectRect(&visibleCard, &margin, &cards[index].rect) &&
            PtInRect(&visibleCard, point)) {
            SIZE_T entryIndex = cards[index].entryIndex;
            comments_free_card_layout(cards);
            app->comments->active = entryIndex;
            comments_select_active(app);
            return TRUE;
        }
    }
    comments_free_card_layout(cards);
    return FALSE;
}

SIZE_T comments_count(const AppState *app)
{
    return comments_valid(app) ? app->comments->count : 0;
}

static void builder_free(ByteBuilder *builder)
{
    if (builder->data != NULL) {
        HeapFree(GetProcessHeap(), 0, builder->data);
    }
    ZeroMemory(builder, sizeof(*builder));
}

static BOOL builder_reserve(ByteBuilder *builder, SIZE_T additional)
{
    SIZE_T needed;
    SIZE_T capacity;
    BYTE *data;

    if (additional > COMMENT_METADATA_LIMIT ||
        builder->size > COMMENT_METADATA_LIMIT - additional) {
        SetLastError(ERROR_FILE_TOO_LARGE);
        return FALSE;
    }
    needed = builder->size + additional;
    if (needed <= builder->capacity) {
        return TRUE;
    }
    capacity = builder->capacity == 0 ? 256 : builder->capacity;
    while (capacity < needed) {
        if (capacity > COMMENT_METADATA_LIMIT / 2) {
            capacity = COMMENT_METADATA_LIMIT;
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

static BOOL builder_append(ByteBuilder *builder, const void *data, SIZE_T size)
{
    if (!builder_reserve(builder, size)) {
        return FALSE;
    }
    if (size > 0) {
        CopyMemory(builder->data + builder->size, data, size);
        builder->size += size;
    }
    return TRUE;
}

static BOOL builder_append_number(ByteBuilder *builder, ULONGLONG value)
{
    CHAR number[32];
    HRESULT result = StringCchPrintfA(number, ARRAYSIZE(number), "%llu", value);
    return SUCCEEDED(result) &&
           builder_append(builder, number, strlen(number));
}

static BOOL builder_append_byte(ByteBuilder *builder, BYTE value)
{
    return builder_append(builder, &value, 1);
}

static BOOL comments_build_metadata(const CommentStore *store,
                                    ByteBuilder *builder)
{
    static const BYTE hexDigits[] = "0123456789ABCDEF";
    SIZE_T index;

    ZeroMemory(builder, sizeof(*builder));
    if (!builder_append(builder, commentMetadataPrefix,
                        sizeof(commentMetadataPrefix) - 1) ||
        !builder_append_number(builder, store->count) ||
        !builder_append_byte(builder, ';')) {
        return FALSE;
    }
    for (index = 0; index < store->count; ++index) {
        LONG start;
        LONG end;
        SIZE_T length;
        SIZE_T textIndex;
        if (!comments_anchor_bounds(&store->entries[index], &start, &end) ||
            !comment_text_length(store->entries[index].text, &length) ||
            !builder_append_number(builder, (ULONGLONG)start) ||
            !builder_append_byte(builder, ',') ||
            !builder_append_number(builder, (ULONGLONG)end) ||
            !builder_append_byte(builder, ',') ||
            !builder_append_number(builder, length) ||
            !builder_append_byte(builder, ',')) {
            return FALSE;
        }
        for (textIndex = 0; textIndex < length; ++textIndex) {
            unsigned value = (unsigned)store->entries[index].text[textIndex];
            BYTE encoded[4];
            encoded[0] = hexDigits[(value >> 12) & 0x0F];
            encoded[1] = hexDigits[(value >> 8) & 0x0F];
            encoded[2] = hexDigits[(value >> 4) & 0x0F];
            encoded[3] = hexDigits[value & 0x0F];
            if (!builder_append(builder, encoded, sizeof(encoded))) {
                return FALSE;
            }
        }
        if (!builder_append_byte(builder, ';')) {
            return FALSE;
        }
    }
    return builder_append_byte(builder, '}');
}

BOOL comments_embed_rtf(AppState *app, const BYTE *rtf, SIZE_T rtfSize,
                        BYTE **output, SIZE_T *outputSize, DWORD *error)
{
    ByteBuilder metadata;
    SIZE_T insertion;
    SIZE_T total;
    BYTE *result;

    if (output == NULL || outputSize == NULL || error == NULL ||
        (rtf == NULL && rtfSize != 0) || !comments_valid(app)) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return FALSE;
    }
    *output = NULL;
    *outputSize = 0;
    *error = ERROR_SUCCESS;
    if (app->comments->count == 0) {
        result = HeapAlloc(GetProcessHeap(), 0, rtfSize == 0 ? 1 : rtfSize);
        if (result == NULL) {
            *error = ERROR_NOT_ENOUGH_MEMORY;
            return FALSE;
        }
        if (rtfSize > 0) {
            CopyMemory(result, rtf, rtfSize);
        }
        *output = result;
        *outputSize = rtfSize;
        return TRUE;
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
    if (!comments_build_metadata(app->comments, &metadata)) {
        *error = GetLastError() != ERROR_SUCCESS
                     ? GetLastError() : ERROR_INVALID_DATA;
        builder_free(&metadata);
        return FALSE;
    }
    if (rtfSize > SIZE_MAX - metadata.size) {
        *error = ERROR_NOT_ENOUGH_MEMORY;
        builder_free(&metadata);
        return FALSE;
    }
    total = rtfSize + metadata.size;
    result = HeapAlloc(GetProcessHeap(), 0, total == 0 ? 1 : total);
    if (result == NULL) {
        *error = ERROR_NOT_ENOUGH_MEMORY;
        builder_free(&metadata);
        return FALSE;
    }
    CopyMemory(result, rtf, insertion);
    CopyMemory(result + insertion, metadata.data, metadata.size);
    CopyMemory(result + insertion + metadata.size,
               rtf + insertion, rtfSize - insertion);
    builder_free(&metadata);
    *output = result;
    *outputSize = total;
    return TRUE;
}

static int comments_hex_value(BYTE character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    return -1;
}

static BOOL comments_ascii_letter(BYTE value)
{
    return (value >= 'a' && value <= 'z') ||
           (value >= 'A' && value <= 'Z');
}

static BOOL comments_find_metadata_group(const BYTE *data, SIZE_T size,
                                         const BYTE **groupOutput)
{
    const SIZE_T prefixSize = sizeof(commentMetadataPrefix) - 1;
    const BYTE *found = NULL;
    SIZE_T index = 0;
    LONG depth = 0;
    BOOL rootClosed = FALSE;

    *groupOutput = NULL;
    if (size == 0) {
        return TRUE;
    }
    if (data == NULL || data[0] != '{') {
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
            if (depth == 1 && prefixSize <= size - index &&
                memcmp(data + index, commentMetadataPrefix, prefixSize) == 0) {
                if (found != NULL) {
                    return FALSE;
                }
                found = data + index;
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
                comments_hex_value(data[index + 1]) < 0 ||
                comments_hex_value(data[index + 2]) < 0) {
                return FALSE;
            }
            index += 3;
            continue;
        }
        if (comments_ascii_letter(value)) {
            SIZE_T wordStart = index;
            SIZE_T binarySize = 0;
            BOOL hasNumber = FALSE;
            BOOL negative = FALSE;
            BOOL binary;

            while (index < size && comments_ascii_letter(data[index])) {
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
            while (index < size && data[index] >= '0' && data[index] <= '9') {
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
    *groupOutput = found;
    return TRUE;
}

static BOOL comments_parse_number(const BYTE **cursor, const BYTE *end,
                                  BYTE delimiter, ULONGLONG maximum,
                                  ULONGLONG *value)
{
    ULONGLONG parsed = 0;
    BOOL sawDigit = FALSE;

    while (*cursor < end && **cursor >= '0' && **cursor <= '9') {
        unsigned digit = (unsigned)(**cursor - '0');
        sawDigit = TRUE;
        if (parsed > (maximum - digit) / 10) {
            return FALSE;
        }
        parsed = parsed * 10 + digit;
        ++*cursor;
    }
    if (!sawDigit || *cursor >= end || **cursor != delimiter) {
        return FALSE;
    }
    ++*cursor;
    *value = parsed;
    return TRUE;
}

static BOOL comments_parse_metadata(const BYTE *data, SIZE_T size,
                                    ParsedComment **parsedOutput,
                                    SIZE_T *countOutput, BOOL *found)
{
    const SIZE_T prefixSize = sizeof(commentMetadataPrefix) - 1;
    const BYTE *group;
    const BYTE *cursor;
    const BYTE *end;
    ParsedComment *parsed = NULL;
    ULONGLONG countValue;
    SIZE_T count;
    SIZE_T index;

    *parsedOutput = NULL;
    *countOutput = 0;
    *found = FALSE;
    if (size == 0) {
        return TRUE;
    }
    if (data == NULL) {
        return FALSE;
    }
    end = data + size;
    if (!comments_find_metadata_group(data, size, &group)) {
        return FALSE;
    }
    if (group == NULL) {
        return TRUE;
    }
    *found = TRUE;
    cursor = group + prefixSize;
    if (!comments_parse_number(&cursor, end, ';', COMMENT_COUNT_LIMIT,
                               &countValue)) {
        return FALSE;
    }
    count = (SIZE_T)countValue;
    if (count > 0) {
        parsed = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                           count * sizeof(*parsed));
        if (parsed == NULL) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
    }
    for (index = 0; index < count; ++index) {
        ULONGLONG start;
        ULONGLONG finish;
        ULONGLONG lengthValue;
        SIZE_T length;
        SIZE_T textIndex;
        if (!comments_parse_number(&cursor, end, ',', LONG_MAX, &start) ||
            !comments_parse_number(&cursor, end, ',', LONG_MAX, &finish) ||
            finish < start ||
            !comments_parse_number(&cursor, end, ',', COMMENT_TEXT_CAPACITY,
                                   &lengthValue)) {
            HeapFree(GetProcessHeap(), 0, parsed);
            return FALSE;
        }
        length = (SIZE_T)lengthValue;
        if (length == 0 || length > (SIZE_T)(end - cursor) / 4) {
            HeapFree(GetProcessHeap(), 0, parsed);
            return FALSE;
        }
        parsed[index].start = (LONG)start;
        parsed[index].end = (LONG)finish;
        for (textIndex = 0; textIndex < length; ++textIndex) {
            int a = comments_hex_value(cursor[0]);
            int b = comments_hex_value(cursor[1]);
            int c = comments_hex_value(cursor[2]);
            int d = comments_hex_value(cursor[3]);
            unsigned value;
            if (a < 0 || b < 0 || c < 0 || d < 0) {
                HeapFree(GetProcessHeap(), 0, parsed);
                return FALSE;
            }
            value = ((unsigned)a << 12) | ((unsigned)b << 8) |
                    ((unsigned)c << 4) | (unsigned)d;
            if (value == 0) {
                HeapFree(GetProcessHeap(), 0, parsed);
                return FALSE;
            }
            parsed[index].text[textIndex] = (WCHAR)value;
            cursor += 4;
        }
        parsed[index].text[length] = L'\0';
        if (cursor >= end || *cursor != ';') {
            HeapFree(GetProcessHeap(), 0, parsed);
            return FALSE;
        }
        ++cursor;
    }
    if (cursor >= end || *cursor != '}' ||
        (SIZE_T)(cursor - group + 1) > COMMENT_METADATA_LIMIT) {
        HeapFree(GetProcessHeap(), 0, parsed);
        return FALSE;
    }
    ++cursor;
    while (cursor < end && (*cursor == ' ' || *cursor == '\t' ||
                            *cursor == '\r' || *cursor == '\n')) {
        ++cursor;
    }
    if (cursor >= end || *cursor != '}') {
        HeapFree(GetProcessHeap(), 0, parsed);
        return FALSE;
    }
    *parsedOutput = parsed;
    *countOutput = count;
    return TRUE;
}

static BOOL comments_read_file(const WCHAR *path, BYTE **data, SIZE_T *size,
                               DWORD *error)
{
    HANDLE file;
    LARGE_INTEGER fileSize;
    BYTE *buffer = NULL;
    SIZE_T total = 0;

    *data = NULL;
    *size = 0;
    file = CreateFileW(path, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
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
    if (fileSize.QuadPart < 0 ||
        (ULONGLONG)fileSize.QuadPart > COMMENT_FILE_LIMIT ||
        (ULONGLONG)fileSize.QuadPart > SIZE_MAX) {
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
        DWORD amount = (DWORD)(((SIZE_T)fileSize.QuadPart - total) > 0x40000000u
                                   ? 0x40000000u
                                   : ((SIZE_T)fileSize.QuadPart - total));
        DWORD read = 0;
        if (!ReadFile(file, buffer + total, amount, &read, NULL) || read == 0) {
            *error = GetLastError() != ERROR_SUCCESS
                         ? GetLastError() : ERROR_HANDLE_EOF;
            HeapFree(GetProcessHeap(), 0, buffer);
            CloseHandle(file);
            return FALSE;
        }
        total += read;
    }
    if (!CloseHandle(file)) {
        *error = GetLastError();
        HeapFree(GetProcessHeap(), 0, buffer);
        return FALSE;
    }
    *data = buffer;
    *size = total;
    return TRUE;
}

BOOL comments_load_rtf_memory(AppState *app, const BYTE *data, SIZE_T size,
                              DWORD *error)
{
    ParsedComment *parsed = NULL;
    SIZE_T count = 0;
    SIZE_T index;
    BOOL found = FALSE;

    if (!comments_valid(app) || (data == NULL && size != 0) || error == NULL) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return FALSE;
    }
    *error = ERROR_SUCCESS;
    SetLastError(ERROR_SUCCESS);
    if (!comments_parse_metadata(data, size, &parsed, &count, &found)) {
        *error = GetLastError() != ERROR_SUCCESS
                     ? GetLastError() : ERROR_INVALID_DATA;
        return FALSE;
    }
    comments_clear(app);
    if (!found) {
        return TRUE;
    }
    for (index = 0; index < count; ++index) {
        if (!comments_append_at(app->comments, parsed[index].start,
                                parsed[index].end, parsed[index].text)) {
            *error = GetLastError() != ERROR_SUCCESS
                         ? GetLastError() : ERROR_INVALID_DATA;
            HeapFree(GetProcessHeap(), 0, parsed);
            comments_clear(app);
            return FALSE;
        }
    }
    HeapFree(GetProcessHeap(), 0, parsed);
    app->comments->active = count > 0 ? 0 : COMMENT_NO_ACTIVE;
    comments_refresh_summary(app);
    comments_invalidate(app);
    return TRUE;
}

BOOL comments_load_rtf_file(AppState *app, const WCHAR *path, DWORD *error)
{
    BYTE *data = NULL;
    SIZE_T size = 0;
    BOOL success;

    if (!comments_valid(app) || path == NULL || error == NULL) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return FALSE;
    }
    *error = ERROR_SUCCESS;
    if (!comments_read_file(path, &data, &size, error)) {
        return FALSE;
    }
    success = comments_load_rtf_memory(app, data, size, error);
    HeapFree(GetProcessHeap(), 0, data);
    return success;
}

static BOOL comments_current_margin_rect(const AppState *app, RECT *rect)
{
    LONG page;

    if (!comments_valid(app) || app->comments->count == 0 ||
        app->pageView == NULL || rect == NULL) {
        if (rect != NULL) {
            SetRectEmpty(rect);
        }
        return FALSE;
    }
    page = app->currentPage;
    if (page < 1 || page > app->pageCount) {
        page = 1;
    }
    return pageview_get_comment_margin_rect((AppState *)app, page, rect);
}

static BOOL comments_active_card_rect(const AppState *app, RECT *rect,
                                      BOOL *visibleOutput)
{
    AppState *mutableApp = (AppState *)app;
    CommentCardLayout *cards = NULL;
    SIZE_T cardCount = 0;
    SIZE_T index;
    RECT client;
    RECT visible;
    HDC dc;
    BOOL result = FALSE;

    if (rect != NULL) {
        SetRectEmpty(rect);
    }
    if (visibleOutput != NULL) {
        *visibleOutput = FALSE;
    }

    if (!comments_valid(app) || app->comments->active >= app->comments->count ||
        app->pageView == NULL || rect == NULL) {
        return FALSE;
    }
    dc = GetDC(app->pageView);
    if (dc == NULL) {
        return FALSE;
    }
    GetClientRect(app->pageView, &client);
    if (comments_build_card_layout(mutableApp, dc, &client,
                                   &cards, &cardCount)) {
        for (index = 0; index < cardCount; ++index) {
            if (cards[index].entryIndex == app->comments->active) {
                *rect = cards[index].rect;
                result = TRUE;
                if (visibleOutput != NULL &&
                    IntersectRect(&visible, &cards[index].rect, &client) &&
                    IntersectRect(&visible, &visible,
                                  &cards[index].margin)) {
                    *visibleOutput = TRUE;
                }
                break;
            }
        }
    }
    ReleaseDC(app->pageView, dc);
    comments_free_card_layout(cards);
    return result;
}

LRESULT comments_query_state(const AppState *app, UINT query, LPARAM index)
{
    const CommentStore *store;
    LONG start;
    LONG end;
    RECT margin;
    RECT cardRect;
    BOOL cardVisible;

    if (!comments_valid(app)) {
        return query == WCQ_COMMENT_ACTIVE_INDEX ||
                       query == WCQ_COMMENT_MARGIN_ACTIVE_INDEX ||
                       query == WCQ_COMMENT_ANCHOR_START ||
                       query == WCQ_COMMENT_ANCHOR_END ||
                       query == WCQ_COMMENT_HIGHLIGHT_START ||
                       query == WCQ_COMMENT_HIGHLIGHT_END
                   ? -1 : 0;
    }
    store = app->comments;
    switch (query) {
    case WCQ_COMMENT_COUNT:
        return (LRESULT)store->count;
    case WCQ_COMMENT_ACTIVE_INDEX:
        return store->active < store->count ? (LRESULT)store->active : -1;
    case WCQ_COMMENT_ANCHOR_START:
    case WCQ_COMMENT_ANCHOR_END:
        if (index < 0 || (SIZE_T)index >= store->count ||
            !comments_anchor_bounds(&store->entries[(SIZE_T)index],
                                    &start, &end)) {
            return -1;
        }
        return query == WCQ_COMMENT_ANCHOR_START ? start : end;
    case WCQ_COMMENT_TEXT_HASH:
        if (index < 0 || (SIZE_T)index >= store->count) {
            return 0;
        }
        return (LRESULT)(UINT_PTR)
            comments_text_hash(store->entries[(SIZE_T)index].text);
    case WCQ_COMMENT_MARGIN_VISIBLE:
        return store->marginVisible &&
               comments_current_margin_rect(app, &margin);
    case WCQ_COMMENT_CARD_COUNT:
        return (LRESULT)store->count;
    case WCQ_COMMENT_MARGIN_ACTIVE_INDEX:
        return store->active < store->count ? (LRESULT)store->active : -1;
    case WCQ_COMMENT_ACTIVE_CARD_VISIBLE:
        return comments_active_card_rect(app, &cardRect, &cardVisible) &&
               cardVisible;
    case WCQ_COMMENT_MARGIN_LEFT:
        return comments_current_margin_rect(app, &margin) ? margin.left : 0;
    case WCQ_COMMENT_MARGIN_WIDTH:
        return comments_current_margin_rect(app, &margin)
                   ? margin.right - margin.left : 0;
    case WCQ_COMMENT_ACTIVE_CARD_LEFT:
    case WCQ_COMMENT_ACTIVE_CARD_TOP:
    case WCQ_COMMENT_ACTIVE_CARD_RIGHT:
    case WCQ_COMMENT_ACTIVE_CARD_BOTTOM:
        if (!comments_active_card_rect(app, &cardRect, &cardVisible)) {
            return 0;
        }
        if (query == WCQ_COMMENT_ACTIVE_CARD_LEFT) {
            return cardRect.left;
        }
        if (query == WCQ_COMMENT_ACTIVE_CARD_TOP) {
            return cardRect.top;
        }
        if (query == WCQ_COMMENT_ACTIVE_CARD_RIGHT) {
            return cardRect.right;
        }
        return cardRect.bottom;
    case WCQ_COMMENT_HIGHLIGHT_VISIBLE:
        return comments_range_bounds(store->highlightAnchor, &start, &end) &&
               start < end;
    case WCQ_COMMENT_HIGHLIGHT_START:
    case WCQ_COMMENT_HIGHLIGHT_END:
        if (!comments_range_bounds(store->highlightAnchor, &start, &end)) {
            return -1;
        }
        return query == WCQ_COMMENT_HIGHLIGHT_START ? start : end;
    case WCQ_COMMENT_HIGHLIGHT_COLOR:
        return comments_range_bounds(store->highlightAnchor, &start, &end) &&
                       start < end
                   ? (LRESULT)(UINT_PTR)WORDCRAFT_COMMENT_HIGHLIGHT_COLOR : 0;
    case WCQ_COMMENT_COMPOSITION_ACTIVE:
        return store->compositionActive;
    default:
        return 0;
    }
}

void comments_shutdown(AppState *app)
{
    CommentStore *store;

    if (app == NULL || app->comments == NULL) {
        return;
    }
    store = app->comments;
    comments_clear(app);
    if (store->highlightAnchor != NULL) {
        ITextRange_Release(store->highlightAnchor);
        store->highlightAnchor = NULL;
    }
    if (store->draftAnchor != NULL) {
        ITextRange_Release(store->draftAnchor);
        store->draftAnchor = NULL;
    }
    if (store->entries != NULL) {
        HeapFree(GetProcessHeap(), 0, store->entries);
    }
    if (store->document != NULL) {
        ITextDocument_Release(store->document);
    }
    HeapFree(GetProcessHeap(), 0, store);
    app->comments = NULL;
}
