#ifndef WORDCRAFT_RENDER_EDITOR_H
#define WORDCRAFT_RENDER_EDITOR_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define WORDCRAFT_RENDER_EDITOR_CLASS L"WordCraftRenderEditor"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Registers the HWND shell which hosts one windowless RichEdit text-services
 * object.  The shell exposes the normal RichEdit message surface while the
 * renderer paints through Direct2D/DirectWrite when the platform supports it.
 */
BOOL render_editor_register(HINSTANCE instance, HMODULE richEditModule);

/*
 * Sets the whitespace between the renderer's client rectangle and its text
 * formatting rectangle.  ITextHost defines these four independent inset
 * values in HIMETRIC units (0.01 millimeter), not as absolute coordinates.
 */
BOOL render_editor_set_view_insets(HWND editor, const RECT *insets);
BOOL render_editor_get_view_insets(HWND editor, RECT *insets);

/* Returns renderer diagnostics for the WCQ_RENDER_* query identifiers. */
LRESULT render_editor_query_state(HWND editor, UINT query);

/* True only for a live WordCraft renderer HWND. */
BOOL render_editor_is_window(HWND editor);

/*
 * Enables inert RTF picture objects on an HWND-backed RichEdit control while
 * rejecting active OLE classes.  The control owns the callback after success.
 */
BOOL render_editor_install_static_picture_callback(HWND richEdit);
BOOL render_editor_begin_static_picture_stream(HWND editor);
BOOL render_editor_end_static_picture_stream(HWND editor, DWORD *error);

#ifdef __cplusplus
}
#endif

#endif
