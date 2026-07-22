#include "editor.h"
#include "live.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define LIVE_SHARE_DEBOUNCE_MS 220u
#define LIVE_SHARE_RETRY_MS 500u
#define LIVE_SHARE_NOTICE_MS 6000u
#define LIVE_SHARE_LEAVE_WAIT_MS 1500u
#define LIVE_SHARE_DOCUMENT_HEADER_SIZE 48u
#define LIVE_SHARE_AUTO_NONE 0
#define LIVE_SHARE_AUTO_HOST 1
#define LIVE_SHARE_AUTO_JOIN 2

struct LiveShareContext {
    LiveContext *transport;
    HWND dialog;
    BOOL applyingRemote;
    BOOL documentPending;
    BOOL waitingForCanonical;
    BOOL awaitingInitialSnapshot;
    BOOL editorLockedForJoin;
    BOOL sendFailureRetryable;
    ULONGLONG appliedCount;
    int autoAction;
    uint16_t autoPort;
    BOOL autoTokenValid;
    uint8_t autoToken[LIVE_TOKEN_SIZE];
    char autoAdvertisedHost[LIVE_MAX_HOST_LENGTH + 1u];
    char autoInvitation[LIVE_INVITATION_CAPACITY];
    WCHAR notice[256];
    ULONGLONG noticeUntil;
};

static const BYTE liveShareDocumentMagic[8] = {
    'W', 'C', 'L', 'I', 'V', 'E', '1', 0
};

static void live_share_write_u32(BYTE *destination, uint32_t value)
{
    destination[0] = (BYTE)(value >> 24);
    destination[1] = (BYTE)(value >> 16);
    destination[2] = (BYTE)(value >> 8);
    destination[3] = (BYTE)value;
}

static uint32_t live_share_read_u32(const BYTE *source)
{
    return ((uint32_t)source[0] << 24) |
           ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) |
           (uint32_t)source[3];
}

static BOOL live_share_layout_is_valid(PaperSizeId id, LONG width, LONG height,
                                       const RECT *margins)
{
    const PaperSizePreset *preset;

    if (margins == NULL || id < 0 || id >= PAPER_SIZE_COUNT ||
        margins->left < 0 || margins->top < 0 || margins->right < 0 ||
        margins->bottom < 0 ||
        !paper_size_validate_dimensions(width, height, margins)) {
        return FALSE;
    }
    preset = paper_size_by_id(id);
    return preset != NULL &&
           (id == PAPER_SIZE_CUSTOM ||
            (preset->widthThousandths == width &&
             preset->heightThousandths == height));
}

static BOOL live_share_capture_document(AppState *app, BYTE **data,
                                        SIZE_T *size, DWORD *error)
{
    BYTE *rtf = NULL;
    SIZE_T rtfSize = 0u;
    BYTE *result;
    SIZE_T total;

    if (error != NULL) {
        *error = ERROR_SUCCESS;
    }
    if (app == NULL || data == NULL || size == NULL) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return FALSE;
    }
    *data = NULL;
    *size = 0u;
    if (!live_share_layout_is_valid(app->paperSizeId, app->pageSize.x,
                                    app->pageSize.y, &app->pageMargins)) {
        if (error != NULL) {
            *error = ERROR_INVALID_DATA;
        }
        return FALSE;
    }
    if (!document_capture_live_snapshot(app, &rtf, &rtfSize, error)) {
        return FALSE;
    }
    if (!document_validate_live_snapshot(rtf, rtfSize, error)) {
        HeapFree(GetProcessHeap(), 0, rtf);
        return FALSE;
    }
    if (rtfSize == 0u ||
        rtfSize > LIVE_MAX_DOCUMENT_SIZE - LIVE_SHARE_DOCUMENT_HEADER_SIZE) {
        HeapFree(GetProcessHeap(), 0, rtf);
        if (error != NULL) {
            *error = ERROR_FILE_TOO_LARGE;
        }
        return FALSE;
    }
    total = LIVE_SHARE_DOCUMENT_HEADER_SIZE + rtfSize;
    result = HeapAlloc(GetProcessHeap(), 0, total);
    if (result == NULL) {
        HeapFree(GetProcessHeap(), 0, rtf);
        if (error != NULL) {
            *error = ERROR_NOT_ENOUGH_MEMORY;
        }
        return FALSE;
    }
    CopyMemory(result, liveShareDocumentMagic, sizeof(liveShareDocumentMagic));
    live_share_write_u32(result + 8, LIVE_SHARE_DOCUMENT_HEADER_SIZE);
    live_share_write_u32(result + 12, (uint32_t)rtfSize);
    live_share_write_u32(result + 16, (uint32_t)app->paperSizeId);
    live_share_write_u32(result + 20, (uint32_t)app->pageSize.x);
    live_share_write_u32(result + 24, (uint32_t)app->pageSize.y);
    live_share_write_u32(result + 28, (uint32_t)app->pageMargins.left);
    live_share_write_u32(result + 32, (uint32_t)app->pageMargins.top);
    live_share_write_u32(result + 36, (uint32_t)app->pageMargins.right);
    live_share_write_u32(result + 40, (uint32_t)app->pageMargins.bottom);
    live_share_write_u32(result + 44, 0u);
    CopyMemory(result + LIVE_SHARE_DOCUMENT_HEADER_SIZE, rtf, rtfSize);
    HeapFree(GetProcessHeap(), 0, rtf);
    *data = result;
    *size = total;
    return TRUE;
}

