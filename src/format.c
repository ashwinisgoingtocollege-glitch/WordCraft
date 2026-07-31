#ifndef COBJMACROS
#define COBJMACROS
#endif
#include "editor.h"

#include <richole.h>
#include <tom.h>

static const IID wordcraftFormatIidTextDocument = {
    0x8CC497C0, 0xA1DF, 0x11CE,
    {0x80, 0x98, 0x00, 0xAA, 0x00, 0x47, 0xBE, 0x5D}
};

/*
 * The first record intentionally reproduces WordCraft's pre-Design style
 * definitions.  The remaining records use fonts that ship with common
 * Windows installations and vary the preview traits exposed by the ribbon.
 */
static const WordcraftDesignStyleSetInfo wordcraftDesignStyleSets[] = {
    {L"Office", L"Times New Roman", L"Times New Roman",
     RGB(37, 76, 132), 520, 320, 280, TRUE, FALSE, FALSE, FALSE, FALSE},
    {L"Basic (Elegant)", L"Garamond", L"Book Antiqua",
     RGB(112, 86, 63), 540, 320, 280, FALSE, FALSE, FALSE, FALSE, FALSE},
    {L"Basic (Simple)", L"Arial", L"Arial",
     RGB(64, 94, 124), 500, 320, 280, FALSE, TRUE, FALSE, FALSE, FALSE},
    {L"Basic (Stylish)", L"Trebuchet MS", L"Calibri",
     RGB(93, 76, 138), 540, 340, 280, FALSE, FALSE, TRUE, FALSE, FALSE},
    {L"Centered", L"Cambria", L"Calibri",
     RGB(47, 94, 124), 560, 340, 280, TRUE, FALSE, FALSE, FALSE, FALSE},
    {L"Casual", L"Comic Sans MS", L"Calibri",
     RGB(68, 130, 111), 520, 320, 280, FALSE, FALSE, FALSE, FALSE, FALSE},
    {L"Compact", L"Arial", L"Arial",
     RGB(70, 88, 106), 480, 300, 260, FALSE, TRUE, FALSE, FALSE, FALSE},
    {L"Lines (Distinctive)", L"Century Gothic", L"Calibri",
     RGB(39, 104, 145), 540, 340, 280, FALSE, FALSE, TRUE, TRUE, FALSE},
    {L"Lines (Elegant)", L"Garamond", L"Georgia",
     RGB(132, 91, 66), 540, 320, 280, FALSE, FALSE, FALSE, TRUE, FALSE},
    {L"Lines (Simple)", L"Arial", L"Calibri",
     RGB(70, 112, 146), 500, 320, 270, FALSE, TRUE, FALSE, TRUE, FALSE},
    {L"Modern", L"Aptos Display", L"Aptos",
     RGB(0, 112, 173), 560, 340, 280, FALSE, FALSE, TRUE, FALSE, FALSE},
    {L"Shaded", L"Cambria", L"Calibri",
     RGB(79, 102, 126), 520, 320, 280, FALSE, FALSE, FALSE, FALSE, TRUE},
    {L"Classic", L"Times New Roman", L"Times New Roman",
     RGB(95, 76, 61), 500, 320, 280, TRUE, FALSE, FALSE, FALSE, FALSE},
    {L"Distinctive", L"Century Gothic", L"Verdana",
     RGB(34, 112, 147), 560, 360, 300, FALSE, FALSE, TRUE, FALSE, FALSE},
    {L"Elegant", L"Palatino Linotype", L"Book Antiqua",
     RGB(116, 83, 78), 560, 320, 280, TRUE, FALSE, FALSE, FALSE, FALSE},
    {L"Formal", L"Cambria", L"Times New Roman",
     RGB(54, 76, 96), 520, 320, 280, TRUE, FALSE, TRUE, TRUE, FALSE},
    {L"Manuscript", L"Courier New", L"Courier New",
     RGB(92, 82, 70), 480, 300, 260, FALSE, TRUE, FALSE, FALSE, FALSE},
    {L"Traditional", L"Georgia", L"Georgia",
     RGB(104, 75, 55), 520, 320, 280, TRUE, FALSE, FALSE, TRUE, FALSE},
    {L"Word 2010", L"Cambria", L"Calibri",
     RGB(31, 73, 125), 520, 320, 280, FALSE, FALSE, FALSE, FALSE, FALSE}
};

_Static_assert(ARRAYSIZE(wordcraftDesignStyleSets) ==
                   DESIGN_STYLE_SET_COUNT,
               "Design style-set metadata must remain complete");

BOOL format_get_design_style_set_info(
    int styleSet, WordcraftDesignStyleSetInfo *info)
{
    if (info == NULL || styleSet < 0 ||
        styleSet >= DESIGN_STYLE_SET_COUNT) {
        return FALSE;
    }
    *info = wordcraftDesignStyleSets[styleSet];
    return TRUE;
}

void format_reset_document_design(AppState *app)
{
    if (app == NULL) {
        return;
    }
    app->designStyleSet = DESIGN_STYLE_SET_OFFICE;
    app->designColorScheme = DESIGN_COLOR_SCHEME_OFFICE;
    app->designFontScheme = DESIGN_FONT_SCHEME_OFFICE;
    /*
     * "Open" is WordCraft's Office paragraph-spacing baseline: 6 pt after
     * and 1.1-line spacing.  "None" is the explicit no-paragraph-space
     * choice, so it is not the reset value.
     */
    app->designParagraphSpacing = DESIGN_PARAGRAPH_SPACING_OPEN;
    app->designEffect = DESIGN_EFFECT_OFFICE;
}

