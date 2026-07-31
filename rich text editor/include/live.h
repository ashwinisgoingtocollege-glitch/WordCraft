#ifndef WORDCRAFT_LIVE_H
#define WORDCRAFT_LIVE_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LIVE_MAX_CLIENTS 32u
#define LIVE_MAX_DOCUMENT_SIZE (16u * 1024u * 1024u)
#define LIVE_TOKEN_SIZE 16u
#define LIVE_TOKEN_HEX_LENGTH 32u
#define LIVE_MAX_HOST_LENGTH 253u
#define LIVE_MAX_DISPLAY_NAME 63u
#define LIVE_INVITATION_CAPACITY 320u
#define LIVE_ERROR_MESSAGE_CAPACITY 256u

typedef struct LiveContext LiveContext;

typedef enum LiveRole {
    LIVE_ROLE_NONE = 0,
    LIVE_ROLE_HOST = 1,
    LIVE_ROLE_CLIENT = 2
} LiveRole;

typedef enum LiveState {
    LIVE_STATE_STOPPED = 0,
    LIVE_STATE_STARTING = 1,
    LIVE_STATE_LISTENING = 2,
    LIVE_STATE_CONNECTING = 3,
    LIVE_STATE_CONNECTED = 4,
    LIVE_STATE_STOPPING = 5,
    LIVE_STATE_ERROR = 6
} LiveState;

typedef enum LiveError {
    LIVE_ERROR_NONE = 0,
    LIVE_ERROR_INVALID_ARGUMENT = 1,
    LIVE_ERROR_OUT_OF_MEMORY = 2,
    LIVE_ERROR_BUSY = 3,
    LIVE_ERROR_DOCUMENT_TOO_LARGE = 4,
    LIVE_ERROR_INVALID_INVITATION = 5,
    LIVE_ERROR_NETWORK = 6,
    LIVE_ERROR_AUTHENTICATION = 7,
    LIVE_ERROR_PROTOCOL = 8,
    LIVE_ERROR_CLIENT_LIMIT = 9,
    LIVE_ERROR_REVISION_OVERFLOW = 10,
    LIVE_ERROR_SHUTDOWN = 11,
    LIVE_ERROR_INTERNAL = 12
} LiveError;

typedef enum LiveEventType {
    LIVE_EVENT_STATUS = 1,
    LIVE_EVENT_PEER_JOINED = 2,
    LIVE_EVENT_PEER_LEFT = 3,
    LIVE_EVENT_SNAPSHOT = 4,
    LIVE_EVENT_CLIENT_EDIT = 5,
    LIVE_EVENT_ERROR = 6
} LiveEventType;

/*
 * Events and their data buffers are owned by the caller after live_pop_event.
 * Release both with live_event_free.  SNAPSHOT and CLIENT_EDIT carry data.
 * CLIENT_EDIT is emitted only by a host; peer_id identifies its origin and
 * stale is true when base_revision did not match the host's canonical revision.
 */
typedef struct LiveEvent {
    LiveEventType type;
    LiveRole role;
    LiveState state;
    uint32_t peer_id;
    uint64_t revision;
    uint64_t base_revision;
    BOOL stale;
    unsigned char *data;
    size_t data_size;
    char display_name[LIVE_MAX_DISPLAY_NAME + 1u];
    LiveError error;
    int system_error;
    char error_message[LIVE_ERROR_MESSAGE_CAPACITY];
    struct LiveEvent *_next;
} LiveEvent;

typedef struct LiveStatus {
    LiveRole role;
    LiveState state;
    uint32_t client_count;
    uint16_t port;
    uint64_t revision;
    uint64_t broadcast_revision;
    BOOL worker_running;
    LiveError last_error;
    int last_system_error;
    uint64_t inbound_count;
    uint64_t outbound_count;
    uint32_t peer_id;
    char invitation[LIVE_INVITATION_CAPACITY];
    char error_message[LIVE_ERROR_MESSAGE_CAPACITY];
} LiveStatus;

/* notify_message is posted with wParam=LiveEventType and lParam=0. */
LiveContext *live_create(HWND notify_window, UINT notify_message);

/*
 * Starts an authoritative host.  port may be zero to request an ephemeral
 * port.  advertised_host is placed in the invitation and is never inferred
 * from an untrusted peer.  Pass NULL for token16 to generate a secure token.
 */
BOOL live_host_start(LiveContext *context, uint16_t port,
                     const char *advertised_host,
                     const uint8_t token16[LIVE_TOKEN_SIZE],
                     const void *initial_data, size_t initial_size,
                     uint64_t initial_revision);

/* Starts an asynchronous authenticated connection to a host invitation. */
BOOL live_client_join(LiveContext *context, const char *invitation,
                      const char *display_name);

/* Replaces the host canonical document and advances its revision by one. */
BOOL live_host_publish(LiveContext *context, const void *data, size_t size);

/* Sends an edit proposal; it never changes canonical state by itself. */
BOOL live_client_submit(LiveContext *context, const void *data, size_t size,
                        uint64_t base_revision);

/* Both calls are safe when repeated.  destroy accepts NULL. */
void live_stop(LiveContext *context);
void live_destroy(LiveContext *context);

LiveEvent *live_pop_event(LiveContext *context);
void live_event_free(LiveEvent *event);
BOOL live_get_status(LiveContext *context, LiveStatus *status);
BOOL live_get_invitation(LiveContext *context, char *buffer,
                         size_t capacity, size_t *required_capacity);

/* Standalone strict invitation helpers (host output excludes IPv6 brackets). */
BOOL live_format_invitation(const char *host, uint16_t port,
                            const uint8_t token16[LIVE_TOKEN_SIZE],
                            char *buffer, size_t capacity,
                            size_t *required_capacity);
BOOL live_parse_invitation(const char *invitation, char *host,
                           size_t host_capacity, uint16_t *port,
                           uint8_t token16[LIVE_TOKEN_SIZE]);

#ifdef __cplusplus
}
#endif

#endif
