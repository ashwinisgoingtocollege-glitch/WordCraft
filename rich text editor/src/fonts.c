#include "editor.h"

#define FONT_ITEM_UNAVAILABLE 0
#define FONT_ITEM_INSTALLED 1
#define FONT_UNAVAILABLE_SUFFIX L"  (not installed)"

static const WCHAR *const supportedFonts[] = {
    L"Abadi",
    L"Agency FB",
    L"Algerian",
    L"Aptos",
    L"Arial",
    L"Arial Nova",
    L"Avenir Next",
    L"Bahnschrift",
    L"Baskerville",
    L"Bell MT",
    L"Bembo",
    L"Berlin Sans FB",
    L"Bernard MT",
    L"Bierstadt",
    L"Bodoni MT",
    L"Book Antiqua",
    L"Bookman Old Style",
    L"Bradley Hand ITC",
    L"Britannic Bold",
    L"Broadway",
    L"Brush Script MT",
    L"Calibri",
    L"Californian FB",
    L"Calisto MT",
    L"Cambria",
    L"Candara",
    L"Cascadia Code",
    L"Cascadia Mono",
    L"Century",
    L"Century Gothic",
    L"Century Schoolbook",
    L"Chiller",
    L"Colonna MT",
    L"Comic Sans MS",
    L"Consolas",
    L"Constantia",
    L"Cooper Black",
    L"Copperplate Gothic",
    L"Corbel",
    L"Courier New",
    L"Curlz MT",
    L"Dubai",
    L"Ebrima",
    L"Edwardian Script ITC",
    L"Elephant",
    L"Engravers MT",
    L"Eras ITC",
    L"Felix Titling",
    L"Footlight MT",
    L"Forte",
    L"Franklin Gothic",
    L"Freestyle Script",
    L"French Script MT",
    L"Garamond",
    L"Georgia",
    L"Gill Sans",
    L"Goudy Old Style",
    L"Haettenschweiler",
    L"Harrington",
    L"High Tower Text",
    L"Impact",
    L"Imprint MT Shadow",
    L"Ink Free",
    L"Javanese Text",
    L"Jokerman",
    L"Juice ITC",
    L"Kristen ITC",
    L"Leelawadee UI",
    L"Lucida Bright",
    L"Lucida Calligraphy",
    L"Lucida Console",
    L"Lucida Fax",
    L"Lucida Handwriting",
    L"Lucida Sans",
    L"Magneto",
    L"Maiandra GD",
    L"Malgun Gothic",
    L"Matura MT Script Capitals",
    L"Microsoft Himalaya",
    L"Microsoft JhengHei",
    L"Microsoft New Tai Lue",
    L"Microsoft PhagsPa",
    L"Microsoft Sans Serif",
    L"Microsoft Tai Le",
    L"Microsoft YaHei",
    L"Microsoft Yi Baiti",
    L"Mistral",
    L"Modern No. 20",
    L"Mongolian Baiti",
    L"Monotype Corsiva",
    L"MS Gothic",
    L"MS Mincho",
    L"MV Boli",
    L"Niagara",
    L"Nirmala UI",
    L"OCR A Extended",
    L"Old English Text MT",
    L"Onyx",
    L"Palatino Linotype",
    L"Papyrus",
    L"Parchment",
    L"Perpetua",
    L"Playbill",
    L"Poor Richard",
    L"Pristina",
    L"Rage Italic",
    L"Ravie",
    L"Rockwell",
    L"Sabon Next",
    L"Script MT",
    L"Segoe Print",
    L"Segoe Script",
    L"Segoe UI",
    L"Showcard Gothic",
    L"SimSun",
    L"Sitka",
    L"Snap ITC",
    L"Stencil",
    L"Sylfaen",
    L"Tahoma",
    L"Tempus Sans ITC",
    L"Times New Roman",
    L"Trebuchet MS",
    L"Tw Cen MT",
    L"Verdana",
    L"Viner Hand ITC",
    L"Vivaldi",
    L"Vladimir Script",
    L"Wide Latin",
    L"Yu Gothic"
};

typedef struct FontEnumeration {
    HWND combo;
} FontEnumeration;

