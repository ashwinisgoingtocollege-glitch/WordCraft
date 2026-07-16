#ifndef WORDCRAFT_FONTS_H
#define WORDCRAFT_FONTS_H

#include <stddef.h>
#include <windows.h>

size_t fonts_catalog_count(void);
const WCHAR *fonts_catalog_name(size_t index);
void fonts_populate_combo(HWND combo, HWND referenceWindow);
BOOL fonts_combo_selection_is_installed(HWND combo);
BOOL fonts_combo_has_family(HWND combo, const WCHAR *name);

#endif
