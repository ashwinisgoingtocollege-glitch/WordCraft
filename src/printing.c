#include "editor.h"

static void retain_printer_settings(AppState *app, HGLOBAL devMode, HGLOBAL devNames)
{
    if (devMode != NULL) {
        app->printerDevMode = devMode;
    }
    if (devNames != NULL) {
        app->printerDevNames = devNames;
    }
}

static void release_print_dc(PRINTDLGW *dialog)
{
    if (dialog->hDC != NULL) {
        DeleteDC(dialog->hDC);
        dialog->hDC = NULL;
    }
}

void printing_page_setup(AppState *app)
{
    PAGESETUPDLGW dialog;
    ZeroMemory(&dialog, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = app->mainWindow;
    dialog.hDevMode = app->printerDevMode;
    dialog.hDevNames = app->printerDevNames;
    dialog.Flags = PSD_MARGINS | PSD_MINMARGINS | PSD_INTHOUSANDTHSOFINCHES;
    dialog.rtMargin = app->pageMargins;
    dialog.rtMinMargin.left = 250;
    dialog.rtMinMargin.top = 250;
    dialog.rtMinMargin.right = 250;
    dialog.rtMinMargin.bottom = 250;

    if (PageSetupDlgW(&dialog)) {
        app->pageMargins = dialog.rtMargin;
        if (dialog.ptPaperSize.x > 0 && dialog.ptPaperSize.y > 0) {
            app->pageSize = dialog.ptPaperSize;
        }
        pageview_mark_dirty(app);
        pageview_layout(app);
        app_update_status(app, TRUE);
    } else {
        DWORD commonError = CommDlgExtendedError();
        if (commonError != 0) {
            app_show_error(app->mainWindow, L"The Page Setup dialog failed.", commonError);
        }
    }
    retain_printer_settings(app, dialog.hDevMode, dialog.hDevNames);
}

static LONG thousandths_to_twips(LONG value)
{
    return MulDiv(value, 1440, 1000);
}

void printing_print_document(AppState *app)
{
    PRINTDLGW dialog;
    DOCINFOW documentInfo;
    FORMATRANGE formatRange;
    LONG textLength;
    SIZE_T preciseTextLength = 0;
    LONG nextCharacter = 0;
    LONG previousCharacter;
    int dpiX;
    int dpiY;
    LONG physicalWidth;
    LONG physicalHeight;
    LONG offsetX;
    LONG offsetY;
    LONG printableWidth;
    LONG printableHeight;
    BOOL startedDocument = FALSE;
    BOOL printSucceeded = TRUE;
    BOOL firstPage = TRUE;
    DWORD error = ERROR_SUCCESS;

    ZeroMemory(&dialog, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = app->mainWindow;
    dialog.hDevMode = app->printerDevMode;
    dialog.hDevNames = app->printerDevNames;
    dialog.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION |
                   PD_USEDEVMODECOPIESANDCOLLATE | PD_DISABLEPRINTTOFILE |
                   PD_HIDEPRINTTOFILE;
    dialog.nCopies = 1;
    if (!PrintDlgW(&dialog)) {
        DWORD commonError = CommDlgExtendedError();
        retain_printer_settings(app, dialog.hDevMode, dialog.hDevNames);
        if (commonError != 0) {
            app_show_error(app->mainWindow, L"The Print dialog failed.", commonError);
        }
        release_print_dc(&dialog);
        return;
    }
    retain_printer_settings(app, dialog.hDevMode, dialog.hDevNames);
    if (dialog.hDC == NULL) {
        app_show_error(app->mainWindow, L"The selected printer did not provide a device context.",
                       ERROR_INVALID_HANDLE);
        release_print_dc(&dialog);
        return;
    }

    dpiX = GetDeviceCaps(dialog.hDC, LOGPIXELSX);
    dpiY = GetDeviceCaps(dialog.hDC, LOGPIXELSY);
    if (dpiX <= 0 || dpiY <= 0) {
        app_show_error(app->mainWindow, L"The selected printer reported invalid page metrics.",
                       ERROR_INVALID_DATA);
        release_print_dc(&dialog);
        return;
    }
    physicalWidth = MulDiv(GetDeviceCaps(dialog.hDC, PHYSICALWIDTH), 1440, dpiX);
    physicalHeight = MulDiv(GetDeviceCaps(dialog.hDC, PHYSICALHEIGHT), 1440, dpiY);
    offsetX = MulDiv(GetDeviceCaps(dialog.hDC, PHYSICALOFFSETX), 1440, dpiX);
    offsetY = MulDiv(GetDeviceCaps(dialog.hDC, PHYSICALOFFSETY), 1440, dpiY);
    printableWidth = MulDiv(GetDeviceCaps(dialog.hDC, HORZRES), 1440, dpiX);
    printableHeight = MulDiv(GetDeviceCaps(dialog.hDC, VERTRES), 1440, dpiY);

    ZeroMemory(&formatRange, sizeof(formatRange));
    formatRange.hdc = dialog.hDC;
    formatRange.hdcTarget = dialog.hDC;
    formatRange.rcPage.left = -offsetX;
    formatRange.rcPage.top = -offsetY;
    formatRange.rcPage.right = physicalWidth - offsetX;
    formatRange.rcPage.bottom = physicalHeight - offsetY;
    formatRange.rc.left = thousandths_to_twips(app->pageMargins.left) - offsetX;
    formatRange.rc.top = thousandths_to_twips(app->pageMargins.top) - offsetY;
    formatRange.rc.right = physicalWidth -
                           thousandths_to_twips(app->pageMargins.right) - offsetX;
    formatRange.rc.bottom = physicalHeight -
                            thousandths_to_twips(app->pageMargins.bottom) - offsetY;
    if (formatRange.rc.left < 0) {
        formatRange.rc.left = 0;
    }
    if (formatRange.rc.top < 0) {
        formatRange.rc.top = 0;
    }
    if (formatRange.rc.right > printableWidth) {
        formatRange.rc.right = printableWidth;
    }
    if (formatRange.rc.bottom > printableHeight) {
        formatRange.rc.bottom = printableHeight;
    }
    if (formatRange.rc.right <= formatRange.rc.left + 144 ||
        formatRange.rc.bottom <= formatRange.rc.top + 144) {
        app_show_error(app->mainWindow,
                       L"The page margins leave no printable area. Choose smaller margins in Page Setup.",
                       ERROR_INVALID_PARAMETER);
        release_print_dc(&dialog);
        return;
    }

    ZeroMemory(&documentInfo, sizeof(documentInfo));
    documentInfo.cbSize = sizeof(documentInfo);
    documentInfo.lpszDocName = app->currentPath[0] != L'\0'
                                  ? app->currentPath
                                  : L"Untitled - WordCraft";
    if (StartDocW(dialog.hDC, &documentInfo) <= 0) {
        error = GetLastError();
        if (error == ERROR_SUCCESS) {
            error = ERROR_PRINT_CANCELLED;
        }
        printSucceeded = FALSE;
    } else {
        startedDocument = TRUE;
    }

    if (!editor_get_text_length(app->editor, FALSE, &preciseTextLength, &error) ||
        preciseTextLength > 0x7FFFFFFEULL) {
        if (error == ERROR_SUCCESS) {
            error = ERROR_FILE_TOO_LARGE;
        }
        printSucceeded = FALSE;
        textLength = 0;
    } else {
        textLength = (LONG)preciseTextLength;
    }
    while (printSucceeded && (firstPage || nextCharacter < textLength)) {
        firstPage = FALSE;
        if (StartPage(dialog.hDC) <= 0) {
            error = GetLastError();
            printSucceeded = FALSE;
            break;
        }
        previousCharacter = nextCharacter;
        formatRange.chrg.cpMin = nextCharacter;
        formatRange.chrg.cpMax = -1;
        nextCharacter = (LONG)SendMessageW(app->editor, EM_FORMATRANGE, TRUE,
                                           (LPARAM)&formatRange);
        if (EndPage(dialog.hDC) <= 0) {
            error = GetLastError();
            printSucceeded = FALSE;
            break;
        }
        if (textLength > 0 && nextCharacter <= previousCharacter) {
            error = ERROR_INVALID_DATA;
            printSucceeded = FALSE;
            break;
        }
    }

    SendMessageW(app->editor, EM_FORMATRANGE, FALSE, 0);
    if (startedDocument) {
        if (printSucceeded) {
            if (EndDoc(dialog.hDC) <= 0) {
                error = GetLastError();
                printSucceeded = FALSE;
            }
        } else {
            AbortDoc(dialog.hDC);
        }
    }
    if (!printSucceeded) {
        if (error == ERROR_SUCCESS) {
            error = ERROR_PRINT_CANCELLED;
        }
        app_show_error(app->mainWindow, L"The document could not be printed.", error);
    } else {
        app_set_status_message(app, L"Document sent to printer");
    }
    release_print_dc(&dialog);
}
