#ifndef COBJMACROS
#define COBJMACROS
#endif
#include "editor.h"
#include "live.h"
#include "rendereditor.h"

#include <limits.h>
#include <richole.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <tom.h>

#ifndef SFF_SELECTION
#define SFF_SELECTION 0x8000
#endif

#ifndef CFM_OUTLINE
#define CFM_OUTLINE 0x00000200
#endif
#ifndef CFE_OUTLINE
#define CFE_OUTLINE CFM_OUTLINE
#endif
#ifndef CFM_SHADOW
#define CFM_SHADOW 0x00000400
#endif
#ifndef CFE_SHADOW
#define CFE_SHADOW CFM_SHADOW
#endif
#ifndef CFM_LINK
#define CFM_LINK 0x00000020
#endif
#ifndef CFE_LINK
#define CFE_LINK CFM_LINK
#endif
#ifndef AURL_ENABLEURL
#define AURL_ENABLEURL 0x00000001
#endif

#define INSERT_PROMPT_CAPACITY 2048
#define INSERT_IMAGE_FILE_LIMIT ((SIZE_T)6u * 1024u * 1024u)
#define INSERT_IMAGE_DIMENSION_LIMIT 32768u
#define INSERT_RTF_LIMIT ((SIZE_T)16u * 1024u * 1024u)
#define INSERT_TABLE_ROW_LIMIT 20u
#define INSERT_TABLE_COLUMN_LIMIT 10u

typedef struct InsertByteBuffer {
    BYTE *data;
    SIZE_T size;
    SIZE_T capacity;
    DWORD error;
} InsertByteBuffer;

typedef struct InsertPromptState {
    const WCHAR *title;
    const WCHAR *label;
    const WCHAR *hint;
    const WCHAR *initial;
    WCHAR *output;
    SIZE_T outputCapacity;
    BOOL accepted;
} InsertPromptState;

typedef struct InsertTableState {
    UINT rows;
    UINT columns;
    BOOL accepted;
} InsertTableState;

typedef struct InsertMemoryStream {
    const BYTE *data;
    SIZE_T size;
    SIZE_T position;
} InsertMemoryStream;

typedef enum InsertPictureKind {
    INSERT_PICTURE_PNG = 0,
    INSERT_PICTURE_JPEG,
    INSERT_PICTURE_DIB,
    INSERT_PICTURE_EMF
} InsertPictureKind;

static const IID insertIidTextDocument = {
    0x8CC497C0, 0xA1DF, 0x11CE,
    {0x80, 0x98, 0x00, 0xAA, 0x00, 0x47, 0xBE, 0x5D}
};

static BOOL insert_buffer_reserve(InsertByteBuffer *buffer, SIZE_T additional)
{
    SIZE_T needed;
    SIZE_T capacity;
    BYTE *grown;

    if (buffer == NULL || buffer->error != ERROR_SUCCESS) {
        return FALSE;
    }
    if (additional > SIZE_MAX - buffer->size) {
        buffer->error = ERROR_NOT_ENOUGH_MEMORY;
        return FALSE;
    }
    needed = buffer->size + additional;
    if (needed > INSERT_RTF_LIMIT) {
        buffer->error = ERROR_FILE_TOO_LARGE;
        return FALSE;
    }
    if (needed <= buffer->capacity) {
        return TRUE;
    }
    capacity = buffer->capacity == 0 ? 1024u : buffer->capacity;
    while (capacity < needed) {
        if (capacity > INSERT_RTF_LIMIT / 2u) {
            capacity = INSERT_RTF_LIMIT;
            break;
        }
        capacity *= 2u;
    }
    if (buffer->data == NULL) {
        grown = HeapAlloc(GetProcessHeap(), 0, capacity);
    } else {
        grown = HeapReAlloc(GetProcessHeap(), 0, buffer->data, capacity);
    }
    if (grown == NULL) {
        buffer->error = ERROR_NOT_ENOUGH_MEMORY;
        return FALSE;
    }
    buffer->data = grown;
    buffer->capacity = capacity;
    return TRUE;
}

static BOOL insert_buffer_append(InsertByteBuffer *buffer,
                                 const void *data, SIZE_T size)
{
    if ((data == NULL && size != 0) ||
        !insert_buffer_reserve(buffer, size)) {
        return FALSE;
    }
    if (size != 0) {
        CopyMemory(buffer->data + buffer->size, data, size);
        buffer->size += size;
    }
    return TRUE;
}

static BOOL insert_buffer_ascii(InsertByteBuffer *buffer, const char *text)
{
    return text != NULL &&
           insert_buffer_append(buffer, text, strlen(text));
}

static BOOL insert_buffer_printf(InsertByteBuffer *buffer,
                                 const char *format, ...)
{
    char text[256];
    va_list arguments;
    HRESULT status;

    va_start(arguments, format);
    status = StringCchVPrintfA(text, ARRAYSIZE(text), format, arguments);
    va_end(arguments);
    if (FAILED(status)) {
        buffer->error = ERROR_INSUFFICIENT_BUFFER;
        return FALSE;
    }
    return insert_buffer_ascii(buffer, text);
}

static BOOL insert_buffer_rtf_text(InsertByteBuffer *buffer,
                                   const WCHAR *text)
{
    SIZE_T index;

    if (buffer == NULL || text == NULL) {
        return FALSE;
    }
    for (index = 0; text[index] != L'\0'; ++index) {
        WCHAR value = text[index];
        if (value == L'\\' || value == L'{' || value == L'}') {
            char escaped[2] = {'\\', (char)value};
            if (!insert_buffer_append(buffer, escaped, sizeof(escaped))) {
                return FALSE;
            }
        } else if (value == L'\r') {
            if (text[index + 1] == L'\n') {
                ++index;
            }
            if (!insert_buffer_ascii(buffer, "\\line ")) {
                return FALSE;
            }
        } else if (value == L'\n') {
            if (!insert_buffer_ascii(buffer, "\\line ")) {
                return FALSE;
            }
        } else if (value == L'\t') {
            if (!insert_buffer_ascii(buffer, "\\tab ")) {
                return FALSE;
            }
        } else if (value >= 0x20 && value <= 0x7e) {
            BYTE byte = (BYTE)value;
            if (!insert_buffer_append(buffer, &byte, 1)) {
                return FALSE;
            }
        } else if (value >= 0x20) {
            if (!insert_buffer_printf(buffer, "\\u%d?",
                                      (int)(SHORT)value)) {
                return FALSE;
            }
        }
    }
    return TRUE;
}

static void insert_buffer_free(InsertByteBuffer *buffer)
{
    if (buffer != NULL) {
        if (buffer->data != NULL) {
            HeapFree(GetProcessHeap(), 0, buffer->data);
        }
        ZeroMemory(buffer, sizeof(*buffer));
    }
}

static DWORD CALLBACK insert_stream_read(DWORD_PTR cookie, LPBYTE output,
                                         LONG requested, LONG *transferred)
{
    InsertMemoryStream *stream = (InsertMemoryStream *)cookie;
    SIZE_T available;
    SIZE_T amount;

    if (stream == NULL || output == NULL || transferred == NULL ||
        requested < 0 || stream->position > stream->size) {
        return ERROR_INVALID_PARAMETER;
    }
    available = stream->size - stream->position;
    amount = min(available, (SIZE_T)requested);
    if (amount != 0) {
        CopyMemory(output, stream->data + stream->position, amount);
        stream->position += amount;
    }
    *transferred = (LONG)amount;
    return ERROR_SUCCESS;
}

static ITextDocument *insert_begin_edit_collection(HWND editor)
{
    IRichEditOle *richEditOle = NULL;
    ITextDocument *document = NULL;

    if (editor == NULL ||
        !SendMessageW(editor, EM_GETOLEINTERFACE, 0,
                      (LPARAM)&richEditOle) ||
        richEditOle == NULL) {
        return NULL;
    }
    if (FAILED(richEditOle->lpVtbl->QueryInterface(
            richEditOle, &insertIidTextDocument, (void **)&document))) {
        document = NULL;
    }
    richEditOle->lpVtbl->Release(richEditOle);
    if (document != NULL &&
        FAILED(ITextDocument_BeginEditCollection(document))) {
        ITextDocument_Release(document);
        document = NULL;
    }
    return document;
}

static void insert_end_edit_collection(ITextDocument *document)
{
    if (document != NULL) {
        ITextDocument_EndEditCollection(document);
        ITextDocument_Release(document);
    }
}

