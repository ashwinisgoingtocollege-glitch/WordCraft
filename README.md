# WordCraft

WordCraft is a native Windows rich-text editor written primarily in C with the
Win32 API. A narrow C++ bridge hosts Windows RichEdit text services so the live
document can be rendered through DirectWrite. It is a practical, compact
Word-like editor rather than a Microsoft Word binary-compatible replacement.

## Brand

WordCraft's original mark is a mischievous dog caught eating a sheet of homework. The Windows executable embeds the multi-size `resources/wordcraft.ico` icon, while `resources/wordcraft-logo.png` contains the full-resolution source artwork. A compact dog badge remains visible in the top toolbar, and subtle static curved-line accents frame the mark there and on the startup screen without adding distracting motion.

## Features

- Create, open, save, and safely replace documents
- A lightweight, DPI-aware startup screen using WordCraft's dog-and-homework
  mark, brand colors, and live initialization/document-loading status. The dog
  bobs and chews through an eight-frame loop while tiny homework scraps tumble
  away. Its small dedicated UI thread keeps that animation moving during slow
  document loads without delaying startup. The finished editor remains hidden
  until ready, avoiding a blank-document flash; high-contrast and
  reduced-animation settings replace the motion with a static logo
- Formatting-preserving Rich Text Format (`.rtf`) files
- Unicode text (`.txt`) import from UTF-8, UTF-16 LE, and UTF-16 BE
- UTF-8 text export with a warning before rich formatting is discarded
- Times New Roman 12 pt defaults for new and plain-text documents, plus a
  130-family Word-style font catalog, every other font installed in Windows,
  font size, bold, italic, underline, strikethrough, and color controls. Catalog
  fonts that are not installed remain visible and are clearly labeled instead
  of being silently replaced with a different typeface
- Left, center, right, and justified paragraphs; bullets and indentation
- A custom windowless rendering host for the words in the editable document.
  On supported Windows systems it uses Direct2D/DirectWrite for Unicode glyph
  shaping, kerning, subpixel line placement, hit testing, selections, and the
  live caret. A deterministic GDI path keeps the editor usable on older or
  graphics-limited systems. Pagination and printing use a synchronized hidden
  GDI formatter because `EM_FORMATRANGE` is a GDI Windows API
- A WordCraft typography engine that enables the full Windows advanced line
  formatter instead of its reduced fast line breaker. New and plain-text
  documents use a more open 1.10-line rhythm with 6 pt after paragraphs, while
  imported RTF keeps its authored spacing. The live editor, page previews,
  pagination, and printing all share this same layout authority, so caret and
  wrap positions stay consistent
- An eleven-tab, keyboard-accessible ribbon with File, Home, Insert, Draw,
  Design, Layout, References, Mailings, Review, View, and Help panels. Existing
  WordCraft commands are grouped into the matching tabs, while unavailable
  document engines are identified clearly instead of being presented as dead
  controls. Home uses the familiar Clipboard, Font, Paragraph, Styles, and
  Editing groups: paste/cut/copy; character effects, highlighting, and font
  sizing; lists, indentation, alignment, and line spacing; five paragraph
  style presets; and find/replace/select-all. The style gallery collapses into
  a keyboard-friendly selector when the window is narrower
- A minimal ribbon treatment with softly rounded tabs and buttons, a restrained
  curved ribbon-card outline, and generous spacing around related controls.
  The native GDI chrome scales with the window DPI and adapts its fills,
  outlines, and accents to both light and dark mode. Windows high-contrast mode
  suppresses the decorative branding and uses compact system-color controls
- Selection-anchored comments with Review-tab add, previous, next, and delete
  controls. Wrapped comment cards stay visible in a page-aligned rail beside
  the document, point back to their text positions, support click-to-navigate,
  and follow nearby edits. The selected anchor is temporarily highlighted pale
  yellow while composing or visiting its comment; that display-only highlight
  never changes saved formatting, undo history, copying, printing, pagination,
  or word counts
- Multi-level undo/redo, clipboard editing, and standard keyboard shortcuts
- Forward/backward find, replace, replace all, case matching, and whole-word matching
- A **Layout > Paper Size** catalog with 24 named Windows paper formats plus
  **Custom**. Selecting a size immediately resizes the live paper, repaginates
  the document, and seeds Windows Page Setup and Print with the same choice.
  Custom dimensions accept inches or millimeters from 0.1 to 100 inches (2.54
  to 2540 mm) and must leave at least 0.1 inch of printable space inside the
  current margins
- Page setup and printing through the Windows print system
- A continuous, scrollable paper-page workspace that can show several pages at
  once, with Word/Google Docs-style one-inch Normal margins on every side,
  page gaps, borders, shadows, click-to-edit page switching, and a live
  `Page X of Y` counter. Wheel and high-resolution trackpad input is coalesced
  into frame-paced animation targeting a consistent 60 fps
