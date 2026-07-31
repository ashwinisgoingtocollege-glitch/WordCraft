#ifndef COBJMACROS
#define COBJMACROS
#endif

/* spellcheck.h normally hides its declarations for the Win7 build target.
 * The class is activated only at runtime, so exposing the declarations here
 * does not raise WordCraft's minimum operating-system version. */
#ifndef MIN_SPELLING_NTDDI
#define MIN_SPELLING_NTDDI NTDDI_WIN7
#endif

#include "editor.h"
#include "language.h"

#include <limits.h>
#include <oleauto.h>
#include <stddef.h>
#include <process.h>
#include <stdint.h>
#include <richole.h>
#include <spellcheck.h>
#include <stdlib.h>
#include <string.h>
#include <tom.h>
#include <wchar.h>
#include <wctype.h>

#define ASSIST_SNAPSHOT_LIMIT 65536
#define ASSIST_MAX_SPELL_RANGES 512
#define SPELL_DEBOUNCE_MS 360
#define COMPLETION_DEBOUNCE_MS 120
#define ASSIST_SHUTDOWN_WAIT_MS 3000
#define ASSIST_FONT_CACHE_CAPACITY 8
#define ASSIST_GLYPH_BUDGET 256

typedef enum AssistResultKind {
    ASSIST_RESULT_SPELL,
    ASSIST_RESULT_COMPLETION
} AssistResultKind;

typedef struct AssistSnapshot {
    volatile LONG references;
    LONG documentGeneration;
    LONG spellGeneration;
    LONG completionGeneration;
    LONG baseCharacter;
    LONG caretCharacter;
    BOOL completionAllowed;
    SIZE_T length;
    WCHAR text[1];
} AssistSnapshot;

typedef struct AssistResult {
    struct AssistResult *next;
    AssistResultKind kind;
    LONG documentGeneration;
    LONG spellGeneration;
    LONG completionGeneration;
    LONG caretCharacter;
    SIZE_T spellCount;
    LanguageRange spellRanges[ASSIST_MAX_SPELL_RANGES];
    WCHAR completion[LANGUAGE_COMPLETION_CAPACITY];
} AssistResult;

struct AssistContext {
    CRITICAL_SECTION lock;
    HANDLE spellEvent;
    HANDLE completionEvent;
    HANDLE spellThread;
    HANDLE completionThread;
    HWND notifyWindow;
    volatile LONG stopping;
    volatile LONG documentGeneration;
    volatile LONG spellGeneration;
    volatile LONG completionGeneration;
    AssistSnapshot *pendingSpell;
    AssistSnapshot *pendingCompletion;
    AssistResult *resultHead;
    AssistResult *resultTail;
    BOOL notificationPending;

    LanguageRange *visibleSpellRanges;
    SIZE_T visibleSpellCount;
    WCHAR visibleCompletion[LANGUAGE_COMPLETION_CAPACITY];
    LONG visibleCompletionCaret;
    LONG visibleCompletionDocumentGeneration;
    LONG visibleCompletionGeneration;
    BOOL imeComposing;
    BOOL spellResultReady;
};

/* uuid.lib does not consistently export these spell-check GUIDs for the
 * Win7-targeted LLVM configuration, so keep private constants. */
static const CLSID wordcraftClsidSpellCheckerFactory = {
    0x7AB36653, 0x1796, 0x484B,
    {0xBD, 0xFA, 0xE7, 0x4F, 0x1D, 0xB7, 0xC1, 0xDC}
};

static const IID wordcraftIidSpellCheckerFactory = {
    0x8E018A9D, 0x2415, 0x4677,
    {0xBF, 0x08, 0x79, 0x4E, 0xA6, 0x1F, 0x94, 0xBB}
};

static const IID wordcraftIidTextDocument = {
    0x8CC497C0, 0xA1DF, 0x11CE,
    {0x80, 0x98, 0x00, 0xAA, 0x00, 0x47, 0xBE, 0x5D}
};

static AssistSnapshot *assist_snapshot_add_ref(AssistSnapshot *snapshot)
{
    if (snapshot != NULL) {
        InterlockedIncrement(&snapshot->references);
    }
    return snapshot;
}

static void assist_snapshot_release(AssistSnapshot *snapshot)
{
    if (snapshot != NULL &&
        InterlockedDecrement(&snapshot->references) == 0) {
        HeapFree(GetProcessHeap(), 0, snapshot);
    }
}

static void assist_free_result_list(AssistResult *result)
{
    while (result != NULL) {
        AssistResult *next = result->next;
        HeapFree(GetProcessHeap(), 0, result);
        result = next;
    }
}

static BOOL assist_result_is_current(AssistContext *context,
                                     const AssistResult *result)
{
    if (result->documentGeneration !=
        InterlockedCompareExchange(&context->documentGeneration, 0, 0)) {
        return FALSE;
    }
    if (result->kind == ASSIST_RESULT_SPELL) {
        return result->spellGeneration ==
               InterlockedCompareExchange(&context->spellGeneration, 0, 0);
    }
    return result->completionGeneration ==
           InterlockedCompareExchange(&context->completionGeneration, 0, 0);
}

static void assist_publish_result(AssistContext *context,
                                  AssistResult *result)
{
    BOOL shouldNotify = FALSE;

    EnterCriticalSection(&context->lock);
    if (context->stopping || !assist_result_is_current(context, result)) {
        LeaveCriticalSection(&context->lock);
        HeapFree(GetProcessHeap(), 0, result);
        return;
    }
    result->next = NULL;
    if (context->resultTail != NULL) {
        context->resultTail->next = result;
    } else {
        context->resultHead = result;
    }
    context->resultTail = result;
    if (!context->notificationPending) {
        context->notificationPending = TRUE;
        shouldNotify = TRUE;
    }
    LeaveCriticalSection(&context->lock);

    if (shouldNotify &&
        !PostMessageW(context->notifyWindow, WCM_ASSIST_RESULT, 0, 0)) {
        EnterCriticalSection(&context->lock);
        context->notificationPending = FALSE;
        LeaveCriticalSection(&context->lock);
    }
}

static ULONGLONG assist_range_distance(const LanguageRange *range,
                                       LONG caret)
{
    LONGLONG start = range->start;
    LONGLONG end = start + range->length;

    if ((LONGLONG)caret < start) {
        return (ULONGLONG)(start - caret);
    }
    if ((LONGLONG)caret > end) {
        return (ULONGLONG)((LONGLONG)caret - end);
    }
    return 0;
}

static BOOL assist_range_is_farther(const LanguageRange *left,
                                    const LanguageRange *right,
                                    LONG caret)
{
    ULONGLONG leftDistance = assist_range_distance(left, caret);
    ULONGLONG rightDistance = assist_range_distance(right, caret);

    if (leftDistance != rightDistance) {
        return leftDistance > rightDistance;
    }
    if (left->start != right->start) {
        return left->start < right->start;
    }
    return left->length > right->length;
}

static void assist_swap_ranges(LanguageRange *left, LanguageRange *right)
{
    LanguageRange temporary = *left;
    *left = *right;
    *right = temporary;
}

