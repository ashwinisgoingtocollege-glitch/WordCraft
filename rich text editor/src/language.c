#include "language.h"

#include <limits.h>
#include <wchar.h>
#include <wctype.h>

typedef struct PhraseCompletion {
    const WCHAR *trigger;
    const WCHAR *suffix;
} PhraseCompletion;

typedef struct WordCandidate {
    const WCHAR *latest;
    size_t length;
    size_t count;
    size_t latestPosition;
} WordCandidate;

static const WCHAR *const COMMON_MISSPELLINGS[] = {
    L"acheive",
    L"adress",
    L"arguement",
    L"becuase",
    L"definately",
    L"embarass",
    L"goverment",
    L"independant",
    L"occured",
    L"recieve",
    L"seperate",
    L"teh",
    L"thier",
    L"untill",
    L"wierd"
};

static const PhraseCompletion PHRASE_COMPLETIONS[] = {
    {L"please let me know", L" if you have any questions."},
    {L"looking forward", L" to hearing from you."},
    {L"thank you", L" for your time."},
    {L"as soon as", L" possible."},
    {L"to whom it may", L" concern:"}
};

/* Deliberately small: ambiguous prefixes are not offered from this list. */
static const WCHAR *const STATIC_WORDS[] = {
    L"autocomplete",
    L"available",
    L"because",
    L"completion",
    L"definitely",
    L"document",
    L"important",
    L"information",
    L"language",
    L"necessary",
    L"possible",
    L"receive",
    L"separate",
    L"suggestion",
    L"through",
    L"tomorrow",
    L"WordCraft"
};

static BOOL language_is_letter(WCHAR value)
{
    return iswalpha((wint_t)value) != 0;
}

static BOOL language_is_word_character(WCHAR value)
{
    return iswalnum((wint_t)value) != 0 || value == L'\'' ||
           value == (WCHAR)0x2019;
}

static BOOL language_equal_case_insensitive(const WCHAR *left,
                                            const WCHAR *right,
                                            size_t length)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        if (towlower((wint_t)left[index]) !=
            towlower((wint_t)right[index])) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL language_token_equals(const WCHAR *token, size_t tokenLength,
                                  const WCHAR *word)
{
    size_t wordLength = wcslen(word);
    return tokenLength == wordLength &&
           language_equal_case_insensitive(token, word, tokenLength);
}

static BOOL language_is_common_misspelling(const WCHAR *token,
                                           size_t tokenLength)
{
    size_t index;

    for (index = 0; index < ARRAYSIZE(COMMON_MISSPELLINGS); ++index) {
        if (language_token_equals(token, tokenLength,
                                  COMMON_MISSPELLINGS[index])) {
            return TRUE;
        }
    }
    return FALSE;
}

size_t language_find_common_misspellings(const WCHAR *text, size_t length,
                                         LanguageRange *ranges,
                                         size_t rangeCapacity)
{
    size_t position = 0;
    size_t written = 0;

    if (text == NULL || ranges == NULL || rangeCapacity == 0) {
        return 0;
    }

    while (position < length && written < rangeCapacity) {
        size_t start;

        while (position < length && !language_is_letter(text[position])) {
            ++position;
        }
        start = position;
        while (position < length && language_is_letter(text[position])) {
            ++position;
        }
        if (start == position) {
            continue;
        }
        if (language_is_common_misspelling(text + start, position - start) &&
            start <= (size_t)LONG_MAX &&
            position - start <= (size_t)LONG_MAX) {
            ranges[written].start = (LONG)start;
            ranges[written].length = (LONG)(position - start);
            ++written;
        }
    }
    return written;
}

static BOOL language_copy_completion(const WCHAR *source, size_t length,
                                     WCHAR *completion,
                                     size_t completionCapacity,
                                     const WCHAR *typedPrefix,
                                     size_t typedPrefixLength)
{
    BOOL sawLetter = FALSE;
    BOOL allUpper = TRUE;
    size_t index;

    if (length == 0 || length >= completionCapacity) {
        return FALSE;
    }
    if (typedPrefix != NULL) {
        for (index = 0; index < typedPrefixLength; ++index) {
            if (language_is_letter(typedPrefix[index])) {
                sawLetter = TRUE;
                if (iswlower((wint_t)typedPrefix[index])) {
                    allUpper = FALSE;
                }
            }
        }
    }
    for (index = 0; index < length; ++index) {
        WCHAR value = source[index];
        if (sawLetter && allUpper && language_is_letter(value)) {
            value = (WCHAR)towupper((wint_t)value);
        }
        completion[index] = value;
    }
    completion[length] = L'\0';
    return TRUE;
}

