#include "editor.h"

#include <stdio.h>
#include <wchar.h>

typedef struct ExpectedPaper {
    PaperSizeId id;
    WORD devicePaperSize;
    LONG widthThousandths;
    LONG heightThousandths;
    const WCHAR *name;
    const WCHAR *displayName;
} ExpectedPaper;

static const ExpectedPaper expectedPapers[] = {
    {PAPER_SIZE_LETTER, DMPAPER_LETTER, 8500, 11000,
     L"Letter", L"Letter (8.5 x 11 in)"},
    {PAPER_SIZE_LETTER_SMALL, DMPAPER_LETTERSMALL, 8500, 11000,
     L"Letter Small", L"Letter Small (8.5 x 11 in)"},
    {PAPER_SIZE_TABLOID, DMPAPER_TABLOID, 11000, 17000,
     L"Tabloid", L"Tabloid (11 x 17 in)"},
    {PAPER_SIZE_LEDGER, DMPAPER_LEDGER, 17000, 11000,
     L"Ledger", L"Ledger (17 x 11 in)"},
    {PAPER_SIZE_LEGAL, DMPAPER_LEGAL, 8500, 14000,
     L"Legal", L"Legal (8.5 x 14 in)"},
    {PAPER_SIZE_STATEMENT, DMPAPER_STATEMENT, 5500, 8500,
     L"Statement", L"Statement (5.5 x 8.5 in)"},
    {PAPER_SIZE_EXECUTIVE, DMPAPER_EXECUTIVE, 7250, 10500,
     L"Executive", L"Executive (7.25 x 10.5 in)"},
    {PAPER_SIZE_A3, DMPAPER_A3, 11693, 16535,
     L"A3", L"A3 (297 x 420 mm)"},
    {PAPER_SIZE_A4, DMPAPER_A4, 8268, 11693,
     L"A4", L"A4 (210 x 297 mm)"},
    {PAPER_SIZE_A4_SMALL, DMPAPER_A4SMALL, 8268, 11693,
     L"A4 Small", L"A4 Small (210 x 297 mm)"},
    {PAPER_SIZE_A5, DMPAPER_A5, 5827, 8268,
     L"A5", L"A5 (148 x 210 mm)"},
    {PAPER_SIZE_B4_JIS, DMPAPER_B4, 9843, 13937,
     L"B4 (JIS)", L"B4 (JIS) (250 x 354 mm)"},
    {PAPER_SIZE_B5_JIS, DMPAPER_B5, 7165, 10118,
     L"B5 (JIS)", L"B5 (JIS) (182 x 257 mm)"},
    {PAPER_SIZE_FOLIO, DMPAPER_FOLIO, 8500, 13000,
     L"Folio", L"Folio (8.5 x 13 in)"},
    {PAPER_SIZE_QUARTO, DMPAPER_QUARTO, 8465, 10827,
     L"Quarto", L"Quarto (215 x 275 mm)"},
    {PAPER_SIZE_10X14, DMPAPER_10X14, 10000, 14000,
     L"10 x 14", L"10 x 14 (10 x 14 in)"},
    {PAPER_SIZE_11X17, DMPAPER_11X17, 11000, 17000,
     L"11 x 17", L"11 x 17 (11 x 17 in)"},
    {PAPER_SIZE_NOTE, DMPAPER_NOTE, 8500, 11000,
     L"Note", L"Note (8.5 x 11 in)"},
    {PAPER_SIZE_C_SHEET, DMPAPER_CSHEET, 17000, 22000,
     L"C Sheet", L"C Sheet (ANSI C, 17 x 22 in)"},
    {PAPER_SIZE_D_SHEET, DMPAPER_DSHEET, 22000, 34000,
     L"D Sheet", L"D Sheet (ANSI D, 22 x 34 in)"},
    {PAPER_SIZE_E_SHEET, DMPAPER_ESHEET, 34000, 44000,
     L"E Sheet", L"E Sheet (ANSI E, 34 x 44 in)"},
    {PAPER_SIZE_US_FANFOLD, DMPAPER_FANFOLD_US, 14875, 11000,
     L"US Fanfold", L"US Fanfold (14.875 x 11 in)"},
    {PAPER_SIZE_GERMAN_STD_FANFOLD, DMPAPER_FANFOLD_STD_GERMAN,
     8500, 12000, L"German Std. Fanfold",
     L"German Std. Fanfold (8.5 x 12 in)"},
    {PAPER_SIZE_GERMAN_LEGAL_FANFOLD, DMPAPER_FANFOLD_LGL_GERMAN,
     8500, 13000, L"German Legal Fanfold",
     L"German Legal Fanfold (8.5 x 13 in)"},
    {PAPER_SIZE_CUSTOM, 0, 0, 0, L"Custom", L"Custom..."}
};