static void assist_keep_nearest_range(AssistResult *result, LONG start,
                                      LONG length, LONG caret)
{
    LanguageRange candidate;
    SIZE_T index;

    candidate.start = start;
    candidate.length = length;
    if (result->spellCount < ARRAYSIZE(result->spellRanges)) {
        index = result->spellCount++;
        result->spellRanges[index] = candidate;
        while (index > 0) {
            SIZE_T parent = (index - 1) / 2;
            if (!assist_range_is_farther(&result->spellRanges[index],
                                         &result->spellRanges[parent],
                                         caret)) {
                break;
            }
            assist_swap_ranges(&result->spellRanges[index],
                               &result->spellRanges[parent]);
            index = parent;
        }
        return;
    }
    if (!assist_range_is_farther(&result->spellRanges[0], &candidate,
                                 caret)) {
        return;
    }
    result->spellRanges[0] = candidate;
    index = 0;
    for (;;) {
        SIZE_T left = index * 2 + 1;
        SIZE_T right = left + 1;
        SIZE_T farther = index;

        if (left < result->spellCount &&
            assist_range_is_farther(&result->spellRanges[left],
                                    &result->spellRanges[farther], caret)) {
            farther = left;
        }
        if (right < result->spellCount &&
            assist_range_is_farther(&result->spellRanges[right],
                                    &result->spellRanges[farther], caret)) {
            farther = right;
        }
        if (farther == index) {
            break;
        }
        assist_swap_ranges(&result->spellRanges[index],
                           &result->spellRanges[farther]);
        index = farther;
    }
}

static int assist_compare_ranges(const void *leftValue,
                                 const void *rightValue)
{
    const LanguageRange *left = (const LanguageRange *)leftValue;
    const LanguageRange *right = (const LanguageRange *)rightValue;

    if (left->start < right->start) {
        return -1;
    }
    if (left->start > right->start) {
        return 1;
    }
    if (left->length < right->length) {
        return -1;
    }
    if (left->length > right->length) {
        return 1;
    }
    return 0;
}

static LONG assist_snapshot_local_caret(const AssistSnapshot *snapshot)
{
    LONGLONG localCaret = (LONGLONG)snapshot->caretCharacter -
                          snapshot->baseCharacter;

    if (localCaret < 0) {
        return 0;
    }
    if ((ULONGLONG)localCaret > snapshot->length) {
        return (LONG)snapshot->length;
    }
    return (LONG)localCaret;
}

static BOOL assist_spell_snapshot_is_current(AssistContext *context,
                                             const AssistSnapshot *snapshot)
{
    return !context->stopping &&
           snapshot->documentGeneration ==
               InterlockedCompareExchange(&context->documentGeneration,
                                          0, 0) &&
           snapshot->spellGeneration ==
               InterlockedCompareExchange(&context->spellGeneration, 0, 0);
}

static BOOL assist_should_ignore_system_error(const WCHAR *text,
                                              SIZE_T textLength,
                                              ULONG start, ULONG length)
{
    SIZE_T index;
    BOOL hasLetter = FALSE;
    BOOL allUpper = TRUE;

    if (length <= 1 || start > textLength || length > textLength - start) {
        return TRUE;
    }
    for (index = 0; index < length; ++index) {
        WCHAR value = text[start + index];
        if (iswdigit((wint_t)value)) {
            return TRUE;
        }
        if (iswalpha((wint_t)value)) {
            hasLetter = TRUE;
            if (iswlower((wint_t)value)) {
                allUpper = FALSE;
            }
        }
    }
    if (!hasLetter || (allUpper && length <= 8)) {
        return TRUE;
    }
    if ((start > 0 && text[start - 1] == L'@') ||
        (start + length < textLength && text[start + length] == L'@')) {
        return TRUE;
    }
    return FALSE;
}

static ISpellChecker *assist_create_system_spell_checker(void)
{
    ISpellCheckerFactory *factory = NULL;
    ISpellChecker *checker = NULL;
    WCHAR language[LOCALE_NAME_MAX_LENGTH];
    BOOL supported = FALSE;
    HRESULT result;

    result = CoCreateInstance(&wordcraftClsidSpellCheckerFactory, NULL,
                              CLSCTX_INPROC_SERVER,
                              &wordcraftIidSpellCheckerFactory,
                              (void **)&factory);
    if (FAILED(result) || factory == NULL) {
        return NULL;
    }
    if (GetUserDefaultLocaleName(language, ARRAYSIZE(language)) == 0) {
        StringCchCopyW(language, ARRAYSIZE(language), L"en-US");
    }
    result = ISpellCheckerFactory_IsSupported(factory, language, &supported);
    if (FAILED(result) || !supported) {
        StringCchCopyW(language, ARRAYSIZE(language), L"en-US");
        supported = FALSE;
        result = ISpellCheckerFactory_IsSupported(factory, language,
                                                  &supported);
    }
    if (SUCCEEDED(result) && supported) {
        ISpellCheckerFactory_CreateSpellChecker(factory, language, &checker);
    }
    ISpellCheckerFactory_Release(factory);
    return checker;
}

static BOOL assist_collect_system_spell_errors(AssistContext *context,
                                               ISpellChecker *checker,
                                               const AssistSnapshot *snapshot,
                                               AssistResult *result)
{
    IEnumSpellingError *errors = NULL;
    HRESULT status;
    LONG localCaret = assist_snapshot_local_caret(snapshot);
    SIZE_T enumerated = 0;
    BOOL enumerationUsable = TRUE;

    if (checker == NULL || snapshot->length == 0 ||
        snapshot->text[0] == L'\0') {
        return FALSE;
    }
    status = ISpellChecker_Check(checker, snapshot->text, &errors);
    if (FAILED(status) || errors == NULL) {
        return FALSE;
    }
    for (;;) {
        ISpellingError *error = NULL;
        ULONG start = 0;
        ULONG length = 0;
        CORRECTIVE_ACTION action = CORRECTIVE_ACTION_NONE;

        if ((enumerated & 63u) == 0 &&
            !assist_spell_snapshot_is_current(context, snapshot)) {
            break;
        }
        status = IEnumSpellingError_Next(errors, &error);
        if (status != S_OK || error == NULL) {
            if (FAILED(status) && result->spellCount == 0) {
                enumerationUsable = FALSE;
            }
            break;
        }
        ++enumerated;
        if (SUCCEEDED(ISpellingError_get_StartIndex(error, &start)) &&
            SUCCEEDED(ISpellingError_get_Length(error, &length)) &&
            SUCCEEDED(ISpellingError_get_CorrectiveAction(error, &action)) &&
            action != CORRECTIVE_ACTION_NONE && start <= (ULONG)LONG_MAX &&
            length <= (ULONG)LONG_MAX &&
            !assist_should_ignore_system_error(snapshot->text,
                                               snapshot->length,
                                               start, length)) {
            assist_keep_nearest_range(result, (LONG)start, (LONG)length,
                                      localCaret);
        }
        ISpellingError_Release(error);
    }
    IEnumSpellingError_Release(errors);
    qsort(result->spellRanges, result->spellCount,
          sizeof(result->spellRanges[0]), assist_compare_ranges);
    return enumerationUsable;
}

