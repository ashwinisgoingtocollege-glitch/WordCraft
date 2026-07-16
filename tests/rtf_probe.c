#include "editor.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef struct FixedBuffer {
    BYTE *data;
    SIZE_T capacity;
    SIZE_T size;
    SIZE_T position;
} FixedBuffer;

typedef struct ParagraphExpectation {
    const WCHAR *marker;
    WORD alignment;
    BOOL bullets;
} ParagraphExpectation;

static DWORD CALLBACK write_buffer(DWORD_PTR cookie, LPBYTE data,
                                   LONG requested, LONG *written)
{
    FixedBuffer *buffer = (FixedBuffer *)cookie;
    if (requested < 0 || buffer->size + (SIZE_T)requested > buffer->capacity) {
        *written = 0;
        return 1;
    }
    CopyMemory(buffer->data + buffer->size, data, (SIZE_T)requested);
    buffer->size += (SIZE_T)requested;
    *written = requested;
    return 0;
}

static DWORD CALLBACK read_buffer(DWORD_PTR cookie, LPBYTE data,
                                  LONG requested, LONG *read)
{
    FixedBuffer *buffer = (FixedBuffer *)cookie;
    SIZE_T remaining = buffer->size - buffer->position;
    SIZE_T amount = remaining < (SIZE_T)requested ? remaining : (SIZE_T)requested;
    CopyMemory(data, buffer->data + buffer->position, amount);
    buffer->position += amount;
    *read = (LONG)amount;
    return 0;
}

static LONG find_marker(const WCHAR *text, const WCHAR *marker)
{
    const WCHAR *match = wcsstr(text, marker);
    ptrdiff_t offset;

    if (match == NULL) {
        return -1;
    }
    offset = match - text;
    if (offset < 0 || offset > LONG_MAX - 1) {
        return -1;
    }
    return (LONG)offset;
}

static BOOL set_paragraph_alignment(HWND editor, LONG position, WORD alignment)
{
    PARAFORMAT2 paragraph;

    if (position < 0) {
        return FALSE;
    }
    SendMessageW(editor, EM_SETSEL, (WPARAM)position, (LPARAM)(position + 1));
    ZeroMemory(&paragraph, sizeof(paragraph));
    paragraph.cbSize = sizeof(paragraph);
    paragraph.dwMask = PFM_ALIGNMENT;
    paragraph.wAlignment = alignment;
    return SendMessageW(editor, EM_SETPARAFORMAT, 0,
                        (LPARAM)&paragraph) != 0;
}

static BOOL set_paragraph_bullets(HWND editor, LONG first, LONG last)
{
    PARAFORMAT2 paragraph;

    if (first < 0 || last < first || last == LONG_MAX) {
        return FALSE;
    }
    SendMessageW(editor, EM_SETSEL, (WPARAM)first, (LPARAM)(last + 1));
    ZeroMemory(&paragraph, sizeof(paragraph));
    paragraph.cbSize = sizeof(paragraph);
    paragraph.dwMask = PFM_NUMBERING | PFM_OFFSET;
    paragraph.wNumbering = PFN_BULLET;
    paragraph.dxOffset = 360;
    return SendMessageW(editor, EM_SETPARAFORMAT, 0,
                        (LPARAM)&paragraph) != 0;
}

static BOOL paragraph_matches(HWND editor, LONG position,
                              const ParagraphExpectation *expected)
{
    PARAFORMAT2 paragraph;
    BOOL hasBullets;

    if (position < 0 || expected == NULL) {
        return FALSE;
    }
    SendMessageW(editor, EM_SETSEL, (WPARAM)position, (LPARAM)(position + 1));
    ZeroMemory(&paragraph, sizeof(paragraph));
    paragraph.cbSize = sizeof(paragraph);
    SendMessageW(editor, EM_GETPARAFORMAT, 0, (LPARAM)&paragraph);
    if ((paragraph.dwMask & PFM_ALIGNMENT) == 0 ||
        paragraph.wAlignment != expected->alignment) {
        return FALSE;
    }
    hasBullets = (paragraph.dwMask & PFM_NUMBERING) != 0 &&
                 paragraph.wNumbering == PFN_BULLET;
    return hasBullets == expected->bullets;
}

