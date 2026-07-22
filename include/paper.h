#ifndef WORDCRAFT_PAPER_H
#define WORDCRAFT_PAPER_H

#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <stddef.h>

typedef enum PaperSizeId {
    PAPER_SIZE_LETTER = 0,
    PAPER_SIZE_LETTER_SMALL,
    PAPER_SIZE_TABLOID,
    PAPER_SIZE_LEDGER,
    PAPER_SIZE_LEGAL,
    PAPER_SIZE_STATEMENT,
    PAPER_SIZE_EXECUTIVE,
    PAPER_SIZE_A3,
    PAPER_SIZE_A4,
    PAPER_SIZE_A4_SMALL,
    PAPER_SIZE_A5,
    PAPER_SIZE_B4_JIS,
    PAPER_SIZE_B5_JIS,
    PAPER_SIZE_FOLIO,
    PAPER_SIZE_QUARTO,
    PAPER_SIZE_10X14,
    PAPER_SIZE_11X17,
    PAPER_SIZE_NOTE,
    PAPER_SIZE_C_SHEET,
    PAPER_SIZE_D_SHEET,
    PAPER_SIZE_E_SHEET,
    PAPER_SIZE_US_FANFOLD,
    PAPER_SIZE_GERMAN_STD_FANFOLD,
    PAPER_SIZE_GERMAN_LEGAL_FANFOLD,
    PAPER_SIZE_CUSTOM,
    PAPER_SIZE_COUNT
} PaperSizeId;

typedef struct PaperSizePreset {
    PaperSizeId id;
    const WCHAR *name;
    const WCHAR *displayName;
    LONG widthThousandths;
    LONG heightThousandths;
    WORD devicePaperSize;
} PaperSizePreset;

struct AppState;

size_t paper_size_count(void);
const PaperSizePreset *paper_size_at(size_t index);
const PaperSizePreset *paper_size_by_id(PaperSizeId id);
PaperSizeId paper_size_match(LONG widthThousandths,
                             LONG heightThousandths,
                             PaperSizeId preferredId);
BOOL paper_size_validate_dimensions(LONG widthThousandths,
                                    LONG heightThousandths,
                                    const RECT *margins);
BOOL paper_size_select(struct AppState *app, PaperSizeId id);
BOOL paper_size_select_catalog_index(struct AppState *app, size_t index);
BOOL paper_size_apply_shared_layout(struct AppState *app, PaperSizeId id,
                                    LONG widthThousandths,
                                    LONG heightThousandths,
                                    const RECT *margins);
void paper_size_note_external_change(struct AppState *app);
BOOL paper_size_apply_to_devmode(const struct AppState *app,
                                 HGLOBAL devModeHandle);
LRESULT paper_size_query_state(const struct AppState *app, UINT query,
                               LPARAM index);

#endif