static void assist_collect_fallback_spell_errors(
    AssistContext *context, const AssistSnapshot *snapshot,
    AssistResult *result)
{
    LanguageRange chunkRanges[ASSIST_MAX_SPELL_RANGES];
    SIZE_T chunkStart = 0;
    LONG localCaret = assist_snapshot_local_caret(snapshot);

    while (chunkStart < snapshot->length &&
           assist_spell_snapshot_is_current(context, snapshot)) {
        SIZE_T chunkEnd = min(chunkStart + 2048, snapshot->length);
        SIZE_T count;
        SIZE_T index;

        while (chunkEnd < snapshot->length &&
               iswalpha((wint_t)snapshot->text[chunkEnd])) {
            ++chunkEnd;
        }
        count = language_find_common_misspellings(
            snapshot->text + chunkStart, chunkEnd - chunkStart,
            chunkRanges, ARRAYSIZE(chunkRanges));
        for (index = 0; index < count; ++index) {
            LONGLONG absoluteStart = (LONGLONG)chunkStart +
                                     chunkRanges[index].start;
            if (absoluteStart <= LONG_MAX) {
                assist_keep_nearest_range(
                    result, (LONG)absoluteStart, chunkRanges[index].length,
                    localCaret);
            }
        }
        if (chunkEnd == chunkStart) {
            ++chunkEnd;
        }
        chunkStart = chunkEnd;
    }
    qsort(result->spellRanges, result->spellCount,
          sizeof(result->spellRanges[0]), assist_compare_ranges);
}

static unsigned __stdcall assist_spell_thread(void *parameter)
{
    AssistContext *context = (AssistContext *)parameter;
    ISpellChecker *checker = NULL;
    HRESULT comStatus = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    if (SUCCEEDED(comStatus)) {
        checker = assist_create_system_spell_checker();
    }
    for (;;) {
        AssistSnapshot *snapshot;
        AssistResult *result;
        DWORD waitStatus = WaitForSingleObject(context->spellEvent, INFINITE);

        if (waitStatus != WAIT_OBJECT_0 || context->stopping) {
            break;
        }
        EnterCriticalSection(&context->lock);
        snapshot = context->pendingSpell;
        context->pendingSpell = NULL;
        LeaveCriticalSection(&context->lock);
        if (snapshot == NULL) {
            continue;
        }
        if (snapshot->documentGeneration !=
                InterlockedCompareExchange(&context->documentGeneration,
                                           0, 0) ||
            snapshot->spellGeneration !=
                InterlockedCompareExchange(&context->spellGeneration, 0, 0)) {
            assist_snapshot_release(snapshot);
            continue;
        }
        result = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                           sizeof(*result));
        if (result != NULL) {
            result->kind = ASSIST_RESULT_SPELL;
            result->documentGeneration = snapshot->documentGeneration;
            result->spellGeneration = snapshot->spellGeneration;
            if (!assist_collect_system_spell_errors(context, checker,
                                                    snapshot, result)) {
                assist_collect_fallback_spell_errors(context, snapshot,
                                                     result);
            }
            {
                SIZE_T index;
                for (index = 0; index < result->spellCount; ++index) {
                    result->spellRanges[index].start +=
                        snapshot->baseCharacter;
                }
            }
            assist_publish_result(context, result);
        }
        assist_snapshot_release(snapshot);
    }
    if (checker != NULL) {
        ISpellChecker_Release(checker);
    }
    if (SUCCEEDED(comStatus)) {
        CoUninitialize();
    }
    return 0;
}

static unsigned __stdcall assist_completion_thread(void *parameter)
{
    AssistContext *context = (AssistContext *)parameter;

    for (;;) {
        AssistSnapshot *snapshot;
        AssistResult *result;
        DWORD waitStatus = WaitForSingleObject(context->completionEvent,
                                               INFINITE);

        if (waitStatus != WAIT_OBJECT_0 || context->stopping) {
            break;
        }
        EnterCriticalSection(&context->lock);
        snapshot = context->pendingCompletion;
        context->pendingCompletion = NULL;
        LeaveCriticalSection(&context->lock);
        if (snapshot == NULL) {
            continue;
        }
        if (snapshot->documentGeneration !=
                InterlockedCompareExchange(&context->documentGeneration,
                                           0, 0) ||
            snapshot->completionGeneration !=
                InterlockedCompareExchange(&context->completionGeneration,
                                           0, 0)) {
            assist_snapshot_release(snapshot);
            continue;
        }
        result = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                           sizeof(*result));
        if (result != NULL) {
            SIZE_T caret = (SIZE_T)(snapshot->caretCharacter -
                                   snapshot->baseCharacter);
            result->kind = ASSIST_RESULT_COMPLETION;
            result->documentGeneration = snapshot->documentGeneration;
            result->completionGeneration = snapshot->completionGeneration;
            result->caretCharacter = snapshot->caretCharacter;
            if (snapshot->completionAllowed && caret <= snapshot->length) {
                language_predict_completion(
                    snapshot->text, snapshot->length, caret,
                    result->completion, ARRAYSIZE(result->completion));
            }
            assist_publish_result(context, result);
        }
        assist_snapshot_release(snapshot);
    }
    return 0;
}

