#include "editor.h"

#include <limits.h>
#include <stdlib.h>

#define PAPER_MATCH_TOLERANCE 5
#define PAPER_MIN_DIMENSION 100
#define PAPER_MAX_DIMENSION 100000
#define PAPER_MIN_PRINTABLE 100

typedef struct CustomPaperDialogState {
    AppState *app;
    LONG widthThousandths;
    LONG heightThousandths;
    int units;
} CustomPaperDialogState;

static const PaperSizePreset paperSizes[] = {
    {PAPER_SIZE_LETTER, L"Letter", L"Letter (8.5 x 11 in)",
     8500, 11000, DMPAPER_LETTER},
    {PAPER_SIZE_LETTER_SMALL, L"Letter Small",
     L"Letter Small (8.5 x 11 in)", 8500, 11000,
     DMPAPER_LETTERSMALL},
    {PAPER_SIZE_TABLOID, L"Tabloid", L"Tabloid (11 x 17 in)",
     11000, 17000, DMPAPER_TABLOID},
    {PAPER_SIZE_LEDGER, L"Ledger", L"Ledger (17 x 11 in)",
     17000, 11000, DMPAPER_LEDGER},
    {PAPER_SIZE_LEGAL, L"Legal", L"Legal (8.5 x 14 in)",
     8500, 14000, DMPAPER_LEGAL},
    {PAPER_SIZE_STATEMENT, L"Statement", L"Statement (5.5 x 8.5 in)",
     5500, 8500, DMPAPER_STATEMENT},
    {PAPER_SIZE_EXECUTIVE, L"Executive",
     L"Executive (7.25 x 10.5 in)", 7250, 10500,
     DMPAPER_EXECUTIVE},
    {PAPER_SIZE_A3, L"A3", L"A3 (297 x 420 mm)",
     11693, 16535, DMPAPER_A3},
    {PAPER_SIZE_A4, L"A4", L"A4 (210 x 297 mm)",
     8268, 11693, DMPAPER_A4},
    {PAPER_SIZE_A4_SMALL, L"A4 Small", L"A4 Small (210 x 297 mm)",
     8268, 11693, DMPAPER_A4SMALL},
    {PAPER_SIZE_A5, L"A5", L"A5 (148 x 210 mm)",
     5827, 8268, DMPAPER_A5},
    {PAPER_SIZE_B4_JIS, L"B4 (JIS)", L"B4 (JIS) (250 x 354 mm)",
     9843, 13937, DMPAPER_B4},
    {PAPER_SIZE_B5_JIS, L"B5 (JIS)", L"B5 (JIS) (182 x 257 mm)",
     7165, 10118, DMPAPER_B5},
    {PAPER_SIZE_FOLIO, L"Folio", L"Folio (8.5 x 13 in)",
     8500, 13000, DMPAPER_FOLIO},
    {PAPER_SIZE_QUARTO, L"Quarto", L"Quarto (215 x 275 mm)",
     8465, 10827, DMPAPER_QUARTO},
    {PAPER_SIZE_10X14, L"10 x 14", L"10 x 14 (10 x 14 in)",
     10000, 14000, DMPAPER_10X14},
    {PAPER_SIZE_11X17, L"11 x 17", L"11 x 17 (11 x 17 in)",
     11000, 17000, DMPAPER_11X17},
    {PAPER_SIZE_NOTE, L"Note", L"Note (8.5 x 11 in)",
     8500, 11000, DMPAPER_NOTE},
    {PAPER_SIZE_C_SHEET, L"C Sheet", L"C Sheet (ANSI C, 17 x 22 in)",
     17000, 22000, DMPAPER_CSHEET},
    {PAPER_SIZE_D_SHEET, L"D Sheet", L"D Sheet (ANSI D, 22 x 34 in)",
     22000, 34000, DMPAPER_DSHEET},
    {PAPER_SIZE_E_SHEET, L"E Sheet", L"E Sheet (ANSI E, 34 x 44 in)",
     34000, 44000, DMPAPER_ESHEET},
    {PAPER_SIZE_US_FANFOLD, L"US Fanfold",
     L"US Fanfold (14.875 x 11 in)", 14875, 11000,
     DMPAPER_FANFOLD_US},
    {PAPER_SIZE_GERMAN_STD_FANFOLD, L"German Std. Fanfold",
     L"German Std. Fanfold (8.5 x 12 in)", 8500, 12000,
     DMPAPER_FANFOLD_STD_GERMAN},
    {PAPER_SIZE_GERMAN_LEGAL_FANFOLD, L"German Legal Fanfold",
     L"German Legal Fanfold (8.5 x 13 in)", 8500, 13000,
     DMPAPER_FANFOLD_LGL_GERMAN},
    {PAPER_SIZE_CUSTOM, L"Custom", L"Custom...", 0, 0, 0}
};

