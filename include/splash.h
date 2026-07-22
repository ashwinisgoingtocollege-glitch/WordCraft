#ifndef WORDCRAFT_SPLASH_H
#define WORDCRAFT_SPLASH_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define APP_SPLASH_CLASS_NAME L"WordCraftSplashWindow"

/* Scalar-only messages used by the deterministic startup animation probe. */
#define WCM_SPLASH_QUERY_ANIMATION (WM_APP + 0x533)
#define WCM_SPLASH_STEP_ANIMATION (WM_APP + 0x534)
#define WCM_SPLASH_RESUME_ANIMATION (WM_APP + 0x535)
#define WCSQ_ANIMATION_FRAME 1
#define WCSQ_ANIMATION_FRAME_COUNT 2
#define WCSQ_ANIMATION_MOTION_ENABLED 3
#define WCSQ_ANIMATION_TIMER_ACTIVE 4
#define WCSQ_ANIMATION_PAINT_COUNT 5
#define WCSQ_ANIMATION_PROBE_HOLD 6
#define WCSQ_ANIMATION_TIMER_TICK_COUNT 7
#define WCSQ_WINDOW_CORNER_RADIUS_PIXELS 8
#define WCSQ_LOGO_ACCENT_CURVE_COUNT 9
#define WCSQ_LOGO_ACCENT_PAINT_COUNT 10
#define WORDCRAFT_SPLASH_ANIMATION_FRAME_COUNT 8

typedef struct WordcraftSplash WordcraftSplash;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Creates and shows WordCraft's non-activating startup window on a small,
 * dedicated UI thread so its decorative dog animation remains responsive
 * while the editor initializes. The returned controller stays valid until
 * wordcraft_splash_close, which releases it.
 */
WordcraftSplash *wordcraft_splash_show(HINSTANCE instance, HWND owner,
                                       LPCWSTR initialStatus);

/* Replaces the status line and paints it immediately. */
BOOL wordcraft_splash_set_status(WordcraftSplash *splash, LPCWSTR status);

/*
 * Runs a short fade when fade is TRUE, or closes immediately otherwise. This
 * joins the splash thread and releases the controller; do not reuse it.
 */
BOOL wordcraft_splash_close(WordcraftSplash *splash, BOOL fade);

/* Returns TRUE only for a live window created by this module. */
BOOL wordcraft_splash_is_window(const WordcraftSplash *splash);

#ifdef __cplusplus
}
#endif

#endif