static BOOL insert_query_object_state(
    HWND editor, const CHARRANGE *selection,
    LONG *objectCount, LONG *selectedObjectCount)
{
    IRichEditOle *richEditOle = NULL;
    LONG count;
    LONG selected = 0;
    LONG index;

    if (editor == NULL || objectCount == NULL ||
        selectedObjectCount == NULL ||
        !SendMessageW(editor, EM_GETOLEINTERFACE, 0,
                      (LPARAM)&richEditOle) ||
        richEditOle == NULL) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    count = richEditOle->lpVtbl->GetObjectCount(richEditOle);
    if (count < 0) {
        richEditOle->lpVtbl->Release(richEditOle);
        SetLastError(ERROR_CAN_NOT_COMPLETE);
        return FALSE;
    }
    if (selection != NULL && selection->cpMin != selection->cpMax) {
        LONG first = min(selection->cpMin, selection->cpMax);
        LONG last = max(selection->cpMin, selection->cpMax);

        for (index = 0; index < count; ++index) {
            REOBJECT object;

            ZeroMemory(&object, sizeof(object));
            object.cbStruct = sizeof(object);
            if (FAILED(richEditOle->lpVtbl->GetObject(
                    richEditOle, index, &object,
                    REO_GETOBJ_NO_INTERFACES))) {
                richEditOle->lpVtbl->Release(richEditOle);
                SetLastError(ERROR_CAN_NOT_COMPLETE);
                return FALSE;
            }
            if (object.cp >= first && object.cp < last) {
                ++selected;
            }
        }
    }
    richEditOle->lpVtbl->Release(richEditOle);
    *objectCount = count;
    *selectedObjectCount = selected;
    return TRUE;
}