static BOOL live_share_apply_document(AppState *app, const BYTE *data,
                                      SIZE_T size, DWORD *error)
{
    const BYTE *rtf = data;
    SIZE_T rtfSize = size;
    RECT margins;
    PaperSizeId id;
    LONG width;
    LONG height;
    BOOL hasLayout = FALSE;
    uint32_t value;

    if (error != NULL) {
        *error = ERROR_SUCCESS;
    }
    if (app == NULL || data == NULL || size == 0u) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return FALSE;
    }
    if (size >= LIVE_SHARE_DOCUMENT_HEADER_SIZE &&
        memcmp(data, liveShareDocumentMagic, sizeof(liveShareDocumentMagic)) == 0) {
        uint32_t headerSize = live_share_read_u32(data + 8);
        uint32_t encodedRtfSize = live_share_read_u32(data + 12);
        uint32_t encodedId = live_share_read_u32(data + 16);

        if (headerSize != LIVE_SHARE_DOCUMENT_HEADER_SIZE ||
            encodedRtfSize == 0u ||
            (SIZE_T)encodedRtfSize != size - LIVE_SHARE_DOCUMENT_HEADER_SIZE ||
            encodedId >= PAPER_SIZE_COUNT ||
            live_share_read_u32(data + 44) != 0u) {
            if (error != NULL) {
                *error = ERROR_INVALID_DATA;
            }
            return FALSE;
        }
        value = live_share_read_u32(data + 20);
        if (value > LONG_MAX) {
            goto invalid_layout;
        }
        width = (LONG)value;
        value = live_share_read_u32(data + 24);
        if (value > LONG_MAX) {
            goto invalid_layout;
        }
        height = (LONG)value;
        value = live_share_read_u32(data + 28);
        if (value > LONG_MAX) {
            goto invalid_layout;
        }
        margins.left = (LONG)value;
        value = live_share_read_u32(data + 32);
        if (value > LONG_MAX) {
            goto invalid_layout;
        }
        margins.top = (LONG)value;
        value = live_share_read_u32(data + 36);
        if (value > LONG_MAX) {
            goto invalid_layout;
        }
        margins.right = (LONG)value;
        value = live_share_read_u32(data + 40);
        if (value > LONG_MAX) {
            goto invalid_layout;
        }
        margins.bottom = (LONG)value;
        id = (PaperSizeId)encodedId;
        if (!live_share_layout_is_valid(id, width, height, &margins)) {
            goto invalid_layout;
        }
        rtf = data + LIVE_SHARE_DOCUMENT_HEADER_SIZE;
        rtfSize = encodedRtfSize;
        hasLayout = TRUE;
    }
    if (!document_apply_live_snapshot(app, rtf, rtfSize, error)) {
        return FALSE;
    }
    if (hasLayout &&
        !paper_size_apply_shared_layout(app, id, width, height, &margins)) {
        if (error != NULL) {
            *error = GetLastError();
        }
        return FALSE;
    }
    return TRUE;

invalid_layout:
    if (error != NULL) {
        *error = ERROR_INVALID_DATA;
    }
    return FALSE;
}

static BOOL live_share_valid(const AppState *app)
{
    return app != NULL && app->liveShare != NULL &&
           app->liveShare->transport != NULL;
}

static void live_share_set_notice(AppState *app, const WCHAR *message)
{
    LiveShareContext *share;

    if (app == NULL || app->liveShare == NULL) {
        return;
    }
    share = app->liveShare;
    if (message == NULL || message[0] == L'\0') {
        share->notice[0] = L'\0';
        share->noticeUntil = 0u;
        return;
    }
    StringCchCopyW(share->notice, ARRAYSIZE(share->notice), message);
    share->noticeUntil = GetTickCount64() + LIVE_SHARE_NOTICE_MS;
}

void live_share_apply_status_notice(AppState *app)
{
    LiveShareContext *share;

    if (app == NULL || app->liveShare == NULL) {
        return;
    }
    share = app->liveShare;
    if (share->notice[0] == L'\0') {
        return;
    }
    if (share->noticeUntil != 0u && GetTickCount64() > share->noticeUntil) {
        share->notice[0] = L'\0';
        share->noticeUntil = 0u;
        return;
    }
    app_set_status_message(app, share->notice);
}

static BOOL live_share_utf8_to_wide(const char *source, WCHAR *destination,
                                    size_t capacity)
{
    int count;

    if (source == NULL || destination == NULL || capacity == 0 ||
        capacity > INT_MAX) {
        return FALSE;
    }
    destination[0] = L'\0';
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, -1,
                                destination, (int)capacity);
    if (count <= 0) {
        count = MultiByteToWideChar(CP_ACP, 0, source, -1, destination,
                                    (int)capacity);
    }
    return count > 0;
}

static BOOL live_share_wide_to_utf8(const WCHAR *source, char *destination,
                                    size_t capacity)
{
    int count;

    if (source == NULL || destination == NULL || capacity == 0 ||
        capacity > INT_MAX) {
        return FALSE;
    }
    destination[0] = '\0';
    count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source, -1,
                                destination, (int)capacity, NULL, NULL);
    return count > 0;
}

static void live_share_copy_ascii(char *destination, size_t capacity,
                                  const char *source)
{
    size_t length;

    if (destination == NULL || capacity == 0) {
        return;
    }
    destination[0] = '\0';
    if (source == NULL) {
        return;
    }
    length = strlen(source);
    if (length >= capacity) {
        length = capacity - 1;
    }
    if (length > 0) {
        CopyMemory(destination, source, length);
    }
    destination[length] = '\0';
}