static AssistSnapshot *assist_capture_snapshot(AppState *app,
                                               BOOL forCompletion)
{
    AssistContext *context = app->assist;
    CHARRANGE selection;
    TEXTRANGEW textRange;
    SIZE_T documentLength = 0;
    DWORD error = ERROR_SUCCESS;
    LONG base;
    LONG end;
    LONG caret;
    SIZE_T wanted;
    SIZE_T allocation;
    LRESULT copied;
    AssistSnapshot *snapshot;

    if (context == NULL || app->editor == NULL ||
        !editor_get_text_length(app->editor, FALSE, &documentLength, &error) ||
        documentLength > (SIZE_T)LONG_MAX) {
        return NULL;
    }
    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
    caret = selection.cpMax;
    if (caret < 0 || (SIZE_T)caret > documentLength) {
        return NULL;
    }
    if (forCompletion) {
        base = caret > ASSIST_SNAPSHOT_LIMIT ?
                   caret - ASSIST_SNAPSHOT_LIMIT : 0;
        end = caret < (LONG)documentLength ? caret + 1 : caret;
    } else {
        LONG half = ASSIST_SNAPSHOT_LIMIT / 2;
        SIZE_T baseSize = caret > half ? (SIZE_T)(caret - half) : 0;
        SIZE_T endSize = documentLength - baseSize > ASSIST_SNAPSHOT_LIMIT
                             ? baseSize + ASSIST_SNAPSHOT_LIMIT
                             : documentLength;
        if (endSize == documentLength && endSize > ASSIST_SNAPSHOT_LIMIT) {
            baseSize = endSize - ASSIST_SNAPSHOT_LIMIT;
        }
        base = (LONG)baseSize;
        end = (LONG)endSize;
    }
    wanted = (SIZE_T)(end - base);
    if (wanted > ((SIZE_T)-1 - offsetof(AssistSnapshot, text)) /
                     sizeof(WCHAR) - 1) {
        return NULL;
    }
    allocation = offsetof(AssistSnapshot, text) +
                 (wanted + 1) * sizeof(WCHAR);
    snapshot = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, allocation);
    if (snapshot == NULL) {
        return NULL;
    }
    snapshot->references = 1;
    snapshot->documentGeneration =
        InterlockedCompareExchange(&context->documentGeneration, 0, 0);
    snapshot->spellGeneration =
        InterlockedCompareExchange(&context->spellGeneration, 0, 0);
    snapshot->completionGeneration =
        InterlockedCompareExchange(&context->completionGeneration, 0, 0);
    snapshot->baseCharacter = base;
    snapshot->caretCharacter = caret;
    textRange.chrg.cpMin = base;
    textRange.chrg.cpMax = end;
    textRange.lpstrText = snapshot->text;
    copied = SendMessageW(app->editor, EM_GETTEXTRANGE, 0,
                          (LPARAM)&textRange);
    if (copied < 0 || (SIZE_T)copied > wanted) {
        assist_snapshot_release(snapshot);
        return NULL;
    }
    snapshot->length = (SIZE_T)copied;
    snapshot->text[snapshot->length] = L'\0';
    snapshot->completionAllowed =
        forCompletion && selection.cpMin == selection.cpMax &&
        !context->imeComposing &&
        ((SIZE_T)caret == documentLength ||
         ((SIZE_T)(caret - base) < snapshot->length &&
          (snapshot->text[caret - base] == L'\r' ||
           snapshot->text[caret - base] == L'\n')));
    return snapshot;
}

static void assist_replace_pending(AssistContext *context,
                                   AssistSnapshot **slot,
                                   AssistSnapshot *snapshot,
                                   HANDLE event)
{
    AssistSnapshot *previous;

    EnterCriticalSection(&context->lock);
    previous = *slot;
    *slot = assist_snapshot_add_ref(snapshot);
    LeaveCriticalSection(&context->lock);
    assist_snapshot_release(previous);
    SetEvent(event);
}

BOOL assist_initialize(AppState *app)
{
    AssistContext *context;
    uintptr_t threadHandle;

    if (app == NULL || app->mainWindow == NULL || app->editor == NULL) {
        return FALSE;
    }
    context = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                        sizeof(*context));
    if (context == NULL) {
        return FALSE;
    }
    InitializeCriticalSection(&context->lock);
    context->notifyWindow = app->mainWindow;
    context->documentGeneration = 1;
    context->spellGeneration = 1;
    context->completionGeneration = 1;
    context->visibleCompletionCaret = -1;
    context->spellEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    context->completionEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (context->spellEvent == NULL || context->completionEvent == NULL) {
        goto failure;
    }
    threadHandle = _beginthreadex(NULL, 0, assist_spell_thread,
                                 context, 0, NULL);
    context->spellThread = (HANDLE)threadHandle;
    if (context->spellThread == NULL) {
        goto failure;
    }
    threadHandle = _beginthreadex(NULL, 0, assist_completion_thread,
                                 context, 0, NULL);
    context->completionThread = (HANDLE)threadHandle;
    if (context->completionThread == NULL) {
        InterlockedExchange(&context->stopping, TRUE);
        SetEvent(context->spellEvent);
        WaitForSingleObject(context->spellThread, INFINITE);
        goto failure;
    }
    app->assist = context;
    return TRUE;

failure:
    if (context->spellThread != NULL) {
        CloseHandle(context->spellThread);
    }
    if (context->completionThread != NULL) {
        CloseHandle(context->completionThread);
    }
    if (context->spellEvent != NULL) {
        CloseHandle(context->spellEvent);
    }
    if (context->completionEvent != NULL) {
        CloseHandle(context->completionEvent);
    }
    DeleteCriticalSection(&context->lock);
    HeapFree(GetProcessHeap(), 0, context);
    return FALSE;
}

static void assist_invalidate_editor(AppState *app)
{
    if (app != NULL && app->editor != NULL) {
        InvalidateRect(app->editor, NULL, FALSE);
    }
}

void assist_clear_completion(AppState *app)
{
    AssistContext *context = app != NULL ? app->assist : NULL;
    if (context == NULL || context->visibleCompletion[0] == L'\0') {
        return;
    }
    context->visibleCompletion[0] = L'\0';
    context->visibleCompletionCaret = -1;
    assist_invalidate_editor(app);
}

void assist_schedule(AppState *app)
{
    AssistContext *context = app != NULL ? app->assist : NULL;
    if (context == NULL || context->stopping) {
        return;
    }
    InterlockedIncrement(&context->documentGeneration);
    InterlockedIncrement(&context->spellGeneration);
    InterlockedIncrement(&context->completionGeneration);
    context->visibleSpellCount = 0;
    context->spellResultReady = FALSE;
    assist_clear_completion(app);
    KillTimer(app->mainWindow, SPELL_TIMER_ID);
    KillTimer(app->mainWindow, COMPLETION_TIMER_ID);
    if (app->spellCheckEnabled) {
        SetTimer(app->mainWindow, SPELL_TIMER_ID, SPELL_DEBOUNCE_MS, NULL);
    }
    if (app->autoCompleteEnabled) {
        SetTimer(app->mainWindow, COMPLETION_TIMER_ID,
                 COMPLETION_DEBOUNCE_MS, NULL);
    }
    assist_invalidate_editor(app);
}

void assist_selection_changed(AppState *app)
{
    AssistContext *context = app != NULL ? app->assist : NULL;
    if (context == NULL || context->stopping) {
        return;
    }
    InterlockedIncrement(&context->completionGeneration);
    InterlockedIncrement(&context->spellGeneration);
    assist_clear_completion(app);
    context->spellResultReady = FALSE;
    KillTimer(app->mainWindow, COMPLETION_TIMER_ID);
    KillTimer(app->mainWindow, SPELL_TIMER_ID);
    if (app->autoCompleteEnabled) {
        SetTimer(app->mainWindow, COMPLETION_TIMER_ID,
                 COMPLETION_DEBOUNCE_MS, NULL);
    }
    /* The spell snapshot is intentionally bounded. Debouncing a new scan as
     * the caret moves lets later pages of very large documents get checked
     * without rescanning on every individual arrow-key message. */
    if (app->spellCheckEnabled) {
        SetTimer(app->mainWindow, SPELL_TIMER_ID, SPELL_DEBOUNCE_MS, NULL);
    }
}

void assist_document_changed(AppState *app)
{
    assist_schedule(app);
}