_Static_assert(PAPER_SIZE_LETTER == 0, "Letter enum value changed");
_Static_assert(PAPER_SIZE_LETTER_SMALL == 1,
               "Letter Small enum value changed");
_Static_assert(PAPER_SIZE_TABLOID == 2, "Tabloid enum value changed");
_Static_assert(PAPER_SIZE_LEDGER == 3, "Ledger enum value changed");
_Static_assert(PAPER_SIZE_LEGAL == 4, "Legal enum value changed");
_Static_assert(PAPER_SIZE_STATEMENT == 5, "Statement enum value changed");
_Static_assert(PAPER_SIZE_EXECUTIVE == 6, "Executive enum value changed");
_Static_assert(PAPER_SIZE_A3 == 7, "A3 enum value changed");
_Static_assert(PAPER_SIZE_A4 == 8, "A4 enum value changed");
_Static_assert(PAPER_SIZE_A4_SMALL == 9, "A4 Small enum value changed");
_Static_assert(PAPER_SIZE_A5 == 10, "A5 enum value changed");
_Static_assert(PAPER_SIZE_B4_JIS == 11, "B4 enum value changed");
_Static_assert(PAPER_SIZE_B5_JIS == 12, "B5 enum value changed");
_Static_assert(PAPER_SIZE_FOLIO == 13, "Folio enum value changed");
_Static_assert(PAPER_SIZE_QUARTO == 14, "Quarto enum value changed");
_Static_assert(PAPER_SIZE_10X14 == 15, "10 x 14 enum value changed");
_Static_assert(PAPER_SIZE_11X17 == 16, "11 x 17 enum value changed");
_Static_assert(PAPER_SIZE_NOTE == 17, "Note enum value changed");
_Static_assert(PAPER_SIZE_C_SHEET == 18, "C Sheet enum value changed");
_Static_assert(PAPER_SIZE_D_SHEET == 19, "D Sheet enum value changed");
_Static_assert(PAPER_SIZE_E_SHEET == 20, "E Sheet enum value changed");
_Static_assert(PAPER_SIZE_US_FANFOLD == 21,
               "US Fanfold enum value changed");
_Static_assert(PAPER_SIZE_GERMAN_STD_FANFOLD == 22,
               "German Std. Fanfold enum value changed");
_Static_assert(PAPER_SIZE_GERMAN_LEGAL_FANFOLD == 23,
               "German Legal Fanfold enum value changed");
_Static_assert(PAPER_SIZE_CUSTOM == 24, "Custom enum value changed");
_Static_assert(PAPER_SIZE_COUNT == 25, "paper enum count changed");
_Static_assert(ARRAYSIZE(expectedPapers) == PAPER_SIZE_COUNT,
               "expected paper table is incomplete");

/* src/paper.c deliberately owns UI side effects. The probe links that source
 * directly and replaces unrelated application services with inert stubs. */
void text_engine_note_layout_change(AppState *app)
{
    (void)app;
}

void pageview_mark_dirty(AppState *app)
{
    (void)app;
}

void pageview_layout(AppState *app)
{
    (void)app;
}

void app_update_status(AppState *app, BOOL recountWords)
{
    (void)app;
    (void)recountWords;
}

void pageview_sync_to_caret(AppState *app, BOOL ensureVisible)
{
    (void)app;
    (void)ensureVisible;
}

void ribbon_sync_paper_size(AppState *app)
{
    (void)app;
}

void live_share_document_changed(AppState *app)
{
    (void)app;
}

void app_show_error(HWND owner, const WCHAR *action, DWORD errorCode)
{
    (void)owner;
    (void)action;
    (void)errorCode;
}

static UINT expected_name_hash(const WCHAR *text)
{
    UINT hash = 2166136261u;

    while (text != NULL && *text != L'\0') {
        hash ^= (UINT)*text++;
        hash *= 16777619u;
    }
    return hash;
}