static int live_share_hex_value(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static BOOL live_share_parse_token(const char *text,
                                   uint8_t token[LIVE_TOKEN_SIZE])
{
    size_t index;

    if (text == NULL || strlen(text) != LIVE_TOKEN_HEX_LENGTH) {
        return FALSE;
    }
    for (index = 0; index < LIVE_TOKEN_SIZE; ++index) {
        int high = live_share_hex_value(text[index * 2]);
        int low = live_share_hex_value(text[index * 2 + 1]);
        if (high < 0 || low < 0) {
            ZeroMemory(token, LIVE_TOKEN_SIZE);
            return FALSE;
        }
        token[index] = (uint8_t)((high << 4) | low);
    }
    return TRUE;
}

static BOOL live_share_status(const AppState *app, LiveStatus *status)
{
    if (status == NULL) {
        return FALSE;
    }
    ZeroMemory(status, sizeof(*status));
    return live_share_valid(app) &&
           live_get_status(app->liveShare->transport, status);
}

static void live_share_unlock_editor(AppState *app)
{
    if (app != NULL && app->liveShare != NULL &&
        app->liveShare->editorLockedForJoin && app->editor != NULL) {
        SendMessageW(app->editor, EM_SETREADONLY, FALSE, 0);
        app->liveShare->editorLockedForJoin = FALSE;
    }
}

static void live_share_format_status(const LiveStatus *status, WCHAR *text,
                                     size_t capacity)
{
    WCHAR detail[128];

    if (text == NULL || capacity == 0) {
        return;
    }
    text[0] = L'\0';
    if (status == NULL || status->role == LIVE_ROLE_NONE) {
        StringCchCopyW(text, capacity, L"Not sharing");
        return;
    }
    if (status->role == LIVE_ROLE_HOST &&
        status->state == LIVE_STATE_LISTENING) {
        StringCchPrintfW(text, capacity,
                         L"Hosting - %lu of %u clients - revision %llu",
                         (unsigned long)status->client_count,
                         (unsigned)LIVE_MAX_CLIENTS,
                         (unsigned long long)status->revision);
        return;
    }
    if (status->role == LIVE_ROLE_HOST) {
        if (status->state == LIVE_STATE_STARTING) {
            StringCchCopyW(text, capacity, L"Starting the host...");
        } else if (status->state == LIVE_STATE_ERROR) {
            if (live_share_utf8_to_wide(status->error_message, detail,
                                        ARRAYSIZE(detail)) &&
                detail[0] != L'\0') {
                StringCchPrintfW(text, capacity, L"Host error: %s", detail);
            } else {
                StringCchCopyW(text, capacity, L"Host error");
            }
        } else {
            StringCchCopyW(text, capacity, L"Host is stopping...");
        }
        return;
    }
    switch (status->state) {
    case LIVE_STATE_STARTING:
    case LIVE_STATE_CONNECTING:
        StringCchCopyW(text, capacity, L"Connecting to the host...");
        break;
    case LIVE_STATE_CONNECTED:
        StringCchPrintfW(text, capacity, L"Connected - revision %llu",
                         (unsigned long long)status->revision);
        break;
    case LIVE_STATE_ERROR:
        if (live_share_utf8_to_wide(status->error_message, detail,
                                    ARRAYSIZE(detail)) && detail[0] != L'\0') {
            StringCchPrintfW(text, capacity, L"Connection error: %s", detail);
        } else {
            StringCchCopyW(text, capacity, L"Connection error");
        }
        break;
    default:
        StringCchCopyW(text, capacity, L"Session is stopping...");
        break;
    }
}

static void live_share_refresh_dialog(AppState *app)
{
    LiveShareContext *share;
    LiveStatus status;
    WCHAR statusText[192];
    WCHAR invitation[LIVE_INVITATION_CAPACITY];
    BOOL inactive;

    if (!live_share_valid(app) || app->liveShare->dialog == NULL ||
        !IsWindow(app->liveShare->dialog)) {
        return;
    }
    share = app->liveShare;
    if (!live_share_status(app, &status)) {
        ZeroMemory(&status, sizeof(status));
    }
    live_share_format_status(&status, statusText, ARRAYSIZE(statusText));
    if (share->notice[0] != L'\0' &&
        (share->noticeUntil == 0u ||
         GetTickCount64() <= share->noticeUntil)) {
        StringCchCopyW(statusText, ARRAYSIZE(statusText), share->notice);
    }
    SetDlgItemTextW(share->dialog, IDC_LIVE_STATUS, statusText);
    invitation[0] = L'\0';
    if (status.role == LIVE_ROLE_HOST &&
        status.state == LIVE_STATE_LISTENING) {
        (void)live_share_utf8_to_wide(status.invitation, invitation,
                                      ARRAYSIZE(invitation));
    }
    SetDlgItemTextW(share->dialog, IDC_LIVE_HOST_INVITATION, invitation);

    inactive = status.role == LIVE_ROLE_NONE && !status.worker_running;
    EnableWindow(GetDlgItem(share->dialog, IDC_LIVE_START_HOST), inactive);
    EnableWindow(GetDlgItem(share->dialog, IDC_LIVE_ADVERTISED_HOST), inactive);
    EnableWindow(GetDlgItem(share->dialog, IDC_LIVE_LISTEN_PORT), inactive);
    EnableWindow(GetDlgItem(share->dialog, IDC_LIVE_JOIN_SESSION), inactive);
    EnableWindow(GetDlgItem(share->dialog, IDC_LIVE_JOIN_INVITATION), inactive);
    EnableWindow(GetDlgItem(share->dialog, IDC_LIVE_COPY_INVITATION),
                 status.role == LIVE_ROLE_HOST &&
                     status.state == LIVE_STATE_LISTENING &&
                     status.invitation[0] != '\0');
    EnableWindow(GetDlgItem(share->dialog, IDC_LIVE_LEAVE_SESSION),
                 status.role != LIVE_ROLE_NONE || status.worker_running);
}

static void live_share_refresh_application(AppState *app)
{
    if (app == NULL || app->mainWindow == NULL) {
        return;
    }
    live_share_refresh_dialog(app);
    app_update_command_ui(app);
    app_update_status(app, FALSE);
}

static void live_share_describe_transport_error(AppState *app,
                                                const WCHAR *action)
{
    LiveStatus status;
    WCHAR detail[256];
    WCHAR message[512];

    if (!live_share_status(app, &status)) {
        MessageBoxW(app->mainWindow, L"The live sharing operation failed.",
                    APP_NAME, MB_OK | MB_ICONERROR);
        return;
    }
    detail[0] = L'\0';
    (void)live_share_utf8_to_wide(status.error_message, detail,
                                  ARRAYSIZE(detail));
    if (detail[0] == L'\0') {
        StringCchCopyW(detail, ARRAYSIZE(detail), L"The network operation failed.");
    }
    StringCchPrintfW(message, ARRAYSIZE(message), L"%s\n\n%s", action, detail);
    MessageBoxW(app->mainWindow, message, APP_NAME, MB_OK | MB_ICONERROR);
}

static BOOL live_share_get_computer_name(char *host, size_t capacity)
{
    WCHAR wideName[256];
    DWORD count = ARRAYSIZE(wideName);

    if (host == NULL || capacity == 0) {
        return FALSE;
    }
    host[0] = '\0';
    if (!GetComputerNameW(wideName, &count) || count == 0) {
        return FALSE;
    }
    return live_share_wide_to_utf8(wideName, host, capacity);
}

static BOOL live_share_get_display_name(char *name, size_t capacity)
{
    WCHAR wideName[128];
    DWORD count;

    if (name == NULL || capacity == 0) {
        return FALSE;
    }
    name[0] = '\0';
    count = GetEnvironmentVariableW(L"USERNAME", wideName,
                                    ARRAYSIZE(wideName));
    if (count == 0 || count >= ARRAYSIZE(wideName) ||
        !live_share_wide_to_utf8(wideName, name, capacity)) {
        live_share_copy_ascii(name, capacity, "WordCraft guest");
    }
    return TRUE;
}

static BOOL live_share_copy_invitation(AppState *app, BOOL showFeedback)
{
    LiveStatus status;
    WCHAR invitation[LIVE_INVITATION_CAPACITY];
    SIZE_T bytes;
    HGLOBAL memory;
    WCHAR *clipboardText;

    if (!live_share_status(app, &status) || status.role != LIVE_ROLE_HOST ||
        status.state != LIVE_STATE_LISTENING || !status.worker_running ||
        status.invitation[0] == '\0' ||
        !live_share_utf8_to_wide(status.invitation, invitation,
                                 ARRAYSIZE(invitation))) {
        MessageBeep(MB_ICONWARNING);
        live_share_set_notice(app,
                              L"Start hosting before copying an invitation");
        live_share_refresh_application(app);
        return FALSE;
    }
    bytes = (wcslen(invitation) + 1) * sizeof(WCHAR);
    memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory == NULL) {
        app_show_error(app->mainWindow, L"The invitation could not be copied.",
                       ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    clipboardText = (WCHAR *)GlobalLock(memory);
    if (clipboardText == NULL) {
        DWORD error = GetLastError();
        GlobalFree(memory);
        app_show_error(app->mainWindow, L"The invitation could not be copied.", error);
        return FALSE;
    }
    CopyMemory(clipboardText, invitation, bytes);
    GlobalUnlock(memory);
    if (!OpenClipboard(app->mainWindow)) {
        DWORD error = GetLastError();
        GlobalFree(memory);
        app_show_error(app->mainWindow, L"The invitation could not be copied.", error);
        return FALSE;
    }
    EmptyClipboard();
    if (SetClipboardData(CF_UNICODETEXT, memory) == NULL) {
        DWORD error = GetLastError();
        CloseClipboard();
        GlobalFree(memory);
        app_show_error(app->mainWindow, L"The invitation could not be copied.", error);
        return FALSE;
    }
    CloseClipboard();
    live_share_set_notice(app, L"Live invitation copied to the clipboard");
    live_share_refresh_application(app);
    if (showFeedback) {
        MessageBoxW(app->mainWindow,
                    L"The invitation was copied. Send it only through a trusted "
                    L"channel and use a trusted LAN or private VPN.",
                    L"WordCraft Live Sharing", MB_OK | MB_ICONINFORMATION);
    }
    return TRUE;
}

static BOOL live_share_start_host(AppState *app, uint16_t port,
                                  const char *advertisedHost,
                                  const uint8_t token[LIVE_TOKEN_SIZE],
                                  BOOL showFeedback)
{
    LiveStatus status;
    BYTE *snapshot = NULL;
    SIZE_T snapshotSize = 0;
    DWORD error = ERROR_SUCCESS;
    char host[LIVE_MAX_HOST_LENGTH + 1u];
    BOOL started;

    if (!live_share_valid(app) || !live_share_status(app, &status) ||
        status.role != LIVE_ROLE_NONE || status.worker_running) {
        MessageBeep(MB_ICONWARNING);
        return FALSE;
    }
    if (!live_share_capture_document(app, &snapshot, &snapshotSize, &error)) {
        if (error == ERROR_INVALID_DATA) {
            MessageBoxW(
                app->mainWindow,
                L"This document contains embedded or active RTF content that "
                L"WordCraft does not send to live-session peers. Remove objects, "
                L"pictures, fields, links, or embedded fonts before hosting.",
                APP_NAME, MB_OK | MB_ICONWARNING);
        } else {
            app_show_error(
                app->mainWindow,
                L"The document could not be prepared for live sharing.", error);
        }
        return FALSE;
    }
    host[0] = '\0';
    if (advertisedHost != NULL && advertisedHost[0] != '\0') {
        live_share_copy_ascii(host, ARRAYSIZE(host), advertisedHost);
    } else if (!live_share_get_computer_name(host, ARRAYSIZE(host))) {
        live_share_copy_ascii(host, ARRAYSIZE(host), "localhost");
    }
    started = live_host_start(app->liveShare->transport, port, host, token,
                              snapshot, snapshotSize, 1);
    HeapFree(GetProcessHeap(), 0, snapshot);
    if (!started) {
        live_share_describe_transport_error(
            app, L"WordCraft could not start the live session.");
        live_share_refresh_application(app);
        return FALSE;
    }
    app->liveShare->documentPending = FALSE;
    app->liveShare->waitingForCanonical = FALSE;
    app->liveShare->awaitingInitialSnapshot = FALSE;
    live_share_set_notice(app, L"Live session started");
    live_share_refresh_application(app);
    if (showFeedback) {
        (void)live_share_copy_invitation(app, TRUE);
    }
    return TRUE;
}

static BOOL live_share_join(AppState *app, const WCHAR *wideInvitation,
                            BOOL promptToSave)
{
    LiveStatus status;
    char invitation[LIVE_INVITATION_CAPACITY];
    char displayName[LIVE_MAX_DISPLAY_NAME + 1u];
    char parsedHost[LIVE_MAX_HOST_LENGTH + 1u];
    uint16_t parsedPort = 0u;
    uint8_t parsedToken[LIVE_TOKEN_SIZE];

    if (!live_share_valid(app) || wideInvitation == NULL ||
        wideInvitation[0] == L'\0' || !live_share_status(app, &status) ||
        status.role != LIVE_ROLE_NONE || status.worker_running) {
        MessageBeep(MB_ICONWARNING);
        return FALSE;
    }
    if (!live_share_wide_to_utf8(wideInvitation, invitation,
                                 ARRAYSIZE(invitation)) ||
        !live_parse_invitation(invitation, parsedHost, ARRAYSIZE(parsedHost),
                               &parsedPort, parsedToken)) {
        SecureZeroMemory(parsedToken, sizeof(parsedToken));
        MessageBeep(MB_ICONWARNING);
        live_share_set_notice(app, L"That live invitation is not valid");
        live_share_refresh_application(app);
        return FALSE;
    }
    SecureZeroMemory(parsedToken, sizeof(parsedToken));
    if (promptToSave && !document_prompt_save(app)) {
        return FALSE;
    }
    live_share_get_display_name(displayName, ARRAYSIZE(displayName));
    if (!live_client_join(app->liveShare->transport, invitation, displayName)) {
        live_share_describe_transport_error(
            app, L"WordCraft could not join the live session.");
        live_share_refresh_application(app);
        return FALSE;
    }
    app->liveShare->awaitingInitialSnapshot = TRUE;
    app->liveShare->documentPending = FALSE;
    app->liveShare->waitingForCanonical = FALSE;
    SendMessageW(app->editor, EM_SETREADONLY, TRUE, 0);
    app->liveShare->editorLockedForJoin = TRUE;
    live_share_set_notice(app, L"Connecting to the live session...");
    live_share_refresh_application(app);
    return TRUE;
}

static BOOL live_share_publish_current(AppState *app)
{
    BYTE *snapshot = NULL;
    SIZE_T snapshotSize = 0;
    DWORD error = ERROR_SUCCESS;
    BOOL result;

    app->liveShare->sendFailureRetryable = TRUE;
    if (!live_share_capture_document(app, &snapshot, &snapshotSize, &error)) {
        app->liveShare->sendFailureRetryable =
            error != ERROR_INVALID_DATA && error != ERROR_FILE_TOO_LARGE;
        live_share_set_notice(
            app, error == ERROR_INVALID_DATA
                     ? L"Live sharing rejected unsupported embedded RTF content"
                     : error == ERROR_FILE_TOO_LARGE
                           ? L"The live document exceeds the 16 MiB limit"
                           : L"The live update could not be prepared");
        return FALSE;
    }
    result = live_host_publish(app->liveShare->transport, snapshot, snapshotSize);
    HeapFree(GetProcessHeap(), 0, snapshot);
    if (!result) {
        live_share_set_notice(app, L"The live update could not be sent");
    }
    return result;
}

static BOOL live_share_submit_current(AppState *app)
{
    LiveStatus status;
    BYTE *snapshot = NULL;
    SIZE_T snapshotSize = 0;
    DWORD error = ERROR_SUCCESS;
    BOOL result;

    app->liveShare->sendFailureRetryable = TRUE;
    if (!live_share_status(app, &status) || status.role != LIVE_ROLE_CLIENT ||
        status.state != LIVE_STATE_CONNECTED) {
        return FALSE;
    }
    if (!live_share_capture_document(app, &snapshot, &snapshotSize, &error)) {
        app->liveShare->sendFailureRetryable =
            error != ERROR_INVALID_DATA && error != ERROR_FILE_TOO_LARGE;
        live_share_set_notice(
            app, error == ERROR_INVALID_DATA
                     ? L"Live sharing rejected unsupported embedded RTF content"
                     : error == ERROR_FILE_TOO_LARGE
                           ? L"The live document exceeds the 16 MiB limit"
                           : L"The live update could not be prepared");
        return FALSE;
    }
    result = live_client_submit(app->liveShare->transport, snapshot,
                                snapshotSize, status.revision);
    HeapFree(GetProcessHeap(), 0, snapshot);
    if (result) {
        app->liveShare->waitingForCanonical = TRUE;
    } else {
        live_share_set_notice(app, L"The live update could not be sent");
    }
    return result;
}

static void live_share_schedule_retry(AppState *app)
{
    if (app != NULL && app->mainWindow != NULL && app->liveShare != NULL) {
        if (!app->liveShare->sendFailureRetryable) {
            app->liveShare->documentPending = FALSE;
            KillTimer(app->mainWindow, LIVE_SHARE_TIMER_ID);
            return;
        }
        app->liveShare->documentPending = TRUE;
        SetTimer(app->mainWindow, LIVE_SHARE_TIMER_ID,
                 LIVE_SHARE_RETRY_MS, NULL);
    }
}

static void live_share_handle_snapshot(AppState *app, const LiveEvent *event)
{
    LiveShareContext *share = app->liveShare;
    BYTE *deferred = NULL;
    SIZE_T deferredSize = 0;
    DWORD error = ERROR_SUCCESS;
    BOOL hadDeferred = FALSE;
    BOOL firstSnapshot;
    BOOL applied = FALSE;
    BOOL restoredDeferred = FALSE;

    if (event->data == NULL || event->data_size == 0) {
        return;
    }
    firstSnapshot = share->awaitingInitialSnapshot;
    if (!firstSnapshot && share->documentPending) {
        hadDeferred = live_share_capture_document(
            app, &deferred, &deferredSize, &error);
    }
    KillTimer(app->mainWindow, LIVE_SHARE_TIMER_ID);
    share->documentPending = FALSE;
    share->waitingForCanonical = FALSE;
    live_share_unlock_editor(app);
    share->applyingRemote = TRUE;
    if (live_share_apply_document(app, event->data, event->data_size,
                                  &error)) {
        applied = TRUE;
        ++share->appliedCount;
        if (firstSnapshot) {
            app->currentPath[0] = L'\0';
            ZeroMemory(&app->fileIdentity, sizeof(app->fileIdentity));
            app->currentIsRtf = TRUE;
            document_update_title(app);
            share->awaitingInitialSnapshot = FALSE;
        }
        live_share_set_notice(app, L"Live document updated");
    } else {
        live_share_set_notice(
            app, L"An invalid live document was rejected; the session ended");
        share->awaitingInitialSnapshot = FALSE;
        share->documentPending = FALSE;
        share->waitingForCanonical = FALSE;
    }

    if (applied && hadDeferred && deferred != NULL) {
        DWORD restoreError = ERROR_SUCCESS;
        restoredDeferred = live_share_apply_document(
            app, deferred, deferredSize, &restoreError);
        share->sendFailureRetryable = TRUE;
        if (restoredDeferred &&
            live_client_submit(share->transport, deferred, deferredSize,
                               event->revision)) {
            share->waitingForCanonical = TRUE;
            live_share_set_notice(
                app, L"Your newer edit was sent after a competing revision");
        } else if (restoredDeferred) {
            live_share_set_notice(app,
                                  L"Your newer live edit will be retried");
            live_share_schedule_retry(app);
        }
    }
    share->applyingRemote = FALSE;

    if (!applied) {
        live_stop(share->transport);
        live_share_unlock_editor(app);
    }
    if (deferred != NULL) {
        HeapFree(GetProcessHeap(), 0, deferred);
    }
}

static void live_share_handle_client_edit(AppState *app,
                                          const LiveEvent *event)
{
    LiveShareContext *share = app->liveShare;
    DWORD error = ERROR_SUCCESS;

    if (event->data == NULL || event->data_size == 0) {
        return;
    }
    if (event->stale || share->documentPending) {
        KillTimer(app->mainWindow, LIVE_SHARE_TIMER_ID);
        share->documentPending = FALSE;
        if (!live_share_publish_current(app)) {
            live_share_schedule_retry(app);
        } else {
            live_share_set_notice(
                app,
                L"A competing client revision was replaced by the host document");
        }
        return;
    }

    share->applyingRemote = TRUE;
    if (live_share_apply_document(app, event->data, event->data_size,
                                  &error)) {
        ++share->appliedCount;
        share->applyingRemote = FALSE;
        if (live_share_publish_current(app)) {
            live_share_set_notice(app, L"A client edit was synchronized");
        } else {
            live_share_schedule_retry(app);
        }
    } else {
        share->applyingRemote = FALSE;
        if (!live_share_publish_current(app)) {
            live_share_schedule_retry(app);
        } else {
            live_share_set_notice(app,
                                  L"A client sent an invalid document update");
        }
    }
}

static BOOL live_share_start_from_dialog(AppState *app, HWND dialog)
{
    WCHAR wideHost[LIVE_MAX_HOST_LENGTH + 1u];
    WCHAR portText[16];
    char host[LIVE_MAX_HOST_LENGTH + 1u];
    WCHAR *end = NULL;
    unsigned long port;
    size_t index;

    if (app == NULL || dialog == NULL) {
        return FALSE;
    }
    wideHost[0] = L'\0';
    portText[0] = L'\0';
    (void)GetDlgItemTextW(dialog, IDC_LIVE_ADVERTISED_HOST, wideHost,
                          ARRAYSIZE(wideHost));
    if (GetDlgItemTextW(dialog, IDC_LIVE_LISTEN_PORT, portText,
                        ARRAYSIZE(portText)) <= 0) {
        MessageBoxW(dialog, L"Enter a listen port from 0 through 65535.",
                    APP_NAME, MB_OK | MB_ICONWARNING);
        return FALSE;
    }
    for (index = 0; portText[index] != L'\0'; ++index) {
        if (portText[index] < L'0' || portText[index] > L'9') {
            MessageBoxW(dialog, L"Enter a listen port from 0 through 65535.",
                        APP_NAME, MB_OK | MB_ICONWARNING);
            return FALSE;
        }
    }
    port = wcstoul(portText, &end, 10);
    if (end == portText || *end != L'\0' || port > 65535ul) {
        MessageBoxW(dialog, L"Enter a listen port from 0 through 65535.",
                    APP_NAME, MB_OK | MB_ICONWARNING);
        return FALSE;
    }
    host[0] = '\0';
    if (wideHost[0] != L'\0' &&
        !live_share_wide_to_utf8(wideHost, host, ARRAYSIZE(host))) {
        MessageBoxW(dialog,
                    L"The advertised address is too long or is not valid text.",
                    APP_NAME, MB_OK | MB_ICONWARNING);
        return FALSE;
    }
    return live_share_start_host(app, (uint16_t)port,
                                 host[0] != '\0' ? host : NULL,
                                 NULL, FALSE);
}

static INT_PTR CALLBACK live_share_dialog_proc(HWND dialog, UINT message,
                                               WPARAM wParam, LPARAM lParam)
{
    AppState *app = (AppState *)GetWindowLongPtrW(dialog, DWLP_USER);

    switch (message) {
    case WM_INITDIALOG:
    {
        char host[LIVE_MAX_HOST_LENGTH + 1u];
        WCHAR wideHost[LIVE_MAX_HOST_LENGTH + 1u];

        app = (AppState *)lParam;
        SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)app);
        if (live_share_valid(app)) {
            app->liveShare->dialog = dialog;
        }
        SendDlgItemMessageW(dialog, IDC_LIVE_JOIN_INVITATION, EM_SETLIMITTEXT,
                            LIVE_INVITATION_CAPACITY - 1u, 0);
        SendDlgItemMessageW(dialog, IDC_LIVE_ADVERTISED_HOST, EM_SETLIMITTEXT,
                            LIVE_MAX_HOST_LENGTH, 0);
        SendDlgItemMessageW(dialog, IDC_LIVE_LISTEN_PORT, EM_SETLIMITTEXT,
                            5, 0);
        host[0] = '\0';
        wideHost[0] = L'\0';
        if (!live_share_get_computer_name(host, ARRAYSIZE(host))) {
            live_share_copy_ascii(host, ARRAYSIZE(host), "localhost");
        }
        if (live_share_utf8_to_wide(host, wideHost, ARRAYSIZE(wideHost))) {
            SetDlgItemTextW(dialog, IDC_LIVE_ADVERTISED_HOST, wideHost);
        }
        SetDlgItemTextW(dialog, IDC_LIVE_LISTEN_PORT, L"0");
        live_share_refresh_dialog(app);
        return TRUE;
    }
    case WM_COMMAND:
        if (!live_share_valid(app)) {
            break;
        }
        switch (LOWORD(wParam)) {
        case IDC_LIVE_START_HOST:
            (void)live_share_start_from_dialog(app, dialog);
            live_share_refresh_dialog(app);
            return TRUE;
        case IDC_LIVE_COPY_INVITATION:
            (void)live_share_copy_invitation(app, FALSE);
            return TRUE;
        case IDC_LIVE_JOIN_SESSION: {
            WCHAR invitation[LIVE_INVITATION_CAPACITY];
            if (GetDlgItemTextW(dialog, IDC_LIVE_JOIN_INVITATION,
                                invitation, ARRAYSIZE(invitation)) <= 0) {
                MessageBeep(MB_ICONWARNING);
                live_share_set_notice(app, L"Paste a live invitation first");
                live_share_refresh_application(app);
                return TRUE;
            }
            (void)live_share_join(app, invitation, TRUE);
            live_share_refresh_dialog(app);
            return TRUE;
        }
        case IDC_LIVE_LEAVE_SESSION:
            live_share_leave_command(app);
            live_share_refresh_dialog(app);
            return TRUE;
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        default:
            break;
        }
        break;
    case WM_DESTROY:
        if (live_share_valid(app) && app->liveShare->dialog == dialog) {
            app->liveShare->dialog = NULL;
        }
        break;
    default:
        break;
    }
    return FALSE;
}