void assist_handle_timer(AppState *app, UINT_PTR timerId)
{
    AssistContext *context = app != NULL ? app->assist : NULL;
    AssistSnapshot *snapshot;

    if (context == NULL || context->stopping) {
        return;
    }
    KillTimer(app->mainWindow, timerId);
    if (timerId == SPELL_TIMER_ID) {
        if (!app->spellCheckEnabled) {
            return;
        }
        snapshot = assist_capture_snapshot(app, FALSE);
        if (snapshot != NULL) {
            assist_replace_pending(context, &context->pendingSpell, snapshot,
                                   context->spellEvent);
            assist_snapshot_release(snapshot);
        }
    } else if (timerId == COMPLETION_TIMER_ID) {
        if (!app->autoCompleteEnabled) {
            return;
        }
        snapshot = assist_capture_snapshot(app, TRUE);
        if (snapshot != NULL) {
            assist_replace_pending(context, &context->pendingCompletion,
                                   snapshot, context->completionEvent);
            assist_snapshot_release(snapshot);
        }
    }
}

void assist_handle_result(AppState *app, LPARAM resultPointer)
{
    AssistContext *context = app != NULL ? app->assist : NULL;
    AssistResult *results;
    AssistResult *result;
    (void)resultPointer;

    if (context == NULL) {
        return;
    }
    EnterCriticalSection(&context->lock);
    results = context->resultHead;
    context->resultHead = NULL;
    context->resultTail = NULL;
    context->notificationPending = FALSE;
    LeaveCriticalSection(&context->lock);

    result = results;
    while (result != NULL) {
        AssistResult *next = result->next;
        if (assist_result_is_current(context, result)) {
            if (result->kind == ASSIST_RESULT_SPELL &&
                app->spellCheckEnabled) {
                SIZE_T bytes = result->spellCount * sizeof(LanguageRange);
                LanguageRange *replacement = NULL;
                if (bytes != 0) {
                    replacement = HeapAlloc(GetProcessHeap(), 0, bytes);
                    if (replacement != NULL) {
                        CopyMemory(replacement, result->spellRanges, bytes);
                    }
                }
                if (bytes == 0 || replacement != NULL) {
                    HeapFree(GetProcessHeap(), 0,
                             context->visibleSpellRanges);
                    context->visibleSpellRanges = replacement;
                    context->visibleSpellCount = result->spellCount;
                }
                context->spellResultReady = TRUE;
            } else if (result->kind == ASSIST_RESULT_COMPLETION &&
                       app->autoCompleteEnabled &&
                       result->completion[0] != L'\0') {
                StringCchCopyW(context->visibleCompletion,
                               ARRAYSIZE(context->visibleCompletion),
                               result->completion);
                context->visibleCompletionCaret = result->caretCharacter;
                context->visibleCompletionDocumentGeneration =
                    result->documentGeneration;
                context->visibleCompletionGeneration =
                    result->completionGeneration;
            }
        }
        HeapFree(GetProcessHeap(), 0, result);
        result = next;
    }
    assist_invalidate_editor(app);
}

BOOL assist_has_completion(const AppState *app)
{
    return app != NULL && app->assist != NULL &&
           app->autoCompleteEnabled &&
           app->assist->visibleCompletion[0] != L'\0' &&
           !app->assist->imeComposing &&
           app->assist->visibleCompletionDocumentGeneration ==
               app->assist->documentGeneration &&
           app->assist->visibleCompletionGeneration ==
               app->assist->completionGeneration;
}

BOOL assist_accept_completion(AppState *app)
{
    AssistContext *context = app != NULL ? app->assist : NULL;
    CHARRANGE selection;
    WCHAR insertion[LANGUAGE_COMPLETION_CAPACITY];

    if (!assist_has_completion(app)) {
        return FALSE;
    }
    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
    if (selection.cpMin != selection.cpMax ||
        selection.cpMax != context->visibleCompletionCaret ||
        context->visibleCompletionDocumentGeneration !=
            InterlockedCompareExchange(&context->documentGeneration, 0, 0) ||
        context->visibleCompletionGeneration !=
            InterlockedCompareExchange(&context->completionGeneration, 0, 0)) {
        assist_clear_completion(app);
        return FALSE;
    }
    if (FAILED(StringCchCopyW(insertion, ARRAYSIZE(insertion),
                              context->visibleCompletion))) {
        assist_clear_completion(app);
        return FALSE;
    }
    assist_clear_completion(app);
    SendMessageW(app->editor, EM_STOPGROUPTYPING, 0, 0);
    SendMessageW(app->editor, EM_REPLACESEL, TRUE, (LPARAM)insertion);
    SendMessageW(app->editor, EM_STOPGROUPTYPING, 0, 0);
    return TRUE;
}

static BOOL assist_get_character_position(HWND editor, LONG character,
                                          POINT *position)
{
    position->x = -1;
    position->y = -1;
    SendMessageW(editor, EM_POSFROMCHAR, (WPARAM)position,
                 (LPARAM)character);
    return position->x >= 0 && position->y >= 0;
}

static void assist_draw_wave(HDC dc, int left, int right, int y, int amplitude)
{
    POINT points[256];
    int cursor = left;
    int count = 0;
    int step = amplitude > 1 ? amplitude : 1;

    if (right <= left) {
        return;
    }
    while (cursor <= right && count < (int)ARRAYSIZE(points)) {
        points[count].x = cursor;
        points[count].y = y + ((count & 1) ? amplitude : 0);
        ++count;
        cursor += step;
    }
    if (count >= 2) {
        Polyline(dc, points, count);
    }
}

typedef struct AssistTextSpan {
    int left;
    int right;
    int baseline;
    int bottom;
} AssistTextSpan;

typedef struct AssistCachedFont {
    LOGFONTW description;
    HFONT handle;
} AssistCachedFont;

typedef struct AssistFontCache {
    AssistCachedFont entries[ASSIST_FONT_CACHE_CAPACITY];
    SIZE_T count;
} AssistFontCache;

static ITextDocument *assist_get_text_document(HWND editor)
{
    IRichEditOle *richEditOle = NULL;
    ITextDocument *document = NULL;

    if (!SendMessageW(editor, EM_GETOLEINTERFACE, 0,
                      (LPARAM)&richEditOle) || richEditOle == NULL) {
        return NULL;
    }
    richEditOle->lpVtbl->QueryInterface(richEditOle,
                                       &wordcraftIidTextDocument,
                                       (void **)&document);
    richEditOle->lpVtbl->Release(richEditOle);
    return document;
}