static size_t language_collect_context_starts(const WCHAR *text, size_t caret,
                                              size_t *starts,
                                              size_t startCapacity)
{
    size_t count = 0;
    size_t cursor = caret;

    while (count < startCapacity) {
        size_t wordEnd;

        while (cursor > 0 &&
               !language_is_word_character(text[cursor - 1])) {
            --cursor;
        }
        wordEnd = cursor;
        while (cursor > 0 &&
               language_is_word_character(text[cursor - 1])) {
            --cursor;
        }
        if (cursor == wordEnd) {
            break;
        }
        starts[count++] = cursor;
        if (cursor == 0) {
            break;
        }
    }
    return count;
}

static BOOL language_copy_prior_continuation(const WCHAR *text,
                                             size_t contextStart,
                                             size_t caret,
                                             WCHAR *completion,
                                             size_t completionCapacity)
{
    const size_t contextLength = caret - contextStart;
    size_t match = (size_t)-1;
    size_t position;
    size_t candidateStart;
    size_t candidateEnd;
    size_t maximumLength;
    size_t wordCharacters = 0;

    if (contextLength < 6 || contextLength > 96 ||
        contextStart < contextLength + 1) {
        return FALSE;
    }
    for (position = 0; position + contextLength < contextStart; ++position) {
        if (language_is_word_character(text[contextStart]) && position > 0 &&
            language_is_word_character(text[position - 1])) {
            continue;
        }
        if (language_equal_case_insensitive(text + position,
                                            text + contextStart,
                                            contextLength) &&
            (!language_is_word_character(text[caret - 1]) ||
             !language_is_word_character(
                 text[position + contextLength]))) {
            match = position;
        }
    }
    if (match == (size_t)-1) {
        return FALSE;
    }

    candidateStart = match + contextLength;
    if (candidateStart >= contextStart) {
        return FALSE;
    }
    maximumLength = completionCapacity - 1;
    if (maximumLength > LANGUAGE_COMPLETION_CAPACITY - 1) {
        maximumLength = LANGUAGE_COMPLETION_CAPACITY - 1;
    }
    candidateEnd = candidateStart;
    while (candidateEnd < contextStart &&
           candidateEnd - candidateStart < maximumLength) {
        WCHAR value = text[candidateEnd];
        if (value == L'\r' || value == L'\n' || value == L'\0') {
            break;
        }
        if (language_is_word_character(value)) {
            ++wordCharacters;
        }
        ++candidateEnd;
        if (value == L'.' || value == L'!' || value == L'?') {
            break;
        }
    }

    if (candidateEnd < contextStart && candidateEnd > candidateStart &&
        language_is_word_character(text[candidateEnd - 1]) &&
        language_is_word_character(text[candidateEnd])) {
        while (candidateEnd > candidateStart &&
               language_is_word_character(text[candidateEnd - 1])) {
            --candidateEnd;
        }
    }
    while (candidateEnd > candidateStart &&
           iswspace((wint_t)text[candidateEnd - 1])) {
        --candidateEnd;
    }
    if (wordCharacters < 2 || candidateEnd <= candidateStart) {
        return FALSE;
    }
    return language_copy_completion(text + candidateStart,
                                    candidateEnd - candidateStart,
                                    completion, completionCapacity, NULL, 0);
}