static void live_share_load_auto_settings(AppState *app)
{
    LiveShareContext *share = app->liveShare;
    char value[1024];
    DWORD length;

    length = GetEnvironmentVariableA("WORDCRAFT_LIVE_TEST_MODE", value,
                                     ARRAYSIZE(value));
    if (length != 1u || value[0] != '1') {
        return;
    }

    length = GetEnvironmentVariableA("WORDCRAFT_LIVE_AUTOSTART_JOIN", value,
                                     ARRAYSIZE(value));
    if (length > 0 && length < ARRAYSIZE(value)) {
        share->autoAction = LIVE_SHARE_AUTO_JOIN;
        live_share_copy_ascii(share->autoInvitation,
                              ARRAYSIZE(share->autoInvitation), value);
        SetTimer(app->mainWindow, LIVE_SHARE_TIMER_ID, 1, NULL);
        return;
    }
    length = GetEnvironmentVariableA("WORDCRAFT_LIVE_AUTOSTART_HOST", value,
                                     ARRAYSIZE(value));
    if (length == 0 || length >= ARRAYSIZE(value) || value[0] == '0') {
        return;
    }
    share->autoAction = LIVE_SHARE_AUTO_HOST;
    length = GetEnvironmentVariableA("WORDCRAFT_LIVE_PORT", value,
                                     ARRAYSIZE(value));
    if (length > 0 && length < ARRAYSIZE(value)) {
        char *end = NULL;
        unsigned long port = strtoul(value, &end, 10);
        if (end != value && *end == '\0' && port <= 65535ul) {
            share->autoPort = (uint16_t)port;
        }
    }
    length = GetEnvironmentVariableA("WORDCRAFT_LIVE_TOKEN", value,
                                     ARRAYSIZE(value));
    if (length > 0 && length < ARRAYSIZE(value)) {
        share->autoTokenValid = live_share_parse_token(value, share->autoToken);
    }
    length = GetEnvironmentVariableA("WORDCRAFT_LIVE_ADVERTISED_HOST", value,
                                     ARRAYSIZE(value));
    if (length > 0 && length < ARRAYSIZE(value)) {
        live_share_copy_ascii(share->autoAdvertisedHost,
                              ARRAYSIZE(share->autoAdvertisedHost), value);
    }
    SetTimer(app->mainWindow, LIVE_SHARE_TIMER_ID, 1, NULL);
}

