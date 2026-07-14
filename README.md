# WordCraft

WordCraft is a native Windows rich-text editor written in C with the Win32 API and the Windows Rich Edit 4.1 control. It is a practical, compact Word-like editor rather than a Microsoft Word binary-compatible replacement.

## Brand

WordCraft's original mark is a mischievous dog caught eating a sheet of homework. The Windows executable embeds the multi-size `resources/wordcraft.ico` icon, while `resources/wordcraft-logo.png` contains the full-resolution source artwork.

## Features

- Create, open, save, and safely replace documents
- Formatting-preserving Rich Text Format (`.rtf`) files
- Unicode text (`.txt`) import from UTF-8, UTF-16 LE, and UTF-16 BE
- UTF-8 text export with a warning before rich formatting is discarded
- Font family, font size, bold, italic, underline, strikethrough, and color
- Left, center, right, and justified paragraphs; bullets and indentation
- Multi-level undo/redo, clipboard editing, and standard keyboard shortcuts
- Forward/backward find, replace, replace all, case matching, and whole-word matching
- Page setup and printing through the Windows print system
- A continuous, scrollable paper-page workspace that can show several pages at once, with margins, page gaps, borders, shadows, click-to-edit page switching, and a live `Page X of Y` counter
- Light and dark application themes (`Ctrl+Shift+D`); dark mode keeps the document paper light so saved text colors remain accurate
- As-you-type spelling checks using the installed Windows spell service, with a conservative built-in typo detector when that service is unavailable (including Windows 7)
- Copilot-style inline ghost-text autocomplete based on common phrases and earlier document context; press `Tab` to accept a suggestion or `Esc` to dismiss it
- Separate spelling and completion worker threads analyze immutable text snapshots so language assistance does not block typing
- Independent **Tools** menu toggles for spelling checks and inline autocomplete
- Zoom levels, page-width wrapping, a live word/character count, and line/column status
- Drag-and-drop opening and opening a document from the command line

## Build

The default Makefile uses LLVM for Windows (`clang`, `lld-link`, and `llvm-rc`) plus the Windows SDK:

```powershell
make
```

For a debug build:

```powershell
make debug
```

This produces `wordcraft-debug.exe` in a separate `build-debug` tree, so debug and release objects never mix.

Run the headless Rich Edit regression probes after building:

```powershell
make test
```

The probes cover large text extraction (beyond 64K), CRLF export, Unicode RTF formatting round trips, grouped Replace All undo behavior, whole-word matching, the RichEdit wrapping behavior used by the paged layout, conservative typo detection, phrase/context completion, prefix completion, and language-engine bounds checks.

`make gui-test` additionally launches WordCraft in hidden windows and checks UTF-8 command-line opening, one-page and multi-page pagination, continuous top-to-bottom scrolling, simultaneous page visibility, selection preservation while scrolling, forward and reverse cross-page selection state, caret-to-page tracking, dark-mode toggling without document changes, both worker threads, asynchronous spell/completion results, Tab acceptance, one-step completion undo, feature toggles, ordinary Tab behavior, UTF-8 saving, and clean shutdown.

MinGW-w64 is also supported when `gcc` and `windres` are on `PATH`:

```powershell
make clean
make TOOLCHAIN=mingw
```

The result is `wordcraft.exe`. Run it directly or pass an RTF/text document:

```powershell
.\wordcraft.exe
.\wordcraft.exe "C:\Documents\notes.rtf"
```

## Document compatibility

Use `.rtf` to preserve the formatting WordCraft supports. Plain-text saves intentionally contain only text. `.doc` and `.docx` are not supported: those formats require a separate binary/OOXML document engine and are not handled by the Windows Rich Edit control.

Text input accepts UTF-8 (with or without a BOM) and BOM-marked UTF-16 little- or big-endian files. Text output is UTF-8 with a BOM and Windows CRLF line endings. Files containing embedded NUL characters or invalid Unicode are rejected instead of being silently truncated.

WordCraft checks whether an opened file changed on disk before overwriting it. It does not currently provide autosave, crash recovery, cloud collaboration, tracked changes, macros, or a Word-compatible `.docx` layout engine. Printer, paper, orientation, and margin choices are retained for the current application session.

The source targets Windows 7 or newer. Windows 8 and later can use the operating system's installed spell checker for the user's locale (falling back to US English when available); Windows 7 and systems without a supported spell service retain the deliberately conservative built-in typo checks. Inline autocomplete is local and deterministic: it does not send document text to a network service. The output architecture follows the selected compiler; the default LLVM installation in this workspace produces x64 Windows binaries.

## Source layout

- `src/main.c` — application window, toolbars, status, commands, and layout
- `src/document.c` — Unicode/RTF I/O and the safe document lifecycle
- `src/format.c` — character and paragraph formatting
- `src/pageview.c` — paper-page layout, scrolling, navigation, and pagination
- `src/assist.c` — background spelling/completion workers and UI-thread overlays
- `src/language.c` — local typo fallback and deterministic completion engine
- `src/dialogs.c` — find/replace, date/time, and About dialogs
- `src/printing.c` — page setup and paginated printing
- `tests/language_probe.c` — typo and completion engine regression coverage
- `resources/app.rc` — icon, menus, shortcuts, and Windows manifest
- `resources/wordcraft.ico` — multi-size Windows application icon
- `resources/wordcraft-logo.png` — full-resolution dog-and-homework brand artwork