- Light and dark application themes (`Ctrl+Shift+D`); dark mode keeps the document paper light so saved text colors remain accurate
- As-you-type spelling checks using the installed Windows spell service, with a conservative built-in typo detector when that service is unavailable (including Windows 7)
- Copilot-style inline ghost-text autocomplete based on common phrases and earlier document context; press `Tab` to accept a suggestion or `Esc` to dismiss it
- Separate spelling and completion worker threads analyze immutable text snapshots so language assistance does not block typing
- Independent **Tools** menu toggles for spelling checks and inline autocomplete
- Zoom levels, page-width wrapping, a live word/character count, and line/column status
- Drag-and-drop opening and opening a document from the command line

## Live sharing

WordCraft can share the current document directly without a central server. The
host computer runs the embedded session server and supports up to 32 connected
clients. Use **Start Hosting**, **Copy Invitation**, **Join Session**, and
**Leave Session** to manage a session. Full formatted RTF snapshots, including
WordCraft comments, paper size, and page margins, are synchronized between the
host and clients.

The **Live Sharing...** window lets the host choose the address placed in the
invitation and the TCP listen port. Port `0` asks Windows to choose an available
port automatically. For a routed session, enter the LAN, mesh-VPN, or public
address that recipients can actually reach and a fixed port that is allowed by
the host firewall and forwarded by the router when applicable.

Sharing works on a LAN without additional infrastructure. Across the Internet,
the host must be reachable through router port forwarding or a private mesh
VPN; Windows Firewall may also ask for permission when hosting for the first
time. The invitation's bearer token authenticates clients over raw TCP but does
not encrypt the connection, so use live sharing only on a trusted LAN or through
an encrypted private VPN.

The host is the document authority. WordCraft serializes whole-document
revisions as edits arrive; this is not a conflict-free OT or CRDT engine, so
simultaneous edits to the same content can overwrite a competing revision.
WordCraft retries transient snapshot-queue failures and makes a bounded attempt
to flush a final pending edit when a participant leaves or exits.
Authenticated connections exchange lightweight heartbeats so abandoned peers
do not occupy one of the 32 client slots indefinitely.

## Build

The default Makefile uses LLVM for Windows (`clang`, `clang++`, `lld-link`, and
`llvm-rc`) plus the Windows SDK:

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

The probes cover large text extraction (beyond 64K), CRLF export, Unicode RTF
character and paragraph formatting round trips, grouped Replace All undo
behavior, whole-word matching, the RichEdit wrapping behavior used by the paged
layout, all 130 requested font-catalog entries, conservative typo detection,
phrase/context completion, prefix completion, language-engine bounds checks,
advanced line breaking, multilingual text-state preservation, and Word-like
default paragraph metrics. The paper probe verifies all 24 named presets and
Custom, catalog dimensions and Windows paper identifiers, size matching,
custom bounds and printable-area validation, and printer `DEVMODE` seeding.
The renderer probe additionally captures real `WM_PRINTCLIENT` pixels and
checks DirectWrite property bits, Unicode glyph output, Times New Roman 12 pt,
typing, selection, undo/redo, hit testing, wrapping, pagination, notification
payloads, and nondestructive painting. It runs once through DirectWrite and
again through the forced GDI fallback.

`make gui-test` additionally launches WordCraft in hidden windows and checks
UTF-8 command-line opening, the Times New Roman 12 pt default, the real
format-bar alignment and bullet controls, RTF paragraph-format saves, one-page
and multi-page pagination, continuous top-to-bottom scrolling, simultaneous
page visibility, selection preservation while scrolling, high-resolution wheel
input, smooth-scroll frame pacing and event coalescing, forward and reverse
cross-page selection state, caret-to-page tracking, dark-mode toggling without
document or typography changes, both worker threads, asynchronous spell/completion results,
Tab acceptance, one-step completion undo, feature toggles, ordinary Tab
behavior, UTF-8 saving, and clean shutdown.
The Home-ribbon portion verifies all five group boundaries, every command's
group assignment, keyboard entry and traversal, full/gallery, compact/combo,
and minimum-width collapsed layouts, font growth and shrinking, scripts,
highlighting, numbering, line spacing, and the Normal/Heading style bundles.
It also checks partial-paragraph style expansion, selection preservation, and
single-step undo for each combined character-and-paragraph style transaction.
The GUI probe also verifies the one-inch live page geometry, side-by-side
comment geometry, temporary non-persistent yellow anchors, card clicking,
multi-page anchoring, dark-mode persistence, rail collapse, and RTF reopen.
It also selects paper formats through the Layout ribbon and verifies that the
stored dimensions, DPI-scaled page geometry, pagination, selection, and
current-session state remain synchronized.
The GUI coverage deterministically verifies the rounded ribbon card, buttons,
and tabs, the persistent toolbar dog badge, and the DPI-scaled static curves
around the logo. These checks run in both normal and forced-GDI configurations
so the application chrome remains consistent independently of the document
renderer.

