#ifndef WORDCRAFT_LANGUAGE_H
#define WORDCRAFT_LANGUAGE_H

#include <stddef.h>
#include <windows.h>

#define LANGUAGE_COMPLETION_CAPACITY 160

typedef struct LanguageRange {
    LONG start;
    LONG length;
} LanguageRange;

/* A deterministic fallback for systems without the Windows spell service. */
size_t language_find_common_misspellings(const WCHAR *text, size_t length,
                                         LanguageRange *ranges,
                                         size_t rangeCapacity);

/* Produces text to append at caret, never text that is already present. */
BOOL language_predict_completion(const WCHAR *text, size_t length,
                                 size_t caret, WCHAR *completion,
                                 size_t completionCapacity);

#endif
