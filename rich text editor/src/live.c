#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "live.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIVE_WIRE_MAGIC UINT32_C(0x57434c56) /* "WCLV" */
#define LIVE_WIRE_VERSION 1u
#define LIVE_WIRE_HEADER_SIZE 32u
#define LIVE_HOST_SOCKET_CAPACITY 48u
#define LIVE_IO_CHUNK 65536u
#define LIVE_IO_BUDGET (1024u * 1024u)
/*
 * select() returns immediately for socket activity.  This timeout only polls
 * the Win32 stop/wake events, which cannot participate in a Winsock fd_set.
 * A 100 ms idle interval avoids waking a battery-powered ARM64 system roughly
 * 50 times per second while keeping queued local work and shutdown responsive.
 */
#define LIVE_SELECT_MILLISECONDS 100u
#define LIVE_HANDSHAKE_TIMEOUT_MS 10000u
#define LIVE_CONNECT_TIMEOUT_MS 15000u
#define LIVE_FRAME_TIMEOUT_MS 30000u
#define LIVE_SHUTDOWN_DATA_FLUSH_MS 250u
#define LIVE_SHUTDOWN_GOODBYE_FLUSH_MS 100u
#define LIVE_HEARTBEAT_INTERVAL_MS 5000u
#define LIVE_HEARTBEAT_TIMEOUT_MS 5000u
#define LIVE_EVENT_QUEUE_MAX 1024u
#define LIVE_EVENT_QUEUE_MAX_BYTES (64u * 1024u * 1024u)
#define LIVE_COMMAND_QUEUE_MAX 64u
#define LIVE_COMMAND_QUEUE_MAX_BYTES (32u * 1024u * 1024u)
#define LIVE_PEER_QUEUE_MAX_BYTES (32u * 1024u * 1024u)
#define LIVE_PUBLICATION_QUEUE_MAX 1024u
#define LIVE_PUBLICATION_QUEUE_MAX_BYTES (64u * 1024u * 1024u)

typedef enum WireType {
    WIRE_HELLO = 1,
    WIRE_WELCOME = 2,
    WIRE_SNAPSHOT = 3,
    WIRE_PROPOSAL = 4,
    WIRE_REJECT = 5,
    WIRE_GOODBYE = 6,
    WIRE_PING = 7,
    WIRE_PONG = 8
} WireType;

typedef enum PeerPhase {
    PEER_UNUSED = 0,
    PEER_WAIT_HELLO,
    PEER_AUTHENTICATED,
    PEER_REJECTING,
    PEER_CONNECTING,
    PEER_WAIT_WELCOME,
    PEER_WAIT_SNAPSHOT,
    PEER_READY
} PeerPhase;

typedef struct WireBuffer {
    LONG references;
    size_t size;
    unsigned char bytes[1];
} WireBuffer;

typedef struct SendNode {
    WireBuffer *buffer;
    size_t offset;
    struct SendNode *next;
} SendNode;

typedef struct RxState {
    unsigned char header[LIVE_WIRE_HEADER_SIZE];
    size_t header_used;
    WireType type;
    uint32_t length;
    uint32_t flags;
    uint64_t revision;
    uint64_t base_revision;
    unsigned char *payload;
    size_t payload_used;
} RxState;

typedef struct Peer {
    SOCKET socket;
    PeerPhase phase;
    uint32_t id;
    char display_name[LIVE_MAX_DISPLAY_NAME + 1u];
    ULONGLONG deadline;
    ULONGLONG heartbeat_due;
    ULONGLONG heartbeat_deadline;
    BOOL awaiting_pong;
    uint64_t snapshot_revision_queued;
    RxState rx;
    SendNode *send_head;
    SendNode *send_tail;
    size_t queued_bytes;
} Peer;

typedef struct LiveCommand {
    unsigned char *data;
    size_t size;
    uint64_t base_revision;
    struct LiveCommand *next;
} LiveCommand;

typedef struct HostPublication {
    WireBuffer *buffer;
    uint64_t revision;
    uint64_t generation;
    struct HostPublication *next;
} HostPublication;

struct LiveContext {
    CRITICAL_SECTION lifecycle_lock;
    CRITICAL_SECTION lock;
    HWND notify_window;
    UINT notify_message;

    HANDLE thread;
    HANDLE stop_event;
    HANDLE wake_event;
    HANDLE startup_event;
    BOOL startup_success;

    LiveRole role;
    LiveState state;
    BOOL worker_running;
    LiveError last_error;
    int last_system_error;
    char error_message[LIVE_ERROR_MESSAGE_CAPACITY];
    uint32_t client_count;
    uint16_t port;
    uint64_t revision;
    uint64_t inbound_count;
    uint64_t outbound_count;
    uint32_t peer_id;
    char invitation[LIVE_INVITATION_CAPACITY];

    uint8_t token[LIVE_TOKEN_SIZE];
    char advertised_host[LIVE_MAX_HOST_LENGTH + 1u];
    char connect_host[LIVE_MAX_HOST_LENGTH + 1u];
    char display_name[LIVE_MAX_DISPLAY_NAME + 1u];

    unsigned char *document;
    size_t document_size;
    uint64_t document_generation;
    uint64_t broadcast_revision;
    HostPublication *publication_head;
    HostPublication *publication_tail;
    size_t publication_count;
    size_t publication_bytes;

    LiveCommand *command_head;
    LiveCommand *command_tail;
    size_t command_count;
    size_t command_bytes;

    LiveEvent *event_head;
    LiveEvent *event_tail;
    size_t event_count;
    size_t event_bytes;
    BOOL notification_pending;
};

typedef struct HostWorker {
    SOCKET listener;
    Peer peers[LIVE_HOST_SOCKET_CAPACITY];
    uint32_t next_peer_id;
    uint64_t seen_generation;
} HostWorker;

typedef struct ClientWorker {
    Peer peer;
    ADDRINFOA *addresses;
    ADDRINFOA *next_address;
    BOOL graceful_close;
} ClientWorker;

/*
 * getaddrinfo has no cancellation API on Windows 7.  A resolver request owns
 * every value touched by its helper thread and is shared through two
 * references: one for that thread and one for the client worker.  The client
 * worker can therefore release its reference as soon as stop_event is set;
 * the resolver then releases the final reference and self-cleans whenever the
 * system resolver eventually returns.
 */
typedef struct ResolverRequest {
    LONG references;
    HANDLE completed;
    char host[LIVE_MAX_HOST_LENGTH + 1u];
    char service[6];
    ADDRINFOA *addresses;
    int result;
} ResolverRequest;

static DWORD WINAPI live_worker_main(void *parameter);
static DWORD WINAPI resolver_thread_main(void *parameter);
static void wire_buffer_release(WireBuffer *buffer);

static void copy_text(char *destination, size_t capacity, const char *source)
{
    size_t length;

    if (destination == NULL || capacity == 0u) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    length = strlen(source);
    if (length >= capacity) {
        length = capacity - 1u;
    }
    if (length != 0u) {
        memcpy(destination, source, length);
    }
    destination[length] = '\0';
}

static size_t bounded_length(const char *text, size_t maximum)
{
    size_t length = 0u;

    if (text == NULL) {
        return 0u;
    }
    while (length < maximum && text[length] != '\0') {
        ++length;
    }
    return length;
}

static void secure_zero(void *data, size_t size)
{
    if (data != NULL && size != 0u) {
        SecureZeroMemory(data, size);
    }
}

static void event_release(LiveEvent *event)
{
    if (event == NULL) {
        return;
    }
    secure_zero(event->data, event->data_size);
    free(event->data);
    event->data = NULL;
    free(event);
}

void live_event_free(LiveEvent *event)
{
    event_release(event);
}

static LiveEvent *event_allocate(LiveEventType type)
{
    LiveEvent *event = (LiveEvent *)calloc(1u, sizeof(*event));

    if (event != NULL) {
        event->type = type;
    }
    return event;
}

static BOOL enqueue_event(LiveContext *context, LiveEvent *event)
{
    BOOL accepted = FALSE;
    BOOL should_notify = FALSE;
    HWND window = NULL;
    UINT message = 0u;
    WPARAM event_type = 0u;

    if (context == NULL || event == NULL) {
        event_release(event);
        return FALSE;
    }

    EnterCriticalSection(&context->lock);
    if (context->event_count < LIVE_EVENT_QUEUE_MAX &&
        event->data_size <= LIVE_EVENT_QUEUE_MAX_BYTES -
                                context->event_bytes) {
        event->_next = NULL;
        if (context->event_tail != NULL) {
            context->event_tail->_next = event;
        } else {
            context->event_head = event;
        }
        context->event_tail = event;
        ++context->event_count;
        context->event_bytes += event->data_size;
        window = context->notify_window;
        message = context->notify_message;
        if (window != NULL && message != 0u &&
            !context->notification_pending) {
            context->notification_pending = TRUE;
            event_type = (WPARAM)event->type;
            should_notify = TRUE;
        }
        accepted = TRUE;
    }
    LeaveCriticalSection(&context->lock);

    if (!accepted) {
        event_release(event);
        return FALSE;
    }
    if (should_notify && !PostMessageW(window, message, event_type, 0)) {
        EnterCriticalSection(&context->lock);
        context->notification_pending = FALSE;
        LeaveCriticalSection(&context->lock);
    }
    return TRUE;
}

static LiveEvent *event_from_status_locked(const LiveContext *context)
{
    LiveEvent *event = event_allocate(LIVE_EVENT_STATUS);

    if (event != NULL) {
        event->role = context->role;
        event->state = context->state;
        event->peer_id = context->peer_id;
        event->revision = context->revision;
        event->error = context->last_error;
        event->system_error = context->last_system_error;
        copy_text(event->error_message, sizeof(event->error_message),
                  context->error_message);
    }
    return event;
}

static void queue_status_event(LiveContext *context)
{
    LiveEvent *event;

    EnterCriticalSection(&context->lock);
    event = event_from_status_locked(context);
    LeaveCriticalSection(&context->lock);
    if (event != NULL) {
        (void)enqueue_event(context, event);
    }
}

static void set_context_error(LiveContext *context, LiveError error,
                              int system_error, const char *message,
                              BOOL fatal)
{
    LiveEvent *event;

    if (context == NULL) {
        return;
    }
    event = event_allocate(LIVE_EVENT_ERROR);

    EnterCriticalSection(&context->lock);
    context->last_error = error;
    context->last_system_error = system_error;
    copy_text(context->error_message, sizeof(context->error_message), message);
    if (fatal) {
        context->state = LIVE_STATE_ERROR;
    }
    if (event != NULL) {
        event->role = context->role;
        event->state = context->state;
        event->peer_id = context->peer_id;
        event->revision = context->revision;
        event->error = error;
        event->system_error = system_error;
        copy_text(event->error_message, sizeof(event->error_message), message);
    }
    LeaveCriticalSection(&context->lock);

    if (event != NULL) {
        (void)enqueue_event(context, event);
    }
    if (fatal) {
        queue_status_event(context);
    }
}

static BOOL api_failure(LiveContext *context, LiveError error,
                        DWORD windows_error, const char *message)
{
    SetLastError(windows_error);
    set_context_error(context, error, (int)windows_error, message, FALSE);
    return FALSE;
}