static BOOL assist_get_tom_span(ITextDocument *document, LONG start, LONG end,
                                AssistTextSpan *span, BOOL *singleLine)
{
    ITextRange *range = NULL;
    long left = 0;
    long startBaseline = 0;
    long bottomX = 0;
    long startBottom = 0;
    long right = 0;
    long endBaseline = 0;
    HRESULT startStatus;
    HRESULT bottomStatus;
    HRESULT endStatus;

    *singleLine = FALSE;
    if (document == NULL || start < 0 || end <= start ||
        FAILED(ITextDocument_Range(document, start, end, &range)) ||
        range == NULL) {
        return FALSE;
    }
    startStatus = ITextRange_GetPoint(
        range, tomStart | tomClientCoord | tomAllowOffClient |
                   TA_BASELINE | TA_LEFT,
        &left, &startBaseline);
    bottomStatus = ITextRange_GetPoint(
        range, tomStart | tomClientCoord | tomAllowOffClient |
                   TA_BOTTOM | TA_LEFT,
        &bottomX, &startBottom);
    endStatus = ITextRange_GetPoint(
        range, tomEnd | tomClientCoord | tomAllowOffClient |
                   TA_BASELINE | TA_LEFT,
        &right, &endBaseline);
    ITextRange_Release(range);
    if (startStatus != S_OK || endStatus != S_OK ||
        left < INT_MIN || left > INT_MAX || right < INT_MIN ||
        right > INT_MAX || startBaseline < INT_MIN ||
        startBaseline > INT_MAX || endBaseline < INT_MIN ||
        endBaseline > INT_MAX ||
        (bottomStatus == S_OK &&
         (startBottom < INT_MIN || startBottom > INT_MAX))) {
        return FALSE;
    }
    span->left = (int)left;
    span->right = (int)right;
    span->baseline = (int)startBaseline;
    span->bottom = bottomStatus == S_OK ? (int)startBottom
                                        : (int)startBaseline;
    *singleLine = startBaseline == endBaseline && right > left;
    return TRUE;
}

static HFONT assist_cached_font(AssistFontCache *cache,
                                const LOGFONTW *description)
{
    SIZE_T index;
    HFONT font;

    for (index = 0; index < cache->count; ++index) {
        if (memcmp(&cache->entries[index].description, description,
                   sizeof(*description)) == 0) {
            return cache->entries[index].handle;
        }
    }
    if (cache->count >= ARRAYSIZE(cache->entries)) {
        return NULL;
    }
    font = CreateFontIndirectW(description);
    if (font == NULL) {
        return NULL;
    }
    cache->entries[cache->count].description = *description;
    cache->entries[cache->count].handle = font;
    ++cache->count;
    return font;
}

static void assist_release_font_cache(AssistFontCache *cache)
{
    SIZE_T index;

    for (index = 0; index < cache->count; ++index) {
        DeleteObject(cache->entries[index].handle);
    }
    cache->count = 0;
}

static BOOL assist_get_range_font(ITextDocument *document, LONG start,
                                  LONG end, AppState *app,
                                  AssistFontCache *cache, HFONT *font)
{
    ITextRange *range = NULL;
    ITextFont *textFont = NULL;
    BSTR faceName = NULL;
    LOGFONTW description;
    float pointSize = 0.0f;
    long bold = tomUndefined;
    long italic = tomUndefined;
    int pointTenths;
    int pixelHeight;
    BOOL success = FALSE;

    *font = NULL;
    if (document == NULL ||
        ITextDocument_Range(document, start, end, &range) != S_OK ||
        range == NULL || ITextRange_GetFont(range, &textFont) != S_OK ||
        textFont == NULL ||
        ITextFont_GetName(textFont, &faceName) != S_OK ||
        faceName == NULL || faceName[0] == L'\0' ||
        ITextFont_GetSize(textFont, &pointSize) != S_OK ||
        pointSize <= 0.0f || pointSize > 1000.0f ||
        ITextFont_GetBold(textFont, &bold) != S_OK ||
        ITextFont_GetItalic(textFont, &italic) != S_OK ||
        bold == tomUndefined || italic == tomUndefined) {
        goto cleanup;
    }

    ZeroMemory(&description, sizeof(description));
    pointTenths = (int)(pointSize * 10.0f + 0.5f);
    pixelHeight = MulDiv(pointTenths, app_scale(app->editor, 96), 720);
    pixelHeight = MulDiv(max(1, pixelHeight), app->zoomPercent, 100);
    description.lfHeight = -max(1, pixelHeight);
    description.lfWeight = bold == tomTrue ? FW_BOLD : FW_NORMAL;
    description.lfItalic = (BYTE)(italic == tomTrue);
    description.lfCharSet = DEFAULT_CHARSET;
    description.lfQuality = CLEARTYPE_QUALITY;
    if (FAILED(StringCchCopyW(description.lfFaceName,
                              ARRAYSIZE(description.lfFaceName),
                              faceName))) {
        goto cleanup;
    }
    *font = assist_cached_font(cache, &description);
    success = *font != NULL;

cleanup:
    if (faceName != NULL) {
        SysFreeString(faceName);
    }
    if (textFont != NULL) {
        ITextFont_Release(textFont);
    }
    if (range != NULL) {
        ITextRange_Release(range);
    }
    return success;
}

static BOOL assist_measure_glyph_descent(AppState *app,
                                         ITextDocument *document, HDC dc,
                                         AssistFontCache *cache, LONG start,
                                         LONG end, SIZE_T *glyphBudget,
                                         int *descent)
{
    WCHAR text[129];
    TEXTRANGEW textRange;
    HFONT font = NULL;
    HGDIOBJ previousFont;
    TEXTMETRICW metrics;
    MAT2 transform;
    LRESULT copied;
    SIZE_T index;
    int maximumDescent = 0;

    *descent = 0;
    if (end <= start || end - start >= (LONG)ARRAYSIZE(text) ||
        *glyphBudget < (SIZE_T)(end - start) ||
        !assist_get_range_font(document, start, end, app, cache, &font)) {
        return FALSE;
    }
    textRange.chrg.cpMin = start;
    textRange.chrg.cpMax = end;
    textRange.lpstrText = text;
    copied = SendMessageW(app->editor, EM_GETTEXTRANGE, 0,
                          (LPARAM)&textRange);
    if (copied <= 0 || copied > end - start ||
        copied >= (LRESULT)ARRAYSIZE(text)) {
        return FALSE;
    }
    text[copied] = L'\0';

    previousFont = SelectObject(dc, font);
    if (previousFont == NULL || previousFont == HGDI_ERROR ||
        !GetTextMetricsW(dc, &metrics)) {
        if (previousFont != NULL && previousFont != HGDI_ERROR) {
            SelectObject(dc, previousFont);
        }
        return FALSE;
    }
    ZeroMemory(&transform, sizeof(transform));
    transform.eM11.value = 1;
    transform.eM22.value = 1;
    for (index = 0; index < (SIZE_T)copied; ++index) {
        GLYPHMETRICS glyph;
        int glyphDescent;
        DWORD status;

        --*glyphBudget;
        if (text[index] >= 0xD800 && text[index] <= 0xDFFF) {
            maximumDescent = max(maximumDescent, metrics.tmDescent);
            continue;
        }
        status = GetGlyphOutlineW(dc, text[index], GGO_METRICS, &glyph,
                                  0, NULL, &transform);
        if (status == GDI_ERROR || glyph.gmBlackBoxY > (UINT)INT_MAX) {
            maximumDescent = max(maximumDescent, metrics.tmDescent);
            continue;
        }
        glyphDescent = (int)glyph.gmBlackBoxY - glyph.gmptGlyphOrigin.y;
        maximumDescent = max(maximumDescent, max(0, glyphDescent));
    }
    SelectObject(dc, previousFont);
    *descent = maximumDescent;
    return TRUE;
}

