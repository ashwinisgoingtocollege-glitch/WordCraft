#include "fonts.h"

#include <stdio.h>
#include <strsafe.h>
#include <wchar.h>

#define FONT_ITEM_UNAVAILABLE 0
#define FONT_ITEM_INSTALLED 1
#define FONT_UNAVAILABLE_SUFFIX L"  (not installed)"

static const WCHAR *const expectedFonts[] = {
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

static size_t catalog_occurrences(const WCHAR *name)
{
    size_t count = 0;
    size_t index;

    for (index = 0; index < fonts_catalog_count(); ++index) {
        const WCHAR *candidate = fonts_catalog_name(index);
        if (candidate != NULL && wcscmp(candidate, name) == 0) {
            ++count;
        }
    }
    return count;
}

static BOOL verify_catalog(void)
{
    size_t index;

    if (fonts_catalog_count() != ARRAYSIZE(expectedFonts)) {
        fwprintf(stderr, L"catalog count mismatch: expected %zu, got %zu\n",
                 ARRAYSIZE(expectedFonts), fonts_catalog_count());
        return FALSE;
    }
    for (index = 0; index < ARRAYSIZE(expectedFonts); ++index) {
        const WCHAR *actual = fonts_catalog_name(index);
        if (actual == NULL || wcscmp(actual, expectedFonts[index]) != 0) {
            fwprintf(stderr, L"catalog mismatch at %zu: expected '%ls', got '%ls'\n",
                     index, expectedFonts[index],
                     actual != NULL ? actual : L"(null)");
            return FALSE;
        }
        if (catalog_occurrences(expectedFonts[index]) != 1) {
            fwprintf(stderr, L"catalog entry '%ls' did not occur exactly once\n",
                     expectedFonts[index]);
            return FALSE;
        }
    }
    if (fonts_catalog_name(fonts_catalog_count()) != NULL) {
        fwprintf(stderr, L"out-of-range catalog lookup did not return NULL\n");
        return FALSE;
    }
    return TRUE;
}

static BOOL verify_combo(HWND combo, size_t *installedCount,
                         size_t *unavailableCount)
{
    LRESULT comboCount = SendMessageW(combo, CB_GETCOUNT, 0, 0);
    size_t expectedIndex;

    if (comboCount == CB_ERR) {
        fwprintf(stderr, L"could not read the populated font combo\n");
        return FALSE;
    }
    *installedCount = 0;
    *unavailableCount = 0;

    for (expectedIndex = 0; expectedIndex < ARRAYSIZE(expectedFonts);
         ++expectedIndex) {
        WCHAR unavailableLabel[128];
        size_t representationCount = 0;
        BOOL foundInstalled = FALSE;
        LRESULT item;

        if (FAILED(StringCchPrintfW(unavailableLabel,
                                    ARRAYSIZE(unavailableLabel), L"%ls%ls",
                                    expectedFonts[expectedIndex],
                                    FONT_UNAVAILABLE_SUFFIX))) {
            fwprintf(stderr, L"could not create the unavailable label for '%ls'\n",
                     expectedFonts[expectedIndex]);
            return FALSE;
        }

        for (item = 0; item < comboCount; ++item) {
            WCHAR label[128];
            LRESULT itemData;
            BOOL rawMatch;
            BOOL unavailableMatch;

            if (SendMessageW(combo, CB_GETLBTEXT, (WPARAM)item,
                             (LPARAM)label) == CB_ERR) {
                fwprintf(stderr, L"could not read font combo item %lld\n",
                         (long long)item);
                return FALSE;
            }
            rawMatch = lstrcmpiW(label, expectedFonts[expectedIndex]) == 0;
            unavailableMatch = lstrcmpiW(label, unavailableLabel) == 0;
            if (!rawMatch && !unavailableMatch) {
                continue;
            }

            ++representationCount;
            itemData = SendMessageW(combo, CB_GETITEMDATA, (WPARAM)item, 0);
            if ((rawMatch && itemData != FONT_ITEM_INSTALLED) ||
                (unavailableMatch && itemData != FONT_ITEM_UNAVAILABLE)) {
                fwprintf(stderr, L"font combo availability marker was wrong for '%ls'\n",
                         label);
                return FALSE;
            }
            foundInstalled = rawMatch;
        }

        if (representationCount != 1 ||
            !fonts_combo_has_family(combo, expectedFonts[expectedIndex])) {
            fwprintf(stderr,
                     L"font combo did not contain exactly one visible representation of '%ls'\n",
                     expectedFonts[expectedIndex]);
            return FALSE;
        }
        if (foundInstalled) {
            ++*installedCount;
        } else {
            ++*unavailableCount;
        }
    }
    return TRUE;
}

int wmain(void)
{
    HWND host;
    HWND combo;
    size_t installedCount;
    size_t unavailableCount;

    if (!verify_catalog()) {
        return 1;
    }

    host = CreateWindowExW(0, L"STATIC", L"font probe", WS_OVERLAPPED,
                           0, 0, 320, 240, NULL, NULL,
                           GetModuleHandleW(NULL), NULL);
    if (host == NULL) {
        fwprintf(stderr, L"could not create font probe host: %lu\n",
                 GetLastError());
        return 1;
    }
    combo = CreateWindowExW(
        0, L"COMBOBOX", NULL,
        WS_CHILD | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_SORT | CBS_HASSTRINGS,
        0, 0, 300, 300, host, NULL, GetModuleHandleW(NULL), NULL);
    if (combo == NULL) {
        fwprintf(stderr, L"could not create font probe combo: %lu\n",
                 GetLastError());
        DestroyWindow(host);
        return 1;
    }

    fonts_populate_combo(combo, host);
    if (!verify_combo(combo, &installedCount, &unavailableCount)) {
        DestroyWindow(host);
        return 1;
    }

    DestroyWindow(host);
    printf("font_catalog=%zu exact_once=ok combo_catalog=ok installed=%zu unavailable=%zu\n",
           ARRAYSIZE(expectedFonts), installedCount, unavailableCount);
    return 0;
}