static BOOL all_paragraphs_match(HWND editor, const WCHAR *text,
                                 const ParagraphExpectation *expected,
                                 SIZE_T count)
{
    SIZE_T index;

    for (index = 0; index < count; ++index) {
        LONG position = find_marker(text, expected[index].marker);
        if (!paragraph_matches(editor, position, &expected[index])) {
            return FALSE;
        }
    }
    return TRUE;
}

int main(void)
{
    static const WCHAR source[] = L"Bold caf\x00E9 \x6F22\x5B57 plain";
    static const WCHAR paragraphSource[] =
        L"Left paragraph.\r"
        L"Center paragraph.\r"
        L"Right paragraph.\r"
        L"Justified paragraph has enough words to wrap across multiple lines "
        L"inside the narrow probe control.\r"
        L"First bullet paragraph.\r"
        L"Second bullet paragraph.\r"
        L"Plain paragraph after bullets.";
    static const ParagraphExpectation paragraphExpectations[] = {
        {L"Left paragraph", PFA_LEFT, FALSE},
        {L"Center paragraph", PFA_CENTER, FALSE},
        {L"Right paragraph", PFA_RIGHT, FALSE},
        {L"Justified paragraph", PFA_JUSTIFY, FALSE},
        {L"First bullet paragraph", PFA_LEFT, TRUE},
        {L"Second bullet paragraph", PFA_LEFT, TRUE},
        {L"Plain paragraph after bullets", PFA_LEFT, FALSE},
    };
    HMODULE rich = LoadLibraryW(L"Msftedit.dll");
    HWND parent;
    HWND editor;
    FixedBuffer buffer;
    EDITSTREAM stream;
    CHARFORMAT2W format;
    WCHAR restored[1024];
    LONG positions[ARRAYSIZE(paragraphExpectations)];
    SIZE_T index;
    int result = 1;

    if (rich == NULL) {
        return 10;
    }
    parent = CreateWindowExW(0, L"STATIC", L"probe", WS_OVERLAPPED,
                             0, 0, 320, 200, NULL, NULL, GetModuleHandleW(NULL), NULL);
    editor = CreateWindowExW(0, MSFTEDIT_CLASS, NULL, WS_CHILD | ES_MULTILINE,
                             0, 0, 300, 180, parent, NULL, GetModuleHandleW(NULL), NULL);
    if (parent == NULL || editor == NULL) {
        result = 11;
        goto cleanup;
    }
    SendMessageW(editor, EM_EXLIMITTEXT, 0, 0x7FFFFFFE);
    SetWindowTextW(editor, source);
    SendMessageW(editor, EM_SETSEL, 0, 4);
    ZeroMemory(&format, sizeof(format));
    format.cbSize = sizeof(format);
    format.dwMask = CFM_BOLD;
    format.dwEffects = CFE_BOLD;
    if (!SendMessageW(editor, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&format)) {
        result = 12;
        goto cleanup;
    }

    ZeroMemory(&buffer, sizeof(buffer));
    buffer.capacity = 1024 * 1024;
    buffer.data = HeapAlloc(GetProcessHeap(), 0, buffer.capacity);
    if (buffer.data == NULL) {
        result = 13;
        goto cleanup;
    }
    ZeroMemory(&stream, sizeof(stream));
    stream.dwCookie = (DWORD_PTR)&buffer;
    stream.pfnCallback = write_buffer;
    SendMessageW(editor, EM_STREAMOUT, SF_RTF, (LPARAM)&stream);
    if (stream.dwError != 0 || buffer.size < 5 ||
        memcmp(buffer.data, "{\\rtf", 5) != 0) {
        result = 14;
        goto cleanup_buffer;
    }

    SetWindowTextW(editor, L"");
    buffer.position = 0;
    ZeroMemory(&stream, sizeof(stream));
    stream.dwCookie = (DWORD_PTR)&buffer;
    stream.pfnCallback = read_buffer;
    SendMessageW(editor, EM_STREAMIN, SF_RTF, (LPARAM)&stream);
    GetWindowTextW(editor, restored, ARRAYSIZE(restored));
    if (stream.dwError != 0 || wcscmp(restored, source) != 0) {
        result = 15;
        goto cleanup_buffer;
    }
    SendMessageW(editor, EM_SETSEL, 0, 4);
    ZeroMemory(&format, sizeof(format));
    format.cbSize = sizeof(format);
    SendMessageW(editor, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&format);
    if ((format.dwMask & CFM_BOLD) == 0 || (format.dwEffects & CFE_BOLD) == 0) {
        result = 16;
        goto cleanup_buffer;
    }
    SendMessageW(editor, EM_SETSEL, 5, -1);
    ZeroMemory(&format, sizeof(format));
    format.cbSize = sizeof(format);
    SendMessageW(editor, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&format);
    if ((format.dwMask & CFM_BOLD) == 0 || (format.dwEffects & CFE_BOLD) != 0) {
        result = 17;
        goto cleanup_buffer;
    }

    if (!SetWindowTextW(editor, paragraphSource) ||
        GetWindowTextW(editor, restored, ARRAYSIZE(restored)) <= 0) {
        result = 18;
        goto cleanup_buffer;
    }
    for (index = 0; index < ARRAYSIZE(paragraphExpectations); ++index) {
        positions[index] = find_marker(restored, paragraphExpectations[index].marker);
        if (positions[index] < 0) {
            result = 19;
            goto cleanup_buffer;
        }
    }
    if (!set_paragraph_alignment(editor, positions[0], PFA_LEFT) ||
        !set_paragraph_alignment(editor, positions[1], PFA_CENTER) ||
        !set_paragraph_alignment(editor, positions[2], PFA_RIGHT) ||
        !set_paragraph_alignment(editor, positions[3], PFA_JUSTIFY) ||
        !set_paragraph_bullets(editor, positions[4], positions[5])) {
        result = 20;
        goto cleanup_buffer;
    }
    if (!all_paragraphs_match(editor, restored, paragraphExpectations,
                              ARRAYSIZE(paragraphExpectations))) {
        result = 21;
        goto cleanup_buffer;
    }

    buffer.size = 0;
    buffer.position = 0;
    ZeroMemory(&stream, sizeof(stream));
    stream.dwCookie = (DWORD_PTR)&buffer;
    stream.pfnCallback = write_buffer;
    SendMessageW(editor, EM_STREAMOUT, SF_RTF, (LPARAM)&stream);
    if (stream.dwError != 0 || buffer.size < 5 ||
        memcmp(buffer.data, "{\\rtf", 5) != 0) {
        result = 22;
        goto cleanup_buffer;
    }

    SetWindowTextW(editor, L"");
    buffer.position = 0;
    ZeroMemory(&stream, sizeof(stream));
    stream.dwCookie = (DWORD_PTR)&buffer;
    stream.pfnCallback = read_buffer;
    SendMessageW(editor, EM_STREAMIN, SF_RTF, (LPARAM)&stream);
    ZeroMemory(restored, sizeof(restored));
    if (stream.dwError != 0 ||
        GetWindowTextW(editor, restored, ARRAYSIZE(restored)) <= 0 ||
        !all_paragraphs_match(editor, restored, paragraphExpectations,
                              ARRAYSIZE(paragraphExpectations))) {
        result = 23;
        goto cleanup_buffer;
    }
    printf("rtf_unicode=ok formatting_round_trip=ok\n");
    result = 0;

cleanup_buffer:
    HeapFree(GetProcessHeap(), 0, buffer.data);
cleanup:
    if (parent != NULL) {
        DestroyWindow(parent);
    }
    FreeLibrary(rich);
    return result;
}