static BOOL verify_catalog(void)
{
    AppState app;
    size_t index;

    ZeroMemory(&app, sizeof(app));
    app.paperSizeId = PAPER_SIZE_LETTER;
    if (paper_size_count() != ARRAYSIZE(expectedPapers) ||
        paper_size_query_state(&app, WCQ_PAPER_SIZE_COUNT, 0) !=
            (LRESULT)ARRAYSIZE(expectedPapers)) {
        fwprintf(stderr, L"paper catalog count mismatch\n");
        return FALSE;
    }

    for (index = 0; index < ARRAYSIZE(expectedPapers); ++index) {
        const ExpectedPaper *expected = &expectedPapers[index];
        const PaperSizePreset *actual = paper_size_at(index);
        const PaperSizePreset *byId = paper_size_by_id(expected->id);

        if (actual == NULL || byId != actual ||
            actual->id != expected->id || actual->id != (PaperSizeId)index ||
            actual->devicePaperSize != expected->devicePaperSize ||
            actual->widthThousandths != expected->widthThousandths ||
            actual->heightThousandths != expected->heightThousandths ||
            actual->name == NULL ||
            wcscmp(actual->name, expected->name) != 0 ||
            actual->displayName == NULL ||
            wcscmp(actual->displayName, expected->displayName) != 0) {
            fwprintf(stderr,
                     L"paper catalog mismatch at %zu (expected '%ls')\n",
                     index, expected->name);
            return FALSE;
        }
        if (paper_size_query_state(&app, WCQ_PAPER_PRESET_DEVICE_ID,
                                   (LPARAM)index) !=
                expected->devicePaperSize ||
            paper_size_query_state(&app, WCQ_PAPER_PRESET_WIDTH,
                                   (LPARAM)index) !=
                expected->widthThousandths ||
            paper_size_query_state(&app, WCQ_PAPER_PRESET_HEIGHT,
                                   (LPARAM)index) !=
                expected->heightThousandths ||
            (UINT)paper_size_query_state(&app, WCQ_PAPER_PRESET_NAME_HASH,
                                         (LPARAM)index) !=
                expected_name_hash(expected->name)) {
            fwprintf(stderr, L"paper query mismatch at %zu ('%ls')\n",
                     index, expected->name);
            return FALSE;
        }
    }

    if (paper_size_at(ARRAYSIZE(expectedPapers)) != NULL ||
        paper_size_by_id(PAPER_SIZE_COUNT) != NULL ||
        paper_size_by_id((PaperSizeId)-1) != NULL ||
        paper_size_query_state(&app, WCQ_PAPER_PRESET_DEVICE_ID,
                               (LPARAM)ARRAYSIZE(expectedPapers)) != -1 ||
        paper_size_query_state(&app, WCQ_PAPER_PRESET_WIDTH,
                               (LPARAM)ARRAYSIZE(expectedPapers)) != -1 ||
        paper_size_query_state(&app, WCQ_PAPER_PRESET_HEIGHT,
                               (LPARAM)ARRAYSIZE(expectedPapers)) != -1 ||
        paper_size_query_state(&app, WCQ_PAPER_PRESET_NAME_HASH,
                               (LPARAM)ARRAYSIZE(expectedPapers)) != 0 ||
        paper_size_query_state(NULL, WCQ_PAPER_SIZE_COUNT, 0) != 0) {
        fwprintf(stderr, L"out-of-range paper lookup was not rejected\n");
        return FALSE;
    }
    app.paperSizeId = PAPER_SIZE_E_SHEET;
    if (paper_size_query_state(&app, WCQ_PAPER_SIZE_ID, 0) !=
        PAPER_SIZE_E_SHEET) {
        fwprintf(stderr, L"active paper id query mismatch\n");
        return FALSE;
    }
    return TRUE;
}

static BOOL expect_match(LONG width, LONG height, PaperSizeId preferred,
                         PaperSizeId expected, const WCHAR *description)
{
    PaperSizeId actual = paper_size_match(width, height, preferred);

    if (actual != expected) {
        fwprintf(stderr,
                 L"paper match '%ls' returned id %d instead of %d\n",
                 description, (int)actual, (int)expected);
        return FALSE;
    }
    return TRUE;
}