static BOOL valid_utf8_name(const unsigned char *bytes, size_t length)
{
    size_t index = 0u;

    if (bytes == NULL || length == 0u || length > LIVE_MAX_DISPLAY_NAME) {
        return FALSE;
    }
    while (index < length) {
        unsigned char first = bytes[index++];
        uint32_t codepoint;
        size_t continuation;

        if (first < 0x80u) {
            if (first < 0x20u || first == 0x7fu) {
                return FALSE;
            }
            continue;
        }
        if (first >= 0xc2u && first <= 0xdfu) {
            codepoint = (uint32_t)(first & 0x1fu);
            continuation = 1u;
        } else if (first >= 0xe0u && first <= 0xefu) {
            codepoint = (uint32_t)(first & 0x0fu);
            continuation = 2u;
        } else if (first >= 0xf0u && first <= 0xf4u) {
            codepoint = (uint32_t)(first & 0x07u);
            continuation = 3u;
        } else {
            return FALSE;
        }
        if (continuation > length - index) {
            return FALSE;
        }
        while (continuation-- != 0u) {
            unsigned char next = bytes[index++];
            if ((next & 0xc0u) != 0x80u) {
                return FALSE;
            }
            codepoint = (codepoint << 6u) | (uint32_t)(next & 0x3fu);
        }
        if ((first == 0xe0u && codepoint < 0x800u) ||
            (first == 0xedu && codepoint >= 0xd800u) ||
            (first == 0xf0u && codepoint < 0x10000u) ||
            codepoint > 0x10ffffu ||
            (codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL valid_host(const char *host, size_t *length_out, BOOL *ipv6_out)
{
    size_t length;
    size_t index;
    BOOL has_colon = FALSE;
    struct in6_addr address6;

    if (host == NULL) {
        return FALSE;
    }
    length = bounded_length(host, LIVE_MAX_HOST_LENGTH + 1u);
    if (length == 0u || length > LIVE_MAX_HOST_LENGTH || host[length] != '\0') {
        return FALSE;
    }
    for (index = 0u; index < length; ++index) {
        unsigned char character = (unsigned char)host[index];
        if (character == ':') {
            has_colon = TRUE;
            continue;
        }
        if (character >= 0x80u || character <= 0x20u || character == 0x7fu ||
            character == '/' || character == '\\' || character == '[' ||
            character == ']' || character == '@' || character == '?' ||
            character == '#' || character == '%') {
            return FALSE;
        }
        if (!has_colon && !((character >= 'a' && character <= 'z') ||
                            (character >= 'A' && character <= 'Z') ||
                            (character >= '0' && character <= '9') ||
                            character == '.' || character == '-' ||
                            character == '_')) {
            return FALSE;
        }
    }
    if (has_colon && InetPtonA(AF_INET6, host, &address6) != 1) {
        return FALSE;
    }
    if (length_out != NULL) {
        *length_out = length;
    }
    if (ipv6_out != NULL) {
        *ipv6_out = has_colon;
    }
    return TRUE;
}

static int hex_value(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

BOOL live_format_invitation(const char *host, uint16_t port,
                            const uint8_t token16[LIVE_TOKEN_SIZE],
                            char *buffer, size_t capacity,
                            size_t *required_capacity)
{
    static const char digits[] = "0123456789abcdef";
    static const char prefix[] = "wordcraft://";
    char port_text[6];
    size_t host_length;
    size_t port_length;
    size_t required;
    size_t cursor = 0u;
    size_t index;
    BOOL ipv6;
    int result;

    if (!valid_host(host, &host_length, &ipv6) || port == 0u ||
        token16 == NULL) {
        if (required_capacity != NULL) {
            *required_capacity = 0u;
        }
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    result = snprintf(port_text, sizeof(port_text), "%u", (unsigned int)port);
    if (result <= 0 || (size_t)result >= sizeof(port_text)) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    port_length = (size_t)result;
    required = (sizeof(prefix) - 1u) + host_length + (ipv6 ? 2u : 0u) +
               1u + port_length + 1u + LIVE_TOKEN_HEX_LENGTH + 1u;
    if (required_capacity != NULL) {
        *required_capacity = required;
    }
    if (buffer == NULL || capacity < required) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    memcpy(buffer + cursor, prefix, sizeof(prefix) - 1u);
    cursor += sizeof(prefix) - 1u;
    if (ipv6) {
        buffer[cursor++] = '[';
    }
    memcpy(buffer + cursor, host, host_length);
    cursor += host_length;
    if (ipv6) {
        buffer[cursor++] = ']';
    }
    buffer[cursor++] = ':';
    memcpy(buffer + cursor, port_text, port_length);
    cursor += port_length;
    buffer[cursor++] = '/';
    for (index = 0u; index < LIVE_TOKEN_SIZE; ++index) {
        buffer[cursor++] = digits[token16[index] >> 4u];
        buffer[cursor++] = digits[token16[index] & 0x0fu];
    }
    buffer[cursor] = '\0';
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

BOOL live_parse_invitation(const char *invitation, char *host,
                           size_t host_capacity, uint16_t *port,
                           uint8_t token16[LIVE_TOKEN_SIZE])
{
    static const char prefix[] = "wordcraft://";
    const char *cursor;
    const char *host_start;
    const char *host_end;
    const char *port_start;
    const char *slash;
    size_t host_length;
    unsigned long port_value = 0u;
    size_t index;
    char parsed_host[LIVE_MAX_HOST_LENGTH + 1u];
    uint8_t parsed_token[LIVE_TOKEN_SIZE];
    BOOL ipv6;

    if (invitation == NULL || host == NULL || host_capacity == 0u ||
        port == NULL || token16 == NULL ||
        strncmp(invitation, prefix, sizeof(prefix) - 1u) != 0) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    cursor = invitation + sizeof(prefix) - 1u;
    if (*cursor == '[') {
        host_start = ++cursor;
        host_end = strchr(cursor, ']');
        if (host_end == NULL || host_end[1] != ':') {
            SetLastError(ERROR_INVALID_DATA);
            return FALSE;
        }
        port_start = host_end + 2;
    } else {
        host_start = cursor;
        host_end = strchr(cursor, ':');
        if (host_end == NULL || strchr(host_end + 1, ':') != NULL) {
            SetLastError(ERROR_INVALID_DATA);
            return FALSE;
        }
        port_start = host_end + 1;
    }
    host_length = (size_t)(host_end - host_start);
    if (host_length == 0u || host_length > LIVE_MAX_HOST_LENGTH) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    memcpy(parsed_host, host_start, host_length);
    parsed_host[host_length] = '\0';
    if (!valid_host(parsed_host, NULL, &ipv6) ||
        (host_start[-1] == '[' && !ipv6)) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    if (host_start[-1] != '[' && ipv6) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }

    slash = strchr(port_start, '/');
    if (slash == NULL || slash == port_start) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    for (cursor = port_start; cursor < slash; ++cursor) {
        unsigned int digit;
        if (*cursor < '0' || *cursor > '9') {
            SetLastError(ERROR_INVALID_DATA);
            return FALSE;
        }
        digit = (unsigned int)(*cursor - '0');
        if (port_value > (65535u - digit) / 10u) {
            SetLastError(ERROR_INVALID_DATA);
            return FALSE;
        }
        port_value = port_value * 10u + digit;
    }
    if (port_value == 0u || port_value > 65535u) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    cursor = slash + 1;
    if (strlen(cursor) != LIVE_TOKEN_HEX_LENGTH) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    for (index = 0u; index < LIVE_TOKEN_SIZE; ++index) {
        int high = hex_value(cursor[index * 2u]);
        int low = hex_value(cursor[index * 2u + 1u]);
        if (high < 0 || low < 0) {
            SetLastError(ERROR_INVALID_DATA);
            return FALSE;
        }
        parsed_token[index] = (uint8_t)((high << 4) | low);
    }
    if (cursor[LIVE_TOKEN_HEX_LENGTH] != '\0') {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    if (host_capacity <= host_length) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(host, parsed_host, host_length + 1u);
    *port = (uint16_t)port_value;
    memcpy(token16, parsed_token, sizeof(parsed_token));
    secure_zero(parsed_token, sizeof(parsed_token));
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

static BOOL secure_random_bytes(uint8_t *destination, size_t size)
{
    typedef BOOLEAN(WINAPI *RtlGenRandomFunction)(PVOID, ULONG);
    HMODULE module;
    FARPROC procedure;
    RtlGenRandomFunction random_function = NULL;
    BOOL loaded = FALSE;
    BOOLEAN result;

    if (destination == NULL || size > ULONG_MAX) {
        return FALSE;
    }
    module = GetModuleHandleW(L"advapi32.dll");
    if (module == NULL) {
        module = LoadLibraryW(L"advapi32.dll");
        loaded = module != NULL;
    }
    if (module == NULL) {
        return FALSE;
    }
    procedure = GetProcAddress(module, "SystemFunction036");
    if (procedure != NULL && sizeof(procedure) == sizeof(random_function)) {
        memcpy(&random_function, &procedure, sizeof(random_function));
    }
    result = random_function != NULL
                 ? random_function(destination, (ULONG)size)
                 : FALSE;
    if (loaded) {
        FreeLibrary(module);
    }
    return result ? TRUE : FALSE;
}

static void free_commands_locked(LiveContext *context)
{
    LiveCommand *command = context->command_head;

    while (command != NULL) {
        LiveCommand *next = command->next;
        secure_zero(command->data, command->size);
        free(command->data);
        free(command);
        command = next;
    }
    context->command_head = NULL;
    context->command_tail = NULL;
    context->command_count = 0u;
    context->command_bytes = 0u;
}

static void free_publications_locked(LiveContext *context)
{
    HostPublication *publication = context->publication_head;

    while (publication != NULL) {
        HostPublication *next = publication->next;
        wire_buffer_release(publication->buffer);
        free(publication);
        publication = next;
    }
    context->publication_head = NULL;
    context->publication_tail = NULL;
    context->publication_count = 0u;
    context->publication_bytes = 0u;
}

static void free_events_locked(LiveContext *context)
{
    LiveEvent *event = context->event_head;

    while (event != NULL) {
        LiveEvent *next = event->_next;
        event_release(event);
        event = next;
    }
    context->event_head = NULL;
    context->event_tail = NULL;
    context->event_count = 0u;
    context->event_bytes = 0u;
    context->notification_pending = FALSE;
}

LiveContext *live_create(HWND notify_window, UINT notify_message)
{
    LiveContext *context = (LiveContext *)calloc(1u, sizeof(*context));

    if (context == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    InitializeCriticalSection(&context->lifecycle_lock);
    InitializeCriticalSection(&context->lock);
    context->notify_window = notify_window;
    context->notify_message = notify_message;
    context->role = LIVE_ROLE_NONE;
    context->state = LIVE_STATE_STOPPED;
    SetLastError(ERROR_SUCCESS);
    return context;
}

LiveEvent *live_pop_event(LiveContext *context)
{
    LiveEvent *event;

    if (context == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    EnterCriticalSection(&context->lock);
    event = context->event_head;
    if (event != NULL) {
        context->event_head = event->_next;
        if (context->event_head == NULL) {
            context->event_tail = NULL;
            context->notification_pending = FALSE;
        }
        event->_next = NULL;
        --context->event_count;
        context->event_bytes -= event->data_size;
    }
    LeaveCriticalSection(&context->lock);
    return event;
}

BOOL live_get_status(LiveContext *context, LiveStatus *status)
{
    if (context == NULL || status == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    EnterCriticalSection(&context->lock);
    memset(status, 0, sizeof(*status));
    status->role = context->role;
    status->state = context->state;
    status->client_count = context->client_count;
    status->port = context->port;
    status->revision = context->revision;
    status->broadcast_revision = context->broadcast_revision;
    status->worker_running = context->worker_running;
    status->last_error = context->last_error;
    status->last_system_error = context->last_system_error;
    status->inbound_count = context->inbound_count;
    status->outbound_count = context->outbound_count;
    status->peer_id = context->peer_id;
    copy_text(status->invitation, sizeof(status->invitation),
              context->invitation);
    copy_text(status->error_message, sizeof(status->error_message),
              context->error_message);
    LeaveCriticalSection(&context->lock);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

BOOL live_get_invitation(LiveContext *context, char *buffer,
                         size_t capacity, size_t *required_capacity)
{
    size_t required;

    if (context == NULL) {
        if (required_capacity != NULL) {
            *required_capacity = 0u;
        }
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    EnterCriticalSection(&context->lock);
    required = strlen(context->invitation) + 1u;
    if (required_capacity != NULL) {
        *required_capacity = required;
    }
    if (context->role != LIVE_ROLE_HOST || context->invitation[0] == '\0') {
        LeaveCriticalSection(&context->lock);
        SetLastError(ERROR_INVALID_STATE);
        return FALSE;
    }
    if (buffer == NULL || capacity < required) {
        LeaveCriticalSection(&context->lock);
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(buffer, context->invitation, required);
    LeaveCriticalSection(&context->lock);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

static uint16_t read_u16(const unsigned char *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8u) | (uint16_t)bytes[1]);
}

static uint32_t read_u32(const unsigned char *bytes)
{
    return ((uint32_t)bytes[0] << 24u) | ((uint32_t)bytes[1] << 16u) |
           ((uint32_t)bytes[2] << 8u) | (uint32_t)bytes[3];
}

static uint64_t read_u64(const unsigned char *bytes)
{
    return ((uint64_t)read_u32(bytes) << 32u) |
           (uint64_t)read_u32(bytes + 4u);
}

static void write_u16(unsigned char *bytes, uint16_t value)
{
    bytes[0] = (unsigned char)(value >> 8u);
    bytes[1] = (unsigned char)value;
}

static void write_u32(unsigned char *bytes, uint32_t value)
{
    bytes[0] = (unsigned char)(value >> 24u);
    bytes[1] = (unsigned char)(value >> 16u);
    bytes[2] = (unsigned char)(value >> 8u);
    bytes[3] = (unsigned char)value;
}

static void write_u64(unsigned char *bytes, uint64_t value)
{
    write_u32(bytes, (uint32_t)(value >> 32u));
    write_u32(bytes + 4u, (uint32_t)value);
}

static WireBuffer *wire_buffer_create(WireType type, uint32_t flags,
                                      uint64_t revision,
                                      uint64_t base_revision,
                                      const void *payload, size_t length)
{
    WireBuffer *buffer;
    size_t allocation_size;

    if (length > UINT32_MAX ||
        length > SIZE_MAX - (sizeof(*buffer) - 1u) - LIVE_WIRE_HEADER_SIZE) {
        return NULL;
    }
    allocation_size = (sizeof(*buffer) - 1u) + LIVE_WIRE_HEADER_SIZE + length;
    buffer = (WireBuffer *)malloc(allocation_size);
    if (buffer == NULL) {
        return NULL;
    }
    buffer->references = 1;
    buffer->size = LIVE_WIRE_HEADER_SIZE + length;
    write_u32(buffer->bytes, LIVE_WIRE_MAGIC);
    write_u16(buffer->bytes + 4u, LIVE_WIRE_VERSION);
    write_u16(buffer->bytes + 6u, (uint16_t)type);
    write_u32(buffer->bytes + 8u, (uint32_t)length);
    write_u32(buffer->bytes + 12u, flags);
    write_u64(buffer->bytes + 16u, revision);
    write_u64(buffer->bytes + 24u, base_revision);
    if (length != 0u) {
        memcpy(buffer->bytes + LIVE_WIRE_HEADER_SIZE, payload, length);
    }
    return buffer;
}

static void wire_buffer_add_ref(WireBuffer *buffer)
{
    (void)InterlockedIncrement(&buffer->references);
}

static void wire_buffer_release(WireBuffer *buffer)
{
    if (buffer != NULL && InterlockedDecrement(&buffer->references) == 0) {
        if (buffer->size >= LIVE_WIRE_HEADER_SIZE &&
            read_u16(buffer->bytes + 6u) == (uint16_t)WIRE_HELLO) {
            secure_zero(buffer->bytes + LIVE_WIRE_HEADER_SIZE,
                        LIVE_TOKEN_SIZE);
        }
        free(buffer);
    }
}

static void peer_initialize(Peer *peer)
{
    memset(peer, 0, sizeof(*peer));
    peer->socket = INVALID_SOCKET;
    peer->phase = PEER_UNUSED;
}

static void peer_mark_alive(Peer *peer)
{
    peer->awaiting_pong = FALSE;
    peer->heartbeat_deadline = 0u;
    peer->heartbeat_due =
        GetTickCount64() + (ULONGLONG)LIVE_HEARTBEAT_INTERVAL_MS;
}

static void rx_reset(RxState *rx)
{
    if (rx->payload != NULL && rx->type == WIRE_HELLO &&
        rx->length >= LIVE_TOKEN_SIZE) {
        secure_zero(rx->payload, LIVE_TOKEN_SIZE);
    }
    free(rx->payload);
    memset(rx, 0, sizeof(*rx));
}

static void peer_release_queue(Peer *peer)
{
    SendNode *node = peer->send_head;

    while (node != NULL) {
        SendNode *next = node->next;
        wire_buffer_release(node->buffer);
        free(node);
        node = next;
    }
    peer->send_head = NULL;
    peer->send_tail = NULL;
    peer->queued_bytes = 0u;
}

static void peer_close(Peer *peer)
{
    if (peer->socket != INVALID_SOCKET) {
        closesocket(peer->socket);
    }
    peer->socket = INVALID_SOCKET;
    rx_reset(&peer->rx);
    peer_release_queue(peer);
    peer->phase = PEER_UNUSED;
    peer->id = 0u;
    peer->display_name[0] = '\0';
    peer->deadline = 0u;
    peer->heartbeat_due = 0u;
    peer->heartbeat_deadline = 0u;
    peer->awaiting_pong = FALSE;
    peer->snapshot_revision_queued = 0u;
}

static BOOL peer_queue_buffer(LiveContext *context, Peer *peer,
                              WireBuffer *buffer)
{
    SendNode *node;

    if (peer == NULL || buffer == NULL || peer->phase == PEER_UNUSED ||
        buffer->size > LIVE_PEER_QUEUE_MAX_BYTES - peer->queued_bytes) {
        return FALSE;
    }
    node = (SendNode *)malloc(sizeof(*node));
    if (node == NULL) {
        return FALSE;
    }
    wire_buffer_add_ref(buffer);
    node->buffer = buffer;
    node->offset = 0u;
    node->next = NULL;
    if (peer->send_tail != NULL) {
        peer->send_tail->next = node;
    } else {
        peer->send_head = node;
    }
    peer->send_tail = node;
    peer->queued_bytes += buffer->size;
    EnterCriticalSection(&context->lock);
    ++context->outbound_count;
    LeaveCriticalSection(&context->lock);
    return TRUE;
}

static BOOL peer_queue_frame(LiveContext *context, Peer *peer, WireType type,
                             uint32_t flags, uint64_t revision,
                             uint64_t base_revision, const void *payload,
                             size_t length)
{
    WireBuffer *buffer = wire_buffer_create(type, flags, revision,
                                            base_revision, payload, length);
    BOOL result;

    if (buffer == NULL) {
        return FALSE;
    }
    result = peer_queue_buffer(context, peer, buffer);
    wire_buffer_release(buffer);
    return result;
}

static int peer_flush(Peer *peer)
{
    size_t budget = LIVE_IO_BUDGET;

    while (peer->send_head != NULL && budget != 0u) {
        SendNode *node = peer->send_head;
        size_t remaining = node->buffer->size - node->offset;
        int request = (int)(remaining > LIVE_IO_CHUNK ? LIVE_IO_CHUNK : remaining);
        int sent = send(peer->socket,
                        (const char *)node->buffer->bytes + node->offset,
                        request, 0);
        if (sent == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
                return 1;
            }
            return -1;
        }
        if (sent == 0) {
            return -1;
        }
        node->offset += (size_t)sent;
        peer->queued_bytes -= (size_t)sent;
        budget -= (size_t)sent < budget ? (size_t)sent : budget;
        if (node->offset == node->buffer->size) {
            peer->send_head = node->next;
            if (peer->send_head == NULL) {
                peer->send_tail = NULL;
            }
            wire_buffer_release(node->buffer);
            free(node);
        }
    }
    return 1;
}

static void peer_drain_for(Peer *peer, DWORD duration_ms)
{
    ULONGLONG deadline = GetTickCount64() + (ULONGLONG)duration_ms;

    while (peer->socket != INVALID_SOCKET && peer->send_head != NULL) {
        fd_set write_set;
        fd_set error_set;
        struct timeval timeout;
        ULONGLONG now;
        ULONGLONG remaining;
        int selected;

        if (peer_flush(peer) < 0 || peer->send_head == NULL) {
            return;
        }
        now = GetTickCount64();
        if (now >= deadline) {
            return;
        }
        remaining = deadline - now;
        if (remaining > LIVE_SELECT_MILLISECONDS) {
            remaining = LIVE_SELECT_MILLISECONDS;
        }
        FD_ZERO(&write_set);
        FD_ZERO(&error_set);
        FD_SET(peer->socket, &write_set);
        FD_SET(peer->socket, &error_set);
        timeout.tv_sec = 0;
        timeout.tv_usec = (long)(remaining * 1000u);
        selected = select(0, NULL, &write_set, &error_set, &timeout);
        if (selected == SOCKET_ERROR ||
            FD_ISSET(peer->socket, &error_set)) {
            return;
        }
    }
}

static BOOL set_nonblocking(SOCKET socket_value)
{
    u_long enabled = 1u;
    return ioctlsocket(socket_value, FIONBIO, &enabled) == 0;
}

static BOOL token_equal(const uint8_t left[LIVE_TOKEN_SIZE],
                        const uint8_t right[LIVE_TOKEN_SIZE])
{
    unsigned int difference = 0u;
    size_t index;

    for (index = 0u; index < LIVE_TOKEN_SIZE; ++index) {
        difference |= (unsigned int)(left[index] ^ right[index]);
    }
    return difference == 0u;
}

static LiveEvent *peer_event(LiveContext *context, LiveEventType type,
                             const Peer *peer)
{
    LiveEvent *event = event_allocate(type);

    if (event == NULL) {
        return NULL;
    }
    EnterCriticalSection(&context->lock);
    event->role = context->role;
    event->state = context->state;
    event->revision = context->revision;
    LeaveCriticalSection(&context->lock);
    if (peer != NULL) {
        event->peer_id = peer->id;
        copy_text(event->display_name, sizeof(event->display_name),
                  peer->display_name);
    }
    return event;
}

static void count_inbound(LiveContext *context)
{
    EnterCriticalSection(&context->lock);
    ++context->inbound_count;
    LeaveCriticalSection(&context->lock);
}

static BOOL parse_and_validate_header(Peer *peer, LiveRole role)
{
    RxState *rx = &peer->rx;
    uint32_t magic = read_u32(rx->header);
    uint16_t version = read_u16(rx->header + 4u);
    uint16_t type = read_u16(rx->header + 6u);

    if (magic != LIVE_WIRE_MAGIC || version != LIVE_WIRE_VERSION ||
        type < (uint16_t)WIRE_HELLO || type > (uint16_t)WIRE_PONG) {
        return FALSE;
    }
    rx->type = (WireType)type;
    rx->length = read_u32(rx->header + 8u);
    rx->flags = read_u32(rx->header + 12u);
    rx->revision = read_u64(rx->header + 16u);
    rx->base_revision = read_u64(rx->header + 24u);

    switch (rx->type) {
    case WIRE_HELLO:
        if (role != LIVE_ROLE_HOST || peer->phase != PEER_WAIT_HELLO ||
            rx->length < LIVE_TOKEN_SIZE + 1u ||
            rx->length > LIVE_TOKEN_SIZE + LIVE_MAX_DISPLAY_NAME ||
            rx->flags != 0u || rx->revision != 0u ||
            rx->base_revision != 0u) {
            return FALSE;
        }
        break;
    case WIRE_WELCOME:
        if (role != LIVE_ROLE_CLIENT || peer->phase != PEER_WAIT_WELCOME ||
            rx->length != 0u || rx->flags == 0u ||
            rx->base_revision != 0u) {
            return FALSE;
        }
        break;
    case WIRE_SNAPSHOT:
        if (role != LIVE_ROLE_CLIENT ||
            (peer->phase != PEER_WAIT_SNAPSHOT && peer->phase != PEER_READY) ||
            rx->length > LIVE_MAX_DOCUMENT_SIZE || rx->flags != 0u ||
            rx->base_revision != 0u) {
            return FALSE;
        }
        break;
    case WIRE_PROPOSAL:
        if (role != LIVE_ROLE_HOST || peer->phase != PEER_AUTHENTICATED ||
            rx->length > LIVE_MAX_DOCUMENT_SIZE || rx->flags != 0u ||
            rx->revision != 0u) {
            return FALSE;
        }
        break;
    case WIRE_REJECT:
        if (role != LIVE_ROLE_CLIENT || peer->phase != PEER_WAIT_WELCOME ||
            rx->length >= LIVE_ERROR_MESSAGE_CAPACITY || rx->revision != 0u ||
            rx->base_revision != 0u ||
            (rx->flags != (uint32_t)LIVE_ERROR_AUTHENTICATION &&
             rx->flags != (uint32_t)LIVE_ERROR_CLIENT_LIMIT &&
             rx->flags != (uint32_t)LIVE_ERROR_PROTOCOL &&
             rx->flags != (uint32_t)LIVE_ERROR_NETWORK)) {
            return FALSE;
        }
        break;
    case WIRE_GOODBYE:
        if (rx->length != 0u || rx->flags != 0u || rx->revision != 0u ||
            rx->base_revision != 0u ||
            !((role == LIVE_ROLE_HOST &&
               peer->phase == PEER_AUTHENTICATED) ||
              (role == LIVE_ROLE_CLIENT && peer->phase == PEER_READY))) {
            return FALSE;
        }
        break;
    case WIRE_PING:
    case WIRE_PONG:
        if (rx->length != 0u || rx->flags != 0u || rx->revision != 0u ||
            rx->base_revision != 0u ||
            !((role == LIVE_ROLE_HOST &&
               peer->phase == PEER_AUTHENTICATED) ||
              (role == LIVE_ROLE_CLIENT && peer->phase == PEER_READY))) {
            return FALSE;
        }
        break;
    default:
        return FALSE;
    }
    return TRUE;
}

static BOOL queue_rejection(LiveContext *context, Peer *peer, LiveError error,
                            const char *message)
{
    size_t length = message != NULL ? strlen(message) : 0u;

    peer_release_queue(peer);
    rx_reset(&peer->rx);
    peer->phase = PEER_REJECTING;
    peer->deadline = GetTickCount64() + 2000u;
    return peer_queue_frame(context, peer, WIRE_REJECT, (uint32_t)error, 0u,
                            0u, message, length);
}

static uint32_t next_host_peer_id(HostWorker *worker)
{
    size_t attempt;

    for (attempt = 0u; attempt < UINT32_MAX; ++attempt) {
        size_t index;
        BOOL used = FALSE;
        ++worker->next_peer_id;
        if (worker->next_peer_id == 0u) {
            ++worker->next_peer_id;
        }
        for (index = 0u; index < LIVE_HOST_SOCKET_CAPACITY; ++index) {
            if (worker->peers[index].phase == PEER_AUTHENTICATED &&
                worker->peers[index].id == worker->next_peer_id) {
                used = TRUE;
                break;
            }
        }
        if (!used) {
            return worker->next_peer_id;
        }
    }
    return 0u;
}

static BOOL host_queue_current_snapshot(LiveContext *context, Peer *peer)
{
    WireBuffer *buffer;
    uint64_t revision;
    BOOL queued;

    EnterCriticalSection(&context->lock);
    revision = context->revision;
    buffer = wire_buffer_create(WIRE_SNAPSHOT, 0u, revision, 0u,
                                context->document, context->document_size);
    LeaveCriticalSection(&context->lock);
    if (buffer == NULL) {
        return FALSE;
    }
    queued = peer_queue_buffer(context, peer, buffer);
    if (queued) {
        peer->snapshot_revision_queued = revision;
    }
    wire_buffer_release(buffer);
    return queued;
}

static BOOL host_process_frame(LiveContext *context, HostWorker *worker,
                               Peer *peer)
{
    RxState *rx = &peer->rx;

    if (rx->type == WIRE_HELLO) {
        LiveEvent *event;
        uint32_t peer_id;
        uint32_t client_count;

        if (!token_equal(context->token, rx->payload)) {
            (void)queue_rejection(context, peer, LIVE_ERROR_AUTHENTICATION,
                                  "Authentication failed");
            return TRUE;
        }
        if (!valid_utf8_name(rx->payload + LIVE_TOKEN_SIZE,
                             rx->length - LIVE_TOKEN_SIZE)) {
            (void)queue_rejection(context, peer, LIVE_ERROR_PROTOCOL,
                                  "Invalid display name");
            return TRUE;
        }
        EnterCriticalSection(&context->lock);
        client_count = context->client_count;
        LeaveCriticalSection(&context->lock);
        if (client_count >= LIVE_MAX_CLIENTS) {
            (void)queue_rejection(context, peer, LIVE_ERROR_CLIENT_LIMIT,
                                  "Session is full");
            return TRUE;
        }
        peer_id = next_host_peer_id(worker);
        if (peer_id == 0u) {
            (void)queue_rejection(context, peer, LIVE_ERROR_PROTOCOL,
                                  "No peer identifier available");
            return TRUE;
        }
        peer->id = peer_id;
        memcpy(peer->display_name, rx->payload + LIVE_TOKEN_SIZE,
               rx->length - LIVE_TOKEN_SIZE);
        peer->display_name[rx->length - LIVE_TOKEN_SIZE] = '\0';

        EnterCriticalSection(&context->lock);
        peer->phase = PEER_AUTHENTICATED;
        ++context->client_count;
        client_count = context->client_count;
        LeaveCriticalSection(&context->lock);
        (void)client_count;

        EnterCriticalSection(&context->lock);
        if (!peer_queue_frame(context, peer, WIRE_WELCOME, peer_id,
                              context->revision, 0u, NULL, 0u) ||
            !host_queue_current_snapshot(context, peer)) {
            LeaveCriticalSection(&context->lock);
            return FALSE;
        }
        LeaveCriticalSection(&context->lock);
        event = peer_event(context, LIVE_EVENT_PEER_JOINED, peer);
        if (event != NULL) {
            (void)enqueue_event(context, event);
        }
        queue_status_event(context);
        return TRUE;
    }

    if (rx->type == WIRE_PROPOSAL) {
        LiveEvent *event = event_allocate(LIVE_EVENT_CLIENT_EDIT);
        BOOL queued;

        if (event == NULL) {
            set_context_error(context, LIVE_ERROR_OUT_OF_MEMORY,
                              ERROR_NOT_ENOUGH_MEMORY,
                              "Could not queue a client edit", FALSE);
            return FALSE;
        }
        event->role = LIVE_ROLE_HOST;
        event->state = LIVE_STATE_LISTENING;
        event->peer_id = peer->id;
        event->base_revision = rx->base_revision;
        event->data = rx->payload;
        event->data_size = rx->length;
        rx->payload = NULL;
        copy_text(event->display_name, sizeof(event->display_name),
                  peer->display_name);
        EnterCriticalSection(&context->lock);
        event->revision = context->revision;
        event->stale = rx->base_revision != context->revision;
        queued = enqueue_event(context, event);
        LeaveCriticalSection(&context->lock);
        if (!queued) {
            set_context_error(context, LIVE_ERROR_BUSY, ERROR_NOT_ENOUGH_QUOTA,
                              "Live event queue is full", FALSE);
            return FALSE;
        }
        return TRUE;
    }
    if (rx->type == WIRE_PING) {
        return peer_queue_frame(context, peer, WIRE_PONG, 0u, 0u, 0u,
                                NULL, 0u);
    }
    if (rx->type == WIRE_PONG) {
        return TRUE;
    }
    if (rx->type == WIRE_GOODBYE) {
        return FALSE;
    }
    return FALSE;
}

static int client_process_frame(LiveContext *context, ClientWorker *worker)
{
    Peer *peer = &worker->peer;
    RxState *rx = &peer->rx;

    if (rx->type == WIRE_WELCOME) {
        EnterCriticalSection(&context->lock);
        context->peer_id = rx->flags;
        context->revision = rx->revision;
        LeaveCriticalSection(&context->lock);
        peer->id = rx->flags;
        peer->phase = PEER_WAIT_SNAPSHOT;
        peer->deadline = GetTickCount64() + LIVE_HANDSHAKE_TIMEOUT_MS;
        return 1;
    }
    if (rx->type == WIRE_REJECT) {
        char message[LIVE_ERROR_MESSAGE_CAPACITY];
        LiveError error = (LiveError)rx->flags;

        if (rx->length != 0u) {
            memcpy(message, rx->payload, rx->length);
        }
        message[rx->length] = '\0';
        set_context_error(context, error, 0,
                          message[0] != '\0' ? message
                                              : "Host rejected the connection",
                          TRUE);
        return -1;
    }
    if (rx->type == WIRE_SNAPSHOT) {
        LiveEvent *event;
        uint64_t previous_revision;

        EnterCriticalSection(&context->lock);
        previous_revision = context->revision;
        LeaveCriticalSection(&context->lock);
        if ((peer->phase == PEER_WAIT_SNAPSHOT &&
             rx->revision != previous_revision) ||
            (peer->phase == PEER_READY &&
             rx->revision <= previous_revision)) {
            set_context_error(context, LIVE_ERROR_PROTOCOL, 0,
                              "Snapshot revision is not monotonic", TRUE);
            return -1;
        }
        event = event_allocate(LIVE_EVENT_SNAPSHOT);
        if (event == NULL) {
            set_context_error(context, LIVE_ERROR_OUT_OF_MEMORY,
                              ERROR_NOT_ENOUGH_MEMORY,
                              "Could not queue a snapshot", TRUE);
            return -1;
        }
        event->role = LIVE_ROLE_CLIENT;
        event->state = LIVE_STATE_CONNECTED;
        event->peer_id = peer->id;
        event->revision = rx->revision;
        event->data = rx->payload;
        event->data_size = rx->length;
        rx->payload = NULL;
        EnterCriticalSection(&context->lock);
        context->revision = rx->revision;
        context->state = LIVE_STATE_CONNECTED;
        LeaveCriticalSection(&context->lock);
        peer->phase = PEER_READY;
        peer->deadline = 0u;
        if (!enqueue_event(context, event)) {
            set_context_error(context, LIVE_ERROR_BUSY, ERROR_NOT_ENOUGH_QUOTA,
                              "Live event queue is full", TRUE);
            return -1;
        }
        queue_status_event(context);
        return 1;
    }
    if (rx->type == WIRE_PING) {
        if (!peer_queue_frame(context, peer, WIRE_PONG, 0u, 0u, 0u, NULL,
                              0u)) {
            set_context_error(context, LIVE_ERROR_BUSY,
                              ERROR_NOT_ENOUGH_QUOTA,
                              "Could not queue a live heartbeat response",
                              TRUE);
            return -1;
        }
        return 1;
    }
    if (rx->type == WIRE_PONG) {
        return 1;
    }
    if (rx->type == WIRE_GOODBYE) {
        LiveEvent *event = peer_event(context, LIVE_EVENT_PEER_LEFT, peer);
        worker->graceful_close = TRUE;
        if (event != NULL) {
            (void)enqueue_event(context, event);
        }
        return 0;
    }
    return -1;
}

/* 1 = keep open, 0 = orderly frame requested close, -1 = I/O/protocol error. */
static int peer_receive(LiveContext *context, LiveRole role, void *worker_data,
                        Peer *peer)
{
    size_t budget = LIVE_IO_BUDGET;

    while (budget != 0u) {
        RxState *rx = &peer->rx;
        unsigned char *destination;
        size_t remaining;
        int request;
        int received;

        if (rx->header_used < LIVE_WIRE_HEADER_SIZE) {
            destination = rx->header + rx->header_used;
            remaining = LIVE_WIRE_HEADER_SIZE - rx->header_used;
        } else {
            destination = rx->payload + rx->payload_used;
            remaining = (size_t)rx->length - rx->payload_used;
        }
        if (remaining != 0u) {
            request = (int)(remaining > LIVE_IO_CHUNK ? LIVE_IO_CHUNK
                                                       : remaining);
            received = recv(peer->socket, (char *)destination, request, 0);
            if (received == SOCKET_ERROR) {
                int error = WSAGetLastError();
                return error == WSAEWOULDBLOCK ? 1 : -1;
            }
            if (received == 0) {
                return 0;
            }
            budget -= (size_t)received < budget ? (size_t)received : budget;
            if (rx->header_used < LIVE_WIRE_HEADER_SIZE) {
                rx->header_used += (size_t)received;
            } else {
                rx->payload_used += (size_t)received;
            }
            if (peer->phase == PEER_AUTHENTICATED ||
                peer->phase == PEER_WAIT_SNAPSHOT ||
                peer->phase == PEER_READY) {
                peer->deadline = GetTickCount64() + LIVE_FRAME_TIMEOUT_MS;
            }
        }

        if (rx->header_used == LIVE_WIRE_HEADER_SIZE &&
            rx->type == (WireType)0) {
            if (!parse_and_validate_header(peer, role)) {
                return -1;
            }
            if (rx->length != 0u) {
                rx->payload = (unsigned char *)malloc(rx->length);
                if (rx->payload == NULL) {
                    return -1;
                }
            }
        }
        if (rx->header_used == LIVE_WIRE_HEADER_SIZE &&
            rx->payload_used == rx->length) {
            int process_result;

            count_inbound(context);
            if (role == LIVE_ROLE_HOST) {
                process_result = host_process_frame(
                    context, (HostWorker *)worker_data, peer)
                                     ? 1
                                     : 0;
            } else {
                process_result = client_process_frame(
                    context, (ClientWorker *)worker_data);
            }
            if (process_result <= 0) {
                return process_result;
            }
            rx_reset(rx);
            if (peer->phase == PEER_AUTHENTICATED ||
                peer->phase == PEER_READY) {
                peer->deadline = 0u;
                peer_mark_alive(peer);
            }
        }
    }
    return 1;
}

static void host_disconnect_peer(LiveContext *context, Peer *peer)
{
    BOOL authenticated = peer->phase == PEER_AUTHENTICATED;
    LiveEvent *event = NULL;

    if (authenticated) {
        event = peer_event(context, LIVE_EVENT_PEER_LEFT, peer);
        EnterCriticalSection(&context->lock);
        if (context->client_count != 0u) {
            --context->client_count;
        }
        LeaveCriticalSection(&context->lock);
    }
    peer_close(peer);
    if (event != NULL) {
        (void)enqueue_event(context, event);
        queue_status_event(context);
    }
}

static void host_drain_send_queues(LiveContext *context, HostWorker *worker,
                                   DWORD duration_ms)
{
    ULONGLONG deadline = GetTickCount64() + (ULONGLONG)duration_ms;

    for (;;) {
        fd_set write_set;
        fd_set error_set;
        struct timeval timeout;
        ULONGLONG now;
        ULONGLONG remaining;
        size_t index;
        BOOL pending = FALSE;
        int selected;

        FD_ZERO(&write_set);
        FD_ZERO(&error_set);
        for (index = 0u; index < LIVE_HOST_SOCKET_CAPACITY; ++index) {
            Peer *peer = &worker->peers[index];
            if (peer->phase != PEER_AUTHENTICATED ||
                peer->send_head == NULL) {
                continue;
            }
            if (peer_flush(peer) < 0) {
                host_disconnect_peer(context, peer);
                continue;
            }
            if (peer->send_head != NULL) {
                pending = TRUE;
                FD_SET(peer->socket, &write_set);
                FD_SET(peer->socket, &error_set);
            }
        }
        if (!pending) {
            return;
        }
        now = GetTickCount64();
        if (now >= deadline) {
            return;
        }
        remaining = deadline - now;
        if (remaining > LIVE_SELECT_MILLISECONDS) {
            remaining = LIVE_SELECT_MILLISECONDS;
        }
        timeout.tv_sec = 0;
        timeout.tv_usec = (long)(remaining * 1000u);
        selected = select(0, NULL, &write_set, &error_set, &timeout);
        if (selected == SOCKET_ERROR) {
            return;
        }
        for (index = 0u; index < LIVE_HOST_SOCKET_CAPACITY; ++index) {
            Peer *peer = &worker->peers[index];
            if (peer->phase == PEER_AUTHENTICATED &&
                FD_ISSET(peer->socket, &error_set)) {
                host_disconnect_peer(context, peer);
            }
        }
    }
}

static BOOL host_broadcast_latest(LiveContext *context, HostWorker *worker)
{
    for (;;) {
        HostPublication *publication;
        size_t index;

        EnterCriticalSection(&context->lock);
        publication = context->publication_head;
        if (publication == NULL) {
            LeaveCriticalSection(&context->lock);
            return TRUE;
        }
        context->publication_head = publication->next;
        if (context->publication_head == NULL) {
            context->publication_tail = NULL;
        }
        publication->next = NULL;
        --context->publication_count;
        context->publication_bytes -= publication->buffer->size;
        LeaveCriticalSection(&context->lock);

        worker->seen_generation = publication->generation;
        for (index = 0u; index < LIVE_HOST_SOCKET_CAPACITY; ++index) {
            Peer *peer = &worker->peers[index];
            if (peer->phase != PEER_AUTHENTICATED ||
                publication->revision <=
                    peer->snapshot_revision_queued) {
                continue;
            }
            if (!peer_queue_buffer(
                    context, peer, publication->buffer)) {
                host_disconnect_peer(context, peer);
            } else {
                peer->snapshot_revision_queued =
                    publication->revision;
            }
        }
        EnterCriticalSection(&context->lock);
        if (context->broadcast_revision <
            publication->revision) {
            context->broadcast_revision =
                publication->revision;
        }
        LeaveCriticalSection(&context->lock);
        wire_buffer_release(publication->buffer);
        free(publication);
    }
}

static void signal_startup(LiveContext *context, BOOL success)
{
    HANDLE event_handle;

    EnterCriticalSection(&context->lock);
    context->startup_success = success;
    event_handle = context->startup_event;
    LeaveCriticalSection(&context->lock);
    if (event_handle != NULL) {
        SetEvent(event_handle);
    }
}

static SOCKET create_host_socket(int family, uint16_t requested_port,
                                 uint16_t *bound_port)
{
    SOCKET listener;
    BOOL exclusive = TRUE;
    int result;

    listener = socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }
    (void)setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                     (const char *)&exclusive, (int)sizeof(exclusive));
    if (family == AF_INET6) {
        struct sockaddr_in6 address6;
        DWORD ipv6_only = 0u;
        int address_length = (int)sizeof(address6);

        memset(&address6, 0, sizeof(address6));
        address6.sin6_family = AF_INET6;
        address6.sin6_addr = in6addr_any;
        address6.sin6_port = htons(requested_port);
        if (setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY,
                       (const char *)&ipv6_only,
                       (int)sizeof(ipv6_only)) == SOCKET_ERROR ||
            bind(listener, (const struct sockaddr *)&address6,
                 (int)sizeof(address6)) == SOCKET_ERROR ||
            getsockname(listener, (struct sockaddr *)&address6,
                        &address_length) == SOCKET_ERROR) {
            closesocket(listener);
            return INVALID_SOCKET;
        }
        *bound_port = ntohs(address6.sin6_port);
    } else {
        struct sockaddr_in address4;
        int address_length = (int)sizeof(address4);

        memset(&address4, 0, sizeof(address4));
        address4.sin_family = AF_INET;
        address4.sin_addr.s_addr = htonl(INADDR_ANY);
        address4.sin_port = htons(requested_port);
        if (bind(listener, (const struct sockaddr *)&address4,
                 (int)sizeof(address4)) == SOCKET_ERROR ||
            getsockname(listener, (struct sockaddr *)&address4,
                        &address_length) == SOCKET_ERROR) {
            closesocket(listener);
            return INVALID_SOCKET;
        }
        *bound_port = ntohs(address4.sin_port);
    }
    result = listen(listener, SOMAXCONN);
    if (result == SOCKET_ERROR || !set_nonblocking(listener)) {
        closesocket(listener);
        return INVALID_SOCKET;
    }
    return listener;
}

static BOOL host_open_listener(LiveContext *context, HostWorker *worker)
{
    uint16_t requested_port;
    uint16_t bound_port = 0u;
    char host[LIVE_MAX_HOST_LENGTH + 1u];
    char invitation[LIVE_INVITATION_CAPACITY];

    EnterCriticalSection(&context->lock);
    requested_port = context->port;
    copy_text(host, sizeof(host), context->advertised_host);
    LeaveCriticalSection(&context->lock);

    worker->listener = create_host_socket(AF_INET6, requested_port,
                                          &bound_port);
    if (worker->listener == INVALID_SOCKET && strchr(host, ':') == NULL) {
        worker->listener = create_host_socket(AF_INET, requested_port,
                                              &bound_port);
    }
    if (worker->listener == INVALID_SOCKET) {
        set_context_error(context, LIVE_ERROR_NETWORK, WSAGetLastError(),
                          "Could not bind the live sharing listener", TRUE);
        return FALSE;
    }
    if (!live_format_invitation(host, bound_port, context->token, invitation,
                                sizeof(invitation), NULL)) {
        set_context_error(context, LIVE_ERROR_INVALID_ARGUMENT,
                          (int)GetLastError(),
                          "Could not format the live invitation", TRUE);
        closesocket(worker->listener);
        worker->listener = INVALID_SOCKET;
        return FALSE;
    }
    EnterCriticalSection(&context->lock);
    context->port = bound_port;
    copy_text(context->invitation, sizeof(context->invitation), invitation);
    context->state = LIVE_STATE_LISTENING;
    LeaveCriticalSection(&context->lock);
    queue_status_event(context);
    return TRUE;
}

static Peer *host_free_peer(HostWorker *worker)
{
    size_t index;

    for (index = 0u; index < LIVE_HOST_SOCKET_CAPACITY; ++index) {
        if (worker->peers[index].phase == PEER_UNUSED) {
            return &worker->peers[index];
        }
    }
    return NULL;
}

static BOOL host_accept_connections(LiveContext *context, HostWorker *worker)
{
    unsigned int accepted_count = 0u;

    while (accepted_count < 16u) {
        SOCKET socket_value = accept(worker->listener, NULL, NULL);
        Peer *peer;

        if (socket_value == INVALID_SOCKET) {
            int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK) {
                set_context_error(context, LIVE_ERROR_NETWORK, error,
                                  "Could not accept a live client", TRUE);
                return FALSE;
            }
            break;
        }
        ++accepted_count;
        peer = host_free_peer(worker);
        if (peer == NULL || !set_nonblocking(socket_value)) {
            closesocket(socket_value);
            continue;
        }
        peer_initialize(peer);
        peer->socket = socket_value;
        peer->phase = PEER_WAIT_HELLO;
        peer->deadline = GetTickCount64() + LIVE_HANDSHAKE_TIMEOUT_MS;
    }
    return TRUE;
}

static void host_cleanup(LiveContext *context, HostWorker *worker,
                         BOOL orderly)
{
    size_t index;

    if (worker->listener != INVALID_SOCKET) {
        closesocket(worker->listener);
        worker->listener = INVALID_SOCKET;
    }
    if (orderly) {
        (void)host_broadcast_latest(context, worker);
        host_drain_send_queues(context, worker,
                               LIVE_SHUTDOWN_DATA_FLUSH_MS);
        for (index = 0u; index < LIVE_HOST_SOCKET_CAPACITY; ++index) {
            Peer *peer = &worker->peers[index];
            if (peer->phase == PEER_AUTHENTICATED) {
                (void)peer_queue_frame(context, peer, WIRE_GOODBYE, 0u, 0u,
                                       0u, NULL, 0u);
            }
        }
        host_drain_send_queues(context, worker,
                               LIVE_SHUTDOWN_GOODBYE_FLUSH_MS);
    }
    for (index = 0u; index < LIVE_HOST_SOCKET_CAPACITY; ++index) {
        Peer *peer = &worker->peers[index];
        if (peer->phase != PEER_UNUSED) {
            host_disconnect_peer(context, peer);
        }
    }
}

/* Returns TRUE for a requested orderly stop and FALSE after a fatal error. */
static BOOL host_worker_loop(LiveContext *context)
{
    HostWorker worker;
    size_t index;
    BOOL orderly = FALSE;

    memset(&worker, 0, sizeof(worker));
    worker.listener = INVALID_SOCKET;
    worker.next_peer_id = 0u;
    for (index = 0u; index < LIVE_HOST_SOCKET_CAPACITY; ++index) {
        peer_initialize(&worker.peers[index]);
    }
    EnterCriticalSection(&context->lock);
    worker.seen_generation = context->document_generation;
    LeaveCriticalSection(&context->lock);
    if (!host_open_listener(context, &worker)) {
        signal_startup(context, FALSE);
        return FALSE;
    }
    signal_startup(context, TRUE);

    for (;;) {
        fd_set read_set;
        fd_set write_set;
        fd_set error_set;
        struct timeval timeout;
        int selected;
        ULONGLONG now;

        (void)WaitForSingleObject(context->wake_event, 0u);
        if (!host_broadcast_latest(context, &worker)) {
            break;
        }
        if (WaitForSingleObject(context->stop_event, 0u) == WAIT_OBJECT_0) {
            orderly = TRUE;
            break;
        }

        FD_ZERO(&read_set);
        FD_ZERO(&write_set);
        FD_ZERO(&error_set);
        FD_SET(worker.listener, &read_set);
        FD_SET(worker.listener, &error_set);
        for (index = 0u; index < LIVE_HOST_SOCKET_CAPACITY; ++index) {
            Peer *peer = &worker.peers[index];
            if (peer->phase == PEER_UNUSED) {
                continue;
            }
            if (peer->phase != PEER_REJECTING) {
                FD_SET(peer->socket, &read_set);
            }
            if (peer->send_head != NULL) {
                FD_SET(peer->socket, &write_set);
            }
            FD_SET(peer->socket, &error_set);
        }
        timeout.tv_sec = 0;
        timeout.tv_usec = (long)(LIVE_SELECT_MILLISECONDS * 1000u);
        selected = select(0, &read_set, &write_set, &error_set, &timeout);
        if (selected == SOCKET_ERROR) {
            set_context_error(context, LIVE_ERROR_NETWORK,
                              WSAGetLastError(),
                              "The live listener failed", TRUE);
            break;
        }
        if (FD_ISSET(worker.listener, &error_set)) {
            set_context_error(context, LIVE_ERROR_NETWORK,
                              WSAGetLastError(),
                              "The live listener reported a socket error",
                              TRUE);
            break;
        }
        if (FD_ISSET(worker.listener, &read_set)) {
            if (!host_accept_connections(context, &worker)) {
                break;
            }
        }

        now = GetTickCount64();
        for (index = 0u; index < LIVE_HOST_SOCKET_CAPACITY; ++index) {
            Peer *peer = &worker.peers[index];
            int receive_result = 1;

            if (peer->phase == PEER_UNUSED) {
                continue;
            }
            if (FD_ISSET(peer->socket, &error_set)) {
                host_disconnect_peer(context, peer);
                continue;
            }
            if (FD_ISSET(peer->socket, &write_set) &&
                peer_flush(peer) < 0) {
                host_disconnect_peer(context, peer);
                continue;
            }
            if (peer->phase == PEER_REJECTING &&
                peer->send_head == NULL) {
                peer_close(peer);
                continue;
            }
            if (peer->phase != PEER_REJECTING &&
                FD_ISSET(peer->socket, &read_set)) {
                receive_result = peer_receive(context, LIVE_ROLE_HOST,
                                              &worker, peer);
                if (receive_result <= 0) {
                    host_disconnect_peer(context, peer);
                    continue;
                }
            }
            if ((peer->phase == PEER_WAIT_HELLO ||
                 peer->phase == PEER_REJECTING ||
                 (peer->phase == PEER_AUTHENTICATED &&
                  peer->rx.header_used != 0u)) &&
                peer->deadline != 0u && now >= peer->deadline) {
                host_disconnect_peer(context, peer);
                continue;
            }
            if (peer->phase == PEER_AUTHENTICATED) {
                if (peer->awaiting_pong &&
                    peer->heartbeat_deadline != 0u &&
                    now >= peer->heartbeat_deadline &&
                    peer->rx.header_used == 0u) {
                    host_disconnect_peer(context, peer);
                    continue;
                }
                if (!peer->awaiting_pong && peer->send_head == NULL &&
                    peer->heartbeat_due != 0u &&
                    now >= peer->heartbeat_due) {
                    if (!peer_queue_frame(context, peer, WIRE_PING, 0u, 0u,
                                          0u, NULL, 0u)) {
                        host_disconnect_peer(context, peer);
                        continue;
                    }
                    peer->awaiting_pong = TRUE;
                    peer->heartbeat_deadline =
                        now + (ULONGLONG)LIVE_HEARTBEAT_TIMEOUT_MS;
                }
            }
        }
    }
    host_cleanup(context, &worker, orderly);
    return orderly;
}

static void resolver_request_release(ResolverRequest *request)
{
    if (request != NULL &&
        InterlockedDecrement(&request->references) == 0) {
        if (request->addresses != NULL) {
            freeaddrinfo(request->addresses);
        }
        if (request->completed != NULL) {
            CloseHandle(request->completed);
        }
        secure_zero(request, sizeof(*request));
        free(request);
    }
}

static DWORD WINAPI resolver_thread_main(void *parameter)
{
    ResolverRequest *request = (ResolverRequest *)parameter;
    ADDRINFOA hints;
    WSADATA data;
    int startup_result;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    startup_result = WSAStartup(MAKEWORD(2, 2), &data);
    if (startup_result == 0) {
        request->result = getaddrinfo(request->host, request->service, &hints,
                                      &request->addresses);
    } else {
        request->result = startup_result;
    }
    SetEvent(request->completed);

    /* Release before WSACleanup so a final release can freeaddrinfo safely. */
    resolver_request_release(request);
    if (startup_result == 0) {
        WSACleanup();
    }
    return 0u;
}

/* 1 = resolved, 0 = cancelled by stop_event, -1 = resolver failure. */
static int client_resolve_addresses(LiveContext *context,
                                    ADDRINFOA **addresses_out)
{
    ResolverRequest *request;
    HANDLE thread;
    HANDLE wait_handles[2];
    DWORD wait_result;
    DWORD system_error;
    int resolve_result;

    *addresses_out = NULL;
    if (WaitForSingleObject(context->stop_event, 0u) == WAIT_OBJECT_0) {
        return 0;
    }
    request = (ResolverRequest *)calloc(1u, sizeof(*request));
    if (request == NULL) {
        set_context_error(context, LIVE_ERROR_OUT_OF_MEMORY,
                          ERROR_NOT_ENOUGH_MEMORY,
                          "Could not allocate a hostname resolver", TRUE);
        return -1;
    }
    request->references = 1;
    request->completed = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (request->completed == NULL) {
        system_error = GetLastError();
        resolver_request_release(request);
        set_context_error(context, LIVE_ERROR_INTERNAL, (int)system_error,
                          "Could not create a hostname resolver event", TRUE);
        return -1;
    }
    EnterCriticalSection(&context->lock);
    copy_text(request->host, sizeof(request->host), context->connect_host);
    (void)snprintf(request->service, sizeof(request->service), "%u",
                   (unsigned int)context->port);
    LeaveCriticalSection(&context->lock);

    (void)InterlockedIncrement(&request->references);
    thread = CreateThread(NULL, 0u, resolver_thread_main, request, 0u, NULL);
    if (thread == NULL) {
        system_error = GetLastError();
        resolver_request_release(request); /* Unused resolver-thread ref. */
        resolver_request_release(request); /* Client-worker ref. */
        set_context_error(context, LIVE_ERROR_INTERNAL, (int)system_error,
                          "Could not start the hostname resolver", TRUE);
        return -1;
    }
    CloseHandle(thread);

    wait_handles[0] = context->stop_event;
    wait_handles[1] = request->completed;
    wait_result = WaitForMultipleObjects(2u, wait_handles, FALSE, INFINITE);
    if (wait_result == WAIT_OBJECT_0) {
        resolver_request_release(request);
        return 0;
    }
    if (wait_result != WAIT_OBJECT_0 + 1u) {
        system_error = GetLastError();
        resolver_request_release(request);
        set_context_error(context, LIVE_ERROR_INTERNAL, (int)system_error,
                          "Could not wait for hostname resolution", TRUE);
        return -1;
    }

    resolve_result = request->result;
    if (resolve_result == 0) {
        *addresses_out = request->addresses;
        request->addresses = NULL;
    }
    resolver_request_release(request);
    if (resolve_result != 0 || *addresses_out == NULL) {
        set_context_error(context, LIVE_ERROR_NETWORK,
                          resolve_result != 0 ? resolve_result : WSANO_DATA,
                          "Could not resolve the live host", TRUE);
        return -1;
    }
    return 1;
}

static BOOL client_queue_hello(LiveContext *context, ClientWorker *worker)
{
    unsigned char payload[LIVE_TOKEN_SIZE + LIVE_MAX_DISPLAY_NAME];
    size_t name_length;

    EnterCriticalSection(&context->lock);
    name_length = strlen(context->display_name);
    memcpy(payload, context->token, LIVE_TOKEN_SIZE);
    memcpy(payload + LIVE_TOKEN_SIZE, context->display_name, name_length);
    LeaveCriticalSection(&context->lock);
    if (!peer_queue_frame(context, &worker->peer, WIRE_HELLO, 0u, 0u, 0u,
                          payload, LIVE_TOKEN_SIZE + name_length)) {
        return FALSE;
    }
    worker->peer.phase = PEER_WAIT_WELCOME;
    worker->peer.deadline = GetTickCount64() + LIVE_HANDSHAKE_TIMEOUT_MS;
    return TRUE;
}

static BOOL client_try_next_address(LiveContext *context,
                                    ClientWorker *worker)
{
    ADDRINFOA *address;

    if (worker->peer.socket != INVALID_SOCKET) {
        peer_close(&worker->peer);
    }
    while ((address = worker->next_address) != NULL) {
        SOCKET socket_value;
        int connect_result;
        int error;

        worker->next_address = address->ai_next;
        socket_value = socket(address->ai_family, address->ai_socktype,
                              address->ai_protocol);
        if (socket_value == INVALID_SOCKET) {
            continue;
        }
        if (!set_nonblocking(socket_value)) {
            closesocket(socket_value);
            continue;
        }
        peer_initialize(&worker->peer);
        worker->peer.socket = socket_value;
        worker->peer.phase = PEER_CONNECTING;
        worker->peer.deadline = GetTickCount64() + LIVE_CONNECT_TIMEOUT_MS;
        connect_result = connect(socket_value, address->ai_addr,
                                 (int)address->ai_addrlen);
        if (connect_result == 0) {
            if (!client_queue_hello(context, worker)) {
                peer_close(&worker->peer);
                return FALSE;
            }
            return TRUE;
        }
        error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS ||
            error == WSAEINVAL) {
            return TRUE;
        }
        peer_close(&worker->peer);
    }
    return FALSE;
}

static BOOL client_finish_connect(LiveContext *context,
                                  ClientWorker *worker)
{
    int socket_error = 0;
    int error_size = (int)sizeof(socket_error);

    if (getsockopt(worker->peer.socket, SOL_SOCKET, SO_ERROR,
                   (char *)&socket_error, &error_size) == SOCKET_ERROR) {
        socket_error = WSAGetLastError();
    }
    if (socket_error != 0) {
        return client_try_next_address(context, worker);
    }
    return client_queue_hello(context, worker);
}

static BOOL client_process_commands(LiveContext *context,
                                    ClientWorker *worker)
{
    for (;;) {
        LiveCommand *command;
        BOOL queued;

        EnterCriticalSection(&context->lock);
        command = context->command_head;
        if (command != NULL) {
            context->command_head = command->next;
            if (context->command_head == NULL) {
                context->command_tail = NULL;
            }
            --context->command_count;
            context->command_bytes -= command->size;
        }
        LeaveCriticalSection(&context->lock);
        if (command == NULL) {
            return TRUE;
        }
        if (worker->peer.phase != PEER_READY) {
            secure_zero(command->data, command->size);
            free(command->data);
            free(command);
            set_context_error(context, LIVE_ERROR_SHUTDOWN,
                              ERROR_INVALID_STATE,
                              "The live session closed before an edit was sent",
                              TRUE);
            return FALSE;
        }
        queued = peer_queue_frame(context, &worker->peer, WIRE_PROPOSAL, 0u,
                                  0u, command->base_revision, command->data,
                                  command->size);
        secure_zero(command->data, command->size);
        free(command->data);
        free(command);
        if (!queued) {
            set_context_error(context, LIVE_ERROR_BUSY, ERROR_NOT_ENOUGH_QUOTA,
                              "The outgoing live queue is full", TRUE);
            return FALSE;
        }
    }
}

static void client_queue_leave_event(LiveContext *context,
                                     const ClientWorker *worker)
{
    LiveEvent *event = peer_event(context, LIVE_EVENT_PEER_LEFT,
                                  &worker->peer);
    if (event != NULL) {
        (void)enqueue_event(context, event);
    }
}

/* Returns TRUE for requested/orderly shutdown, FALSE for fatal shutdown. */
static BOOL client_worker_loop(LiveContext *context)
{
    ClientWorker worker;
    int resolve_result;
    BOOL orderly = FALSE;

    memset(&worker, 0, sizeof(worker));
    peer_initialize(&worker.peer);
    resolve_result = client_resolve_addresses(context, &worker.addresses);
    if (resolve_result <= 0) {
        signal_startup(context, FALSE);
        return resolve_result == 0;
    }
    worker.next_address = worker.addresses;
    if (!client_try_next_address(context, &worker)) {
        set_context_error(context, LIVE_ERROR_NETWORK, WSAGetLastError(),
                          "Could not connect to the live host", TRUE);
        freeaddrinfo(worker.addresses);
        signal_startup(context, FALSE);
        return FALSE;
    }
    signal_startup(context, TRUE);

    for (;;) {
        fd_set read_set;
        fd_set write_set;
        fd_set error_set;
        struct timeval timeout;
        int selected;
        ULONGLONG now;

        (void)WaitForSingleObject(context->wake_event, 0u);
        if (!client_process_commands(context, &worker)) {
            break;
        }
        if (WaitForSingleObject(context->stop_event, 0u) == WAIT_OBJECT_0) {
            orderly = TRUE;
            break;
        }
        FD_ZERO(&read_set);
        FD_ZERO(&write_set);
        FD_ZERO(&error_set);
        if (worker.peer.phase != PEER_CONNECTING) {
            FD_SET(worker.peer.socket, &read_set);
        }
        if (worker.peer.phase == PEER_CONNECTING ||
            worker.peer.send_head != NULL) {
            FD_SET(worker.peer.socket, &write_set);
        }
        FD_SET(worker.peer.socket, &error_set);
        timeout.tv_sec = 0;
        timeout.tv_usec = (long)(LIVE_SELECT_MILLISECONDS * 1000u);
        selected = select(0, &read_set, &write_set, &error_set, &timeout);
        if (selected == SOCKET_ERROR) {
            set_context_error(context, LIVE_ERROR_NETWORK,
                              WSAGetLastError(),
                              "The live client socket failed", TRUE);
            break;
        }
        if (worker.peer.phase == PEER_CONNECTING &&
            (FD_ISSET(worker.peer.socket, &write_set) ||
             FD_ISSET(worker.peer.socket, &error_set))) {
            if (!client_finish_connect(context, &worker)) {
                set_context_error(context, LIVE_ERROR_NETWORK,
                                  WSAGetLastError(),
                                  "Could not connect to the live host", TRUE);
                break;
            }
            continue;
        }
        if (FD_ISSET(worker.peer.socket, &error_set)) {
            client_queue_leave_event(context, &worker);
            set_context_error(context, LIVE_ERROR_NETWORK,
                              WSAGetLastError(),
                              "The connection to the live host was lost", TRUE);
            break;
        }
        if (FD_ISSET(worker.peer.socket, &write_set) &&
            peer_flush(&worker.peer) < 0) {
            client_queue_leave_event(context, &worker);
            set_context_error(context, LIVE_ERROR_NETWORK,
                              WSAGetLastError(),
                              "Could not send live session data", TRUE);
            break;
        }
        if (FD_ISSET(worker.peer.socket, &read_set)) {
            int receive_result = peer_receive(context, LIVE_ROLE_CLIENT,
                                              &worker, &worker.peer);
            if (receive_result <= 0) {
                BOOL already_failed;
                EnterCriticalSection(&context->lock);
                already_failed = context->state == LIVE_STATE_ERROR;
                LeaveCriticalSection(&context->lock);
                if (receive_result == 0 && worker.graceful_close) {
                    orderly = TRUE;
                } else {
                    if (!worker.graceful_close && worker.peer.id != 0u) {
                        client_queue_leave_event(context, &worker);
                    }
                    if (!already_failed) {
                        set_context_error(
                            context, LIVE_ERROR_PROTOCOL, 0,
                            "The live host sent invalid data or closed the connection",
                            TRUE);
                    }
                }
                break;
            }
        }
        now = GetTickCount64();
        if ((worker.peer.phase == PEER_CONNECTING ||
             worker.peer.phase == PEER_WAIT_WELCOME ||
             worker.peer.phase == PEER_WAIT_SNAPSHOT ||
             (worker.peer.phase == PEER_READY &&
              worker.peer.rx.header_used != 0u)) &&
            worker.peer.deadline != 0u && now >= worker.peer.deadline) {
            if (worker.peer.phase == PEER_CONNECTING &&
                client_try_next_address(context, &worker)) {
                continue;
            }
            set_context_error(context, LIVE_ERROR_NETWORK, WSAETIMEDOUT,
                              "The live connection timed out", TRUE);
            break;
        }
        if (worker.peer.phase == PEER_READY) {
            if (worker.peer.awaiting_pong &&
                worker.peer.heartbeat_deadline != 0u &&
                now >= worker.peer.heartbeat_deadline &&
                worker.peer.rx.header_used == 0u) {
                client_queue_leave_event(context, &worker);
                set_context_error(context, LIVE_ERROR_NETWORK, WSAETIMEDOUT,
                                  "The live host stopped responding", TRUE);
                break;
            }
            if (!worker.peer.awaiting_pong &&
                worker.peer.send_head == NULL &&
                worker.peer.heartbeat_due != 0u &&
                now >= worker.peer.heartbeat_due) {
                if (!peer_queue_frame(context, &worker.peer, WIRE_PING, 0u,
                                      0u, 0u, NULL, 0u)) {
                    client_queue_leave_event(context, &worker);
                    set_context_error(context, LIVE_ERROR_BUSY,
                                      ERROR_NOT_ENOUGH_QUOTA,
                                      "Could not queue a live heartbeat", TRUE);
                    break;
                }
                worker.peer.awaiting_pong = TRUE;
                worker.peer.heartbeat_deadline =
                    now + (ULONGLONG)LIVE_HEARTBEAT_TIMEOUT_MS;
            }
        }
    }

    if (orderly && worker.peer.phase == PEER_READY) {
        (void)client_process_commands(context, &worker);
        peer_drain_for(&worker.peer, LIVE_SHUTDOWN_DATA_FLUSH_MS);
        (void)peer_queue_frame(context, &worker.peer, WIRE_GOODBYE, 0u, 0u,
                               0u, NULL, 0u);
        peer_drain_for(&worker.peer, LIVE_SHUTDOWN_GOODBYE_FLUSH_MS);
    }
    peer_close(&worker.peer);
    if (worker.addresses != NULL) {
        freeaddrinfo(worker.addresses);
    }
    return orderly;
}

static void worker_finish(LiveContext *context, BOOL orderly)
{
    EnterCriticalSection(&context->lock);
    context->worker_running = FALSE;
    context->client_count = 0u;
    if (orderly) {
        context->role = LIVE_ROLE_NONE;
        context->state = LIVE_STATE_STOPPED;
        context->peer_id = 0u;
        context->port = 0u;
        context->invitation[0] = '\0';
    }
    LeaveCriticalSection(&context->lock);
    queue_status_event(context);
}

static DWORD WINAPI live_worker_main(void *parameter)
{
    LiveContext *context = (LiveContext *)parameter;
    WSADATA data;
    LiveRole role;
    BOOL orderly = FALSE;

    EnterCriticalSection(&context->lock);
    context->worker_running = TRUE;
    role = context->role;
    LeaveCriticalSection(&context->lock);

    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        int error = WSAGetLastError();
        set_context_error(context, LIVE_ERROR_NETWORK, error,
                          "Winsock could not be initialized", TRUE);
        signal_startup(context, FALSE);
        worker_finish(context, FALSE);
        return 0u;
    }
    if (role == LIVE_ROLE_HOST) {
        orderly = host_worker_loop(context);
    } else if (role == LIVE_ROLE_CLIENT) {
        orderly = client_worker_loop(context);
    } else {
        set_context_error(context, LIVE_ERROR_INTERNAL, ERROR_INVALID_STATE,
                          "Live worker started without a role", TRUE);
        signal_startup(context, FALSE);
    }
    WSACleanup();
    worker_finish(context, orderly);
    return 0u;
}

static BOOL create_worker_handles(LiveContext *context)
{
    HANDLE stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE wake_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    HANDLE startup_event = CreateEventW(NULL, TRUE, FALSE, NULL);

    if (stop_event == NULL || wake_event == NULL || startup_event == NULL) {
        DWORD error = GetLastError();
        if (stop_event != NULL) {
            CloseHandle(stop_event);
        }
        if (wake_event != NULL) {
            CloseHandle(wake_event);
        }
        if (startup_event != NULL) {
            CloseHandle(startup_event);
        }
        return api_failure(context, LIVE_ERROR_INTERNAL, error,
                           "Could not create live worker events");
    }
    EnterCriticalSection(&context->lock);
    context->stop_event = stop_event;
    context->wake_event = wake_event;
    context->startup_event = startup_event;
    context->startup_success = FALSE;
    LeaveCriticalSection(&context->lock);
    return TRUE;
}

static void close_worker_handles(LiveContext *context, BOOL close_thread)
{
    HANDLE thread;
    HANDLE stop_event;
    HANDLE wake_event;
    HANDLE startup_event;

    EnterCriticalSection(&context->lock);
    thread = context->thread;
    stop_event = context->stop_event;
    wake_event = context->wake_event;
    startup_event = context->startup_event;
    if (close_thread) {
        context->thread = NULL;
    }
    context->stop_event = NULL;
    context->wake_event = NULL;
    context->startup_event = NULL;
    LeaveCriticalSection(&context->lock);
    if (close_thread && thread != NULL) {
        CloseHandle(thread);
    }
    if (stop_event != NULL) {
        CloseHandle(stop_event);
    }
    if (wake_event != NULL) {
        CloseHandle(wake_event);
    }
    if (startup_event != NULL) {
        CloseHandle(startup_event);
    }
}

void live_stop(LiveContext *context)
{
    HANDLE thread;
    HANDLE stop_event;
    HANDLE wake_event;
    BOOL had_session;

    if (context == NULL) {
        return;
    }
    EnterCriticalSection(&context->lifecycle_lock);
    EnterCriticalSection(&context->lock);
    thread = context->thread;
    stop_event = context->stop_event;
    wake_event = context->wake_event;
    had_session = thread != NULL || context->role != LIVE_ROLE_NONE ||
                  context->state != LIVE_STATE_STOPPED;
    if (thread != NULL && context->worker_running &&
        context->state != LIVE_STATE_STOPPING) {
        context->state = LIVE_STATE_STOPPING;
    }
    LeaveCriticalSection(&context->lock);
    if (thread != NULL) {
        queue_status_event(context);
        if (stop_event != NULL) {
            SetEvent(stop_event);
        }
        if (wake_event != NULL) {
            SetEvent(wake_event);
        }
        (void)WaitForSingleObject(thread, INFINITE);
        close_worker_handles(context, TRUE);
    } else if (stop_event != NULL || wake_event != NULL ||
               context->startup_event != NULL) {
        close_worker_handles(context, FALSE);
    }

    EnterCriticalSection(&context->lock);
    free_commands_locked(context);
    free_publications_locked(context);
    secure_zero(context->document, context->document_size);
    free(context->document);
    context->document = NULL;
    context->document_size = 0u;
    context->document_generation = 0u;
    context->broadcast_revision = 0u;
    context->role = LIVE_ROLE_NONE;
    context->state = LIVE_STATE_STOPPED;
    context->worker_running = FALSE;
    context->client_count = 0u;
    context->port = 0u;
    context->revision = 0u;
    context->peer_id = 0u;
    secure_zero(context->invitation, sizeof(context->invitation));
    secure_zero(context->advertised_host, sizeof(context->advertised_host));
    secure_zero(context->connect_host, sizeof(context->connect_host));
    secure_zero(context->display_name, sizeof(context->display_name));
    secure_zero(context->token, sizeof(context->token));
    LeaveCriticalSection(&context->lock);
    if (had_session) {
        queue_status_event(context);
    }
    LeaveCriticalSection(&context->lifecycle_lock);
}

void live_destroy(LiveContext *context)
{
    if (context == NULL) {
        return;
    }
    live_stop(context);
    EnterCriticalSection(&context->lock);
    free_events_locked(context);
    LeaveCriticalSection(&context->lock);
    DeleteCriticalSection(&context->lock);
    DeleteCriticalSection(&context->lifecycle_lock);
    free(context);
}

BOOL live_host_start(LiveContext *context, uint16_t port,
                     const char *advertised_host,
                     const uint8_t token16[LIVE_TOKEN_SIZE],
                     const void *initial_data, size_t initial_size,
                     uint64_t initial_revision)
{
    static const char default_host[] = "127.0.0.1";
    unsigned char *document = NULL;
    uint8_t token[LIVE_TOKEN_SIZE];
    const char *host = advertised_host;
    HANDLE thread;
    HANDLE startup_event;
    DWORD wait_result;
    BOOL startup_success;

    if (context == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    EnterCriticalSection(&context->lifecycle_lock);
    if (initial_size > LIVE_MAX_DOCUMENT_SIZE) {
        LeaveCriticalSection(&context->lifecycle_lock);
        return api_failure(context, LIVE_ERROR_DOCUMENT_TOO_LARGE,
                           ERROR_FILE_TOO_LARGE,
                           "The live document exceeds 16 MiB");
    }
    if (initial_size != 0u && initial_data == NULL) {
        LeaveCriticalSection(&context->lifecycle_lock);
        return api_failure(context, LIVE_ERROR_INVALID_ARGUMENT,
                           ERROR_INVALID_PARAMETER,
                           "Initial live document data is missing");
    }
    if (host == NULL || host[0] == '\0') {
        host = default_host;
    }
    if (!valid_host(host, NULL, NULL)) {
        LeaveCriticalSection(&context->lifecycle_lock);
        return api_failure(context, LIVE_ERROR_INVALID_ARGUMENT,
                           ERROR_INVALID_NAME,
                           "The advertised live host is invalid");
    }
    if (initial_size != 0u) {
        document = (unsigned char *)malloc(initial_size);
        if (document == NULL) {
            LeaveCriticalSection(&context->lifecycle_lock);
            return api_failure(context, LIVE_ERROR_OUT_OF_MEMORY,
                               ERROR_NOT_ENOUGH_MEMORY,
                               "Could not copy the initial live document");
        }
        memcpy(document, initial_data, initial_size);
    }
    if (token16 != NULL) {
        memcpy(token, token16, sizeof(token));
    } else if (!secure_random_bytes(token, sizeof(token))) {
        secure_zero(document, initial_size);
        free(document);
        LeaveCriticalSection(&context->lifecycle_lock);
        return api_failure(context, LIVE_ERROR_INTERNAL, GetLastError(),
                           "Could not generate a secure invitation token");
    }
    live_stop(context);
    if (!create_worker_handles(context)) {
        secure_zero(token, sizeof(token));
        secure_zero(document, initial_size);
        free(document);
        LeaveCriticalSection(&context->lifecycle_lock);
        return FALSE;
    }

    EnterCriticalSection(&context->lock);
    context->role = LIVE_ROLE_HOST;
    context->state = LIVE_STATE_STARTING;
    context->worker_running = TRUE;
    context->last_error = LIVE_ERROR_NONE;
    context->last_system_error = 0;
    context->error_message[0] = '\0';
    context->client_count = 0u;
    context->port = port;
    context->revision = initial_revision;
    context->inbound_count = 0u;
    context->outbound_count = 0u;
    context->peer_id = 0u;
    context->invitation[0] = '\0';
    memcpy(context->token, token, sizeof(token));
    copy_text(context->advertised_host, sizeof(context->advertised_host), host);
    context->document = document;
    context->document_size = initial_size;
    context->document_generation = 1u;
    context->broadcast_revision = initial_revision;
    startup_event = context->startup_event;
    LeaveCriticalSection(&context->lock);
    secure_zero(token, sizeof(token));
    queue_status_event(context);

    thread = CreateThread(NULL, 0u, live_worker_main, context, 0u, NULL);
    if (thread == NULL) {
        DWORD error = GetLastError();
        EnterCriticalSection(&context->lock);
        context->worker_running = FALSE;
        context->state = LIVE_STATE_ERROR;
        LeaveCriticalSection(&context->lock);
        close_worker_handles(context, FALSE);
        LeaveCriticalSection(&context->lifecycle_lock);
        return api_failure(context, LIVE_ERROR_INTERNAL, error,
                           "Could not start the live host worker");
    }
    EnterCriticalSection(&context->lock);
    context->thread = thread;
    LeaveCriticalSection(&context->lock);
    wait_result = WaitForSingleObject(startup_event, 10000u);
    EnterCriticalSection(&context->lock);
    startup_success = context->startup_success;
    LeaveCriticalSection(&context->lock);
    if (wait_result != WAIT_OBJECT_0 || !startup_success) {
        if (wait_result != WAIT_OBJECT_0) {
            set_context_error(context, LIVE_ERROR_NETWORK, WSAETIMEDOUT,
                              "The live host did not start in time", TRUE);
            SetEvent(context->stop_event);
            SetEvent(context->wake_event);
        }
        (void)WaitForSingleObject(thread, INFINITE);
        SetLastError(wait_result == WAIT_OBJECT_0 ? ERROR_OPEN_FAILED
                                                  : WAIT_TIMEOUT);
        LeaveCriticalSection(&context->lifecycle_lock);
        return FALSE;
    }
    LeaveCriticalSection(&context->lifecycle_lock);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

BOOL live_client_join(LiveContext *context, const char *invitation,
                      const char *display_name)
{
    char host[LIVE_MAX_HOST_LENGTH + 1u];
    uint16_t port;
    uint8_t token[LIVE_TOKEN_SIZE];
    const char *name = display_name;
    size_t name_length;
    HANDLE thread;

    if (context == NULL || invitation == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!live_parse_invitation(invitation, host, sizeof(host), &port, token)) {
        return api_failure(context, LIVE_ERROR_INVALID_INVITATION,
                           GetLastError(), "The live invitation is invalid");
    }
    if (name == NULL || name[0] == '\0') {
        name = "Guest";
    }
    name_length = bounded_length(name, LIVE_MAX_DISPLAY_NAME + 1u);
    if (name_length == 0u || name_length > LIVE_MAX_DISPLAY_NAME ||
        name[name_length] != '\0' ||
        !valid_utf8_name((const unsigned char *)name, name_length)) {
        secure_zero(token, sizeof(token));
        return api_failure(context, LIVE_ERROR_INVALID_ARGUMENT,
                           ERROR_INVALID_NAME,
                           "The live display name is invalid");
    }

    EnterCriticalSection(&context->lifecycle_lock);
    live_stop(context);
    if (!create_worker_handles(context)) {
        secure_zero(token, sizeof(token));
        LeaveCriticalSection(&context->lifecycle_lock);
        return FALSE;
    }
    EnterCriticalSection(&context->lock);
    context->role = LIVE_ROLE_CLIENT;
    context->state = LIVE_STATE_CONNECTING;
    context->worker_running = TRUE;
    context->last_error = LIVE_ERROR_NONE;
    context->last_system_error = 0;
    context->error_message[0] = '\0';
    context->client_count = 0u;
    context->port = port;
    context->revision = 0u;
    context->inbound_count = 0u;
    context->outbound_count = 0u;
    context->peer_id = 0u;
    context->invitation[0] = '\0';
    memcpy(context->token, token, sizeof(token));
    copy_text(context->connect_host, sizeof(context->connect_host), host);
    copy_text(context->display_name, sizeof(context->display_name), name);
    LeaveCriticalSection(&context->lock);
    secure_zero(token, sizeof(token));
    queue_status_event(context);

    thread = CreateThread(NULL, 0u, live_worker_main, context, 0u, NULL);
    if (thread == NULL) {
        DWORD error = GetLastError();
        EnterCriticalSection(&context->lock);
        context->worker_running = FALSE;
        context->state = LIVE_STATE_ERROR;
        LeaveCriticalSection(&context->lock);
        close_worker_handles(context, FALSE);
        LeaveCriticalSection(&context->lifecycle_lock);
        return api_failure(context, LIVE_ERROR_INTERNAL, error,
                           "Could not start the live client worker");
    }
    EnterCriticalSection(&context->lock);
    context->thread = thread;
    LeaveCriticalSection(&context->lock);
    LeaveCriticalSection(&context->lifecycle_lock);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

BOOL live_host_publish(LiveContext *context, const void *data, size_t size)
{
    unsigned char *copy = NULL;
    HostPublication *publication = NULL;
    WireBuffer *buffer = NULL;
    HANDLE wake_event;

    if (context == NULL || (size != 0u && data == NULL)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (size > LIVE_MAX_DOCUMENT_SIZE) {
        return api_failure(context, LIVE_ERROR_DOCUMENT_TOO_LARGE,
                           ERROR_FILE_TOO_LARGE,
                           "The live document exceeds 16 MiB");
    }
    if (size != 0u) {
        copy = (unsigned char *)malloc(size);
        if (copy == NULL) {
            return api_failure(context, LIVE_ERROR_OUT_OF_MEMORY,
                               ERROR_NOT_ENOUGH_MEMORY,
                               "Could not copy the live document");
        }
        memcpy(copy, data, size);
    }
    publication = (HostPublication *)calloc(1u, sizeof(*publication));
    buffer = wire_buffer_create(WIRE_SNAPSHOT, 0u, 0u, 0u, data, size);
    if (publication == NULL || buffer == NULL) {
        free(publication);
        wire_buffer_release(buffer);
        secure_zero(copy, size);
        free(copy);
        return api_failure(context, LIVE_ERROR_OUT_OF_MEMORY,
                           ERROR_NOT_ENOUGH_MEMORY,
                           "Could not queue the live document");
    }
    publication->buffer = buffer;
    EnterCriticalSection(&context->lock);
    if (context->role != LIVE_ROLE_HOST ||
        context->state != LIVE_STATE_LISTENING || !context->worker_running) {
        LeaveCriticalSection(&context->lock);
        wire_buffer_release(buffer);
        free(publication);
        secure_zero(copy, size);
        free(copy);
        return api_failure(context, LIVE_ERROR_BUSY, ERROR_INVALID_STATE,
                           "This context is not an active live host");
    }
    if (context->revision == UINT64_MAX) {
        LeaveCriticalSection(&context->lock);
        wire_buffer_release(buffer);
        free(publication);
        secure_zero(copy, size);
        free(copy);
        return api_failure(context, LIVE_ERROR_REVISION_OVERFLOW,
                           ERROR_ARITHMETIC_OVERFLOW,
                           "The live revision cannot be advanced");
    }
    if (context->publication_count >= LIVE_PUBLICATION_QUEUE_MAX ||
        buffer->size > LIVE_PUBLICATION_QUEUE_MAX_BYTES -
                           context->publication_bytes) {
        LeaveCriticalSection(&context->lock);
        wire_buffer_release(buffer);
        free(publication);
        secure_zero(copy, size);
        free(copy);
        SetLastError(ERROR_NOT_ENOUGH_QUOTA);
        return FALSE;
    }
    secure_zero(context->document, context->document_size);
    free(context->document);
    context->document = copy;
    context->document_size = size;
    ++context->revision;
    ++context->document_generation;
    if (context->document_generation == 0u) {
        context->document_generation = 1u;
    }
    publication->revision = context->revision;
    publication->generation = context->document_generation;
    write_u64(buffer->bytes + 16u, publication->revision);
    if (context->publication_tail != NULL) {
        context->publication_tail->next = publication;
    } else {
        context->publication_head = publication;
    }
    context->publication_tail = publication;
    ++context->publication_count;
    context->publication_bytes += buffer->size;
    wake_event = context->wake_event;
    LeaveCriticalSection(&context->lock);
    queue_status_event(context);
    if (wake_event != NULL) {
        SetEvent(wake_event);
    }
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

BOOL live_client_submit(LiveContext *context, const void *data, size_t size,
                        uint64_t base_revision)
{
    LiveCommand *command;
    HANDLE wake_event;

    if (context == NULL || (size != 0u && data == NULL)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (size > LIVE_MAX_DOCUMENT_SIZE) {
        return api_failure(context, LIVE_ERROR_DOCUMENT_TOO_LARGE,
                           ERROR_FILE_TOO_LARGE,
                           "The proposed live document exceeds 16 MiB");
    }
    command = (LiveCommand *)calloc(1u, sizeof(*command));
    if (command == NULL) {
        return api_failure(context, LIVE_ERROR_OUT_OF_MEMORY,
                           ERROR_NOT_ENOUGH_MEMORY,
                           "Could not queue the live edit");
    }
    if (size != 0u) {
        command->data = (unsigned char *)malloc(size);
        if (command->data == NULL) {
            free(command);
            return api_failure(context, LIVE_ERROR_OUT_OF_MEMORY,
                               ERROR_NOT_ENOUGH_MEMORY,
                               "Could not copy the live edit");
        }
        memcpy(command->data, data, size);
    }
    command->size = size;
    command->base_revision = base_revision;

    EnterCriticalSection(&context->lock);
    if (context->role != LIVE_ROLE_CLIENT ||
        context->state != LIVE_STATE_CONNECTED || !context->worker_running) {
        LeaveCriticalSection(&context->lock);
        secure_zero(command->data, command->size);
        free(command->data);
        free(command);
        return api_failure(context, LIVE_ERROR_BUSY, ERROR_INVALID_STATE,
                           "This context is not a connected live client");
    }
    if (context->command_count >= LIVE_COMMAND_QUEUE_MAX ||
        size > LIVE_COMMAND_QUEUE_MAX_BYTES - context->command_bytes) {
        LeaveCriticalSection(&context->lock);
        secure_zero(command->data, command->size);
        free(command->data);
        free(command);
        return api_failure(context, LIVE_ERROR_BUSY, ERROR_NOT_ENOUGH_QUOTA,
                           "The outgoing live queue is full");
    }
    if (context->command_tail != NULL) {
        context->command_tail->next = command;
    } else {
        context->command_head = command;
    }
    context->command_tail = command;
    ++context->command_count;
    context->command_bytes += size;
    wake_event = context->wake_event;
    LeaveCriticalSection(&context->lock);
    if (wake_event != NULL) {
        SetEvent(wake_event);
    }
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}