BOOL live_share_initialize(AppState *app)
{
    LiveShareContext *share;

    if (app == NULL || app->mainWindow == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (app->liveShare != NULL) {
        return live_share_valid(app);
    }
    share = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*share));
    if (share == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    share->transport = live_create(app->mainWindow, WCM_LIVE_EVENT);
    if (share->transport == NULL) {
        HeapFree(GetProcessHeap(), 0, share);
        return FALSE;
    }
    app->liveShare = share;
    live_share_load_auto_settings(app);
    return TRUE;
}

void live_share_show_dialog(AppState *app)
{
    INT_PTR result;

    if (!live_share_valid(app)) {
        MessageBoxW(app != NULL ? app->mainWindow : NULL,
                    L"Live sharing is unavailable because Windows networking "
                    L"could not be initialized.",
                    APP_NAME, MB_OK | MB_ICONERROR);
        return;
    }
    result = DialogBoxParamW(app->instance, MAKEINTRESOURCEW(IDD_LIVE_SHARE),
                             app->mainWindow, live_share_dialog_proc,
                             (LPARAM)app);
    if (result == -1) {
        app_show_error(app->mainWindow,
                       L"The Live Sharing dialog could not be opened.",
                       GetLastError());
    }
    live_share_refresh_application(app);
}

