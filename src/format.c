#include "editor.h"

static void set_button_state(HWND button, BOOL checked)
{
    if (button != NULL) {
        SendMessageW(button, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

static void set_menu_check(HMENU menu, UINT command, BOOL checked)
{
    CheckMenuItem(menu, command,
                  MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
}

static void record_character_format_change(AppState *app)
{
    CHARRANGE selection;
    app->richFormattingUsed = TRUE;
    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&selection);
    if (selection.cpMin != selection.cpMax) {
        document_mark_modified(app, TRUE);
    }
    text_engine_note_layout_change(app);
    pageview_mark_dirty(app);
}

void format_initialize_document(AppState *app)
{
    CHARFORMAT2W character;
    PARAFORMAT2 paragraph;

    ZeroMemory(&character, sizeof(character));
    character.cbSize = sizeof(character);
    character.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_CHARSET;
    character.yHeight = WORDCRAFT_DEFAULT_FONT_SIZE_TWIPS;
    character.crTextColor = RGB(0, 0, 0);
    character.bCharSet = DEFAULT_CHARSET;
    StringCchCopyW(character.szFaceName, ARRAYSIZE(character.szFaceName),
                   WORDCRAFT_DEFAULT_FONT_FACE);
    SendMessageW(app->editor, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&character);
    SendMessageW(app->editor, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&character);

    ZeroMemory(&paragraph, sizeof(paragraph));
    paragraph.cbSize = sizeof(paragraph);
    paragraph.dwMask = PFM_ALIGNMENT | PFM_NUMBERING | PFM_STARTINDENT |
                       PFM_OFFSET;
    paragraph.wAlignment = PFA_LEFT;
    paragraph.wNumbering = 0;
    paragraph.dxStartIndent = 0;
    paragraph.dxOffset = 0;
    SendMessageW(app->editor, EM_SETPARAFORMAT, 0, (LPARAM)&paragraph);
    text_engine_apply_document_defaults(app);
    pageview_mark_dirty(app);
}

void format_toggle_character_effect(AppState *app, DWORD mask, DWORD effect)
{
    CHARFORMAT2W current;
    CHARFORMAT2W change;
    BOOL currentlyEnabled;

    ZeroMemory(&current, sizeof(current));
    current.cbSize = sizeof(current);
    SendMessageW(app->editor, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&current);
    currentlyEnabled = (current.dwMask & mask) != 0 &&
                       (current.dwEffects & effect) != 0;

    ZeroMemory(&change, sizeof(change));
    change.cbSize = sizeof(change);
    change.dwMask = mask;
    change.dwEffects = currentlyEnabled ? 0 : effect;
    if (SendMessageW(app->editor, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&change)) {
        record_character_format_change(app);
    }
    format_sync_controls(app);
    SetFocus(app->editor);
}

void format_set_font_name(AppState *app, const WCHAR *name)
{
    CHARFORMAT2W format;
    if (name == NULL || name[0] == L'\0') {
        return;
    }
    ZeroMemory(&format, sizeof(format));
    format.cbSize = sizeof(format);
    format.dwMask = CFM_FACE;
    if (FAILED(StringCchCopyW(format.szFaceName, ARRAYSIZE(format.szFaceName), name))) {
        return;
    }
    if (SendMessageW(app->editor, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&format)) {
        record_character_format_change(app);
    }
    format_sync_controls(app);
}

void format_set_font_size(AppState *app, double points)
{
    CHARFORMAT2W format;
    if (points < 1.0 || points > 500.0) {
        return;
    }
    ZeroMemory(&format, sizeof(format));
    format.cbSize = sizeof(format);
    format.dwMask = CFM_SIZE;
    format.yHeight = (LONG)(points * 20.0 + 0.5);
    if (SendMessageW(app->editor, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&format)) {
        record_character_format_change(app);
    }
    format_sync_controls(app);
}

void format_choose_font(AppState *app)
{
    CHARFORMAT2W current;
    CHARFORMAT2W format;
    LOGFONTW logFont;
    CHOOSEFONTW dialog;
    HDC dc;
    int dpi = 96;

    ZeroMemory(&current, sizeof(current));
    current.cbSize = sizeof(current);
    SendMessageW(app->editor, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&current);

    dc = GetDC(app->mainWindow);
    if (dc != NULL) {
        dpi = GetDeviceCaps(dc, LOGPIXELSY);
        ReleaseDC(app->mainWindow, dc);
    }
    ZeroMemory(&logFont, sizeof(logFont));
    logFont.lfHeight = -MulDiv(
        current.yHeight > 0 ? current.yHeight :
                              WORDCRAFT_DEFAULT_FONT_SIZE_TWIPS,
        dpi, 1440);
    logFont.lfWeight = (current.dwEffects & CFE_BOLD) != 0 ? FW_BOLD : FW_NORMAL;
    logFont.lfItalic = (BYTE)((current.dwEffects & CFE_ITALIC) != 0);
    logFont.lfUnderline = (BYTE)((current.dwEffects & CFE_UNDERLINE) != 0);
    logFont.lfStrikeOut = (BYTE)((current.dwEffects & CFE_STRIKEOUT) != 0);
    logFont.lfCharSet = current.bCharSet;
    if ((current.dwMask & CFM_FACE) != 0) {
        StringCchCopyW(logFont.lfFaceName, ARRAYSIZE(logFont.lfFaceName),
                       current.szFaceName);
    } else {
        StringCchCopyW(logFont.lfFaceName, ARRAYSIZE(logFont.lfFaceName),
                       WORDCRAFT_DEFAULT_FONT_FACE);
    }

    ZeroMemory(&dialog, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = app->mainWindow;
    dialog.lpLogFont = &logFont;
    dialog.iPointSize =
        (current.yHeight > 0 ? current.yHeight :
                               WORDCRAFT_DEFAULT_FONT_SIZE_TWIPS) / 2;
    dialog.rgbColors = (current.dwEffects & CFE_AUTOCOLOR) != 0
                           ? RGB(0, 0, 0)
                           : current.crTextColor;
    dialog.Flags = CF_SCREENFONTS | CF_EFFECTS | CF_INITTOLOGFONTSTRUCT |
                   CF_FORCEFONTEXIST;
    if (!ChooseFontW(&dialog)) {
        DWORD commonError = CommDlgExtendedError();
        if (commonError != 0) {
            app_show_error(app->mainWindow, L"The Font dialog failed.", commonError);
        }
        return;
    }

    ZeroMemory(&format, sizeof(format));
    format.cbSize = sizeof(format);
    format.dwMask = CFM_FACE | CFM_SIZE | CFM_BOLD | CFM_ITALIC |
                    CFM_UNDERLINE | CFM_STRIKEOUT | CFM_COLOR | CFM_CHARSET;
    format.yHeight = dialog.iPointSize * 2;
    format.crTextColor = dialog.rgbColors;
    format.bCharSet = logFont.lfCharSet;
    StringCchCopyW(format.szFaceName, ARRAYSIZE(format.szFaceName),
                   logFont.lfFaceName);
    if (logFont.lfWeight >= FW_BOLD) {
        format.dwEffects |= CFE_BOLD;
    }
    if (logFont.lfItalic) {
        format.dwEffects |= CFE_ITALIC;
    }
    if (logFont.lfUnderline) {
        format.dwEffects |= CFE_UNDERLINE;
    }
    if (logFont.lfStrikeOut) {
        format.dwEffects |= CFE_STRIKEOUT;
    }
    if (SendMessageW(app->editor, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&format)) {
        record_character_format_change(app);
    }
    format_sync_controls(app);
    SetFocus(app->editor);
}

void format_choose_color(AppState *app)
{
    CHARFORMAT2W current;
    CHARFORMAT2W change;
    CHOOSECOLORW dialog;

    ZeroMemory(&current, sizeof(current));
    current.cbSize = sizeof(current);
    SendMessageW(app->editor, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&current);
    ZeroMemory(&dialog, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = app->mainWindow;
    dialog.rgbResult = (current.dwEffects & CFE_AUTOCOLOR) != 0
                           ? RGB(0, 0, 0)
                           : current.crTextColor;
    dialog.lpCustColors = app->customColors;
    dialog.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&dialog)) {
        DWORD commonError = CommDlgExtendedError();
        if (commonError != 0) {
            app_show_error(app->mainWindow, L"The Color dialog failed.", commonError);
        }
        return;
    }
    ZeroMemory(&change, sizeof(change));
    change.cbSize = sizeof(change);
    change.dwMask = CFM_COLOR;
    change.crTextColor = dialog.rgbResult;
    if (SendMessageW(app->editor, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&change)) {
        record_character_format_change(app);
    }
    SetFocus(app->editor);
}

void format_set_alignment(AppState *app, WORD alignment)
{
    PARAFORMAT2 paragraph;
    ZeroMemory(&paragraph, sizeof(paragraph));
    paragraph.cbSize = sizeof(paragraph);
    paragraph.dwMask = PFM_ALIGNMENT;
    paragraph.wAlignment = alignment;
    if (SendMessageW(app->editor, EM_SETPARAFORMAT, 0, (LPARAM)&paragraph)) {
        app->richFormattingUsed = TRUE;
        document_mark_modified(app, TRUE);
        text_engine_note_layout_change(app);
        pageview_mark_dirty(app);
    }
    format_sync_controls(app);
    SetFocus(app->editor);
}

void format_toggle_bullets(AppState *app)
{
    PARAFORMAT2 current;
    PARAFORMAT2 change;
    BOOL enabled;

    ZeroMemory(&current, sizeof(current));
    current.cbSize = sizeof(current);
    SendMessageW(app->editor, EM_GETPARAFORMAT, 0, (LPARAM)&current);
    enabled = (current.dwMask & PFM_NUMBERING) != 0 &&
              current.wNumbering == PFN_BULLET;

    ZeroMemory(&change, sizeof(change));
    change.cbSize = sizeof(change);
    change.dwMask = PFM_NUMBERING | PFM_OFFSET;
    change.wNumbering = enabled ? 0 : PFN_BULLET;
    change.dxOffset = enabled ? 0 : 360;
    if (SendMessageW(app->editor, EM_SETPARAFORMAT, 0, (LPARAM)&change)) {
        app->richFormattingUsed = TRUE;
        document_mark_modified(app, TRUE);
        text_engine_note_layout_change(app);
        pageview_mark_dirty(app);
    }
    format_sync_controls(app);
    SetFocus(app->editor);
}

void format_change_indent(AppState *app, LONG deltaTwips)
{
    PARAFORMAT2 change;

    ZeroMemory(&change, sizeof(change));
    change.cbSize = sizeof(change);
    change.dwMask = PFM_OFFSETINDENT;
    change.dxStartIndent = deltaTwips;
    if (SendMessageW(app->editor, EM_SETPARAFORMAT, 0, (LPARAM)&change)) {
        app->richFormattingUsed = TRUE;
        document_mark_modified(app, TRUE);
        text_engine_note_layout_change(app);
        pageview_mark_dirty(app);
    }
    SetFocus(app->editor);
}

void format_sync_controls(AppState *app)
{
    CHARFORMAT2W character;
    PARAFORMAT2 paragraph;
    HMENU menu;
    BOOL bold;
    BOOL italic;
    BOOL underline;
    BOOL strike;
    BOOL bullets;
    BOOL left;
    BOOL center;
    BOOL right;
    BOOL justify;
    WCHAR sizeText[32];

    if (app->editor == NULL || app->fontCombo == NULL) {
        return;
    }
    ZeroMemory(&character, sizeof(character));
    character.cbSize = sizeof(character);
    SendMessageW(app->editor, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&character);

    bold = (character.dwMask & CFM_BOLD) != 0 &&
           (character.dwEffects & CFE_BOLD) != 0;
    italic = (character.dwMask & CFM_ITALIC) != 0 &&
             (character.dwEffects & CFE_ITALIC) != 0;
    underline = (character.dwMask & CFM_UNDERLINE) != 0 &&
                (character.dwEffects & CFE_UNDERLINE) != 0;
    strike = (character.dwMask & CFM_STRIKEOUT) != 0 &&
             (character.dwEffects & CFE_STRIKEOUT) != 0;
    set_button_state(app->boldButton, bold);
    set_button_state(app->italicButton, italic);
    set_button_state(app->underlineButton, underline);
    set_button_state(app->strikeButton, strike);

    if ((character.dwMask & CFM_FACE) != 0) {
        LRESULT item = SendMessageW(app->fontCombo, CB_FINDSTRINGEXACT,
                                    (WPARAM)-1, (LPARAM)character.szFaceName);
        SendMessageW(app->fontCombo, CB_SETCURSEL, item, 0);
    } else {
        SendMessageW(app->fontCombo, CB_SETCURSEL, (WPARAM)-1, 0);
    }
    if ((character.dwMask & CFM_SIZE) != 0 && character.yHeight > 0) {
        LONG whole = character.yHeight / 20;
        LONG tenths = ((character.yHeight % 20) * 10) / 20;
        if (tenths == 0) {
            StringCchPrintfW(sizeText, ARRAYSIZE(sizeText), L"%ld", whole);
        } else {
            StringCchPrintfW(sizeText, ARRAYSIZE(sizeText), L"%ld.%ld", whole, tenths);
        }
        SetWindowTextW(app->sizeCombo, sizeText);
    } else {
        SetWindowTextW(app->sizeCombo, L"");
    }

    ZeroMemory(&paragraph, sizeof(paragraph));
    paragraph.cbSize = sizeof(paragraph);
    SendMessageW(app->editor, EM_GETPARAFORMAT, 0, (LPARAM)&paragraph);
    left = (paragraph.dwMask & PFM_ALIGNMENT) != 0 && paragraph.wAlignment == PFA_LEFT;
    center = (paragraph.dwMask & PFM_ALIGNMENT) != 0 && paragraph.wAlignment == PFA_CENTER;
    right = (paragraph.dwMask & PFM_ALIGNMENT) != 0 && paragraph.wAlignment == PFA_RIGHT;
    justify = (paragraph.dwMask & PFM_ALIGNMENT) != 0 && paragraph.wAlignment == PFA_JUSTIFY;
    bullets = (paragraph.dwMask & PFM_NUMBERING) != 0 &&
              paragraph.wNumbering == PFN_BULLET;
    set_button_state(app->alignLeftButton, left);
    set_button_state(app->alignCenterButton, center);
    set_button_state(app->alignRightButton, right);
    set_button_state(app->alignJustifyButton, justify);
    set_button_state(app->bulletsButton, bullets);

    menu = GetMenu(app->mainWindow);
    if (menu != NULL) {
        set_menu_check(menu, IDM_FORMAT_BOLD, bold);
        set_menu_check(menu, IDM_FORMAT_ITALIC, italic);
        set_menu_check(menu, IDM_FORMAT_UNDERLINE, underline);
        set_menu_check(menu, IDM_FORMAT_STRIKE, strike);
        set_menu_check(menu, IDM_FORMAT_ALIGN_LEFT, left);
        set_menu_check(menu, IDM_FORMAT_ALIGN_CENTER, center);
        set_menu_check(menu, IDM_FORMAT_ALIGN_RIGHT, right);
        set_menu_check(menu, IDM_FORMAT_ALIGN_JUSTIFY, justify);
        set_menu_check(menu, IDM_FORMAT_BULLETS, bullets);
    }
}

void format_set_zoom(AppState *app, int percent)
{
    if (percent < 10 || percent > 500 || app->editor == NULL) {
        return;
    }
    if (SendMessageW(app->editor, EM_SETZOOM, (WPARAM)percent, 100)) {
        app->zoomPercent = percent;
        text_engine_note_layout_change(app);
        pageview_layout(app);
        pageview_sync_to_caret(app, TRUE);
        app_update_status(app, FALSE);
        app_update_command_ui(app);
    }
}

void format_set_word_wrap(AppState *app, BOOL enabled)
{
    LONG_PTR style;
    if (app->editor == NULL) {
        app->wordWrap = TRUE;
        return;
    }
    style = GetWindowLongPtrW(app->editor, GWL_STYLE);
    style &= ~(LONG_PTR)(WS_HSCROLL | ES_AUTOHSCROLL);
    SetWindowLongPtrW(app->editor, GWL_STYLE, style);
    ShowScrollBar(app->editor, SB_HORZ, FALSE);
    SetWindowPos(app->editor, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                     SWP_FRAMECHANGED);
    app->wordWrap = TRUE;
    text_engine_note_layout_change(app);
    pageview_layout(app);
    pageview_mark_dirty(app);
    if (!enabled) {
        app_set_status_message(app, L"Paged view always wraps text to the page width");
    } else {
        app_update_status(app, FALSE);
    }
    app_update_command_ui(app);
}