static BOOL language_predict_from_prior_context(const WCHAR *text,
                                                size_t caret,
                                                WCHAR *completion,
                                                size_t completionCapacity)
{
    size_t starts[6];
    size_t count = language_collect_context_starts(
        text, caret, starts, ARRAYSIZE(starts));
    size_t index;

    /* A repeated context needs at least two words to avoid noisy guesses. */
    for (index = count; index > 1; --index) {
        if (language_copy_prior_continuation(text, starts[index - 1], caret,
                                             completion,
                                             completionCapacity)) {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL language_predict_phrase(const WCHAR *text, size_t caret,
                                    WCHAR *completion,
                                    size_t completionCapacity)
{
    size_t index;

    for (index = 0; index < ARRAYSIZE(PHRASE_COMPLETIONS); ++index) {
        const WCHAR *trigger = PHRASE_COMPLETIONS[index].trigger;
        size_t triggerLength = wcslen(trigger);
        size_t start;

        if (triggerLength > caret) {
            continue;
        }
        start = caret - triggerLength;
        if (start > 0 && language_is_word_character(text[start - 1])) {
            continue;
        }
        if (language_equal_case_insensitive(text + start, trigger,
                                            triggerLength)) {
            const WCHAR *suffix = PHRASE_COMPLETIONS[index].suffix;
            return language_copy_completion(suffix, wcslen(suffix),
                                            completion, completionCapacity,
                                            NULL, 0);
        }
    }
    return FALSE;
}

static BOOL language_prefix_matches(const WCHAR *word, size_t wordLength,
                                    const WCHAR *prefix, size_t prefixLength)
{
    return wordLength > prefixLength &&
           language_equal_case_insensitive(word, prefix, prefixLength);
}

static BOOL language_same_candidate(const WordCandidate *candidate,
                                    const WCHAR *word, size_t wordLength)
{
    return candidate->length == wordLength &&
           language_equal_case_insensitive(candidate->latest, word,
                                           wordLength);
}

static BOOL language_predict_document_word(const WCHAR *text,
                                           size_t wordStart, size_t caret,
                                           WCHAR *completion,
                                           size_t completionCapacity)
{
    WordCandidate candidates[64];
    size_t candidateCount = 0;
    size_t prefixLength = caret - wordStart;
    size_t position = 0;
    size_t best = (size_t)-1;
    size_t index;

    while (position < wordStart) {
        size_t tokenStart;
        size_t tokenLength;

        while (position < wordStart &&
               !language_is_word_character(text[position])) {
            ++position;
        }
        tokenStart = position;
        while (position < wordStart &&
               language_is_word_character(text[position])) {
            ++position;
        }
        tokenLength = position - tokenStart;
        if (!language_prefix_matches(text + tokenStart, tokenLength,
                                     text + wordStart, prefixLength)) {
            continue;
        }
        for (index = 0; index < candidateCount; ++index) {
            if (language_same_candidate(&candidates[index], text + tokenStart,
                                        tokenLength)) {
                ++candidates[index].count;
                candidates[index].latest = text + tokenStart;
                candidates[index].latestPosition = tokenStart;
                break;
            }
        }
        if (index == candidateCount &&
            candidateCount < ARRAYSIZE(candidates)) {
            candidates[candidateCount].latest = text + tokenStart;
            candidates[candidateCount].length = tokenLength;
            candidates[candidateCount].count = 1;
            candidates[candidateCount].latestPosition = tokenStart;
            ++candidateCount;
        }
    }

    for (index = 0; index < candidateCount; ++index) {
        if (best == (size_t)-1 ||
            candidates[index].count > candidates[best].count ||
            (candidates[index].count == candidates[best].count &&
             candidates[index].latestPosition >
                 candidates[best].latestPosition)) {
            best = index;
        }
    }
    if (best == (size_t)-1) {
        return FALSE;
    }
    return language_copy_completion(
        candidates[best].latest + prefixLength,
        candidates[best].length - prefixLength,
        completion, completionCapacity, text + wordStart, prefixLength);
}

static BOOL language_predict_static_word(const WCHAR *prefix,
                                         size_t prefixLength,
                                         WCHAR *completion,
                                         size_t completionCapacity)
{
    const WCHAR *match = NULL;
    size_t matchLength = 0;
    size_t matches = 0;
    size_t index;

    for (index = 0; index < ARRAYSIZE(STATIC_WORDS); ++index) {
        size_t wordLength = wcslen(STATIC_WORDS[index]);
        if (language_prefix_matches(STATIC_WORDS[index], wordLength,
                                    prefix, prefixLength)) {
            match = STATIC_WORDS[index];
            matchLength = wordLength;
            ++matches;
        }
    }
    if (matches != 1) {
        return FALSE;
    }
    return language_copy_completion(match + prefixLength,
                                    matchLength - prefixLength,
                                    completion, completionCapacity,
                                    prefix, prefixLength);
}

static BOOL language_predict_word(const WCHAR *text, size_t caret,
                                  WCHAR *completion,
                                  size_t completionCapacity)
{
    size_t wordStart = caret;
    size_t prefixLength;

    while (wordStart > 0 &&
           language_is_word_character(text[wordStart - 1])) {
        --wordStart;
    }
    prefixLength = caret - wordStart;
    if (prefixLength < 3 || prefixLength > 64) {
        return FALSE;
    }
    if (language_predict_document_word(text, wordStart, caret, completion,
                                       completionCapacity)) {
        return TRUE;
    }
    return language_predict_static_word(text + wordStart, prefixLength,
                                        completion, completionCapacity);
}

BOOL language_predict_completion(const WCHAR *text, size_t length,
                                 size_t caret, WCHAR *completion,
                                 size_t completionCapacity)
{
    size_t phraseCaret;

    if (completion == NULL || completionCapacity == 0) {
        return FALSE;
    }
    completion[0] = L'\0';
    if (text == NULL || caret > length || caret == 0 ||
        completionCapacity < 2) {
        return FALSE;
    }

    if (language_predict_from_prior_context(text, caret, completion,
                                            completionCapacity)) {
        return TRUE;
    }
    phraseCaret = caret;
    while (phraseCaret > 0 &&
           (text[phraseCaret - 1] == L' ' ||
            text[phraseCaret - 1] == L'\t')) {
        --phraseCaret;
    }
    if (language_predict_phrase(text, phraseCaret, completion,
                                completionCapacity)) {
        if (phraseCaret != caret && completion[0] == L' ') {
            size_t completionLength = wcslen(completion);
            wmemmove(completion, completion + 1, completionLength);
        }
        return TRUE;
    }
    return language_predict_word(text, caret, completion,
                                 completionCapacity);
}