static void assist_paint_spell_ranges(AppState *app, HDC dc,
                                      const RECT *formatRect)
{
    AssistContext *context = app->assist;
    ITextDocument *document;
    HPEN pen;
    HGDIOBJ previousPen;
    int amplitude = max(1, app_scale(app->editor, 1));
    SIZE_T rangeIndex;
    SIZE_T paintedCharacters = 0;
    SIZE_T glyphBudget = ASSIST_GLYPH_BUDGET;
    AssistFontCache fontCache;

    if (!app->spellCheckEnabled || context->visibleSpellCount == 0) {
        return;
    }
    pen = CreatePen(PS_SOLID, max(1, app_scale(app->editor, 1)),
                    RGB(211, 47, 65));
    if (pen == NULL) {
        return;
    }
    ZeroMemory(&fontCache, sizeof(fontCache));
    previousPen = SelectObject(dc, pen);
    document = assist_get_text_document(app->editor);
    for (rangeIndex = 0;
         rangeIndex < context->visibleSpellCount &&
         paintedCharacters < 2048;
         ++rangeIndex) {
        LONG start = context->visibleSpellRanges[rangeIndex].start;
        LONG end = start + context->visibleSpellRanges[rangeIndex].length;
        LONG displayEnd;
        LONG character;
        POINT current;
        AssistTextSpan wholeSpan;
        BOOL wholeIsSingleLine = FALSE;

        if (start < 0 || end <= start ||
            !assist_get_character_position(app->editor, start, &current)) {
            continue;
        }
        displayEnd = end - start > 128 ? start + 128 : end;
        if (assist_get_tom_span(document, start, displayEnd, &wholeSpan,
                                &wholeIsSingleLine) &&
            wholeIsSingleLine) {
            int glyphDescent = 0;
            int waveY;
            if (assist_measure_glyph_descent(
                    app, document, dc, &fontCache, start, displayEnd,
                    &glyphBudget, &glyphDescent)) {
                waveY = wholeSpan.baseline +
                        max(amplitude + 1, glyphDescent + amplitude);
            } else {
                waveY = wholeSpan.bottom > wholeSpan.baseline
                            ? wholeSpan.bottom - amplitude
                            : wholeSpan.baseline + amplitude + 1;
            }
            paintedCharacters += (SIZE_T)(displayEnd - start);
            if (waveY >= formatRect->top &&
                waveY <= formatRect->bottom &&
                wholeSpan.right > formatRect->left &&
                wholeSpan.left < formatRect->right) {
                assist_draw_wave(
                    dc, max(wholeSpan.left, formatRect->left),
                    min(wholeSpan.right, formatRect->right),
                    waveY, amplitude);
            }
            continue;
        }
        for (character = start;
             character < displayEnd &&
             paintedCharacters < 2048;
             ++character, ++paintedCharacters) {
            AssistTextSpan characterSpan;
            BOOL characterIsSingleLine = FALSE;
            POINT next;
            int right;
            int underlineY;
            if (assist_get_tom_span(document, character, character + 1,
                                    &characterSpan,
                                    &characterIsSingleLine) &&
                characterIsSingleLine) {
                int glyphDescent = 0;
                int waveY;
                if (assist_measure_glyph_descent(
                        app, document, dc, &fontCache, character,
                        character + 1, &glyphBudget, &glyphDescent)) {
                    waveY = characterSpan.baseline +
                            max(amplitude + 1,
                                glyphDescent + amplitude);
                } else {
                    waveY = characterSpan.bottom > characterSpan.baseline
                                ? characterSpan.bottom - amplitude
                                : characterSpan.baseline + amplitude + 1;
                }
                if (waveY > formatRect->bottom) {
                    break;
                }
                if (waveY >= formatRect->top &&
                    characterSpan.right > formatRect->left &&
                    characterSpan.left < formatRect->right) {
                    assist_draw_wave(
                        dc, max(characterSpan.left, formatRect->left),
                        min(characterSpan.right, formatRect->right),
                        waveY, amplitude);
                }
                continue;
            }
            BOOL hasNext = assist_get_character_position(
                app->editor, character + 1, &next);

            if (current.y > formatRect->bottom ||
                current.y + app_scale(app->editor, 30) < formatRect->top) {
                if (current.y > formatRect->bottom) {
                    break;
                }
                if (hasNext) {
                    current = next;
                }
                continue;
            }
            if (hasNext && next.y == current.y && next.x > current.x) {
                right = next.x;
            } else {
                right = current.x +
                        max(app_scale(app->editor, 5), amplitude * 4);
            }
            underlineY = current.y +
                         MulDiv(app_scale(app->editor, 16),
                                app->zoomPercent, 100) - amplitude - 1;
            if (right > formatRect->left && current.x < formatRect->right) {
                assist_draw_wave(dc, max(current.x, formatRect->left),
                                 min(right, formatRect->right), underlineY,
                                 amplitude);
            }
            if (hasNext) {
                current = next;
            }
        }
    }
    if (document != NULL) {
        ITextDocument_Release(document);
    }
    assist_release_font_cache(&fontCache);
    SelectObject(dc, previousPen);
    DeleteObject(pen);
}

static HFONT assist_create_completion_font(AppState *app)
{
    CHARFORMAT2W format;
    LOGFONTW font;
    int dpi = app_scale(app->editor, 96);
    int height;

    ZeroMemory(&format, sizeof(format));
    format.cbSize = sizeof(format);
    SendMessageW(app->editor, EM_GETCHARFORMAT, SCF_SELECTION,
                 (LPARAM)&format);
    ZeroMemory(&font, sizeof(font));
    height = (format.dwMask & CFM_SIZE) != 0 && format.yHeight > 0
                 ? format.yHeight : WORDCRAFT_DEFAULT_FONT_SIZE_TWIPS;
    font.lfHeight = -MulDiv(height, dpi * app->zoomPercent,
                           1440 * 100);
    if (font.lfHeight == 0) {
        font.lfHeight = -app_scale(app->editor, 16);
    }
    font.lfWeight = (format.dwEffects & CFE_BOLD) != 0 ?
                        FW_BOLD : FW_NORMAL;
    font.lfItalic = (BYTE)((format.dwEffects & CFE_ITALIC) != 0);
    font.lfCharSet = (format.dwMask & CFM_CHARSET) != 0 ?
                         format.bCharSet : DEFAULT_CHARSET;
    font.lfQuality = CLEARTYPE_QUALITY;
    if ((format.dwMask & CFM_FACE) != 0 && format.szFaceName[0] != L'\0') {
        StringCchCopyW(font.lfFaceName, ARRAYSIZE(font.lfFaceName),
                       format.szFaceName);
    } else {
        StringCchCopyW(font.lfFaceName, ARRAYSIZE(font.lfFaceName),
                       WORDCRAFT_DEFAULT_FONT_FACE);
    }
    return CreateFontIndirectW(&font);
}