void live_share_start_host_command(AppState *app)
{
    (void)live_share_start_host(app, 0, NULL, NULL, TRUE);
}

void live_share_join_command(AppState *app)
{
    live_share_show_dialog(app);
}

void live_share_copy_invitation_command(AppState *app)
{
    (void)live_share_copy_invitation(app, TRUE);
}

static BOOL live_share_flush_pending(AppState *app)
{
    LiveShareContext *share;
    LiveStatus status;
    ULONGLONG deadline;

    if (!live_share_valid(app) || !live_share_status(app, &status)) {
        return TRUE;
    }
    share = app->liveShare;
    KillTimer(app->mainWindow, LIVE_SHARE_TIMER_ID);
    if (status.role == LIVE_ROLE_HOST &&
        status.state == LIVE_STATE_LISTENING && share->documentPending) {
        if (live_share_publish_current(app)) {
            share->documentPending = FALSE;
        }
    } else if (status.role == LIVE_ROLE_CLIENT &&
               status.state == LIVE_STATE_CONNECTED &&
               share->documentPending && !share->waitingForCanonical) {
        if (live_share_submit_current(app)) {
            share->documentPending = FALSE;
        }
    }

    deadline = GetTickCount64() + LIVE_SHARE_LEAVE_WAIT_MS;
    while (status.role == LIVE_ROLE_CLIENT &&
           share->waitingForCanonical && GetTickCount64() < deadline) {
        live_share_handle_event(app);
        if (!share->waitingForCanonical) {
            if (share->documentPending &&
                live_share_status(app, &status) &&
                status.role == LIVE_ROLE_CLIENT &&
                status.state == LIVE_STATE_CONNECTED &&
                live_share_submit_current(app)) {
                share->documentPending = FALSE;
                deadline = GetTickCount64() + LIVE_SHARE_LEAVE_WAIT_MS;
                continue;
            }
            break;
        }
        Sleep(10u);
    }
    return !share->documentPending && !share->waitingForCanonical;
}