static BOOL verify_matching_and_aliases(void)
{
    return
        /* Identical media retain the caller's exact printer alias. */
        expect_match(8500, 11000, PAPER_SIZE_LETTER,
                     PAPER_SIZE_LETTER, L"Letter preference") &&
        expect_match(8500, 11000, PAPER_SIZE_LETTER_SMALL,
                     PAPER_SIZE_LETTER_SMALL, L"Letter Small preference") &&
        expect_match(8500, 11000, PAPER_SIZE_NOTE,
                     PAPER_SIZE_NOTE, L"Note preference") &&
        expect_match(8268, 11693, PAPER_SIZE_A4_SMALL,
                     PAPER_SIZE_A4_SMALL, L"A4 Small preference") &&
        expect_match(11000, 17000, PAPER_SIZE_11X17,
                     PAPER_SIZE_11X17, L"11 x 17 preference") &&
        expect_match(8500, 13000, PAPER_SIZE_GERMAN_LEGAL_FANFOLD,
                     PAPER_SIZE_GERMAN_LEGAL_FANFOLD,
                     L"German Legal preference") &&
        /* With no applicable preference, catalog order is deterministic. */
        expect_match(8500, 11000, PAPER_SIZE_A3,
                     PAPER_SIZE_LETTER, L"Letter first alias") &&
        expect_match(8268, 11693, PAPER_SIZE_CUSTOM,
                     PAPER_SIZE_A4, L"A4 first alias") &&
        expect_match(11000, 17000, PAPER_SIZE_COUNT,
                     PAPER_SIZE_TABLOID, L"Tabloid first alias") &&
        expect_match(8500, 13000, (PaperSizeId)-1,
                     PAPER_SIZE_FOLIO, L"Folio first alias") &&
        /* The five-thousandths tolerance is inclusive on both axes. */
        expect_match(11698, 16530, PAPER_SIZE_A3,
                     PAPER_SIZE_A3, L"positive tolerance edge") &&
        expect_match(11688, 16540, PAPER_SIZE_CUSTOM,
                     PAPER_SIZE_A3, L"negative tolerance edge") &&
        expect_match(11699, 16535, PAPER_SIZE_A3,
                     PAPER_SIZE_CUSTOM, L"width beyond tolerance") &&
        expect_match(11693, 16541, PAPER_SIZE_A3,
                     PAPER_SIZE_CUSTOM, L"height beyond tolerance") &&
        /* Matching is ordered: rotated stock resolves to Ledger. */
        expect_match(17000, 11000, PAPER_SIZE_TABLOID,
                     PAPER_SIZE_LEDGER, L"rotated Tabloid") &&
        expect_match(12345, 23456, PAPER_SIZE_LETTER,
                     PAPER_SIZE_CUSTOM, L"unknown dimensions");
}

static BOOL verify_dimension_validation(void)
{
    RECT margins = {1000, 1000, 1000, 1000};
    RECT asymmetric = {250, 500, 750, 1250};
    size_t index;

    if (!paper_size_validate_dimensions(100, 100, NULL) ||
        !paper_size_validate_dimensions(100000, 100000, NULL) ||
        paper_size_validate_dimensions(99, 100, NULL) ||
        paper_size_validate_dimensions(100, 99, NULL) ||
        paper_size_validate_dimensions(100001, 100000, NULL) ||
        paper_size_validate_dimensions(100000, 100001, NULL) ||
        paper_size_validate_dimensions(0, 100, NULL) ||
        paper_size_validate_dimensions(-1, 100, NULL)) {
        fwprintf(stderr, L"absolute custom paper bounds failed\n");
        return FALSE;
    }
    if (!paper_size_validate_dimensions(2100, 2100, &margins) ||
        paper_size_validate_dimensions(2099, 2100, &margins) ||
        paper_size_validate_dimensions(2100, 2099, &margins) ||
        !paper_size_validate_dimensions(1100, 1850, &asymmetric) ||
        paper_size_validate_dimensions(1099, 1850, &asymmetric) ||
        paper_size_validate_dimensions(1100, 1849, &asymmetric)) {
        fwprintf(stderr, L"custom paper printable-area bounds failed\n");
        return FALSE;
    }
    for (index = 0; index + 1 < ARRAYSIZE(expectedPapers); ++index) {
        if (!paper_size_validate_dimensions(
                expectedPapers[index].widthThousandths,
                expectedPapers[index].heightThousandths, &margins)) {
            fwprintf(stderr, L"preset '%ls' failed dimension validation\n",
                     expectedPapers[index].name);
            return FALSE;
        }
    }
    return TRUE;
}