static LRESULT combo_find_exact(HWND combo, const WCHAR *text)
{
    return SendMessageW(combo, CB_FINDSTRINGEXACT, (WPARAM)-1,
                        (LPARAM)text);
}

static void combo_mark_installed(HWND combo, const WCHAR *name)
{
    LRESULT item = combo_find_exact(combo, name);
    if (item == CB_ERR) {
        item = SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)name);
    }
    if (item != CB_ERR && item != CB_ERRSPACE) {
        SendMessageW(combo, CB_SETITEMDATA, (WPARAM)item,
                     FONT_ITEM_INSTALLED);
    }
}

static int CALLBACK enumerate_installed_fonts(
    const LOGFONTW *logFont, const TEXTMETRICW *metrics,
    DWORD fontType, LPARAM data)
{
    FontEnumeration *enumeration = (FontEnumeration *)data;
    (void)metrics;
    (void)fontType;

    if (enumeration == NULL || enumeration->combo == NULL ||
        logFont->lfFaceName[0] == L'@') {
        return 1;
    }
    combo_mark_installed(enumeration->combo, logFont->lfFaceName);
    return 1;
}

static BOOL make_unavailable_label(const WCHAR *name, WCHAR *label,
                                   size_t labelCount)
{
    return SUCCEEDED(StringCchPrintfW(label, labelCount, L"%s%s", name,
                                      FONT_UNAVAILABLE_SUFFIX));
}

size_t fonts_catalog_count(void)
{
    return ARRAYSIZE(supportedFonts);
}

const WCHAR *fonts_catalog_name(size_t index)
{
    if (index >= ARRAYSIZE(supportedFonts)) {
        return NULL;
    }
    return supportedFonts[index];
}

BOOL fonts_combo_has_family(HWND combo, const WCHAR *name)
{
    WCHAR label[LF_FACESIZE + ARRAYSIZE(FONT_UNAVAILABLE_SUFFIX)];

    if (combo == NULL || name == NULL || name[0] == L'\0') {
        return FALSE;
    }
    if (combo_find_exact(combo, name) != CB_ERR) {
        return TRUE;
    }
    return make_unavailable_label(name, label, ARRAYSIZE(label)) &&
           combo_find_exact(combo, label) != CB_ERR;
}

void fonts_populate_combo(HWND combo, HWND referenceWindow)
{
    FontEnumeration enumeration;
    LOGFONTW query;
    WCHAR label[LF_FACESIZE + ARRAYSIZE(FONT_UNAVAILABLE_SUFFIX)];
    HDC dc;
    size_t index;

    if (combo == NULL) {
        return;
    }

    SendMessageW(combo, WM_SETREDRAW, FALSE, 0);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);

    enumeration.combo = combo;
    ZeroMemory(&query, sizeof(query));
    query.lfCharSet = DEFAULT_CHARSET;
    dc = GetDC(referenceWindow);
    if (dc != NULL) {
        EnumFontFamiliesExW(dc, &query, enumerate_installed_fonts,
                            (LPARAM)&enumeration, 0);
        ReleaseDC(referenceWindow, dc);
    }

    for (index = 0; index < ARRAYSIZE(supportedFonts); ++index) {
        LRESULT item;
        if (combo_find_exact(combo, supportedFonts[index]) != CB_ERR ||
            !make_unavailable_label(supportedFonts[index], label,
                                    ARRAYSIZE(label))) {
            continue;
        }
        item = SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)label);
        if (item != CB_ERR && item != CB_ERRSPACE) {
            SendMessageW(combo, CB_SETITEMDATA, (WPARAM)item,
                         FONT_ITEM_UNAVAILABLE);
        }
    }

    SendMessageW(combo, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(combo, NULL, TRUE);
}

BOOL fonts_combo_selection_is_installed(HWND combo)
{
    LRESULT item;
    LRESULT availability;

    if (combo == NULL) {
        return FALSE;
    }
    item = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (item == CB_ERR) {
        return FALSE;
    }
    availability = SendMessageW(combo, CB_GETITEMDATA, (WPARAM)item, 0);
    return availability == FONT_ITEM_INSTALLED;
}