static void live_share_stop_session(AppState *app, BOOL flushPending)
{
    LiveEvent *event;
    BOOL synchronized = TRUE;

    if (!live_share_valid(app)) {
        return;
    }
    if (flushPending) {
        synchronized = live_share_flush_pending(app);
    }
    KillTimer(app->mainWindow, LIVE_SHARE_TIMER_ID);
    app->liveShare->documentPending = FALSE;
    app->liveShare->waitingForCanonical = FALSE;
    app->liveShare->awaitingInitialSnapshot = FALSE;
    app->liveShare->applyingRemote = TRUE;
    live_stop(app->liveShare->transport);
    app->liveShare->applyingRemote = FALSE;
    live_share_unlock_editor(app);
    while ((event = live_pop_event(app->liveShare->transport)) != NULL) {
        live_event_free(event);
    }
    live_share_set_notice(
        app, synchronized
                 ? L"Live session ended; the document remains open"
                 : L"Live session ended with an edit the host did not acknowledge");
    live_share_refresh_application(app);
}

void live_share_leave_command(AppState *app)
{
    live_share_stop_session(app, TRUE);
}

void live_share_leave_for_document_replacement(AppState *app)
{
    /* The replacement has already been loaded and must never be broadcast as
     * a pending edit from the previous document's session.  The normal Open /
     * New flow has already offered to save the previous document locally. */
    live_share_stop_session(app, FALSE);
}

void live_share_document_changed(AppState *app)
{
    LiveStatus status;

    if (!live_share_valid(app) || app->liveShare->applyingRemote ||
        !live_share_status(app, &status) ||
        (status.role != LIVE_ROLE_HOST && status.role != LIVE_ROLE_CLIENT) ||
        (status.role == LIVE_ROLE_CLIENT &&
         status.state != LIVE_STATE_CONNECTED)) {
        return;
    }
    app->liveShare->documentPending = TRUE;
    SetTimer(app->mainWindow, LIVE_SHARE_TIMER_ID,
             LIVE_SHARE_DEBOUNCE_MS, NULL);
}