static HGLOBAL allocate_devmode(void)
{
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,
                                 sizeof(DEVMODEW));
    DEVMODEW *devMode;

    if (handle == NULL) {
        return NULL;
    }
    devMode = (DEVMODEW *)GlobalLock(handle);
    if (devMode == NULL) {
        GlobalFree(handle);
        return NULL;
    }
    devMode->dmSize = sizeof(*devMode);
    devMode->dmFields = DM_COPIES | DM_PAPERSIZE |
                        DM_PAPERWIDTH | DM_PAPERLENGTH;
    devMode->dmCopies = 3;
    devMode->dmPaperSize = DMPAPER_LEGAL;
    devMode->dmPaperWidth = 123;
    devMode->dmPaperLength = 456;
    GlobalUnlock(handle);
    return handle;
}

static BOOL verify_fixed_devmode_mapping(void)
{
    size_t index;

    for (index = 0; index + 1 < ARRAYSIZE(expectedPapers); ++index) {
        const ExpectedPaper *expected = &expectedPapers[index];
        AppState app;
        HGLOBAL handle = allocate_devmode();
        DEVMODEW *devMode;
        BOOL valid;

        if (handle == NULL) {
            fwprintf(stderr, L"could not allocate fixed DEVMODE\n");
            return FALSE;
        }
        ZeroMemory(&app, sizeof(app));
        app.paperSizeId = expected->id;
        app.pageSize.x = expected->widthThousandths;
        app.pageSize.y = expected->heightThousandths;
        if (!paper_size_apply_to_devmode(&app, handle)) {
            fwprintf(stderr, L"fixed DEVMODE mapping failed for '%ls'\n",
                     expected->name);
            GlobalFree(handle);
            return FALSE;
        }
        devMode = (DEVMODEW *)GlobalLock(handle);
        if (devMode == NULL) {
            fwprintf(stderr, L"could not inspect fixed DEVMODE\n");
            GlobalFree(handle);
            return FALSE;
        }
        valid = (devMode->dmFields &
                 (DM_COPIES | DM_PAPERSIZE | DM_ORIENTATION)) ==
                    (DM_COPIES | DM_PAPERSIZE | DM_ORIENTATION) &&
                (devMode->dmFields &
                 (DM_PAPERWIDTH | DM_PAPERLENGTH)) == 0 &&
                devMode->dmCopies == 3 &&
                devMode->dmPaperSize == (SHORT)expected->devicePaperSize &&
                devMode->dmOrientation ==
                    (expected->widthThousandths >
                             expected->heightThousandths
                         ? DMORIENT_LANDSCAPE
                         : DMORIENT_PORTRAIT);
        GlobalUnlock(handle);
        GlobalFree(handle);
        if (!valid) {
            fwprintf(stderr,
                     L"fixed DEVMODE fields were wrong for '%ls'\n",
                     expected->name);
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL verify_custom_devmode_mapping(void)
{
    AppState app;
    HGLOBAL handle = allocate_devmode();
    DEVMODEW *devMode;
    BOOL valid;

    if (handle == NULL) {
        fwprintf(stderr, L"could not allocate custom DEVMODE\n");
        return FALSE;
    }
    ZeroMemory(&app, sizeof(app));
    app.paperSizeId = PAPER_SIZE_CUSTOM;
    app.pageSize.x = 12345;
    app.pageSize.y = 67890;
    if (!paper_size_apply_to_devmode(&app, handle)) {
        fwprintf(stderr, L"custom portrait DEVMODE mapping failed\n");
        GlobalFree(handle);
        return FALSE;
    }
    devMode = (DEVMODEW *)GlobalLock(handle);
    if (devMode == NULL) {
        GlobalFree(handle);
        return FALSE;
    }
    valid = (devMode->dmFields &
             (DM_COPIES | DM_PAPERWIDTH | DM_PAPERLENGTH |
              DM_ORIENTATION)) ==
                (DM_COPIES | DM_PAPERWIDTH | DM_PAPERLENGTH |
                 DM_ORIENTATION) &&
            (devMode->dmFields & DM_PAPERSIZE) == 0 &&
            devMode->dmPaperSize == 0 &&
            devMode->dmPaperWidth == 3136 &&
            devMode->dmPaperLength == 17244 &&
            devMode->dmOrientation == DMORIENT_PORTRAIT &&
            devMode->dmCopies == 3;
    GlobalUnlock(handle);
    if (!valid) {
        fwprintf(stderr, L"custom portrait DEVMODE fields were wrong\n");
        GlobalFree(handle);
        return FALSE;
    }

    app.pageSize.x = 44000;
    app.pageSize.y = 34000;
    if (!paper_size_apply_to_devmode(&app, handle)) {
        fwprintf(stderr, L"custom landscape DEVMODE mapping failed\n");
        GlobalFree(handle);
        return FALSE;
    }
    devMode = (DEVMODEW *)GlobalLock(handle);
    if (devMode == NULL) {
        GlobalFree(handle);
        return FALSE;
    }
    valid = devMode->dmPaperWidth == 11176 &&
            devMode->dmPaperLength == 8636 &&
            devMode->dmOrientation == DMORIENT_LANDSCAPE;
    GlobalUnlock(handle);
    GlobalFree(handle);
    if (!valid) {
        fwprintf(stderr, L"custom landscape DEVMODE fields were wrong\n");
        return FALSE;
    }

    ZeroMemory(&app, sizeof(app));
    app.paperSizeId = PAPER_SIZE_CUSTOM;
    app.pageSize.x = 8500;
    app.pageSize.y = 11000;
    if (paper_size_apply_to_devmode(NULL, NULL) ||
        paper_size_apply_to_devmode(&app, NULL)) {
        fwprintf(stderr, L"invalid DEVMODE arguments were accepted\n");
        return FALSE;
    }
    return TRUE;
}

static BOOL verify_external_devmode_matching(void)
{
    AppState app;
    HGLOBAL handle = allocate_devmode();
    DEVMODEW *devMode;

    if (handle == NULL) {
        return FALSE;
    }
    ZeroMemory(&app, sizeof(app));
    app.printerDevMode = handle;
    app.paperSizeId = PAPER_SIZE_LETTER;
    app.pageSize.x = 8500;
    app.pageSize.y = 11000;
    devMode = (DEVMODEW *)GlobalLock(handle);
    if (devMode == NULL) {
        GlobalFree(handle);
        return FALSE;
    }
    devMode->dmFields = DM_PAPERSIZE;
    devMode->dmPaperSize = DMPAPER_NOTE;
    GlobalUnlock(handle);
    paper_size_note_external_change(&app);
    if (app.paperSizeId != PAPER_SIZE_NOTE) {
        fwprintf(stderr, L"external Note device id lost its alias\n");
        GlobalFree(handle);
        return FALSE;
    }

    devMode = (DEVMODEW *)GlobalLock(handle);
    if (devMode == NULL) {
        GlobalFree(handle);
        return FALSE;
    }
    devMode->dmFields = DM_PAPERSIZE;
    devMode->dmPaperSize = DMPAPER_USER;
    GlobalUnlock(handle);
    app.paperSizeId = PAPER_SIZE_LETTER_SMALL;
    paper_size_note_external_change(&app);
    if (app.paperSizeId != PAPER_SIZE_LETTER_SMALL) {
        fwprintf(stderr, L"dimension fallback did not retain preferred alias\n");
        GlobalFree(handle);
        return FALSE;
    }

    app.pageSize.x = 12345;
    app.pageSize.y = 23456;
    paper_size_note_external_change(&app);
    GlobalFree(handle);
    if (app.paperSizeId != PAPER_SIZE_CUSTOM) {
        fwprintf(stderr, L"unknown external dimensions were not Custom\n");
        return FALSE;
    }
    return TRUE;
}

int main(void)
{
    if (!verify_catalog() ||
        !verify_matching_and_aliases() ||
        !verify_dimension_validation() ||
        !verify_fixed_devmode_mapping() ||
        !verify_custom_devmode_mapping() ||
        !verify_external_devmode_matching()) {
        return 1;
    }
    printf("paper_catalog=ok aliases=ok matching=ok custom_validation=ok "
           "devmode=ok\n");
    return 0;
}