_Static_assert(ARRAYSIZE(paperSizes) == PAPER_SIZE_COUNT,
               "paper catalog must contain every PaperSizeId");

size_t paper_size_count(void)
{
    return ARRAYSIZE(paperSizes);
}

const PaperSizePreset *paper_size_at(size_t index)
{
    return index < ARRAYSIZE(paperSizes) ? &paperSizes[index] : NULL;
}

const PaperSizePreset *paper_size_by_id(PaperSizeId id)
{
    size_t index;

    for (index = 0; index < ARRAYSIZE(paperSizes); ++index) {
        if (paperSizes[index].id == id) {
            return &paperSizes[index];
        }
    }
    return NULL;
}

static BOOL paper_dimensions_match(const PaperSizePreset *preset,
                                   LONG widthThousandths,
                                   LONG heightThousandths)
{
    return preset != NULL && preset->id != PAPER_SIZE_CUSTOM &&
           labs(preset->widthThousandths - widthThousandths) <=
               PAPER_MATCH_TOLERANCE &&
           labs(preset->heightThousandths - heightThousandths) <=
               PAPER_MATCH_TOLERANCE;
}

PaperSizeId paper_size_match(LONG widthThousandths,
                             LONG heightThousandths,
                             PaperSizeId preferredId)
{
    const PaperSizePreset *preferred = paper_size_by_id(preferredId);
    size_t index;

    if (paper_dimensions_match(preferred, widthThousandths,
                               heightThousandths)) {
        return preferredId;
    }
    for (index = 0; index < ARRAYSIZE(paperSizes); ++index) {
        if (paper_dimensions_match(&paperSizes[index], widthThousandths,
                                   heightThousandths)) {
            return paperSizes[index].id;
        }
    }
    return PAPER_SIZE_CUSTOM;
}