void live_share_handle_timer(AppState *app, UINT_PTR timerId)
{
    LiveShareContext *share;
    LiveStatus status;

    if (!live_share_valid(app) || timerId != LIVE_SHARE_TIMER_ID) {
        return;
    }
    share = app->liveShare;
    KillTimer(app->mainWindow, LIVE_SHARE_TIMER_ID);
    if (share->autoAction != LIVE_SHARE_AUTO_NONE) {
        int action = share->autoAction;
        share->autoAction = LIVE_SHARE_AUTO_NONE;
        if (action == LIVE_SHARE_AUTO_HOST) {
            (void)live_share_start_host(
                app, share->autoPort,
                share->autoAdvertisedHost[0] != '\0'
                    ? share->autoAdvertisedHost : "127.0.0.1",
                share->autoTokenValid ? share->autoToken : NULL, FALSE);
        } else {
            WCHAR invitation[LIVE_INVITATION_CAPACITY];
            if (live_share_utf8_to_wide(share->autoInvitation, invitation,
                                        ARRAYSIZE(invitation))) {
                (void)live_share_join(app, invitation, FALSE);
            }
        }
    }
    if (!share->documentPending || !live_share_status(app, &status)) {
        return;
    }
    if (status.role == LIVE_ROLE_HOST &&
        status.state == LIVE_STATE_LISTENING) {
        share->documentPending = FALSE;
        if (!live_share_publish_current(app)) {
            live_share_schedule_retry(app);
        }
    } else if (status.role == LIVE_ROLE_CLIENT &&
               status.state == LIVE_STATE_CONNECTED) {
        if (share->waitingForCanonical) {
            SetTimer(app->mainWindow, LIVE_SHARE_TIMER_ID,
                     LIVE_SHARE_DEBOUNCE_MS, NULL);
        } else {
            share->documentPending = FALSE;
            if (!live_share_submit_current(app)) {
                live_share_schedule_retry(app);
            }
        }
    }
    live_share_refresh_application(app);
}

void live_share_handle_event(AppState *app)
{
    LiveEvent *event;
    WCHAR message[256];

    if (!live_share_valid(app)) {
        return;
    }
    while ((event = live_pop_event(app->liveShare->transport)) != NULL) {
        switch (event->type) {
        case LIVE_EVENT_SNAPSHOT:
            live_share_handle_snapshot(app, event);
            break;
        case LIVE_EVENT_CLIENT_EDIT:
            live_share_handle_client_edit(app, event);
            break;
        case LIVE_EVENT_PEER_JOINED:
            live_share_set_notice(app, L"A client joined the live session");
            break;
        case LIVE_EVENT_PEER_LEFT:
            live_share_set_notice(app, L"A client left the live session");
            break;
        case LIVE_EVENT_ERROR:
            message[0] = L'\0';
            (void)live_share_utf8_to_wide(event->error_message, message,
                                          ARRAYSIZE(message));
            live_share_set_notice(
                app, message[0] != L'\0' ? message
                                         : L"Live sharing network error");
            KillTimer(app->mainWindow, LIVE_SHARE_TIMER_ID);
            app->liveShare->documentPending = FALSE;
            if (event->role == LIVE_ROLE_CLIENT) {
                live_share_unlock_editor(app);
                app->liveShare->awaitingInitialSnapshot = FALSE;
                app->liveShare->waitingForCanonical = FALSE;
            }
            break;
        case LIVE_EVENT_STATUS:
            if (event->role == LIVE_ROLE_CLIENT &&
                (event->state == LIVE_STATE_STOPPED ||
                 event->state == LIVE_STATE_ERROR)) {
                live_share_unlock_editor(app);
                app->liveShare->awaitingInitialSnapshot = FALSE;
                app->liveShare->documentPending = FALSE;
                app->liveShare->waitingForCanonical = FALSE;
            }
            break;
        default:
            break;
        }
        live_event_free(event);
    }
    live_share_refresh_application(app);
}

LRESULT live_share_query_state(const AppState *app, UINT query)
{
    LiveStatus status;
    uint32_t hash;
    size_t index;

    if (!live_share_status(app, &status)) {
        return 0;
    }
    switch (query) {
    case WCQ_LIVE_ROLE:
        return status.role;
    case WCQ_LIVE_STATE:
        return status.state;
    case WCQ_LIVE_CLIENT_COUNT:
        return status.client_count;
    case WCQ_LIVE_LISTEN_PORT:
        return status.port;
    case WCQ_LIVE_REVISION_LOW:
        return (LRESULT)(LONG)(status.revision & 0xFFFFFFFFu);
    case WCQ_LIVE_REVISION_HIGH:
        return (LRESULT)(LONG)(status.revision >> 32);
    case WCQ_LIVE_WORKER_RUNNING:
        return status.worker_running;
    case WCQ_LIVE_LAST_ERROR:
        return status.last_error;
    case WCQ_LIVE_INBOUND_COUNT:
        return (LRESULT)(LONG)(status.inbound_count & 0x7FFFFFFFu);
    case WCQ_LIVE_OUTBOUND_COUNT:
        return (LRESULT)(LONG)(status.outbound_count & 0x7FFFFFFFu);
    case WCQ_LIVE_APPLIED_COUNT:
        return app->liveShare != NULL
                   ? (LRESULT)(LONG)(app->liveShare->appliedCount & 0x7FFFFFFFu)
                   : 0;
    case WCQ_LIVE_APPLYING_REMOTE:
        return app->liveShare != NULL && app->liveShare->applyingRemote;
    case WCQ_LIVE_PEER_ID:
        return status.peer_id;
    case WCQ_LIVE_INVITATION_HASH:
        hash = 2166136261u;
        for (index = 0; status.invitation[index] != '\0'; ++index) {
            hash ^= (unsigned char)status.invitation[index];
            hash *= 16777619u;
        }
        return (LRESULT)(LONG)hash;
    case WCQ_LIVE_DOCUMENT_PENDING:
        return app->liveShare != NULL &&
               (app->liveShare->documentPending ||
                app->liveShare->waitingForCanonical);
    default:
        return 0;
    }
}

void live_share_shutdown(AppState *app)
{
    LiveShareContext *share;

    if (app == NULL || app->liveShare == NULL) {
        return;
    }
    share = app->liveShare;
    (void)live_share_flush_pending(app);
    app->liveShare = NULL;
    if (app->mainWindow != NULL) {
        KillTimer(app->mainWindow, LIVE_SHARE_TIMER_ID);
    }
    if (share->dialog != NULL && IsWindow(share->dialog)) {
        EndDialog(share->dialog, IDCANCEL);
    }
    if (share->editorLockedForJoin && app->editor != NULL &&
        IsWindow(app->editor)) {
        SendMessageW(app->editor, EM_SETREADONLY, FALSE, 0);
    }
    live_destroy(share->transport);
    SecureZeroMemory(share->autoToken, sizeof(share->autoToken));
    HeapFree(GetProcessHeap(), 0, share);
}