The live-network probe exercises invitation parsing, token authentication,
partial framing, the 32-client limit, stale revisions, DNS cancellation,
bounded final-edit flushing, heartbeat eviction, and repeated shutdown. The
two-process GUI probe verifies formatted RTF, comments, paper layout,
bidirectional convergence, no update echo, immediate Leave during the debounce
window, invalid-invitation safety, and clean process exit.
It also exercises the real visible startup transition: the branded splash must
be painted with a Ready status while the initialized editor is still hidden,
then close before the editor becomes interactive. This check runs through both
the normal renderer and its forced-GDI fallback; ordinary hidden GUI probes
remain splash-free. The probe deterministically walks all eight chew frames,
compares their rendered pixels, verifies the cycle wraps, and confirms the
splash animation runs on a UI thread separate from the editor.

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

Use `.rtf` to preserve the formatting and WordCraft comments that WordCraft
supports. Plain-text saves intentionally contain only text and warn before
comment metadata is discarded. `.doc` and `.docx` are not supported: those
formats require a separate binary/OOXML document engine and are not handled by
the Windows Rich Edit control.

Comments are stored in an ignored, versioned WordCraft RTF destination. Other
RTF readers can safely ignore it, but these comments are not Microsoft Word's
native comment format and another editor may discard the metadata when it
re-saves the file.

WordCraft does not redistribute font files. Many catalog families are supplied
by Windows or Microsoft Office, while others are separately licensed products.
Install a typeface through Windows before selecting it; the font list refreshes
automatically when Windows broadcasts a font-change notification.

Text input accepts UTF-8 (with or without a BOM) and BOM-marked UTF-16 little- or big-endian files. Text output is UTF-8 with a BOM and Windows CRLF line endings. Files containing embedded NUL characters or invalid Unicode are rejected instead of being silently truncated.

WordCraft checks whether an opened file changed on disk before overwriting it.
It does not currently provide autosave, crash recovery, cloud-hosted storage,
tracked changes, macros, or a Word-compatible `.docx` layout engine. Printer,
paper size (including Custom), orientation, and margin choices are retained for
the current application session: New and Open keep the active choices while
WordCraft is running, and live sessions synchronize them, but RTF and
plain-text files do not store them.

The source targets Windows 7 or newer. Windows 8 and later can use the operating system's installed spell checker for the user's locale (falling back to US English when available); Windows 7 and systems without a supported spell service retain the deliberately conservative built-in typo checks. Inline autocomplete is local and deterministic: it does not send document text to a network service. The output architecture follows the selected compiler; the default LLVM installation in this workspace produces x64 Windows binaries.

## Source layout

- `src/main.c` — application window, toolbars, status, commands, and layout
- `src/document.c` — Unicode/RTF I/O and the safe document lifecycle
- `src/format.c` — character and paragraph formatting
- `src/fonts.c` — requested font catalog and installed-font discovery
- `src/ribbon.c` — tabbed ribbon navigation and command panels
- `src/comments.c` — tracked comment anchors, side-page cards, and RTF metadata
- `src/textengine.c` — advanced line formatting and document typography policy
- `src/rendereditor.cpp` — C-callable windowless RichEdit host, live
  DirectWrite renderer, GDI fallback, and pagination-format mirror
- `src/pageview.c` — paper-page layout, scrolling, navigation, and pagination
- `src/paper.c` — named paper catalog, Custom validation, and printer settings
- `include/paper.h` — paper-size model and catalog interface
- `src/assist.c` — background spelling/completion workers and UI-thread overlays
- `src/language.c` — local typo fallback and deterministic completion engine
- `src/live.c` — authenticated, framed, host-authoritative WinSock transport
- `src/liveui.c` — session dialog, document bridge, debounce, and status UI
- `src/dialogs.c` — find/replace, date/time, and About dialogs
- `src/printing.c` — page setup and paginated printing
- `tests/language_probe.c` — typo and completion engine regression coverage
- `tests/font_probe.c` — requested catalog and font-list regression coverage
- `tests/comment_probe.c` — tracked-anchor and comment-metadata regression coverage
- `tests/paper_probe.c` — paper catalog, validation, and `DEVMODE` coverage
- `tests/textengine_probe.c` — typography activation and paragraph-metric coverage
- `tests/renderer_probe.c` — live DirectWrite/GDI pixel and editing parity checks
- `tests/live_probe.c` — protocol, capacity, heartbeat, and shutdown coverage
- `tests/live_gui_probe.c` — two-process document synchronization coverage
- `resources/app.rc` — icon, menus, shortcuts, and Windows manifest
- `resources/wordcraft.ico` — multi-size Windows application icon
- `resources/wordcraft-logo.png` — full-resolution dog-and-homework brand artwork