BOOL paper_size_validate_dimensions(LONG widthThousandths,
                                    LONG heightThousandths,
                                    const RECT *margins)
{
    if (widthThousandths < PAPER_MIN_DIMENSION ||
        widthThousandths > PAPER_MAX_DIMENSION ||
        heightThousandths < PAPER_MIN_DIMENSION ||
        heightThousandths > PAPER_MAX_DIMENSION) {
        return FALSE;
    }
    if (margins != NULL) {
        LONGLONG minimumWidth = (LONGLONG)margins->left +
                                margins->right + PAPER_MIN_PRINTABLE;
        LONGLONG minimumHeight = (LONGLONG)margins->top +
                                 margins->bottom + PAPER_MIN_PRINTABLE;

        if (widthThousandths < minimumWidth ||
            heightThousandths < minimumHeight) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL paper_parse_decimal(HWND dialog, int controlId, double *value)
{
    WCHAR text[64];
    WCHAR *end;
    double parsed;

    if (GetDlgItemTextW(dialog, controlId, text, ARRAYSIZE(text)) <= 0) {
        return FALSE;
    }
    parsed = wcstod(text, &end);
    if (end == text) {
        return FALSE;
    }
    while (*end != L'\0' && iswspace(*end)) {
        ++end;
    }
    if (*end != L'\0' || parsed <= 0.0 || parsed > 1000.0) {
        return FALSE;
    }
    *value = parsed;
    return TRUE;
}

static BOOL paper_read_custom_fields(HWND dialog, int units,
                                     LONG *widthThousandths,
                                     LONG *heightThousandths)
{
    double width;
    double height;
    double widthUnits;
    double heightUnits;

    if (!paper_parse_decimal(dialog, IDC_CUSTOM_PAPER_WIDTH, &width) ||
        !paper_parse_decimal(dialog, IDC_CUSTOM_PAPER_HEIGHT, &height)) {
        return FALSE;
    }
    widthUnits = units == 1 ? width * 10000.0 / 254.0 : width * 1000.0;
    heightUnits = units == 1 ? height * 10000.0 / 254.0 : height * 1000.0;
    if (widthUnits < PAPER_MIN_DIMENSION ||
        widthUnits > PAPER_MAX_DIMENSION ||
        heightUnits < PAPER_MIN_DIMENSION ||
        heightUnits > PAPER_MAX_DIMENSION) {
        return FALSE;
    }
    *widthThousandths = (LONG)(widthUnits + 0.5);
    *heightThousandths = (LONG)(heightUnits + 0.5);
    return TRUE;
}

static void paper_write_custom_fields(HWND dialog,
                                      const CustomPaperDialogState *state)
{
    WCHAR text[64];
    double divisor = state->units == 1 ? 10000.0 / 254.0 : 1000.0;

    StringCchPrintfW(text, ARRAYSIZE(text),
                     state->units == 1 ? L"%.1f" : L"%.3f",
                     state->widthThousandths / divisor);
    SetDlgItemTextW(dialog, IDC_CUSTOM_PAPER_WIDTH, text);
    StringCchPrintfW(text, ARRAYSIZE(text),
                     state->units == 1 ? L"%.1f" : L"%.3f",
                     state->heightThousandths / divisor);
    SetDlgItemTextW(dialog, IDC_CUSTOM_PAPER_HEIGHT, text);
}

static INT_PTR CALLBACK paper_custom_dialog_proc(HWND dialog, UINT message,
                                                 WPARAM wParam,
                                                 LPARAM lParam)
{
    CustomPaperDialogState *state =
        (CustomPaperDialogState *)GetWindowLongPtrW(dialog, DWLP_USER);

    switch (message) {
    case WM_INITDIALOG:
        state = (CustomPaperDialogState *)lParam;
        SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)state);
        SendDlgItemMessageW(dialog, IDC_CUSTOM_PAPER_UNITS, CB_ADDSTRING,
                            0, (LPARAM)L"Inches");
        SendDlgItemMessageW(dialog, IDC_CUSTOM_PAPER_UNITS, CB_ADDSTRING,
                            0, (LPARAM)L"Millimeters");
        SendDlgItemMessageW(dialog, IDC_CUSTOM_PAPER_UNITS, CB_SETCURSEL,
                            state->units, 0);
        paper_write_custom_fields(dialog, state);
        SendDlgItemMessageW(dialog, IDC_CUSTOM_PAPER_WIDTH, EM_SETSEL,
                            0, -1);
        return TRUE;

    case WM_COMMAND:
        if (state == NULL) {
            return FALSE;
        }
        if (LOWORD(wParam) == IDC_CUSTOM_PAPER_UNITS &&
            HIWORD(wParam) == CBN_SELCHANGE) {
            LONG width = state->widthThousandths;
            LONG height = state->heightThousandths;
            int newUnits = (int)SendDlgItemMessageW(
                dialog, IDC_CUSTOM_PAPER_UNITS, CB_GETCURSEL, 0, 0);

            if (paper_read_custom_fields(dialog, state->units,
                                         &width, &height)) {
                state->widthThousandths = width;
                state->heightThousandths = height;
            }
            state->units = newUnits == 1 ? 1 : 0;
            paper_write_custom_fields(dialog, state);
            return TRUE;
        }
        if (LOWORD(wParam) == IDOK) {
            LONG width;
            LONG height;

            if (!paper_read_custom_fields(dialog, state->units,
                                          &width, &height) ||
                !paper_size_validate_dimensions(width, height,
                                                &state->app->pageMargins)) {
                MessageBoxW(
                    dialog,
                    L"Enter a width and height between 0.1 and 100 inches "
                    L"(2.54 and 2540 mm). The page must also leave at least "
                    L"0.1 inch of printable space inside the current margins.",
                    L"Custom Paper Size", MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            state->widthThousandths = width;
            state->heightThousandths = height;
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static BOOL paper_apply_dimensions(AppState *app, PaperSizeId id,
                                   LONG widthThousandths,
                                   LONG heightThousandths)
{
    const PaperSizePreset *preset;

    if (app == NULL ||
        !paper_size_validate_dimensions(widthThousandths,
                                        heightThousandths,
                                        &app->pageMargins)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    preset = paper_size_by_id(id);
    if (preset == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    app->paperSizeId = id;
    app->pageSize.x = widthThousandths;
    app->pageSize.y = heightThousandths;
    paper_size_apply_to_devmode(app, app->printerDevMode);
    text_engine_note_layout_change(app);
    pageview_mark_dirty(app);
    pageview_layout(app);
    app_update_status(app, TRUE);
    pageview_sync_to_caret(app, TRUE);
    ribbon_sync_paper_size(app);
    InvalidateRect(app->pageView, NULL, FALSE);
    live_share_document_changed(app);
    return TRUE;
}

BOOL paper_size_apply_shared_layout(AppState *app, PaperSizeId id,
                                    LONG widthThousandths,
                                    LONG heightThousandths,
                                    const RECT *margins)
{
    const PaperSizePreset *preset;

    if (app == NULL || margins == NULL || id < 0 || id >= PAPER_SIZE_COUNT ||
        !paper_size_validate_dimensions(widthThousandths,
                                        heightThousandths, margins)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    preset = paper_size_by_id(id);
    if (preset == NULL ||
        (id != PAPER_SIZE_CUSTOM &&
         (preset->widthThousandths != widthThousandths ||
          preset->heightThousandths != heightThousandths))) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    if (app->paperSizeId == id && app->pageSize.x == widthThousandths &&
        app->pageSize.y == heightThousandths &&
        EqualRect(&app->pageMargins, margins)) {
        return TRUE;
    }

    app->paperSizeId = id;
    app->pageSize.x = widthThousandths;
    app->pageSize.y = heightThousandths;
    app->pageMargins = *margins;
    paper_size_apply_to_devmode(app, app->printerDevMode);
    text_engine_note_layout_change(app);
    pageview_mark_dirty(app);
    pageview_layout(app);
    app_update_status(app, TRUE);
    pageview_sync_to_caret(app, TRUE);
    ribbon_sync_paper_size(app);
    InvalidateRect(app->pageView, NULL, FALSE);
    return TRUE;
}

BOOL paper_size_select(AppState *app, PaperSizeId id)
{
    const PaperSizePreset *preset = paper_size_by_id(id);

    if (app == NULL || preset == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (id == PAPER_SIZE_CUSTOM) {
        CustomPaperDialogState state;
        INT_PTR result;

        state.app = app;
        state.widthThousandths = app->pageSize.x;
        state.heightThousandths = app->pageSize.y;
        state.units = 0;
        result = DialogBoxParamW(app->instance,
                                 MAKEINTRESOURCEW(IDD_CUSTOM_PAPER),
                                 app->mainWindow,
                                 paper_custom_dialog_proc,
                                 (LPARAM)&state);
        if (result != IDOK) {
            if (result == -1) {
                app_show_error(app->mainWindow,
                               L"The Custom Paper Size dialog failed.",
                               GetLastError());
            }
            return FALSE;
        }
        return paper_apply_dimensions(app, PAPER_SIZE_CUSTOM,
                                      state.widthThousandths,
                                      state.heightThousandths);
    }
    if (!paper_size_validate_dimensions(preset->widthThousandths,
                                        preset->heightThousandths,
                                        &app->pageMargins)) {
        MessageBoxW(app->mainWindow,
                    L"The current margins are too large for that paper size. "
                    L"Choose smaller margins in Page Setup and try again.",
                    L"Paper Size", MB_OK | MB_ICONWARNING);
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return paper_apply_dimensions(app, id, preset->widthThousandths,
                                  preset->heightThousandths);
}

BOOL paper_size_select_catalog_index(AppState *app, size_t index)
{
    const PaperSizePreset *preset = paper_size_at(index);
    return preset != NULL && paper_size_select(app, preset->id);
}

static PaperSizeId paper_size_from_device_id(WORD devicePaperSize)
{
    size_t index;

    for (index = 0; index < ARRAYSIZE(paperSizes); ++index) {
        if (paperSizes[index].devicePaperSize != 0 &&
            paperSizes[index].devicePaperSize == devicePaperSize) {
            return paperSizes[index].id;
        }
    }
    return PAPER_SIZE_CUSTOM;
}

void paper_size_note_external_change(AppState *app)
{
    PaperSizeId detected = PAPER_SIZE_CUSTOM;
    DEVMODEW *devMode = NULL;

    if (app == NULL) {
        return;
    }
    if (app->printerDevMode != NULL) {
        devMode = (DEVMODEW *)GlobalLock(app->printerDevMode);
        if (devMode != NULL) {
            if ((devMode->dmFields & DM_PAPERSIZE) != 0 &&
                devMode->dmPaperSize != 0) {
                detected = paper_size_from_device_id(
                    (WORD)devMode->dmPaperSize);
            }
            GlobalUnlock(app->printerDevMode);
        }
    }
    if (detected == PAPER_SIZE_CUSTOM) {
        detected = paper_size_match(app->pageSize.x, app->pageSize.y,
                                    app->paperSizeId);
    }
    app->paperSizeId = detected;
    ribbon_sync_paper_size(app);
}

BOOL paper_size_apply_to_devmode(const AppState *app, HGLOBAL devModeHandle)
{
    const PaperSizePreset *preset;
    DEVMODEW *devMode;
    LONG paperWidth;
    LONG paperLength;

    if (app == NULL || devModeHandle == NULL ||
        app->pageSize.x <= 0 || app->pageSize.y <= 0) {
        return FALSE;
    }
    preset = paper_size_by_id(app->paperSizeId);
    if (preset == NULL) {
        return FALSE;
    }
    devMode = (DEVMODEW *)GlobalLock(devModeHandle);
    if (devMode == NULL) {
        return FALSE;
    }

    devMode->dmFields |= DM_ORIENTATION;
    devMode->dmOrientation = app->pageSize.x > app->pageSize.y
                                 ? DMORIENT_LANDSCAPE
                                 : DMORIENT_PORTRAIT;
    if (preset->id == PAPER_SIZE_CUSTOM ||
        preset->devicePaperSize == 0) {
        paperWidth = MulDiv(app->pageSize.x, 254, 1000);
        paperLength = MulDiv(app->pageSize.y, 254, 1000);
        if (paperWidth <= 0 || paperWidth > SHRT_MAX ||
            paperLength <= 0 || paperLength > SHRT_MAX) {
            GlobalUnlock(devModeHandle);
            return FALSE;
        }
        devMode->dmFields &= ~DM_PAPERSIZE;
        devMode->dmFields |= DM_PAPERWIDTH | DM_PAPERLENGTH;
        devMode->dmPaperSize = 0;
        devMode->dmPaperWidth = (SHORT)paperWidth;
        devMode->dmPaperLength = (SHORT)paperLength;
    } else {
        devMode->dmFields |= DM_PAPERSIZE;
        devMode->dmFields &= ~(DM_PAPERWIDTH | DM_PAPERLENGTH);
        devMode->dmPaperSize = (SHORT)preset->devicePaperSize;
    }
    GlobalUnlock(devModeHandle);
    return TRUE;
}

static UINT paper_text_hash(const WCHAR *text)
{
    UINT hash = 2166136261u;

    while (text != NULL && *text != L'\0') {
        hash ^= (UINT)*text++;
        hash *= 16777619u;
    }
    return hash;
}

LRESULT paper_size_query_state(const AppState *app, UINT query,
                               LPARAM indexValue)
{
    size_t index = (size_t)indexValue;
    const PaperSizePreset *preset;

    if (app == NULL) {
        return 0;
    }
    switch (query) {
    case WCQ_PAPER_SIZE_ID:
        return app->paperSizeId;
    case WCQ_PAPER_SIZE_COUNT:
        return (LRESULT)paper_size_count();
    case WCQ_PAPER_PRESET_DEVICE_ID:
        preset = paper_size_at(index);
        return preset != NULL ? preset->devicePaperSize : -1;
    case WCQ_PAPER_PRESET_WIDTH:
        preset = paper_size_at(index);
        return preset != NULL ? preset->widthThousandths : -1;
    case WCQ_PAPER_PRESET_HEIGHT:
        preset = paper_size_at(index);
        return preset != NULL ? preset->heightThousandths : -1;
    case WCQ_PAPER_PRESET_NAME_HASH:
        preset = paper_size_at(index);
        return preset != NULL ? (LRESULT)paper_text_hash(preset->name) : 0;
    default:
        return 0;
    }
}
