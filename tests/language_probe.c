#include "language.h"

#include <stdio.h>
#include <wchar.h>

static BOOL range_is(const WCHAR *text, const LanguageRange *range,
                     const WCHAR *expected)
{
    size_t expectedLength = wcslen(expected);
    return range->start >= 0 && range->length >= 0 &&
           (size_t)range->length == expectedLength &&
           wcsncmp(text + range->start, expected, expectedLength) == 0;
}

static BOOL expect_completion(const WCHAR *text, const WCHAR *expected)
{
    WCHAR completion[LANGUAGE_COMPLETION_CAPACITY];
    size_t length = wcslen(text);

    completion[0] = L'\0';
    return language_predict_completion(text, length, length, completion,
                                       ARRAYSIZE(completion)) &&
           wcscmp(completion, expected) == 0;
}

int wmain(void)
{
    static const WCHAR typoText[] =
        L"The teh fox will recieve a seperate package. Their form is "
        L"correct. TEH.";
    LanguageRange ranges[8];
    WCHAR completion[LANGUAGE_COMPLETION_CAPACITY];
    size_t typoCount;

    typoCount = language_find_common_misspellings(
        typoText, wcslen(typoText), ranges, ARRAYSIZE(ranges));
    if (typoCount != 4 || !range_is(typoText, &ranges[0], L"teh") ||
        !range_is(typoText, &ranges[1], L"recieve") ||
        !range_is(typoText, &ranges[2], L"seperate") ||
        !range_is(typoText, &ranges[3], L"TEH")) {
        fwprintf(stderr, L"common misspelling ranges were incorrect\n");
        return 1;
    }
    if (language_find_common_misspellings(
            typoText, wcslen(typoText), ranges, 2) != 2) {
        fwprintf(stderr, L"misspelling range capacity was not respected\n");
        return 1;
    }

    if (!expect_completion(L"Please let me know",
                           L" if you have any questions.")) {
        fwprintf(stderr, L"phrase completion failed\n");
        return 1;
    }
    if (!expect_completion(L"Thank you ", L"for your time.")) {
        fwprintf(stderr, L"phrase completion after a typed space failed\n");
        return 1;
    }
    if (!expect_completion(
            L"The quick brown fox jumps over the lazy dog.\r\n"
            L"The quick brown",
            L" fox jumps over the lazy dog.")) {
        fwprintf(stderr, L"prior-document context completion failed\n");
        return 1;
    }
    if (!expect_completion(L"WordCraft improves documents. Wor",
                           L"dCraft")) {
        fwprintf(stderr, L"document word-prefix completion failed\n");
        return 1;
    }
    if (!expect_completion(L"This docu", L"ment")) {
        fwprintf(stderr, L"static word-prefix completion failed\n");
        return 1;
    }

    completion[0] = L'x';
    if (language_predict_completion(
            L"The caterpillar moved. The cat ", 31, 31, completion,
            ARRAYSIZE(completion)) ||
        completion[0] != L'\0') {
        fwprintf(stderr, L"a partial-word context produced a noisy completion\n");
        return 1;
    }

    completion[0] = L'x';
    if (language_predict_completion(L"I a", 3, 3, completion,
                                    ARRAYSIZE(completion)) ||
        completion[0] != L'\0') {
        fwprintf(stderr, L"weak completion was not rejected\n");
        return 1;
    }
    completion[0] = L'x';
    if (language_predict_completion(L"document", 8, 9, completion,
                                    ARRAYSIZE(completion)) ||
        completion[0] != L'\0') {
        fwprintf(stderr, L"invalid caret was not rejected safely\n");
        return 1;
    }
    if (language_predict_completion(L"This docu", 9, 9, completion, 3)) {
        fwprintf(stderr, L"undersized completion buffer was not respected\n");
        return 1;
    }

    printf("misspellings=ok phrase=ok context=ok prefix=ok bounds=ok\n");
    return 0;
}