static ITextDocument *get_format_document(HWND editor)
{
    IRichEditOle *richEditOle = NULL;
    ITextDocument *document = NULL;

    if (editor == NULL ||
        !SendMessageW(editor, EM_GETOLEINTERFACE, 0,
                      (LPARAM)&richEditOle) || richEditOle == NULL) {
        return NULL;
    }
    if (FAILED(richEditOle->lpVtbl->QueryInterface(
            richEditOle, &wordcraftFormatIidTextDocument,
            (void **)&document))) {
        document = NULL;
    }
    richEditOle->lpVtbl->Release(richEditOle);
    return document;
}

static BOOL begin_format_collection(ITextDocument *document)
{
    return document != NULL &&
           SUCCEEDED(ITextDocument_BeginEditCollection(document));
}

static void end_format_collection(ITextDocument *document)
{
    if (document != NULL) {
        ITextDocument_EndEditCollection(document);
    }
}

static BOOL expand_to_paragraphs(ITextDocument *document,
                                 const CHARRANGE *selection,
                                 CHARRANGE *paragraphs)
{
    ITextRange *range = NULL;
    long ignored = 0;
    long start = 0;
    long end = 0;
    BOOL success = FALSE;

    if (document == NULL || selection == NULL || paragraphs == NULL ||
        FAILED(ITextDocument_Range(document, selection->cpMin,
                                   selection->cpMax, &range)) ||
        range == NULL) {
        return FALSE;
    }
    if (SUCCEEDED(ITextRange_Expand(range, tomParagraph, &ignored)) &&
        SUCCEEDED(ITextRange_GetStart(range, &start)) &&
        SUCCEEDED(ITextRange_GetEnd(range, &end)) &&
        start >= 0 && end >= start) {
        paragraphs->cpMin = start;
        paragraphs->cpMax = end;
        success = TRUE;
    }
    ITextRange_Release(range);
    return success;
}

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
    ribbon_set_active_style(app, -1);
}

static void record_paragraph_format_change(AppState *app)
{
    app->richFormattingUsed = TRUE;
    document_mark_modified(app, TRUE);
    text_engine_note_layout_change(app);
    pageview_mark_dirty(app);
    ribbon_set_active_style(app, -1);
}