static void assist_paint_completion(AppState *app, HDC dc,
                                    const RECT *formatRect)
{
    AssistContext *context = app->assist;
    CHARRANGE selection;
    POINT position;
    HFONT font;
    HGDIOBJ previousFont;
    COLORREF previousColor;
    int previousMode;
    RECT clip;

    if (!assist_has_completion(app)) {
        return;
    }
    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
    if (selection.cpMin != selection.cpMax ||
        selection.cpMax != context->visibleCompletionCaret ||
        !assist_get_character_position(app->editor, selection.cpMax,
                                       &position) ||
        position.x < formatRect->left || position.x >= formatRect->right ||
        position.y < formatRect->top || position.y >= formatRect->bottom) {
        return;
    }
    font = assist_create_completion_font(app);
    if (font == NULL) {
        return;
    }
    clip = *formatRect;
    IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);
    previousFont = SelectObject(dc, font);
    previousMode = SetBkMode(dc, TRANSPARENT);
    previousColor = SetTextColor(
        dc, app->useBrandColors ? RGB(126, 134, 143)
                                : GetSysColor(COLOR_GRAYTEXT));
    ExtTextOutW(dc, position.x, position.y, ETO_CLIPPED, &clip,
                context->visibleCompletion,
                (UINT)wcslen(context->visibleCompletion), NULL);
    SetTextColor(dc, previousColor);
    SetBkMode(dc, previousMode);
    SelectObject(dc, previousFont);
    DeleteObject(font);
}

void assist_paint_overlays(AppState *app, HWND editor)
{
    HDC dc;
    RECT formatRect;
    int saved;

    if (app == NULL || app->assist == NULL || editor == NULL) {
        return;
    }
    dc = GetDC(editor);
    if (dc == NULL) {
        return;
    }
    SendMessageW(editor, EM_GETRECT, 0, (LPARAM)&formatRect);
    saved = SaveDC(dc);
    IntersectClipRect(dc, formatRect.left, formatRect.top,
                      formatRect.right, formatRect.bottom);
    assist_paint_spell_ranges(app, dc, &formatRect);
    assist_paint_completion(app, dc, &formatRect);
    if (saved != 0) {
        RestoreDC(dc, saved);
    }
    ReleaseDC(editor, dc);
}

void assist_set_spell_check(AppState *app, BOOL enabled)
{
    AssistContext *context;
    if (app == NULL) {
        return;
    }
    context = app->assist;
    app->spellCheckEnabled = enabled;
    if (context == NULL) {
        return;
    }
    KillTimer(app->mainWindow, SPELL_TIMER_ID);
    context->visibleSpellCount = 0;
    context->spellResultReady = FALSE;
    InterlockedIncrement(&context->spellGeneration);
    if (enabled && !context->stopping) {
        SetTimer(app->mainWindow, SPELL_TIMER_ID, SPELL_DEBOUNCE_MS, NULL);
    }
    assist_invalidate_editor(app);
}

void assist_set_auto_complete(AppState *app, BOOL enabled)
{
    AssistContext *context;
    if (app == NULL) {
        return;
    }
    context = app->assist;
    app->autoCompleteEnabled = enabled;
    if (context == NULL) {
        return;
    }
    KillTimer(app->mainWindow, COMPLETION_TIMER_ID);
    InterlockedIncrement(&context->completionGeneration);
    assist_clear_completion(app);
    if (enabled && !context->stopping) {
        SetTimer(app->mainWindow, COMPLETION_TIMER_ID,
                 COMPLETION_DEBOUNCE_MS, NULL);
    }
}

void assist_set_ime_composing(AppState *app, BOOL composing)
{
    AssistContext *context = app != NULL ? app->assist : NULL;
    if (context == NULL) {
        return;
    }
    context->imeComposing = composing;
    InterlockedIncrement(&context->completionGeneration);
    assist_clear_completion(app);
    KillTimer(app->mainWindow, COMPLETION_TIMER_ID);
    if (!composing && app->autoCompleteEnabled && !context->stopping) {
        SetTimer(app->mainWindow, COMPLETION_TIMER_ID,
                 COMPLETION_DEBOUNCE_MS, NULL);
    }
}

LRESULT assist_query_state(const AppState *app, UINT query)
{
    const AssistContext *context = app != NULL ? app->assist : NULL;
    if (context == NULL) {
        return 0;
    }
    switch (query) {
    case WCQ_SPELL_ERROR_COUNT:
        return (LRESULT)context->visibleSpellCount;
    case WCQ_COMPLETION_VISIBLE:
        return assist_has_completion(app);
    case WCQ_COMPLETION_LENGTH:
        return (LRESULT)wcslen(context->visibleCompletion);
    case WCQ_ASSIST_WORKER_RUNNING:
        return !context->stopping && context->spellThread != NULL &&
               context->completionThread != NULL;
    case WCQ_SPELL_RESULT_READY:
        return context->spellResultReady;
    default:
        return 0;
    }
}

void assist_request_stop(AppState *app)
{
    AssistContext *context = app != NULL ? app->assist : NULL;
    if (context == NULL || InterlockedExchange(&context->stopping, TRUE)) {
        return;
    }
    if (app->mainWindow != NULL) {
        KillTimer(app->mainWindow, SPELL_TIMER_ID);
        KillTimer(app->mainWindow, COMPLETION_TIMER_ID);
    }
    SetEvent(context->spellEvent);
    SetEvent(context->completionEvent);
}

void assist_shutdown(AppState *app)
{
    AssistContext *context = app != NULL ? app->assist : NULL;
    HANDLE workers[2];
    AssistSnapshot *pendingSpell;
    AssistSnapshot *pendingCompletion;
    AssistResult *results;

    if (context == NULL) {
        return;
    }
    assist_request_stop(app);
    workers[0] = context->spellThread;
    workers[1] = context->completionThread;
    if (WaitForMultipleObjects(ARRAYSIZE(workers), workers, TRUE,
                               ASSIST_SHUTDOWN_WAIT_MS) != WAIT_OBJECT_0) {
        /* A third-party spell provider is allowed to be synchronous. Never
         * hold the closing UI forever if one stalls: the context deliberately
         * remains valid until normal process teardown reclaims it. Workers do
         * not retain AppState or RichEdit pointers. */
        app->assist = NULL;
        return;
    }
    EnterCriticalSection(&context->lock);
    pendingSpell = context->pendingSpell;
    pendingCompletion = context->pendingCompletion;
    results = context->resultHead;
    context->pendingSpell = NULL;
    context->pendingCompletion = NULL;
    context->resultHead = NULL;
    context->resultTail = NULL;
    LeaveCriticalSection(&context->lock);
    assist_snapshot_release(pendingSpell);
    assist_snapshot_release(pendingCompletion);
    assist_free_result_list(results);
    HeapFree(GetProcessHeap(), 0, context->visibleSpellRanges);
    if (context->spellThread != NULL) {
        CloseHandle(context->spellThread);
    }
    if (context->completionThread != NULL) {
        CloseHandle(context->completionThread);
    }
    if (context->spellEvent != NULL) {
        CloseHandle(context->spellEvent);
    }
    if (context->completionEvent != NULL) {
        CloseHandle(context->completionEvent);
    }
    DeleteCriticalSection(&context->lock);
    HeapFree(GetProcessHeap(), 0, context);
    app->assist = NULL;
}