static BOOL insert_stream_selection_expected_objects(
    AppState *app, const BYTE *data, SIZE_T size, BOOL formatted,
    const WCHAR *statusText, UINT expectedObjects)
{
    InsertMemoryStream memory;
    EDITSTREAM stream;
    ITextDocument *collection;
    CHARRANGE selection = {0, 0};
    LRESULT streamResult;
    LONG beforeObjects = 0;
    LONG selectedObjects = 0;
    LONG afterObjects = 0;
    LONG ignoredSelected = 0;
    LONG beforeLength;
    LONG afterLength;
    DWORD pictureError = ERROR_SUCCESS;
    BOOL pictureStreamOk;
    BOOL afterObjectStateOk = expectedObjects == 0;

    if (app == NULL || app->editor == NULL || data == NULL || size == 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    beforeLength = GetWindowTextLengthW(app->editor);
    if (expectedObjects != 0) {
        SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
        if (!insert_query_object_state(
                app->editor, &selection, &beforeObjects,
                &selectedObjects) ||
            selectedObjects > beforeObjects) {
            return FALSE;
        }
    }
    if (!render_editor_begin_static_picture_stream(app->editor)) {
        return FALSE;
    }
    ZeroMemory(&memory, sizeof(memory));
    memory.data = data;
    memory.size = size;
    ZeroMemory(&stream, sizeof(stream));
    stream.dwCookie = (DWORD_PTR)&memory;
    stream.pfnCallback = insert_stream_read;

    collection = insert_begin_edit_collection(app->editor);
    streamResult = SendMessageW(
        app->editor, EM_STREAMIN, SF_RTF | SFF_SELECTION,
        (LPARAM)&stream);
    insert_end_edit_collection(collection);
    pictureStreamOk = render_editor_end_static_picture_stream(
        app->editor, &pictureError);
    afterLength = GetWindowTextLengthW(app->editor);
    if (expectedObjects != 0 &&
        insert_query_object_state(
            app->editor, NULL, &afterObjects, &ignoredSelected)) {
        afterObjectStateOk = TRUE;
    } else if (expectedObjects != 0) {
        pictureStreamOk = FALSE;
        if (pictureError == ERROR_SUCCESS) {
            pictureError = GetLastError();
        }
    }
    if (stream.dwError != ERROR_SUCCESS || !pictureStreamOk ||
        (expectedObjects != 0 &&
         (!afterObjectStateOk || streamResult <= 0 ||
          afterObjects != beforeObjects - selectedObjects +
                              (LONG)expectedObjects))) {
        if (streamResult != 0 || afterLength != beforeLength ||
            (expectedObjects != 0 && afterObjectStateOk &&
             afterObjects != beforeObjects)) {
            SendMessageW(app->editor, EM_UNDO, 0, 0);
        }
        SetLastError(
            stream.dwError != ERROR_SUCCESS
                ? stream.dwError
                : pictureError != ERROR_SUCCESS
                      ? pictureError : ERROR_CAN_NOT_COMPLETE);
        return FALSE;
    }
    if (formatted) {
        app->richFormattingUsed = TRUE;
    }
    SetFocus(app->editor);
    if (statusText != NULL) {
        app_set_status_message(app, statusText);
    }
    return TRUE;
}

static BOOL insert_stream_selection(AppState *app,
                                    const BYTE *data, SIZE_T size,
                                    BOOL formatted,
                                    const WCHAR *statusText)
{
    return insert_stream_selection_expected_objects(
        app, data, size, formatted, statusText, 0);
}

static void insert_report_failure(AppState *app, const WCHAR *action)
{
    DWORD error = GetLastError();
    if (error == ERROR_SUCCESS) {
        error = ERROR_INVALID_DATA;
    }
    app_show_error(app != NULL ? app->mainWindow : NULL, action, error);
}

static void insert_trim(WCHAR *text)
{
    WCHAR *begin;
    WCHAR *end;

    if (text == NULL) {
        return;
    }
    begin = text;
    while (*begin != L'\0' && iswspace(*begin)) {
        ++begin;
    }
    if (begin != text) {
        MoveMemory(text, begin, (lstrlenW(begin) + 1) * sizeof(WCHAR));
    }
    end = text + lstrlenW(text);
    while (end > text && iswspace(end[-1])) {
        --end;
    }
    *end = L'\0';
}

static INT_PTR CALLBACK insert_prompt_dialog_proc(
    HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    InsertPromptState *state =
        (InsertPromptState *)GetWindowLongPtrW(dialog, DWLP_USER);

    switch (message) {
    case WM_INITDIALOG:
        state = (InsertPromptState *)lParam;
        if (state == NULL) {
            return FALSE;
        }
        SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)state);
        SetWindowTextW(dialog, state->title);
        SetDlgItemTextW(dialog, IDC_INSERT_PROMPT_LABEL, state->label);
        SetDlgItemTextW(dialog, IDC_INSERT_PROMPT_HINT,
                        state->hint != NULL ? state->hint : L"");
        SetDlgItemTextW(dialog, IDC_INSERT_PROMPT_EDIT,
                        state->initial != NULL ? state->initial : L"");
        SendDlgItemMessageW(dialog, IDC_INSERT_PROMPT_EDIT,
                            EM_SETLIMITTEXT,
                            state->outputCapacity > 0
                                ? state->outputCapacity - 1
                                : 0,
                            0);
        SendDlgItemMessageW(dialog, IDC_INSERT_PROMPT_EDIT,
                            EM_SETSEL, 0, -1);
        SetFocus(GetDlgItem(dialog, IDC_INSERT_PROMPT_EDIT));
        return FALSE;
    case WM_COMMAND:
        if (state == NULL) {
            break;
        }
        switch (LOWORD(wParam)) {
        case IDOK:
            if (state->output == NULL || state->outputCapacity == 0 ||
                GetDlgItemTextW(dialog, IDC_INSERT_PROMPT_EDIT,
                                state->output,
                                (int)min(state->outputCapacity,
                                         (SIZE_T)INT_MAX)) < 0) {
                MessageBeep(MB_ICONWARNING);
                return TRUE;
            }
            state->output[state->outputCapacity - 1] = L'\0';
            insert_trim(state->output);
            if (state->output[0] == L'\0') {
                MessageBoxW(dialog, L"Enter a value before continuing.",
                            state->title, MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(dialog, IDC_INSERT_PROMPT_EDIT));
                return TRUE;
            }
            state->accepted = TRUE;
            EndDialog(dialog, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return FALSE;
}

static BOOL insert_prompt(AppState *app, const WCHAR *title,
                          const WCHAR *label, const WCHAR *hint,
                          const WCHAR *initial, WCHAR *output,
                          SIZE_T outputCapacity)
{
    InsertPromptState state;
    INT_PTR result;

    ZeroMemory(&state, sizeof(state));
    state.title = title;
    state.label = label;
    state.hint = hint;
    state.initial = initial;
    state.output = output;
    state.outputCapacity = outputCapacity;
    result = DialogBoxParamW(
        app->instance, MAKEINTRESOURCEW(IDD_INSERT_PROMPT),
        app->mainWindow, insert_prompt_dialog_proc, (LPARAM)&state);
    if (result == -1) {
        app_show_error(app->mainWindow,
                       L"The Insert dialog could not be opened.",
                       GetLastError());
        return FALSE;
    }
    return state.accepted;
}

static INT_PTR CALLBACK insert_table_dialog_proc(
    HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    InsertTableState *state =
        (InsertTableState *)GetWindowLongPtrW(dialog, DWLP_USER);

    switch (message) {
    case WM_INITDIALOG:
        state = (InsertTableState *)lParam;
        if (state == NULL) {
            return FALSE;
        }
        SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)state);
        SetDlgItemInt(dialog, IDC_INSERT_TABLE_ROWS, state->rows, FALSE);
        SetDlgItemInt(dialog, IDC_INSERT_TABLE_COLUMNS, state->columns,
                      FALSE);
        SendDlgItemMessageW(dialog, IDC_INSERT_TABLE_ROWS,
                            EM_SETLIMITTEXT, 2, 0);
        SendDlgItemMessageW(dialog, IDC_INSERT_TABLE_COLUMNS,
                            EM_SETLIMITTEXT, 2, 0);
        return TRUE;
    case WM_COMMAND:
        if (state == NULL) {
            break;
        }
        if (LOWORD(wParam) == IDOK) {
            BOOL rowsValid = FALSE;
            BOOL columnsValid = FALSE;
            UINT rows = GetDlgItemInt(dialog, IDC_INSERT_TABLE_ROWS,
                                      &rowsValid, FALSE);
            UINT columns = GetDlgItemInt(
                dialog, IDC_INSERT_TABLE_COLUMNS, &columnsValid, FALSE);
            if (!rowsValid || !columnsValid || rows < 1 ||
                rows > INSERT_TABLE_ROW_LIMIT || columns < 1 ||
                columns > INSERT_TABLE_COLUMN_LIMIT) {
                MessageBoxW(
                    dialog,
                    L"Rows must be from 1 to 20 and columns from 1 to 10.",
                    L"Insert Table", MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            state->rows = rows;
            state->columns = columns;
            state->accepted = TRUE;
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;
    default:
        break;
    }
    return FALSE;
}

static BOOL insert_table_prompt(AppState *app, UINT *rows, UINT *columns)
{
    InsertTableState state;
    INT_PTR result;

    state.rows = 2;
    state.columns = 2;
    state.accepted = FALSE;
    result = DialogBoxParamW(
        app->instance, MAKEINTRESOURCEW(IDD_INSERT_TABLE),
        app->mainWindow, insert_table_dialog_proc, (LPARAM)&state);
    if (result == -1) {
        app_show_error(app->mainWindow,
                       L"The Insert Table dialog could not be opened.",
                       GetLastError());
        return FALSE;
    }
    if (!state.accepted) {
        return FALSE;
    }
    *rows = state.rows;
    *columns = state.columns;
    return TRUE;
}

static LONG insert_printable_width_twips(const AppState *app)
{
    LONGLONG width;

    if (app == NULL) {
        return 9360;
    }
    width = (LONGLONG)app->pageSize.x - app->pageMargins.left -
            app->pageMargins.right;
    if (width < 1000) {
        width = 6500;
    }
    width = width * 1440 / 1000;
    if (width < 1440) {
        width = 1440;
    }
    if (width > 30000) {
        width = 30000;
    }
    return (LONG)width;
}

static BOOL insert_build_table_rtf(AppState *app, UINT rows, UINT columns,
                                   const WCHAR *const *cellText,
                                   InsertByteBuffer *buffer)
{
    UINT row;
    UINT column;
    LONG width;

    if (rows == 0 || columns == 0 ||
        rows > INSERT_TABLE_ROW_LIMIT ||
        columns > INSERT_TABLE_COLUMN_LIMIT) {
        buffer->error = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    width = insert_printable_width_twips(app);
    if (!insert_buffer_ascii(
            buffer,
            "{\\rtf1\\ansi\\uc1\\deff0"
            "{\\fonttbl{\\f0\\fnil Aptos;}}"
            "\\f0\\fs22 ")) {
        return FALSE;
    }
    for (row = 0; row < rows; ++row) {
        if (!insert_buffer_ascii(
                buffer, "\\trowd\\trgaph108\\trleft0")) {
            return FALSE;
        }
        for (column = 0; column < columns; ++column) {
            LONG right = (LONG)(((LONGLONG)width * (column + 1)) /
                                columns);
            if (!insert_buffer_ascii(
                    buffer,
                    "\\clbrdrt\\brdrs\\brdrw10"
                    "\\clbrdrl\\brdrs\\brdrw10"
                    "\\clbrdrb\\brdrs\\brdrw10"
                    "\\clbrdrr\\brdrs\\brdrw10") ||
                !insert_buffer_printf(buffer, "\\cellx%ld", right)) {
                return FALSE;
            }
        }
        for (column = 0; column < columns; ++column) {
            const WCHAR *text = cellText != NULL
                                    ? cellText[row * columns + column]
                                    : NULL;
            if (!insert_buffer_ascii(
                    buffer, "\\pard\\intbl\\ql\\li72\\ri72 ")) {
                return FALSE;
            }
            if (text != NULL && !insert_buffer_rtf_text(buffer, text)) {
                return FALSE;
            }
            if (!insert_buffer_ascii(buffer, "\\cell ")) {
                return FALSE;
            }
        }
        if (!insert_buffer_ascii(buffer, "\\row ")) {
            return FALSE;
        }
    }
    return insert_buffer_ascii(buffer, "\\pard\\par}");
}

static BOOL insert_page_break_text(AppState *app, BOOL blankPage)
{
    static const BYTE oneBreak[] = "{\\rtf1\\ansi\\page }";
    static const BYTE twoBreaks[] = "{\\rtf1\\ansi\\page\\page }";
    CHARRANGE selection;
    LONG textLength;
    BOOL useTwoBreaks;

    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
    textLength = GetWindowTextLengthW(app->editor);
    useTwoBreaks = blankPage && selection.cpMin > 0 &&
                   selection.cpMax < textLength;
    return insert_stream_selection(
        app, useTwoBreaks ? twoBreaks : oneBreak,
        useTwoBreaks ? sizeof(twoBreaks) - 1 : sizeof(oneBreak) - 1,
        TRUE,
        blankPage ? L"Blank page inserted" : L"Page break inserted");
}

static BOOL insert_cover_page(AppState *app)
{
    InsertByteBuffer rtf = {0};
    CHARRANGE beginning = {0, 0};
    SYSTEMTIME current;
    WCHAR date[96] = L"";
    BOOL success;

    GetLocalTime(&current);
    (void)GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE,
                          &current, NULL, date, ARRAYSIZE(date), NULL);
    SendMessageW(app->editor, EM_EXSETSEL, 0, (LPARAM)&beginning);
    if (!insert_buffer_ascii(
            &rtf,
            "{\\rtf1\\ansi\\uc1\\deff0"
            "{\\fonttbl{\\f0\\fnil Aptos;}}"
            "{\\colortbl;\\red31\\green78\\blue121;"
            "\\red90\\green90\\blue90;}"
            "\\pard\\qc\\sb960\\sa360\\f0\\cf1\\fs64\\b "
            "Document Title\\b0\\par"
            "\\sa240\\cf2\\fs30 Document subtitle\\par"
            "\\sb720\\sa120\\cf0\\fs24 Author name\\par"
            "\\sa720\\fs20 ") ||
        !insert_buffer_rtf_text(&rtf, date[0] != L'\0' ? date : L"Date") ||
        !insert_buffer_ascii(
            &rtf,
            "\\par\\page\\pard\\ql\\cf0\\fs24 }")) {
        SetLastError(rtf.error);
        insert_buffer_free(&rtf);
        return FALSE;
    }
    success = insert_stream_selection(
        app, rtf.data, rtf.size, TRUE, L"Cover page inserted");
    insert_buffer_free(&rtf);
    return success;
}

static BOOL insert_table(AppState *app)
{
    UINT rows;
    UINT columns;
    InsertByteBuffer rtf = {0};
    BOOL success;

    if (!insert_table_prompt(app, &rows, &columns)) {
        return TRUE;
    }
    if (!insert_build_table_rtf(app, rows, columns, NULL, &rtf)) {
        SetLastError(rtf.error);
        insert_buffer_free(&rtf);
        return FALSE;
    }
    success = insert_stream_selection(
        app, rtf.data, rtf.size, TRUE, L"Table inserted");
    insert_buffer_free(&rtf);
    return success;
}

static uint16_t insert_read_le16(const BYTE *data)
{
    return (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t insert_read_le32(const BYTE *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint32_t insert_read_be32(const BYTE *data)
{
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static BOOL insert_png_dimensions(const BYTE *data, SIZE_T size,
                                  UINT *width, UINT *height)
{
    static const BYTE signature[] = {
        0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a
    };
    uint32_t w;
    uint32_t h;

    if (size < 24 || memcmp(data, signature, sizeof(signature)) != 0 ||
        memcmp(data + 12, "IHDR", 4) != 0) {
        return FALSE;
    }
    w = insert_read_be32(data + 16);
    h = insert_read_be32(data + 20);
    if (w == 0 || h == 0 || w > INSERT_IMAGE_DIMENSION_LIMIT ||
        h > INSERT_IMAGE_DIMENSION_LIMIT) {
        return FALSE;
    }
    *width = (UINT)w;
    *height = (UINT)h;
    return TRUE;
}

static BOOL insert_jpeg_dimensions(const BYTE *data, SIZE_T size,
                                   UINT *width, UINT *height)
{
    SIZE_T position = 2;

    if (size < 4 || data[0] != 0xff || data[1] != 0xd8) {
        return FALSE;
    }
    while (position + 3 < size) {
        BYTE marker;
        SIZE_T segmentLength;

        while (position < size && data[position] != 0xff) {
            ++position;
        }
        while (position < size && data[position] == 0xff) {
            ++position;
        }
        if (position >= size) {
            break;
        }
        marker = data[position++];
        if (marker == 0xd8 || marker == 0xd9 ||
            (marker >= 0xd0 && marker <= 0xd7) || marker == 0x01) {
            continue;
        }
        if (position + 1 >= size) {
            break;
        }
        segmentLength =
            ((SIZE_T)data[position] << 8) | data[position + 1];
        if (segmentLength < 2 || segmentLength > size - position) {
            break;
        }
        if ((marker >= 0xc0 && marker <= 0xc3) ||
            (marker >= 0xc5 && marker <= 0xc7) ||
            (marker >= 0xc9 && marker <= 0xcb) ||
            (marker >= 0xcd && marker <= 0xcf)) {
            UINT h;
            UINT w;
            if (segmentLength < 7) {
                return FALSE;
            }
            h = ((UINT)data[position + 3] << 8) |
                data[position + 4];
            w = ((UINT)data[position + 5] << 8) |
                data[position + 6];
            if (w == 0 || h == 0 ||
                w > INSERT_IMAGE_DIMENSION_LIMIT ||
                h > INSERT_IMAGE_DIMENSION_LIMIT) {
                return FALSE;
            }
            *width = w;
            *height = h;
            return TRUE;
        }
        position += segmentLength;
    }
    return FALSE;
}

static BOOL insert_dib_dimensions(const BYTE *data, SIZE_T size,
                                  UINT *width, UINT *height)
{
    uint32_t headerSize;
    LONG signedWidth;
    LONG signedHeight;

    if (size < 12) {
        return FALSE;
    }
    headerSize = insert_read_le32(data);
    if (headerSize == 12) {
        UINT w = insert_read_le16(data + 4);
        UINT h = insert_read_le16(data + 6);
        if (w == 0 || h == 0) {
            return FALSE;
        }
        *width = w;
        *height = h;
        return TRUE;
    }
    if (headerSize < 40 || headerSize > size || size < 12) {
        return FALSE;
    }
    signedWidth = (LONG)insert_read_le32(data + 4);
    signedHeight = (LONG)insert_read_le32(data + 8);
    if (signedWidth <= 0 || signedHeight == 0 ||
        signedHeight == LONG_MIN ||
        (UINT)signedWidth > INSERT_IMAGE_DIMENSION_LIMIT ||
        (UINT)abs(signedHeight) > INSERT_IMAGE_DIMENSION_LIMIT) {
        return FALSE;
    }
    *width = (UINT)signedWidth;
    *height = (UINT)abs(signedHeight);
    return TRUE;
}

static BOOL insert_build_picture_rtf(
    AppState *app, InsertPictureKind kind,
    const BYTE *data, SIZE_T size, UINT width, UINT height,
    InsertByteBuffer *rtf)
{
    static const char hex[] = "0123456789abcdef";
    const char *control;
    LONGLONG goalWidth;
    LONGLONG goalHeight;
    LONG maximumWidth;
    SIZE_T index;

    if (data == NULL || size == 0 || width == 0 || height == 0 ||
        size > (INSERT_RTF_LIMIT - 512u) / 2u) {
        rtf->error = size > (INSERT_RTF_LIMIT - 512u) / 2u
                         ? ERROR_FILE_TOO_LARGE
                         : ERROR_INVALID_DATA;
        return FALSE;
    }
    switch (kind) {
    case INSERT_PICTURE_PNG:
        control = "\\pngblip";
        break;
    case INSERT_PICTURE_JPEG:
        control = "\\jpegblip";
        break;
    case INSERT_PICTURE_DIB:
        control = "\\dibitmap0";
        break;
    case INSERT_PICTURE_EMF:
        control = "\\emfblip";
        break;
    default:
        rtf->error = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    goalWidth = (LONGLONG)width * 15;
    goalHeight = (LONGLONG)height * 15;
    maximumWidth = insert_printable_width_twips(app);
    if (goalWidth > maximumWidth) {
        goalHeight = goalHeight * maximumWidth / goalWidth;
        goalWidth = maximumWidth;
    }
    if (goalHeight > 10800) {
        goalWidth = goalWidth * 10800 / goalHeight;
        goalHeight = 10800;
    }
    goalWidth = max(goalWidth, 120);
    goalHeight = max(goalHeight, 120);
    if (!insert_buffer_ascii(rtf, "{\\rtf1\\ansi{\\pict") ||
        !insert_buffer_ascii(rtf, control) ||
        !insert_buffer_printf(
            rtf, "\\picw%u\\pich%u\\picwgoal%lld\\pichgoal%lld ",
            width, height, goalWidth, goalHeight) ||
        !insert_buffer_reserve(rtf, size * 2u + 3u)) {
        return FALSE;
    }
    for (index = 0; index < size; ++index) {
        BYTE encoded[2];
        encoded[0] = (BYTE)hex[data[index] >> 4];
        encoded[1] = (BYTE)hex[data[index] & 0x0f];
        CopyMemory(rtf->data + rtf->size, encoded, 2);
        rtf->size += 2;
    }
    return insert_buffer_ascii(rtf, "}}");
}

static BOOL insert_static_picture_allowed(AppState *app)
{
    if (live_share_query_state(app, WCQ_LIVE_ROLE) == LIVE_ROLE_NONE) {
        return TRUE;
    }
    MessageBeep(MB_ICONWARNING);
    app_set_status_message(
        app,
        L"Static pictures cannot be inserted during a live sharing session");
    return FALSE;
}

static BOOL insert_picture_payload(
    AppState *app, InsertPictureKind kind,
    const BYTE *data, SIZE_T size, UINT width, UINT height,
    const WCHAR *statusText)
{
    InsertByteBuffer rtf = {0};
    BOOL success;

    if (!insert_static_picture_allowed(app)) {
        SetLastError(ERROR_SHARING_VIOLATION);
        return FALSE;
    }
    if (!insert_build_picture_rtf(
            app, kind, data, size, width, height, &rtf)) {
        SetLastError(rtf.error);
        insert_buffer_free(&rtf);
        return FALSE;
    }
    success = insert_stream_selection_expected_objects(
        app, rtf.data, rtf.size, TRUE, statusText, 1u);
    insert_buffer_free(&rtf);
    return success;
}

BOOL insert_emf_picture(AppState *app, const BYTE *data, SIZE_T size,
                        UINT width, UINT height,
                        const WCHAR *statusText)
{
    if (app == NULL || app->editor == NULL || data == NULL || size == 0 ||
        width == 0 || height == 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return insert_picture_payload(
        app, INSERT_PICTURE_EMF, data, size, width, height,
        statusText != NULL ? statusText : L"Drawing inserted");
}

static BOOL insert_read_file(const WCHAR *path, BYTE **data,
                             SIZE_T *size)
{
    HANDLE file;
    LARGE_INTEGER length;
    BYTE *buffer;
    SIZE_T total = 0;

    *data = NULL;
    *size = 0;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    if (!GetFileSizeEx(file, &length) || length.QuadPart <= 0 ||
        (ULONGLONG)length.QuadPart > INSERT_IMAGE_FILE_LIMIT) {
        DWORD error = GetLastError();
        CloseHandle(file);
        SetLastError(error != ERROR_SUCCESS ? error :
                     length.QuadPart <= 0 ? ERROR_INVALID_DATA :
                                            ERROR_FILE_TOO_LARGE);
        return FALSE;
    }
    buffer = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)length.QuadPart);
    if (buffer == NULL) {
        CloseHandle(file);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    while (total < (SIZE_T)length.QuadPart) {
        DWORD read = 0;
        DWORD request = (DWORD)min(
            (SIZE_T)1024u * 1024u,
            (SIZE_T)length.QuadPart - total);
        if (!ReadFile(file, buffer + total, request, &read, NULL) ||
            read == 0) {
            DWORD error = GetLastError();
            HeapFree(GetProcessHeap(), 0, buffer);
            CloseHandle(file);
            SetLastError(error != ERROR_SUCCESS ? error :
                                                  ERROR_HANDLE_EOF);
            return FALSE;
        }
        total += read;
    }
    if (!CloseHandle(file)) {
        DWORD error = GetLastError();
        HeapFree(GetProcessHeap(), 0, buffer);
        SetLastError(error);
        return FALSE;
    }
    *data = buffer;
    *size = total;
    return TRUE;
}

static BOOL insert_picture(AppState *app)
{
    static const WCHAR filter[] =
        L"Pictures (*.png;*.jpg;*.jpeg;*.bmp)\0"
        L"*.png;*.jpg;*.jpeg;*.bmp\0"
        L"All Files (*.*)\0*.*\0\0";
    WCHAR path[PATH_CAPACITY] = L"";
    OPENFILENAMEW dialog;
    BYTE *fileData = NULL;
    const BYTE *payload;
    SIZE_T fileSize = 0;
    SIZE_T payloadSize;
    UINT width = 0;
    UINT height = 0;
    InsertPictureKind kind;
    BOOL valid = FALSE;
    BOOL success;

    if (!insert_static_picture_allowed(app)) {
        return TRUE;
    }
    ZeroMemory(&dialog, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = app->mainWindow;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = path;
    dialog.nMaxFile = ARRAYSIZE(path);
    dialog.lpstrTitle = L"Insert Picture";
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST |
                   OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&dialog)) {
        DWORD error = CommDlgExtendedError();
        if (error != 0) {
            app_show_error(app->mainWindow,
                           L"The picture chooser could not be opened.",
                           error);
        }
        return TRUE;
    }
    if (!insert_read_file(path, &fileData, &fileSize)) {
        return FALSE;
    }
    payload = fileData;
    payloadSize = fileSize;
    if (insert_png_dimensions(fileData, fileSize, &width, &height)) {
        kind = INSERT_PICTURE_PNG;
        valid = TRUE;
    } else if (insert_jpeg_dimensions(
                   fileData, fileSize, &width, &height)) {
        kind = INSERT_PICTURE_JPEG;
        valid = TRUE;
    } else if (fileSize > 14 && fileData[0] == 'B' &&
               fileData[1] == 'M' &&
               insert_dib_dimensions(fileData + 14, fileSize - 14,
                                     &width, &height)) {
        kind = INSERT_PICTURE_DIB;
        payload = fileData + 14;
        payloadSize = fileSize - 14;
        valid = TRUE;
    }
    if (!valid) {
        HeapFree(GetProcessHeap(), 0, fileData);
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    success = insert_picture_payload(
        app, kind, payload, payloadSize, width, height,
        L"Picture inserted");
    HeapFree(GetProcessHeap(), 0, fileData);
    return success;
}

static BOOL insert_create_shape_emf(const WCHAR *shape,
                                    BYTE **data, SIZE_T *size)
{
    RECT frame = {0, 0, 8000, 4000};
    HDC reference;
    HDC metafile;
    HENHMETAFILE handle;
    HPEN pen;
    HBRUSH brush;
    HGDIOBJ oldPen;
    HGDIOBJ oldBrush;
    UINT byteCount;
    BYTE *bytes;
    BOOL line;
    BOOL arrow;

    *data = NULL;
    *size = 0;
    reference = GetDC(NULL);
    if (reference == NULL) {
        return FALSE;
    }
    metafile = CreateEnhMetaFileW(
        reference, NULL, &frame, L"WordCraft\0Inline shape\0");
    ReleaseDC(NULL, reference);
    if (metafile == NULL) {
        return FALSE;
    }
    SetMapMode(metafile, MM_ANISOTROPIC);
    SetWindowExtEx(metafile, 800, 400, NULL);
    SetViewportExtEx(metafile, 800, 400, NULL);
    SetBkMode(metafile, TRANSPARENT);
    pen = CreatePen(PS_SOLID, 10, RGB(35, 84, 130));
    brush = CreateSolidBrush(RGB(221, 235, 247));
    if (pen == NULL || brush == NULL) {
        if (pen != NULL) {
            DeleteObject(pen);
        }
        if (brush != NULL) {
            DeleteObject(brush);
        }
        DeleteEnhMetaFile(CloseEnhMetaFile(metafile));
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    oldPen = SelectObject(metafile, pen);
    oldBrush = SelectObject(metafile, brush);
    line = _wcsicmp(shape, L"line") == 0;
    arrow = _wcsicmp(shape, L"arrow") == 0;
    if (_wcsicmp(shape, L"ellipse") == 0 ||
        _wcsicmp(shape, L"oval") == 0) {
        Ellipse(metafile, 35, 35, 765, 365);
    } else if (_wcsicmp(shape, L"rectangle") == 0) {
        Rectangle(metafile, 35, 35, 765, 365);
    } else if (line || arrow) {
        HGDIOBJ previousBrush = SelectObject(
            metafile, GetStockObject(line ? NULL_BRUSH : DC_BRUSH));
        MoveToEx(metafile, 55, 200, NULL);
        LineTo(metafile, arrow ? 665 : 745, 200);
        if (arrow) {
            POINT points[] = {
                {650, 115}, {755, 200}, {650, 285}
            };
            SetDCBrushColor(metafile, RGB(35, 84, 130));
            Polygon(metafile, points, ARRAYSIZE(points));
        }
        SelectObject(metafile, previousBrush);
    } else {
        RoundRect(metafile, 35, 35, 765, 365, 90, 90);
    }
    SelectObject(metafile, oldBrush);
    SelectObject(metafile, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
    handle = CloseEnhMetaFile(metafile);
    if (handle == NULL) {
        return FALSE;
    }
    byteCount = GetEnhMetaFileBits(handle, 0, NULL);
    if (byteCount == 0 ||
        byteCount > (INSERT_RTF_LIMIT - 512u) / 2u) {
        DeleteEnhMetaFile(handle);
        SetLastError(byteCount == 0 ? ERROR_INVALID_DATA :
                                      ERROR_FILE_TOO_LARGE);
        return FALSE;
    }
    bytes = HeapAlloc(GetProcessHeap(), 0, byteCount);
    if (bytes == NULL) {
        DeleteEnhMetaFile(handle);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    if (GetEnhMetaFileBits(handle, byteCount, bytes) != byteCount) {
        DWORD error = GetLastError();
        HeapFree(GetProcessHeap(), 0, bytes);
        DeleteEnhMetaFile(handle);
        SetLastError(error != ERROR_SUCCESS ? error : ERROR_INVALID_DATA);
        return FALSE;
    }
    DeleteEnhMetaFile(handle);
    *data = bytes;
    *size = byteCount;
    return TRUE;
}

static BOOL insert_shape(AppState *app)
{
    WCHAR shape[64];
    BYTE *emf = NULL;
    SIZE_T emfSize = 0;
    BOOL success;

    if (!insert_static_picture_allowed(app)) {
        return TRUE;
    }
    if (!insert_prompt(
            app, L"Insert Shape", L"Shape type:",
            L"Choose rectangle, rounded rectangle, ellipse, line, or arrow.",
            L"Rounded rectangle", shape, ARRAYSIZE(shape))) {
        return TRUE;
    }
    if (_wcsicmp(shape, L"rectangle") != 0 &&
        _wcsicmp(shape, L"rounded rectangle") != 0 &&
        _wcsicmp(shape, L"ellipse") != 0 &&
        _wcsicmp(shape, L"oval") != 0 &&
        _wcsicmp(shape, L"line") != 0 &&
        _wcsicmp(shape, L"arrow") != 0) {
        MessageBeep(MB_ICONWARNING);
        app_set_status_message(
            app,
            L"Choose rectangle, rounded rectangle, ellipse, line, or arrow");
        return TRUE;
    }
    if (!insert_create_shape_emf(shape, &emf, &emfSize)) {
        return FALSE;
    }
    success = insert_picture_payload(
        app, INSERT_PICTURE_EMF, emf, emfSize, 800, 400,
        L"Shape inserted");
    HeapFree(GetProcessHeap(), 0, emf);
    return success;
}

static BOOL insert_copy_clipboard_dib(BYTE **data, SIZE_T *size,
                                      UINT *width, UINT *height)
{
    UINT format;
    HANDLE handle;
    SIZE_T bytes;
    const BYTE *locked;
    BYTE *copy;

    *data = NULL;
    *size = 0;
    format = IsClipboardFormatAvailable(CF_DIBV5) ? CF_DIBV5 :
             IsClipboardFormatAvailable(CF_DIB) ? CF_DIB : 0;
    if (format == 0) {
        return FALSE;
    }
    handle = GetClipboardData(format);
    if (handle == NULL) {
        return FALSE;
    }
    bytes = GlobalSize(handle);
    if (bytes == 0 || bytes > (INSERT_RTF_LIMIT - 512u) / 2u) {
        SetLastError(bytes == 0 ? ERROR_INVALID_DATA :
                                  ERROR_FILE_TOO_LARGE);
        return FALSE;
    }
    locked = GlobalLock(handle);
    if (locked == NULL) {
        return FALSE;
    }
    if (!insert_dib_dimensions(locked, bytes, width, height)) {
        GlobalUnlock(handle);
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    copy = HeapAlloc(GetProcessHeap(), 0, bytes);
    if (copy == NULL) {
        GlobalUnlock(handle);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    CopyMemory(copy, locked, bytes);
    GlobalUnlock(handle);
    *data = copy;
    *size = bytes;
    return TRUE;
}

static BOOL insert_copy_clipboard_bitmap(BYTE **data, SIZE_T *size,
                                         UINT *width, UINT *height)
{
    HBITMAP bitmap;
    BITMAP description;
    BITMAPINFO information;
    SIZE_T stride;
    SIZE_T bitsSize;
    SIZE_T total;
    BYTE *dib;
    HDC dc;
    int lines;

    *data = NULL;
    *size = 0;
    bitmap = (HBITMAP)GetClipboardData(CF_BITMAP);
    if (bitmap == NULL ||
        GetObjectW(bitmap, sizeof(description), &description) !=
            sizeof(description) ||
        description.bmWidth <= 0 || description.bmHeight == 0 ||
        description.bmHeight == LONG_MIN ||
        (UINT)description.bmWidth > INSERT_IMAGE_DIMENSION_LIMIT ||
        (UINT)abs(description.bmHeight) > INSERT_IMAGE_DIMENSION_LIMIT) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    stride = (((SIZE_T)description.bmWidth * 32u + 31u) / 32u) * 4u;
    if (stride > SIZE_MAX / (UINT)abs(description.bmHeight)) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    bitsSize = stride * (UINT)abs(description.bmHeight);
    total = sizeof(BITMAPINFOHEADER) + bitsSize;
    if (total > (INSERT_RTF_LIMIT - 512u) / 2u) {
        SetLastError(ERROR_FILE_TOO_LARGE);
        return FALSE;
    }
    dib = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, total);
    if (dib == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    ZeroMemory(&information, sizeof(information));
    information.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    information.bmiHeader.biWidth = description.bmWidth;
    information.bmiHeader.biHeight = abs(description.bmHeight);
    information.bmiHeader.biPlanes = 1;
    information.bmiHeader.biBitCount = 32;
    information.bmiHeader.biCompression = BI_RGB;
    information.bmiHeader.biSizeImage = (DWORD)bitsSize;
    dc = GetDC(NULL);
    if (dc == NULL) {
        HeapFree(GetProcessHeap(), 0, dib);
        return FALSE;
    }
    lines = GetDIBits(
        dc, bitmap, 0, (UINT)abs(description.bmHeight),
        dib + sizeof(BITMAPINFOHEADER), &information, DIB_RGB_COLORS);
    ReleaseDC(NULL, dc);
    if (lines != abs(description.bmHeight)) {
        HeapFree(GetProcessHeap(), 0, dib);
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    CopyMemory(dib, &information.bmiHeader, sizeof(BITMAPINFOHEADER));
    *data = dib;
    *size = total;
    *width = (UINT)description.bmWidth;
    *height = (UINT)abs(description.bmHeight);
    return TRUE;
}

static BOOL insert_screenshot(AppState *app)
{
    BYTE *dib = NULL;
    SIZE_T dibSize = 0;
    UINT width = 0;
    UINT height = 0;
    BOOL copied = FALSE;
    BOOL success;

    if (!insert_static_picture_allowed(app)) {
        return TRUE;
    }
    SetLastError(ERROR_SUCCESS);
    if (!OpenClipboard(app->mainWindow)) {
        if (GetLastError() == ERROR_SUCCESS) {
            SetLastError(ERROR_BUSY);
        }
        return FALSE;
    }
    copied = insert_copy_clipboard_dib(
        &dib, &dibSize, &width, &height);
    if (!copied && IsClipboardFormatAvailable(CF_BITMAP)) {
        copied = insert_copy_clipboard_bitmap(
            &dib, &dibSize, &width, &height);
    }
    CloseClipboard();
    if (!copied) {
        if (GetLastError() == ERROR_SUCCESS) {
            MessageBeep(MB_ICONWARNING);
            app_set_status_message(
                app, L"Copy a screenshot to the clipboard first");
            return TRUE;
        }
        return FALSE;
    }
    success = insert_picture_payload(
        app, INSERT_PICTURE_DIB, dib, dibSize, width, height,
        L"Screenshot inserted");
    HeapFree(GetProcessHeap(), 0, dib);
    return success;
}

static BOOL insert_replace_text(AppState *app, const WCHAR *text,
                                const WCHAR *statusText)
{
    ITextDocument *collection;

    if (text == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    collection = insert_begin_edit_collection(app->editor);
    SendMessageW(app->editor, EM_REPLACESEL, TRUE, (LPARAM)text);
    insert_end_edit_collection(collection);
    SetFocus(app->editor);
    if (statusText != NULL) {
        app_set_status_message(app, statusText);
    }
    return TRUE;
}

static BOOL insert_apply_character_format(
    AppState *app, LONG start, LONG end, const WCHAR *face,
    LONG heightTwips, COLORREF color, DWORD extraMask,
    DWORD extraEffects)
{
    CHARRANGE range;
    CHARFORMAT2W format;

    if (start < 0 || end <= start) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    range.cpMin = start;
    range.cpMax = end;
    SendMessageW(app->editor, EM_EXSETSEL, 0, (LPARAM)&range);
    ZeroMemory(&format, sizeof(format));
    format.cbSize = sizeof(format);
    format.dwMask = extraMask;
    format.dwEffects = extraEffects;
    if (face != NULL) {
        format.dwMask |= CFM_FACE | CFM_CHARSET;
        format.bCharSet = DEFAULT_CHARSET;
        StringCchCopyW(format.szFaceName, ARRAYSIZE(format.szFaceName),
                       face);
    }
    if (heightTwips > 0) {
        format.dwMask |= CFM_SIZE;
        format.yHeight = heightTwips;
    }
    if (color != CLR_INVALID) {
        format.dwMask |= CFM_COLOR;
        format.crTextColor = color;
        format.dwEffects &= ~CFE_AUTOCOLOR;
    }
    SendMessageW(app->editor, EM_SETCHARFORMAT, SCF_SELECTION,
                 (LPARAM)&format);
    return TRUE;
}

static BOOL insert_formatted_text(
    AppState *app, const WCHAR *text, const WCHAR *face,
    LONG heightTwips, COLORREF color, DWORD extraMask,
    DWORD extraEffects, const WCHAR *statusText)
{
    CHARRANGE selection;
    CHARRANGE caret;
    LONG length;
    ITextDocument *collection;

    if (text == NULL || text[0] == L'\0') {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    length = lstrlenW(text);
    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
    collection = insert_begin_edit_collection(app->editor);
    SendMessageW(app->editor, EM_REPLACESEL, TRUE, (LPARAM)text);
    (void)insert_apply_character_format(
        app, selection.cpMin, selection.cpMin + length, face,
        heightTwips, color, extraMask, extraEffects);
    caret.cpMin = selection.cpMin + length;
    caret.cpMax = caret.cpMin;
    SendMessageW(app->editor, EM_EXSETSEL, 0, (LPARAM)&caret);
    insert_end_edit_collection(collection);
    app->richFormattingUsed = TRUE;
    SetFocus(app->editor);
    if (statusText != NULL) {
        app_set_status_message(app, statusText);
    }
    return TRUE;
}

static BOOL insert_icon(AppState *app)
{
    WCHAR icon[32];

    if (!insert_prompt(
            app, L"Insert Icon", L"Icon:",
            L"Try ★, ✓, ☑, ☀, ☁, ☎, ✉, ➜, or a Unicode symbol.",
            L"\x2605", icon, ARRAYSIZE(icon))) {
        return TRUE;
    }
    return insert_formatted_text(
        app, icon, L"Segoe UI Symbol", 480, RGB(31, 78, 121),
        CFM_BOLD, CFE_BOLD, L"Icon inserted");
}

static SIZE_T insert_split_items(WCHAR *text, WCHAR **items,
                                 SIZE_T capacity)
{
    WCHAR *cursor;
    SIZE_T count = 0;

    if (text == NULL || items == NULL || capacity == 0) {
        return 0;
    }
    cursor = text;
    while (*cursor != L'\0' && count < capacity) {
        WCHAR *separator;
        WCHAR *item = cursor;
        separator = wcspbrk(cursor, L">;");
        if (separator != NULL) {
            *separator = L'\0';
            cursor = separator + 1;
        } else {
            cursor += lstrlenW(cursor);
        }
        insert_trim(item);
        if (item[0] != L'\0') {
            items[count++] = item;
        }
        if (separator == NULL) {
            break;
        }
    }
    return count;
}

static BOOL insert_smartart(AppState *app)
{
    WCHAR input[INSERT_PROMPT_CAPACITY];
    WCHAR *items[8];
    SIZE_T count;
    InsertByteBuffer rtf = {0};
    BOOL success;

    if (!insert_prompt(
            app, L"Insert SmartArt", L"Process steps:",
            L"Separate two to eight editable steps with > or semicolons.",
            L"Plan > Draft > Review > Publish",
            input, ARRAYSIZE(input))) {
        return TRUE;
    }
    count = insert_split_items(input, items, ARRAYSIZE(items));
    if (count < 2) {
        MessageBeep(MB_ICONWARNING);
        app_set_status_message(
            app, L"Enter at least two SmartArt process steps");
        return TRUE;
    }
    if (!insert_build_table_rtf(
            app, 1, (UINT)count, (const WCHAR *const *)items, &rtf)) {
        SetLastError(rtf.error);
        insert_buffer_free(&rtf);
        return FALSE;
    }
    success = insert_stream_selection(
        app, rtf.data, rtf.size, TRUE,
        L"Editable SmartArt process inserted");
    insert_buffer_free(&rtf);
    return success;
}

static BOOL insert_chart(AppState *app)
{
    WCHAR input[INSERT_PROMPT_CAPACITY];
    WCHAR output[4096] = L"Chart\r\n";
    WCHAR *items[8];
    SIZE_T count;
    SIZE_T index;
    BOOL added = FALSE;

    if (!insert_prompt(
            app, L"Insert Chart", L"Chart data:",
            L"Use Label=value pairs separated with semicolons.",
            L"Sales=8; Costs=5; Profit=3",
            input, ARRAYSIZE(input))) {
        return TRUE;
    }
    count = insert_split_items(input, items, ARRAYSIZE(items));
    for (index = 0; index < count; ++index) {
        WCHAR *equals = wcschr(items[index], L'=');
        WCHAR line[384];
        WCHAR bars[24];
        LONG value;
        LONG barCount;
        SIZE_T bar;

        if (equals == NULL) {
            continue;
        }
        *equals++ = L'\0';
        insert_trim(items[index]);
        insert_trim(equals);
        if (items[index][0] == L'\0' || equals[0] == L'\0') {
            continue;
        }
        value = wcstol(equals, NULL, 10);
        barCount = value < 0 ? -value : value;
        barCount = min(barCount, 20);
        if (barCount == 0) {
            barCount = 1;
        }
        for (bar = 0; bar < (SIZE_T)barCount; ++bar) {
            bars[bar] = 0x2588;
        }
        bars[barCount] = L'\0';
        if (FAILED(StringCchPrintfW(
                line, ARRAYSIZE(line), L"%-16.16s %s %ld\r\n",
                items[index], bars, value)) ||
            FAILED(StringCchCatW(output, ARRAYSIZE(output), line))) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        added = TRUE;
    }
    if (!added) {
        MessageBeep(MB_ICONWARNING);
        app_set_status_message(
            app, L"Enter chart data such as Sales=8; Costs=5");
        return TRUE;
    }
    return insert_formatted_text(
        app, output, L"Consolas", 220, CLR_INVALID,
        CFM_BOLD, 0, L"Editable chart inserted");
}

static BOOL insert_url_is_allowed(const WCHAR *url, BOOL video)
{
    SIZE_T index;
    BOOL allowed =
        _wcsnicmp(url, L"https://", 8) == 0 ||
        _wcsnicmp(url, L"http://", 7) == 0 ||
        (!video && _wcsnicmp(url, L"mailto:", 7) == 0);

    if (!allowed) {
        return FALSE;
    }
    for (index = 0; url[index] != L'\0'; ++index) {
        if (url[index] < 0x20 || url[index] == 0x7f ||
            url[index] == L'"' || url[index] == L'<' ||
            url[index] == L'>') {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL insert_url_text(AppState *app, const WCHAR *url,
                            const WCHAR *prefix,
                            const WCHAR *statusText)
{
    WCHAR combined[INSERT_PROMPT_CAPACITY + 64];
    CHARRANGE selection;
    CHARRANGE caret;
    LONG prefixLength;
    LONG totalLength;
    ITextDocument *collection;
    LRESULT eventMask;

    if (FAILED(StringCchPrintfW(
            combined, ARRAYSIZE(combined), L"%s%s",
            prefix != NULL ? prefix : L"", url))) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    prefixLength = prefix != NULL ? lstrlenW(prefix) : 0;
    totalLength = lstrlenW(combined);
    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
    collection = insert_begin_edit_collection(app->editor);
    SendMessageW(app->editor, EM_REPLACESEL, TRUE, (LPARAM)combined);
    (void)insert_apply_character_format(
        app, selection.cpMin + prefixLength,
        selection.cpMin + totalLength, NULL, 0,
        RGB(0, 102, 204),
        CFM_LINK | CFM_UNDERLINE, CFE_LINK | CFE_UNDERLINE);
    caret.cpMin = selection.cpMin + totalLength;
    caret.cpMax = caret.cpMin;
    SendMessageW(app->editor, EM_EXSETSEL, 0, (LPARAM)&caret);
    insert_end_edit_collection(collection);
    SendMessageW(app->editor, EM_AUTOURLDETECT, AURL_ENABLEURL, 0);
    eventMask = SendMessageW(app->editor, EM_GETEVENTMASK, 0, 0);
    SendMessageW(app->editor, EM_SETEVENTMASK, 0,
                 eventMask | ENM_LINK);
    app->richFormattingUsed = TRUE;
    SetFocus(app->editor);
    app_set_status_message(app, statusText);
    return TRUE;
}

static BOOL insert_link(AppState *app, BOOL video)
{
    WCHAR url[INSERT_PROMPT_CAPACITY];

    if (!insert_prompt(
            app, video ? L"Insert Online Video" : L"Insert Link",
            video ? L"Video URL:" : L"Address:",
            video ? L"Enter a safe http:// or https:// video address. "
                    L"WordCraft inserts an inert, clickable reference."
                  : L"Enter an http://, https://, or mailto: address.",
            L"https://", url, ARRAYSIZE(url))) {
        return TRUE;
    }
    if (!insert_url_is_allowed(url, video)) {
        MessageBeep(MB_ICONWARNING);
        app_set_status_message(
            app,
            video ? L"Enter a valid http:// or https:// video URL"
                  : L"Enter a valid http://, https://, or mailto: address");
        return TRUE;
    }
    return insert_url_text(
        app, url, video ? L"Online video: " : NULL,
        video ? L"Online video reference inserted"
              : L"Link inserted");
}

static BOOL insert_get_selected_text(AppState *app, WCHAR **text,
                                     LONG *start, LONG *end)
{
    CHARRANGE selection;
    TEXTRANGEW range;
    LONG length;
    WCHAR *copy;

    *text = NULL;
    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
    length = selection.cpMax - selection.cpMin;
    *start = selection.cpMin;
    *end = selection.cpMax;
    if (length <= 0) {
        copy = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                         sizeof(WCHAR));
        if (copy == NULL) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        *text = copy;
        return TRUE;
    }
    if (length > INSERT_PROMPT_CAPACITY - 1) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    copy = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                     ((SIZE_T)length + 1) * sizeof(WCHAR));
    if (copy == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    range.chrg = selection;
    range.lpstrText = copy;
    if (SendMessageW(app->editor, EM_GETTEXTRANGE, 0,
                     (LPARAM)&range) != length) {
        HeapFree(GetProcessHeap(), 0, copy);
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    copy[length] = L'\0';
    *text = copy;
    return TRUE;
}

static BOOL insert_text_box(AppState *app)
{
    WCHAR *selected = NULL;
    LONG start;
    LONG end;
    const WCHAR *cells[1];
    InsertByteBuffer rtf = {0};
    BOOL placeholder;
    BOOL success;
    CHARRANGE placeholderRange;

    if (!insert_get_selected_text(app, &selected, &start, &end)) {
        return FALSE;
    }
    placeholder = selected[0] == L'\0';
    cells[0] = placeholder ? L"Type here" : selected;
    if (!insert_build_table_rtf(app, 1, 1, cells, &rtf)) {
        SetLastError(rtf.error);
        insert_buffer_free(&rtf);
        HeapFree(GetProcessHeap(), 0, selected);
        return FALSE;
    }
    success = insert_stream_selection(
        app, rtf.data, rtf.size, TRUE, L"Text box inserted");
    if (success && placeholder) {
        placeholderRange.cpMin = start;
        placeholderRange.cpMax = start + 9;
        SendMessageW(app->editor, EM_EXSETSEL, 0,
                     (LPARAM)&placeholderRange);
    }
    insert_buffer_free(&rtf);
    HeapFree(GetProcessHeap(), 0, selected);
    return success;
}

static BOOL insert_quick_part(AppState *app)
{
    WCHAR text[INSERT_PROMPT_CAPACITY];

    if (!insert_prompt(
            app, L"Insert Quick Part", L"Reusable text:",
            L"Enter the reusable text to place in this document.",
            L"Document title", text, ARRAYSIZE(text))) {
        return TRUE;
    }
    return insert_replace_text(app, text, L"Quick Part inserted");
}

static BOOL insert_wordart(AppState *app)
{
    CHARRANGE selection;
    CHARRANGE caret;
    WCHAR text[INSERT_PROMPT_CAPACITY];
    LONG start;
    LONG end;
    ITextDocument *collection;

    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
    start = selection.cpMin;
    end = selection.cpMax;
    if (start == end) {
        if (!insert_prompt(
                app, L"Insert WordArt", L"Text:",
                L"WordArt is inserted as editable, styled document text.",
                L"WordArt", text, ARRAYSIZE(text))) {
            return TRUE;
        }
    }
    collection = insert_begin_edit_collection(app->editor);
    if (start == end) {
        SendMessageW(app->editor, EM_REPLACESEL, TRUE, (LPARAM)text);
        end = start + lstrlenW(text);
    }
    (void)insert_apply_character_format(
        app, start, end, L"Arial", 720, RGB(31, 78, 121),
        CFM_BOLD | CFM_OUTLINE | CFM_SHADOW,
        CFE_BOLD | CFE_OUTLINE | CFE_SHADOW);
    caret.cpMin = end;
    caret.cpMax = end;
    SendMessageW(app->editor, EM_EXSETSEL, 0, (LPARAM)&caret);
    insert_end_edit_collection(collection);
    app->richFormattingUsed = TRUE;
    SetFocus(app->editor);
    app_set_status_message(app, L"WordArt text inserted");
    return TRUE;
}

static ITextDocument *insert_get_text_document(HWND editor)
{
    IRichEditOle *richEditOle = NULL;
    ITextDocument *document = NULL;

    if (!SendMessageW(editor, EM_GETOLEINTERFACE, 0,
                      (LPARAM)&richEditOle) ||
        richEditOle == NULL) {
        return NULL;
    }
    if (FAILED(richEditOle->lpVtbl->QueryInterface(
            richEditOle, &insertIidTextDocument, (void **)&document))) {
        document = NULL;
    }
    richEditOle->lpVtbl->Release(richEditOle);
    return document;
}

static BOOL insert_drop_cap(AppState *app)
{
    CHARRANGE selection;
    CHARRANGE original;
    ITextDocument *document;
    ITextRange *paragraph = NULL;
    BSTR text = NULL;
    long ignored = 0;
    long paragraphStart = 0;
    UINT index;
    LONG first;
    LONG finish;
    BOOL success = FALSE;

    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
    original = selection;
    document = insert_get_text_document(app->editor);
    if (document == NULL ||
        FAILED(ITextDocument_Range(
            document, selection.cpMin, selection.cpMin, &paragraph)) ||
        paragraph == NULL ||
        FAILED(ITextRange_Expand(paragraph, tomParagraph, &ignored)) ||
        FAILED(ITextRange_GetStart(paragraph, &paragraphStart)) ||
        FAILED(ITextRange_GetText(paragraph, &text)) ||
        text == NULL) {
        goto cleanup;
    }
    for (index = 0; index < SysStringLen(text); ++index) {
        if (!iswspace(text[index])) {
            break;
        }
    }
    if (index >= SysStringLen(text) || text[index] == L'\r' ||
        text[index] == L'\n') {
        MessageBeep(MB_ICONWARNING);
        app_set_status_message(
            app, L"Type a paragraph before adding a drop cap");
        success = TRUE;
        goto cleanup;
    }
    first = paragraphStart + (LONG)index;
    finish = first + 1;
    if (index + 1 < SysStringLen(text) &&
        text[index] >= 0xd800 && text[index] <= 0xdbff &&
        text[index + 1] >= 0xdc00 && text[index + 1] <= 0xdfff) {
        ++finish;
    }
    (void)insert_apply_character_format(
        app, first, finish, NULL, 960, CLR_INVALID,
        CFM_BOLD, CFE_BOLD);
    SendMessageW(app->editor, EM_EXSETSEL, 0, (LPARAM)&original);
    app->richFormattingUsed = TRUE;
    SetFocus(app->editor);
    app_set_status_message(app, L"Drop cap applied");
    success = TRUE;

cleanup:
    if (text != NULL) {
        SysFreeString(text);
    }
    if (paragraph != NULL) {
        ITextRange_Release(paragraph);
    }
    if (document != NULL) {
        ITextDocument_Release(document);
    }
    if (!success && GetLastError() == ERROR_SUCCESS) {
        SetLastError(ERROR_INVALID_DATA);
    }
    return success;
}

static BOOL insert_signature_line(AppState *app)
{
    WCHAR signer[256];
    InsertByteBuffer rtf = {0};
    BOOL success;

    if (!insert_prompt(
            app, L"Insert Signature Line", L"Suggested signer:",
            L"Insert an editable signature line. This does not apply "
            L"a cryptographic signature.",
            L"Signer name", signer, ARRAYSIZE(signer))) {
        return TRUE;
    }
    if (!insert_buffer_ascii(
            &rtf,
            "{\\rtf1\\ansi\\uc1\\deff0"
            "{\\fonttbl{\\f0\\fnil Aptos;}}"
            "\\f0\\pard\\qr\\sb240\\sa40\\fs22 "
            "________________________________\\par"
            "\\pard\\qr\\sa0\\b ") ||
        !insert_buffer_rtf_text(&rtf, signer) ||
        !insert_buffer_ascii(
            &rtf, "\\b0\\line Signature\\par\\pard}")) {
        SetLastError(rtf.error);
        insert_buffer_free(&rtf);
        return FALSE;
    }
    success = insert_stream_selection(
        app, rtf.data, rtf.size, TRUE, L"Signature line inserted");
    insert_buffer_free(&rtf);
    return success;
}

static BOOL insert_equation(AppState *app)
{
    WCHAR equation[INSERT_PROMPT_CAPACITY];

    if (!insert_prompt(
            app, L"Insert Equation", L"Linear equation:",
            L"Enter editable UnicodeMath-style text.",
            L"x\x00B2 + y\x00B2 = z\x00B2", equation,
            ARRAYSIZE(equation))) {
        return TRUE;
    }
    return insert_formatted_text(
        app, equation, L"Cambria Math", 320, CLR_INVALID,
        0, 0, L"Equation inserted");
}

static BOOL insert_symbol(AppState *app)
{
    WCHAR symbol[64];

    if (!insert_prompt(
            app, L"Insert Symbol", L"Symbol:",
            L"Try \x00A9, \x00AE, \x00B1, \x2260, \x2264, \x2265, "
            L"\x03C0, \x03A3, \x2192, or another Unicode symbol.",
            L"\x00A9", symbol, ARRAYSIZE(symbol))) {
        return TRUE;
    }
    return insert_replace_text(app, symbol, L"Symbol inserted");
}

static BOOL insert_esignature_fields(AppState *app)
{
    WCHAR signer[512];
    WCHAR body[1024];
    const WCHAR *cells[1];
    InsertByteBuffer rtf = {0};
    BOOL success;

    if (!insert_prompt(
            app, L"Insert eSignature Fields",
            L"Signer name or email:",
            L"These are editable document fields, not a cryptographic "
            L"digital signature.",
            L"Signer name <email@example.com>", signer,
            ARRAYSIZE(signer))) {
        return TRUE;
    }
    if (FAILED(StringCchPrintfW(
            body, ARRAYSIZE(body),
            L"Electronic signature fields\r\n"
            L"Signer: %s\r\n"
            L"Signature: ______________________________\r\n"
            L"Date: __________________",
            signer))) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    cells[0] = body;
    if (!insert_build_table_rtf(app, 1, 1, cells, &rtf)) {
        SetLastError(rtf.error);
        insert_buffer_free(&rtf);
        return FALSE;
    }
    success = insert_stream_selection(
        app, rtf.data, rtf.size, TRUE,
        L"Editable eSignature fields inserted (not digitally signed)");
    insert_buffer_free(&rtf);
    return success;
}

static BOOL insert_comment(AppState *app)
{
    if (comments_begin_draft(app)) {
        ribbon_focus_comment_editor(app);
        app_set_status_message(
            app, L"Type your comment, then choose Add Comment");
    } else {
        MessageBeep(MB_ICONWARNING);
    }
    return TRUE;
}

BOOL insert_execute_command(AppState *app, UINT command)
{
    BOOL success;

    if (app == NULL || app->editor == NULL || !IsWindow(app->editor)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    switch (command) {
    case IDM_INSERT_DATETIME:
        dialogs_insert_datetime(app);
        app_set_status_message(app, L"Date and time inserted");
        return TRUE;
    case IDM_INSERT_COVER_PAGE:
        success = insert_cover_page(app);
        break;
    case IDM_INSERT_BLANK_PAGE:
        success = insert_page_break_text(app, TRUE);
        break;
    case IDM_INSERT_PAGE_BREAK:
        success = insert_page_break_text(app, FALSE);
        break;
    case IDM_INSERT_TABLE:
        success = insert_table(app);
        break;
    case IDM_INSERT_PICTURES:
        success = insert_picture(app);
        break;
    case IDM_INSERT_SHAPES:
        success = insert_shape(app);
        break;
    case IDM_INSERT_ICONS:
        success = insert_icon(app);
        break;
    case IDM_INSERT_SMARTART:
        success = insert_smartart(app);
        break;
    case IDM_INSERT_CHART:
        success = insert_chart(app);
        break;
    case IDM_INSERT_SCREENSHOT:
        success = insert_screenshot(app);
        break;
    case IDM_INSERT_ONLINE_VIDEO:
        success = insert_link(app, TRUE);
        break;
    case IDM_INSERT_LINK:
        success = insert_link(app, FALSE);
        break;
    case IDM_INSERT_COMMENT:
        success = insert_comment(app);
        break;
    case IDM_INSERT_TEXT_BOX:
        success = insert_text_box(app);
        break;
    case IDM_INSERT_QUICK_PARTS:
        success = insert_quick_part(app);
        break;
    case IDM_INSERT_WORDART:
        success = insert_wordart(app);
        break;
    case IDM_INSERT_DROP_CAP:
        success = insert_drop_cap(app);
        break;
    case IDM_INSERT_SIGNATURE_LINE:
        success = insert_signature_line(app);
        break;
    case IDM_INSERT_EQUATION:
        success = insert_equation(app);
        break;
    case IDM_INSERT_SYMBOL:
        success = insert_symbol(app);
        break;
    case IDM_INSERT_ESIGNATURE_FIELDS:
        success = insert_esignature_fields(app);
        break;
    case IDM_INSERT_3D_MODELS:
    case IDM_INSERT_BOOKMARK:
    case IDM_INSERT_CROSS_REFERENCE:
    case IDM_INSERT_HEADER:
    case IDM_INSERT_FOOTER:
    case IDM_INSERT_PAGE_NUMBER:
    case IDM_INSERT_OBJECT:
        return FALSE;
    default:
        return FALSE;
    }
    if (!success) {
        insert_report_failure(app, L"The content could not be inserted.");
    }
    return TRUE;
}