void format_initialize_document(AppState *app)
{
    CHARFORMAT2W character;
    PARAFORMAT2 paragraph;

    format_reset_document_design(app);
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
    ribbon_set_active_style(app, WORDCRAFT_STYLE_NORMAL);
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

void format_adjust_font_size(AppState *app, int direction)
{
    static const LONG standardSizesTwips[] = {
        160, 180, 200, 220, 240, 280, 320, 360,
        400, 440, 480, 520, 560, 720, 960, 1440
    };
    CHARFORMAT2W current;
    LONG nextSize;
    size_t index;

    if (app == NULL || app->editor == NULL || direction == 0) {
        return;
    }
    ZeroMemory(&current, sizeof(current));
    current.cbSize = sizeof(current);
    SendMessageW(app->editor, EM_GETCHARFORMAT, SCF_SELECTION,
                 (LPARAM)&current);
    if ((current.dwMask & CFM_SIZE) == 0 || current.yHeight <= 0) {
        MessageBeep(MB_ICONWARNING);
        app_set_status_message(app,
                               L"Choose text with one font size before resizing");
        SetFocus(app->editor);
        return;
    }
    nextSize = current.yHeight;
    if (direction > 0) {
        for (index = 0; index < ARRAYSIZE(standardSizesTwips); ++index) {
            if (standardSizesTwips[index] > current.yHeight) {
                nextSize = standardSizesTwips[index];
                break;
            }
        }
    } else {
        for (index = ARRAYSIZE(standardSizesTwips); index > 0; --index) {
            if (standardSizesTwips[index - 1] < current.yHeight) {
                nextSize = standardSizesTwips[index - 1];
                break;
            }
        }
    }
    if (nextSize != current.yHeight) {
        format_set_font_size(app, nextSize / 20.0);
    }
    SetFocus(app->editor);
}

void format_toggle_script(AppState *app, BOOL superscript)
{
    CHARFORMAT2W current;
    CHARFORMAT2W change;
    DWORD desired = superscript ? CFE_SUPERSCRIPT : CFE_SUBSCRIPT;
    BOOL enabled;

    if (app == NULL || app->editor == NULL) {
        return;
    }
    ZeroMemory(&current, sizeof(current));
    current.cbSize = sizeof(current);
    SendMessageW(app->editor, EM_GETCHARFORMAT, SCF_SELECTION,
                 (LPARAM)&current);
    enabled = (current.dwMask & CFM_SUBSCRIPT) == CFM_SUBSCRIPT &&
              (current.dwEffects & desired) != 0;

    ZeroMemory(&change, sizeof(change));
    change.cbSize = sizeof(change);
    change.dwMask = CFM_SUBSCRIPT;
    change.dwEffects = enabled ? 0 : desired;
    if (SendMessageW(app->editor, EM_SETCHARFORMAT, SCF_SELECTION,
                     (LPARAM)&change)) {
        record_character_format_change(app);
    }
    format_sync_controls(app);
    SetFocus(app->editor);
}

void format_toggle_highlight(AppState *app)
{
    const COLORREF highlight = RGB(255, 235, 92);
    CHARFORMAT2W current;
    CHARFORMAT2W change;
    BOOL enabled;

    if (app == NULL || app->editor == NULL) {
        return;
    }
    ZeroMemory(&current, sizeof(current));
    current.cbSize = sizeof(current);
    SendMessageW(app->editor, EM_GETCHARFORMAT, SCF_SELECTION,
                 (LPARAM)&current);
    enabled = (current.dwMask & CFM_BACKCOLOR) != 0 &&
              (current.dwEffects & CFE_AUTOBACKCOLOR) == 0 &&
              current.crBackColor == highlight;

    ZeroMemory(&change, sizeof(change));
    change.cbSize = sizeof(change);
    change.dwMask = CFM_BACKCOLOR;
    change.dwEffects = enabled ? CFE_AUTOBACKCOLOR : 0;
    change.crBackColor = highlight;
    if (SendMessageW(app->editor, EM_SETCHARFORMAT, SCF_SELECTION,
                     (LPARAM)&change)) {
        record_character_format_change(app);
    }
    format_sync_controls(app);
    SetFocus(app->editor);
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
        record_paragraph_format_change(app);
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
    change.dwMask = PFM_NUMBERING | PFM_NUMBERINGSTYLE |
                    PFM_NUMBERINGSTART | PFM_NUMBERINGTAB | PFM_OFFSET;
    change.wNumbering = enabled ? 0 : PFN_BULLET;
    change.wNumberingStyle = 0;
    change.wNumberingStart = 0;
    change.wNumberingTab = enabled ? 0 : 360;
    change.dxOffset = enabled ? 0 : 360;
    if (SendMessageW(app->editor, EM_SETPARAFORMAT, 0, (LPARAM)&change)) {
        record_paragraph_format_change(app);
    }
    format_sync_controls(app);
    SetFocus(app->editor);
}

void format_toggle_numbering(AppState *app)
{
    PARAFORMAT2 current;
    PARAFORMAT2 change;
    BOOL enabled;

    if (app == NULL || app->editor == NULL) {
        return;
    }
    ZeroMemory(&current, sizeof(current));
    current.cbSize = sizeof(current);
    SendMessageW(app->editor, EM_GETPARAFORMAT, 0, (LPARAM)&current);
    enabled = (current.dwMask & PFM_NUMBERING) != 0 &&
              current.wNumbering == PFN_ARABIC;

    ZeroMemory(&change, sizeof(change));
    change.cbSize = sizeof(change);
    change.dwMask = PFM_NUMBERING | PFM_NUMBERINGSTYLE |
                    PFM_NUMBERINGSTART | PFM_NUMBERINGTAB | PFM_OFFSET;
    change.wNumbering = enabled ? 0 : PFN_ARABIC;
    change.wNumberingStyle = enabled ? 0 : PFNS_PERIOD;
    change.wNumberingStart = enabled ? 0 : 1;
    change.wNumberingTab = enabled ? 0 : 360;
    change.dxOffset = enabled ? 0 : 360;
    if (SendMessageW(app->editor, EM_SETPARAFORMAT, 0,
                     (LPARAM)&change)) {
        record_paragraph_format_change(app);
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
        record_paragraph_format_change(app);
    }
    format_sync_controls(app);
    SetFocus(app->editor);
}

void format_cycle_line_spacing(AppState *app)
{
    static const BYTE rules[] = {0, 5, 1, 2};
    static const LONG spacings[] = {0, 23, 0, 0};
    static const WCHAR *const labels[] = {
        L"1.0", L"1.15", L"1.5", L"2.0"
    };
    PARAFORMAT2 current;
    PARAFORMAT2 change;
    WCHAR status[64];
    size_t currentIndex = 0;
    size_t nextIndex;

    if (app == NULL || app->editor == NULL) {
        return;
    }
    ZeroMemory(&current, sizeof(current));
    current.cbSize = sizeof(current);
    SendMessageW(app->editor, EM_GETPARAFORMAT, 0, (LPARAM)&current);
    if ((current.dwMask & PFM_LINESPACING) != 0) {
        for (currentIndex = 0; currentIndex < ARRAYSIZE(rules);
             ++currentIndex) {
            if (current.bLineSpacingRule == rules[currentIndex] &&
                (rules[currentIndex] != 5 ||
                 current.dyLineSpacing == spacings[currentIndex])) {
                break;
            }
        }
    }
    if (currentIndex >= ARRAYSIZE(rules)) {
        currentIndex = 0;
    }
    nextIndex = (currentIndex + 1) % ARRAYSIZE(rules);

    ZeroMemory(&change, sizeof(change));
    change.cbSize = sizeof(change);
    change.dwMask = PFM_LINESPACING;
    change.bLineSpacingRule = rules[nextIndex];
    change.dyLineSpacing = spacings[nextIndex];
    if (SendMessageW(app->editor, EM_SETPARAFORMAT, 0,
                     (LPARAM)&change)) {
        record_paragraph_format_change(app);
        StringCchPrintfW(status, ARRAYSIZE(status), L"Line spacing: %s",
                         labels[nextIndex]);
        app_set_status_message(app, status);
    }
    format_sync_controls(app);
    SetFocus(app->editor);
}

static COLORREF format_blend_color(COLORREF first, COLORREF second,
                                   unsigned secondPercent)
{
    unsigned firstPercent = 100u - min(secondPercent, 100u);

    return RGB(
        (GetRValue(first) * firstPercent +
         GetRValue(second) * secondPercent) / 100u,
        (GetGValue(first) * firstPercent +
         GetGValue(second) * secondPercent) / 100u,
        (GetBValue(first) * firstPercent +
         GetBValue(second) * secondPercent) / 100u);
}

static COLORREF format_design_accent(
    int colorScheme, const WordcraftDesignStyleSetInfo *info)
{
    static const COLORREF accents[DESIGN_COLOR_SCHEME_COUNT] = {
        RGB(46, 116, 181), RGB(38, 126, 191), RGB(63, 139, 83),
        RGB(224, 112, 42), RGB(192, 57, 62), RGB(128, 83, 167)
    };

    if (info == NULL) {
        return RGB(37, 76, 132);
    }
    /*
     * Office means "use this style set's native accent".  The explicit
     * color families replace it, matching the Design ribbon swatches.
     */
    if (colorScheme == DESIGN_COLOR_SCHEME_OFFICE) {
        return info->accent;
    }
    if (colorScheme < 0 || colorScheme >= DESIGN_COLOR_SCHEME_COUNT) {
        return info->accent;
    }
    return accents[colorScheme];
}

static void format_design_fonts(
    int fontScheme, const WordcraftDesignStyleSetInfo *info,
    const WCHAR **headingFont, const WCHAR **bodyFont)
{
    static const WCHAR *const headingFonts[DESIGN_FONT_SCHEME_COUNT] = {
        NULL, L"Cambria", L"Aptos Display", L"Trebuchet MS",
        L"Garamond", L"Courier New"
    };
    static const WCHAR *const bodyFonts[DESIGN_FONT_SCHEME_COUNT] = {
        NULL, L"Georgia", L"Aptos", L"Calibri",
        L"Book Antiqua", L"Courier New"
    };

    *headingFont =
        info != NULL ? info->headingFont : WORDCRAFT_DEFAULT_FONT_FACE;
    *bodyFont =
        info != NULL ? info->bodyFont : WORDCRAFT_DEFAULT_FONT_FACE;
    if (fontScheme > DESIGN_FONT_SCHEME_OFFICE &&
        fontScheme < DESIGN_FONT_SCHEME_COUNT) {
        *headingFont = headingFonts[fontScheme];
        *bodyFont = bodyFonts[fontScheme];
    }
}

static void format_design_base_spacing(
    int paragraphSpacing, BOOL compactStyle, LONG *spaceAfter,
    BYTE *lineRule, LONG *lineSpacing)
{
    switch (paragraphSpacing) {
    case DESIGN_PARAGRAPH_SPACING_NONE:
        *spaceAfter = 0;
        *lineRule = 0;
        *lineSpacing = 0;
        break;
    case DESIGN_PARAGRAPH_SPACING_COMPACT:
        *spaceAfter = 60;
        *lineRule = 0;
        *lineSpacing = 0;
        break;
    case DESIGN_PARAGRAPH_SPACING_TIGHT:
        *spaceAfter = 80;
        *lineRule = 5;
        *lineSpacing = 21;
        break;
    case DESIGN_PARAGRAPH_SPACING_RELAXED:
        *spaceAfter = 200;
        *lineRule = 5;
        *lineSpacing = 24;
        break;
    case DESIGN_PARAGRAPH_SPACING_DOUBLE:
        *spaceAfter = 120;
        *lineRule = 2;
        *lineSpacing = 0;
        break;
    case DESIGN_PARAGRAPH_SPACING_OPEN:
    default:
        *spaceAfter = WORDCRAFT_DEFAULT_PARAGRAPH_SPACE_AFTER_TWIPS;
        *lineRule = WORDCRAFT_DEFAULT_LINE_SPACING_RULE;
        *lineSpacing = WORDCRAFT_DEFAULT_LINE_SPACING;
        break;
    }
    if (compactStyle &&
        paragraphSpacing == DESIGN_PARAGRAPH_SPACING_OPEN) {
        *spaceAfter = 60;
        *lineRule = 0;
        *lineSpacing = 0;
    }
}

static LONG format_scale_style_spacing(
    LONG openValue, int paragraphSpacing, BOOL compactStyle)
{
    unsigned percent;

    switch (paragraphSpacing) {
    case DESIGN_PARAGRAPH_SPACING_NONE:
        percent = 0;
        break;
    case DESIGN_PARAGRAPH_SPACING_COMPACT:
        percent = 50;
        break;
    case DESIGN_PARAGRAPH_SPACING_TIGHT:
        percent = 75;
        break;
    case DESIGN_PARAGRAPH_SPACING_RELAXED:
        percent = 140;
        break;
    case DESIGN_PARAGRAPH_SPACING_DOUBLE:
        percent = 165;
        break;
    case DESIGN_PARAGRAPH_SPACING_OPEN:
    default:
        percent = compactStyle ? 55 : 100;
        break;
    }
    return MulDiv(openValue, (int)percent, 100);
}

static void format_build_style_for_design(
    WordcraftStyle style, int styleSet, int colorScheme,
    int fontScheme, int paragraphSpacing,
    CHARFORMAT2W *character, PARAFORMAT2 *paragraph)
{
    WordcraftDesignStyleSetInfo info;
    const WCHAR *headingFont;
    const WCHAR *bodyFont;
    COLORREF accent;

    if (!format_get_design_style_set_info(styleSet, &info)) {
        info = wordcraftDesignStyleSets[DESIGN_STYLE_SET_OFFICE];
        styleSet = DESIGN_STYLE_SET_OFFICE;
    }
    if (colorScheme < 0 || colorScheme >= DESIGN_COLOR_SCHEME_COUNT) {
        colorScheme = DESIGN_COLOR_SCHEME_OFFICE;
    }
    if (fontScheme < 0 || fontScheme >= DESIGN_FONT_SCHEME_COUNT) {
        fontScheme = DESIGN_FONT_SCHEME_OFFICE;
    }
    if (paragraphSpacing < 0 ||
        paragraphSpacing >= DESIGN_PARAGRAPH_SPACING_COUNT) {
        paragraphSpacing = DESIGN_PARAGRAPH_SPACING_OPEN;
    }
    format_design_fonts(fontScheme, &info, &headingFont, &bodyFont);
    accent = format_design_accent(colorScheme, &info);

    ZeroMemory(character, sizeof(*character));
    character->cbSize = sizeof(*character);
    character->dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_CHARSET |
                        CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE |
                        CFM_STRIKEOUT | CFM_SUBSCRIPT | CFM_OFFSET |
                        CFM_BACKCOLOR;
    character->dwEffects = CFE_AUTOBACKCOLOR;
    character->yHeight = WORDCRAFT_DEFAULT_FONT_SIZE_TWIPS;
    character->yOffset = 0;
    character->crTextColor = RGB(0, 0, 0);
    character->bCharSet = DEFAULT_CHARSET;
    StringCchCopyW(character->szFaceName,
                   ARRAYSIZE(character->szFaceName), bodyFont);

    ZeroMemory(paragraph, sizeof(*paragraph));
    paragraph->cbSize = sizeof(*paragraph);
    paragraph->dwMask = PFM_ALIGNMENT | PFM_NUMBERING |
                        PFM_NUMBERINGSTYLE | PFM_NUMBERINGSTART |
                        PFM_NUMBERINGTAB | PFM_STARTINDENT |
                        PFM_RIGHTINDENT | PFM_OFFSET | PFM_SPACEBEFORE |
                        PFM_SPACEAFTER | PFM_LINESPACING;
    paragraph->wAlignment = PFA_LEFT;
    paragraph->wNumbering = 0;
    paragraph->dxStartIndent = 0;
    paragraph->dxRightIndent = 0;
    paragraph->dxOffset = 0;
    paragraph->dySpaceBefore = 0;
    format_design_base_spacing(
        paragraphSpacing, info.compactSpacing,
        &paragraph->dySpaceAfter, &paragraph->bLineSpacingRule,
        &paragraph->dyLineSpacing);

    switch (style) {
    case WORDCRAFT_STYLE_NO_SPACING:
        paragraph->dySpaceAfter = 0;
        paragraph->bLineSpacingRule = 0;
        paragraph->dyLineSpacing = 0;
        break;
    case WORDCRAFT_STYLE_HEADING_1:
        StringCchCopyW(character->szFaceName,
                       ARRAYSIZE(character->szFaceName), headingFont);
        character->yHeight = info.heading1Size;
        character->dwEffects |= CFE_BOLD;
        character->crTextColor = accent;
        paragraph->dySpaceBefore = format_scale_style_spacing(
            240, paragraphSpacing, info.compactSpacing);
        paragraph->dySpaceAfter = format_scale_style_spacing(
            60, paragraphSpacing, info.compactSpacing);
        break;
    case WORDCRAFT_STYLE_HEADING_2:
        StringCchCopyW(character->szFaceName,
                       ARRAYSIZE(character->szFaceName), headingFont);
        character->yHeight = info.heading2Size;
        character->dwEffects |= CFE_BOLD;
        character->crTextColor =
            styleSet == DESIGN_STYLE_SET_OFFICE &&
                    colorScheme == DESIGN_COLOR_SCHEME_OFFICE
                ? RGB(55, 96, 146)
                : format_blend_color(accent, RGB(255, 255, 255), 14);
        paragraph->dySpaceBefore = format_scale_style_spacing(
            200, paragraphSpacing, info.compactSpacing);
        paragraph->dySpaceAfter = format_scale_style_spacing(
            40, paragraphSpacing, info.compactSpacing);
        break;
    case WORDCRAFT_STYLE_TITLE:
        StringCchCopyW(character->szFaceName,
                       ARRAYSIZE(character->szFaceName), headingFont);
        character->yHeight = info.titleSize;
        character->dwEffects |= CFE_BOLD;
        character->crTextColor =
            styleSet == DESIGN_STYLE_SET_OFFICE &&
                    colorScheme == DESIGN_COLOR_SCHEME_OFFICE
                ? RGB(31, 56, 92)
                : format_blend_color(accent, RGB(0, 0, 0), 28);
        paragraph->wAlignment =
            info.centerTitle ? PFA_CENTER : PFA_LEFT;
        paragraph->dySpaceAfter = format_scale_style_spacing(
            300, paragraphSpacing, info.compactSpacing);
        break;
    case WORDCRAFT_STYLE_NORMAL:
    default:
        break;
    }
}

static void format_build_style(const AppState *app, WordcraftStyle style,
                               CHARFORMAT2W *character,
                               PARAFORMAT2 *paragraph)
{
    int styleSet = DESIGN_STYLE_SET_OFFICE;
    int colorScheme = DESIGN_COLOR_SCHEME_OFFICE;
    int fontScheme = DESIGN_FONT_SCHEME_OFFICE;
    int paragraphSpacing = DESIGN_PARAGRAPH_SPACING_OPEN;

    if (app != NULL) {
        styleSet = app->designStyleSet;
        colorScheme = app->designColorScheme;
        fontScheme = app->designFontScheme;
        paragraphSpacing = app->designParagraphSpacing;
    }
    format_build_style_for_design(
        style, styleSet, colorScheme, fontScheme, paragraphSpacing,
        character, paragraph);
}

BOOL format_apply_style(AppState *app, WordcraftStyle style)
{
    CHARFORMAT2W character;
    PARAFORMAT2 paragraph;
    CHARRANGE original;
    CHARRANGE paragraphs;
    ITextDocument *document;
    ITextSelection *tomSelection = NULL;
    long selectionFlags = 0;
    BOOL haveSelectionFlags = FALSE;
    BOOL changedCharacter;
    BOOL changedParagraph;
    BOOL wasLoading;
    LRESULT eventMask;

    if (app == NULL || app->editor == NULL ||
        style < 0 || style >= WORDCRAFT_STYLE_COUNT) {
        return FALSE;
    }
    format_build_style(app, style, &character, &paragraph);
    SendMessageW(app->editor, EM_EXGETSEL, 0, (LPARAM)&original);
    paragraphs = original;
    document = get_format_document(app->editor);
    if (document == NULL ||
        !expand_to_paragraphs(document, &original, &paragraphs) ||
        !begin_format_collection(document)) {
        if (document != NULL) {
            ITextDocument_Release(document);
        }
        MessageBeep(MB_ICONWARNING);
        app_set_status_message(app, L"The selected style could not be applied");
        return FALSE;
    }
    if (SUCCEEDED(ITextDocument_GetSelection(document, &tomSelection)) &&
        tomSelection != NULL &&
        SUCCEEDED(ITextSelection_GetFlags(tomSelection, &selectionFlags))) {
        haveSelectionFlags = TRUE;
    }

    wasLoading = app->loading;
    app->loading = TRUE;
    eventMask = SendMessageW(app->editor, EM_GETEVENTMASK, 0, 0);
    SendMessageW(app->editor, EM_SETEVENTMASK, 0,
                 eventMask & ~(LRESULT)ENM_SELCHANGE);
    SendMessageW(app->editor, WM_SETREDRAW, FALSE, 0);
    SendMessageW(app->editor, EM_EXSETSEL, 0, (LPARAM)&paragraphs);
    changedCharacter = (BOOL)SendMessageW(
        app->editor, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&character);
    changedParagraph = (BOOL)SendMessageW(
        app->editor, EM_SETPARAFORMAT, 0, (LPARAM)&paragraph);
    SendMessageW(app->editor, EM_EXSETSEL, 0, (LPARAM)&original);
    if (haveSelectionFlags) {
        ITextSelection_SetFlags(tomSelection, selectionFlags);
    }
    SendMessageW(app->editor, EM_SETEVENTMASK, 0, eventMask);
    SendMessageW(app->editor, WM_SETREDRAW, TRUE, 0);
    app->loading = wasLoading;
    end_format_collection(document);
    if (tomSelection != NULL) {
        ITextSelection_Release(tomSelection);
    }
    ITextDocument_Release(document);

    if (changedCharacter || changedParagraph) {
        app->richFormattingUsed = TRUE;
        document_mark_modified(app, TRUE);
        text_engine_note_layout_change(app);
        pageview_mark_dirty(app);
    }
    if (changedCharacter != changedParagraph) {
        MessageBeep(MB_ICONWARNING);
        app_set_status_message(app, L"The selected style was only partially applied");
    } else if (!changedCharacter) {
        MessageBeep(MB_ICONWARNING);
        app_set_status_message(app, L"The selected style could not be applied");
    }
    InvalidateRect(app->editor, NULL, TRUE);
    app_update_command_ui(app);
    SetFocus(app->editor);
    ribbon_set_active_style(app,
                            changedCharacter && changedParagraph
                                ? (int)style
                                : -1);
    return changedCharacter && changedParagraph;
}

static BOOL format_character_matches_style(
    const CHARFORMAT2W *actual, const CHARFORMAT2W *style)
{
    const DWORD requiredMask =
        CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BOLD | CFM_ITALIC |
        CFM_UNDERLINE | CFM_STRIKEOUT | CFM_SUBSCRIPT | CFM_OFFSET |
        CFM_BACKCOLOR;
    const DWORD comparedEffects =
        CFE_BOLD | CFE_ITALIC | CFE_UNDERLINE | CFE_STRIKEOUT |
        CFE_SUBSCRIPT | CFE_SUPERSCRIPT | CFE_AUTOCOLOR |
        CFE_AUTOBACKCOLOR;

    if (actual == NULL || style == NULL ||
        (actual->dwMask & requiredMask) != requiredMask ||
        lstrcmpiW(actual->szFaceName, style->szFaceName) != 0 ||
        actual->yHeight != style->yHeight ||
        actual->yOffset != style->yOffset ||
        actual->crTextColor != style->crTextColor ||
        (actual->dwEffects & comparedEffects) !=
            (style->dwEffects & comparedEffects)) {
        return FALSE;
    }
    /*
     * An explicit highlight is direct formatting even when it happens to
     * use the editor's page color.  WordCraft styles always use automatic
     * background color.
     */
    if ((style->dwEffects & CFE_AUTOBACKCOLOR) == 0 &&
        actual->crBackColor != style->crBackColor) {
        return FALSE;
    }
    return TRUE;
}

static BOOL format_paragraph_matches_style(
    const PARAFORMAT2 *actual, const PARAFORMAT2 *style)
{
    const DWORD requiredMask =
        PFM_ALIGNMENT | PFM_NUMBERING | PFM_STARTINDENT |
        PFM_RIGHTINDENT | PFM_OFFSET | PFM_SPACEBEFORE |
        PFM_SPACEAFTER | PFM_LINESPACING;

    if (actual == NULL || style == NULL ||
        (actual->dwMask & requiredMask) != requiredMask ||
        actual->wAlignment != style->wAlignment ||
        actual->wNumbering != style->wNumbering ||
        actual->dxStartIndent != style->dxStartIndent ||
        actual->dxRightIndent != style->dxRightIndent ||
        actual->dxOffset != style->dxOffset ||
        actual->dySpaceBefore != style->dySpaceBefore ||
        actual->dySpaceAfter != style->dySpaceAfter ||
        actual->bLineSpacingRule != style->bLineSpacingRule) {
        return FALSE;
    }
    if (style->wNumbering != 0 &&
        (actual->wNumberingStyle != style->wNumberingStyle ||
         actual->wNumberingStart != style->wNumberingStart ||
         actual->wNumberingTab != style->wNumberingTab)) {
        return FALSE;
    }
    if (style->bLineSpacingRule == 5 &&
        actual->dyLineSpacing != style->dyLineSpacing) {
        return FALSE;
    }
    return TRUE;
}

static BOOL format_style_definitions_equal(
    const CHARFORMAT2W *firstCharacter,
    const PARAFORMAT2 *firstParagraph,
    const CHARFORMAT2W *secondCharacter,
    const PARAFORMAT2 *secondParagraph)
{
    return format_character_matches_style(
               firstCharacter, secondCharacter) &&
           format_paragraph_matches_style(
               firstParagraph, secondParagraph);
}

static BOOL format_get_paragraph_range(
    ITextDocument *document, LONG position, CHARRANGE *paragraph)
{
    ITextRange *range = NULL;
    long ignored = 0;
    long start = 0;
    long end = 0;
    BOOL success = FALSE;

    if (document == NULL || paragraph == NULL || position < 0 ||
        FAILED(ITextDocument_Range(
            document, position, position, &range)) ||
        range == NULL) {
        return FALSE;
    }
    if (SUCCEEDED(ITextRange_Expand(range, tomParagraph, &ignored)) &&
        SUCCEEDED(ITextRange_GetStart(range, &start)) &&
        SUCCEEDED(ITextRange_GetEnd(range, &end)) &&
        start >= 0 && end >= position) {
        paragraph->cpMin = start;
        paragraph->cpMax = end;
        success = TRUE;
    }
    ITextRange_Release(range);
    return success;
}

BOOL format_apply_document_design(
    AppState *app, int styleSet, int colorScheme, int fontScheme,
    int paragraphSpacing)
{
    CHARFORMAT2W oldCharacters[WORDCRAFT_STYLE_COUNT];
    CHARFORMAT2W newCharacters[WORDCRAFT_STYLE_COUNT];
    PARAFORMAT2 oldParagraphs[WORDCRAFT_STYLE_COUNT];
    PARAFORMAT2 newParagraphs[WORDCRAFT_STYLE_COUNT];
    CHARFORMAT2W actualCharacter;
    PARAFORMAT2 actualParagraph;
    CHARRANGE originalSelection;
    CHARRANGE paragraphRange;
    ITextDocument *document = NULL;
    ITextSelection *tomSelection = NULL;
    HWND originalFocus;
    POINT scrollPosition;
    long selectionFlags = 0;
    LONG textLength;
    LONG position;
    LRESULT eventMask;
    BOOL haveSelectionFlags = FALSE;
    BOOL haveScrollPosition;
    BOOL wasLoading;
    BOOL contentChanged = FALSE;
    BOOL defaultChanged;
    BOOL stateChanged;
    int style;

    if (app == NULL || app->editor == NULL ||
        styleSet < 0 || styleSet >= DESIGN_STYLE_SET_COUNT ||
        colorScheme < 0 ||
            colorScheme >= DESIGN_COLOR_SCHEME_COUNT ||
        fontScheme < 0 || fontScheme >= DESIGN_FONT_SCHEME_COUNT ||
        paragraphSpacing < 0 ||
            paragraphSpacing >= DESIGN_PARAGRAPH_SPACING_COUNT) {
        return FALSE;
    }

    stateChanged =
        app->designStyleSet != styleSet ||
        app->designColorScheme != colorScheme ||
        app->designFontScheme != fontScheme ||
        app->designParagraphSpacing != paragraphSpacing;
    if (!stateChanged) {
        return TRUE;
    }


    for (style = 0; style < WORDCRAFT_STYLE_COUNT; ++style) {
        format_build_style_for_design(
            (WordcraftStyle)style, app->designStyleSet,
            app->designColorScheme, app->designFontScheme,
            app->designParagraphSpacing, &oldCharacters[style],
            &oldParagraphs[style]);
        format_build_style_for_design(
            (WordcraftStyle)style, styleSet, colorScheme, fontScheme,
            paragraphSpacing, &newCharacters[style],
            &newParagraphs[style]);
    }
    defaultChanged = !format_character_matches_style(
        &oldCharacters[WORDCRAFT_STYLE_NORMAL],
        &newCharacters[WORDCRAFT_STYLE_NORMAL]);

    document = get_format_document(app->editor);
    if (document == NULL || !begin_format_collection(document)) {
        if (document != NULL) {
            ITextDocument_Release(document);
        }
        return FALSE;
    }
    originalFocus = GetFocus();
    SendMessageW(app->editor, EM_EXGETSEL, 0,
                 (LPARAM)&originalSelection);
    haveScrollPosition = (BOOL)SendMessageW(
        app->editor, EM_GETSCROLLPOS, 0, (LPARAM)&scrollPosition);
    if (SUCCEEDED(ITextDocument_GetSelection(
            document, &tomSelection)) &&
        tomSelection != NULL &&
        SUCCEEDED(ITextSelection_GetFlags(
            tomSelection, &selectionFlags))) {
        haveSelectionFlags = TRUE;
    }

    textLength = GetWindowTextLengthW(app->editor);
    wasLoading = app->loading;
    app->loading = TRUE;
    eventMask = SendMessageW(app->editor, EM_GETEVENTMASK, 0, 0);
    SendMessageW(app->editor, EM_SETEVENTMASK, 0,
                 eventMask & ~(LRESULT)ENM_SELCHANGE);
    SendMessageW(app->editor, EM_STOPGROUPTYPING, 0, 0);
    SendMessageW(app->editor, WM_SETREDRAW, FALSE, 0);

    position = 0;
    /*
     * Include the story end so an empty document, or the final empty
     * paragraph after a trailing return, receives the new Normal paragraph
     * defaults as well.
     */
    while (position <= textLength &&
           format_get_paragraph_range(
               document, position, &paragraphRange)) {
        int matchedStyle = -1;

        ZeroMemory(&actualCharacter, sizeof(actualCharacter));
        actualCharacter.cbSize = sizeof(actualCharacter);
        ZeroMemory(&actualParagraph, sizeof(actualParagraph));
        actualParagraph.cbSize = sizeof(actualParagraph);
        SendMessageW(app->editor, EM_EXSETSEL, 0,
                     (LPARAM)&paragraphRange);
        SendMessageW(app->editor, EM_GETCHARFORMAT, SCF_SELECTION,
                     (LPARAM)&actualCharacter);
        SendMessageW(app->editor, EM_GETPARAFORMAT, 0,
                     (LPARAM)&actualParagraph);

        for (style = 0; style < WORDCRAFT_STYLE_COUNT; ++style) {
            if (format_character_matches_style(
                    &actualCharacter, &oldCharacters[style]) &&
                format_paragraph_matches_style(
                    &actualParagraph, &oldParagraphs[style])) {
                matchedStyle = style;
                break;
            }
        }
        if (matchedStyle >= 0 &&
            !format_style_definitions_equal(
                &oldCharacters[matchedStyle],
                &oldParagraphs[matchedStyle],
                &newCharacters[matchedStyle],
                &newParagraphs[matchedStyle])) {
            BOOL characterApplied = (BOOL)SendMessageW(
                app->editor, EM_SETCHARFORMAT, SCF_SELECTION,
                (LPARAM)&newCharacters[matchedStyle]);
            BOOL paragraphApplied = (BOOL)SendMessageW(
                app->editor, EM_SETPARAFORMAT, 0,
                (LPARAM)&newParagraphs[matchedStyle]);
            contentChanged =
                contentChanged || characterApplied || paragraphApplied;
        }
        if (paragraphRange.cpMax <= position) {
            break;
        }
        position = paragraphRange.cpMax;
    }

    app->designStyleSet = styleSet;
    app->designColorScheme = colorScheme;
    app->designFontScheme = fontScheme;
    app->designParagraphSpacing = paragraphSpacing;
    if (SendMessageW(app->editor, EM_SETCHARFORMAT, SCF_DEFAULT,
                     (LPARAM)&newCharacters[WORDCRAFT_STYLE_NORMAL]) &&
        defaultChanged) {
        contentChanged = TRUE;
    }

    SendMessageW(app->editor, EM_EXSETSEL, 0,
                 (LPARAM)&originalSelection);
    if (haveSelectionFlags) {
        ITextSelection_SetFlags(tomSelection, selectionFlags);
    }
    if (haveScrollPosition) {
        SendMessageW(app->editor, EM_SETSCROLLPOS, 0,
                     (LPARAM)&scrollPosition);
    }
    SendMessageW(app->editor, EM_SETEVENTMASK, 0, eventMask);
    SendMessageW(app->editor, WM_SETREDRAW, TRUE, 0);
    app->loading = wasLoading;
    end_format_collection(document);
    SendMessageW(app->editor, EM_STOPGROUPTYPING, 0, 0);

    if (tomSelection != NULL) {
        ITextSelection_Release(tomSelection);
    }
    ITextDocument_Release(document);
    document = NULL;

    if (contentChanged) {
        app->richFormattingUsed = TRUE;
        document_mark_modified(app, TRUE);
        text_engine_note_layout_change(app);
        pageview_mark_dirty(app);
        ribbon_set_active_style(app, -1);
        InvalidateRect(app->editor, NULL, TRUE);
    }
    format_sync_controls(app);
    app_update_command_ui(app);
    if (originalFocus != NULL && IsWindow(originalFocus)) {
        SetFocus(originalFocus);
    }
    return TRUE;
}

void format_clear_formatting(AppState *app)
{
    if (format_apply_style(app, WORDCRAFT_STYLE_NORMAL)) {
        app_set_status_message(app, L"Formatting cleared to Normal");
    }
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
    BOOL subscript;
    BOOL superscript;
    BOOL highlighted;
    BOOL bullets;
    BOOL numbering;
    BOOL left;
    BOOL center;
    BOOL right;
    BOOL justify;
    int lineSpacingPercent = 0;
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
    subscript = (character.dwMask & CFM_SUBSCRIPT) == CFM_SUBSCRIPT &&
                (character.dwEffects & CFE_SUBSCRIPT) != 0;
    superscript = (character.dwMask & CFM_SUPERSCRIPT) == CFM_SUPERSCRIPT &&
                  (character.dwEffects & CFE_SUPERSCRIPT) != 0;
    highlighted = (character.dwMask & CFM_BACKCOLOR) != 0 &&
                  (character.dwEffects & CFE_AUTOBACKCOLOR) == 0 &&
                  character.crBackColor == RGB(255, 235, 92);
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
    numbering = (paragraph.dwMask & PFM_NUMBERING) != 0 &&
                paragraph.wNumbering == PFN_ARABIC;
    if ((paragraph.dwMask & PFM_LINESPACING) != 0) {
        if (paragraph.bLineSpacingRule == 0) {
            lineSpacingPercent = 100;
        } else if (paragraph.bLineSpacingRule == 1) {
            lineSpacingPercent = 150;
        } else if (paragraph.bLineSpacingRule == 2) {
            lineSpacingPercent = 200;
        } else if (paragraph.bLineSpacingRule == 5 &&
                   paragraph.dyLineSpacing > 0) {
            lineSpacingPercent = (int)paragraph.dyLineSpacing * 5;
        }
    }
    set_button_state(app->alignLeftButton, left);
    set_button_state(app->alignCenterButton, center);
    set_button_state(app->alignRightButton, right);
    set_button_state(app->alignJustifyButton, justify);
    set_button_state(app->bulletsButton, bullets);
    ribbon_sync_home_formatting(app, subscript, superscript,
                                numbering, highlighted,
                                lineSpacingPercent);

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
