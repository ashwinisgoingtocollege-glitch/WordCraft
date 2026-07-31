#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "live.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROBE_TIMEOUT_MS 8000u
#define HEARTBEAT_TIMEOUT_MS 16000u

static LiveEvent *wait_for_event(LiveContext *context, LiveEventType type,
                                 DWORD timeout_ms)
{
    ULONGLONG deadline = GetTickCount64() + timeout_ms;

    do {
        LiveEvent *event;
        while ((event = live_pop_event(context)) != NULL) {
            if (event->type == type) {
                return event;
            }
            live_event_free(event);
        }
        Sleep(5u);
    } while (GetTickCount64() < deadline);
    return NULL;
}

static LiveEvent *wait_for_error(LiveContext *context, LiveError error,
                                 DWORD timeout_ms)
{
    ULONGLONG deadline = GetTickCount64() + timeout_ms;

    do {
        LiveEvent *event;
        while ((event = live_pop_event(context)) != NULL) {
            if (event->type == LIVE_EVENT_ERROR && event->error == error) {
                return event;
            }
            live_event_free(event);
        }
        Sleep(5u);
    } while (GetTickCount64() < deadline);
    return NULL;
}

static BOOL wait_for_host_count(LiveContext *host, uint32_t expected,
                                DWORD timeout_ms)
{
    ULONGLONG deadline = GetTickCount64() + timeout_ms;
    LiveStatus status;

    do {
        if (live_get_status(host, &status) &&
            status.client_count == expected) {
            return TRUE;
        }
        Sleep(5u);
    } while (GetTickCount64() < deadline);
    return FALSE;
}

static BOOL payload_is(const LiveEvent *event, const void *bytes, size_t size)
{
    return event != NULL && event->data_size == size &&
           (size == 0u || memcmp(event->data, bytes, size) == 0);
}

static void probe_write_u16(unsigned char *bytes, uint16_t value)
{
    bytes[0] = (unsigned char)(value >> 8u);
    bytes[1] = (unsigned char)value;
}

static void probe_write_u32(unsigned char *bytes, uint32_t value)
{
    bytes[0] = (unsigned char)(value >> 24u);
    bytes[1] = (unsigned char)(value >> 16u);
    bytes[2] = (unsigned char)(value >> 8u);
    bytes[3] = (unsigned char)value;
}

static BOOL socket_send_all(SOCKET socket_value, const unsigned char *data,
                            size_t size)
{
    size_t sent = 0u;

    while (sent < size) {
        int request = (int)(size - sent);
        int result = send(socket_value, (const char *)data + sent, request, 0);
        if (result <= 0) {
            return FALSE;
        }
        sent += (size_t)result;
    }
    return TRUE;
}

static SOCKET open_unresponsive_client(uint16_t port,
                                       const uint8_t token[LIVE_TOKEN_SIZE])
{
    static const unsigned char name[] = "Idle raw peer";
    unsigned char hello[32u + LIVE_TOKEN_SIZE + sizeof(name) - 1u];
    struct sockaddr_in address;
    SOCKET socket_value;

    socket_value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_value == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(socket_value, (const struct sockaddr *)&address,
                (int)sizeof(address)) == SOCKET_ERROR) {
        closesocket(socket_value);
        return INVALID_SOCKET;
    }
    memset(hello, 0, sizeof(hello));
    probe_write_u32(hello, UINT32_C(0x57434c56));
    probe_write_u16(hello + 4u, 1u);
    probe_write_u16(hello + 6u, 1u);
    probe_write_u32(hello + 8u,
                    LIVE_TOKEN_SIZE + (uint32_t)(sizeof(name) - 1u));
    memcpy(hello + 32u, token, LIVE_TOKEN_SIZE);
    memcpy(hello + 32u + LIVE_TOKEN_SIZE, name, sizeof(name) - 1u);
    if (!socket_send_all(socket_value, hello, sizeof(hello))) {
        closesocket(socket_value);
        return INVALID_SOCKET;
    }
    return socket_value;
}

int wmain(void)
{
    static const uint8_t token[LIVE_TOKEN_SIZE] = {
        0x00u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u,
        0x88u, 0x99u, 0xaau, 0xbbu, 0xccu, 0xddu, 0xeeu, 0xffu};
    static const unsigned char initial[] = "initial canonical";
    static const unsigned char published[] = "host publication";
    static const unsigned char proposal[] = "fresh client proposal";
    static const unsigned char canonical_two[] = "second canonical";
    static const unsigned char stale_proposal[] = "stale client proposal";
    static const unsigned char final_proposal[] = "final queued proposal";
    static const unsigned char final_publication[] = "final queued publication";
    LiveContext *host = NULL;
    LiveContext *client = NULL;
    LiveContext *bad_client = NULL;
    LiveContext *resolver_client = NULL;
    LiveContext *crowd[LIVE_MAX_CLIENTS + 1u];
    LiveEvent *event = NULL;
    LiveStatus status;
    char invitation[LIVE_INVITATION_CAPACITY];
    char bad_invitation[LIVE_INVITATION_CAPACITY];
    char parsed_host[LIVE_MAX_HOST_LENGTH + 1u];
    uint8_t parsed_token[LIVE_TOKEN_SIZE];
    uint8_t bad_token[LIVE_TOKEN_SIZE];
    uint16_t parsed_port = 0u;
    uint32_t origin_peer = 0u;
    size_t required = 0u;
    size_t index;
    SOCKET idle_socket = INVALID_SOCKET;
    WSADATA winsock_data;
    BOOL winsock_started = FALSE;
    int result = 1;

    memset(crowd, 0, sizeof(crowd));
    if (WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0) {
        fprintf(stderr, "probe Winsock startup failed\n");
        goto cleanup;
    }
    winsock_started = TRUE;
    if (!live_format_invitation("localhost", 4242u, token, invitation,
                                sizeof(invitation), &required) ||
        strcmp(invitation,
               "wordcraft://localhost:4242/00112233445566778899aabbccddeeff") !=
            0 ||
        required != strlen(invitation) + 1u ||
        !live_parse_invitation(invitation, parsed_host, sizeof(parsed_host),
                               &parsed_port, parsed_token) ||
        strcmp(parsed_host, "localhost") != 0 || parsed_port != 4242u ||
        memcmp(parsed_token, token, sizeof(token)) != 0) {
        fprintf(stderr, "invitation round trip failed (error %lu)\n",
                (unsigned long)GetLastError());
        goto cleanup;
    }
    if (live_format_invitation("localhost", 4242u, token, NULL, 0u,
                               &required) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
        required != strlen(invitation) + 1u) {
        fprintf(stderr, "invitation capacity query failed\n");
        goto cleanup;
    }

    resolver_client = live_create(NULL, 0u);
    if (resolver_client == NULL ||
        !live_format_invitation("wordcraft-resolver-cancellation.invalid",
                                9u, token, bad_invitation,
                                sizeof(bad_invitation), NULL) ||
        !live_client_join(resolver_client, bad_invitation,
                          "Resolver cancellation")) {
        fprintf(stderr, "resolver cancellation setup failed\n");
        goto cleanup;
    }
    Sleep(20u);
    {
        ULONGLONG stop_started = GetTickCount64();
        live_stop(resolver_client);
        if (GetTickCount64() - stop_started > 1000u) {
            fprintf(stderr, "resolver cancellation blocked live_stop\n");
            goto cleanup;
        }
    }
    live_destroy(resolver_client);
    resolver_client = NULL;

    host = live_create(NULL, 0u);
    client = live_create(NULL, 0u);
    bad_client = live_create(NULL, 0u);
    if (host == NULL || client == NULL || bad_client == NULL) {
        fprintf(stderr, "live_create failed\n");
        goto cleanup;
    }
    if (!live_host_start(host, 0u, "127.0.0.1", token, initial,
                         sizeof(initial) - 1u, 7u) ||
        !live_get_status(host, &status) ||
        status.role != LIVE_ROLE_HOST ||
        status.state != LIVE_STATE_LISTENING || status.port == 0u ||
        status.revision != 7u || !status.worker_running ||
        !live_get_invitation(host, invitation, sizeof(invitation), &required)) {
        fprintf(stderr, "host startup failed (error %lu)\n",
                (unsigned long)GetLastError());
        goto cleanup;
    }

    memcpy(bad_token, token, sizeof(bad_token));
    bad_token[0] ^= 0xffu;
    if (!live_format_invitation("127.0.0.1", status.port, bad_token,
                                bad_invitation, sizeof(bad_invitation), NULL) ||
        !live_client_join(bad_client, bad_invitation, "Bad token")) {
        fprintf(stderr, "bad-token client could not start\n");
        goto cleanup;
    }
    event = wait_for_error(bad_client, LIVE_ERROR_AUTHENTICATION,
                           PROBE_TIMEOUT_MS);
    if (event == NULL) {
        fprintf(stderr, "bad token was not rejected as authentication\n");
        goto cleanup;
    }
    live_event_free(event);
    event = NULL;
    live_stop(bad_client);
    live_stop(bad_client);

    if (!live_client_join(client, invitation, "Probe client")) {
        fprintf(stderr, "client join could not start\n");
        goto cleanup;
    }
    event = wait_for_event(host, LIVE_EVENT_PEER_JOINED, PROBE_TIMEOUT_MS);
    if (event == NULL || event->peer_id == 0u ||
        strcmp(event->display_name, "Probe client") != 0) {
        fprintf(stderr, "host did not report authenticated join\n");
        goto cleanup;
    }
    origin_peer = event->peer_id;
    live_event_free(event);
    event = wait_for_event(client, LIVE_EVENT_SNAPSHOT, PROBE_TIMEOUT_MS);
    if (!payload_is(event, initial, sizeof(initial) - 1u) ||
        event->revision != 7u) {
        fprintf(stderr, "initial canonical snapshot was incorrect\n");
        goto cleanup;
    }
    live_event_free(event);
    event = NULL;
    if (!live_get_status(client, &status) ||
        status.state != LIVE_STATE_CONNECTED || status.peer_id != origin_peer ||
        status.revision != 7u) {
        fprintf(stderr, "client connected status was incorrect\n");
        goto cleanup;
    }

    if (!live_host_publish(host, published, sizeof(published) - 1u)) {
        fprintf(stderr, "host publish failed\n");
        goto cleanup;
    }
    event = wait_for_event(client, LIVE_EVENT_SNAPSHOT, PROBE_TIMEOUT_MS);
    if (!payload_is(event, published, sizeof(published) - 1u) ||
        event->revision != 8u) {
        fprintf(stderr, "published canonical snapshot was incorrect\n");
        goto cleanup;
    }
    live_event_free(event);
    event = NULL;

    if (!live_client_submit(client, proposal, sizeof(proposal) - 1u, 8u)) {
        fprintf(stderr, "fresh proposal submission failed\n");
        goto cleanup;
    }
    event = wait_for_event(host, LIVE_EVENT_CLIENT_EDIT, PROBE_TIMEOUT_MS);
    if (!payload_is(event, proposal, sizeof(proposal) - 1u) || event->stale ||
        event->peer_id != origin_peer || event->base_revision != 8u ||
        event->revision != 8u) {
        fprintf(stderr, "fresh proposal metadata was incorrect\n");
        goto cleanup;
    }
    live_event_free(event);
    event = NULL;
    if (!live_get_status(host, &status) || status.revision != 8u) {
        fprintf(stderr, "proposal incorrectly changed canonical revision\n");
        goto cleanup;
    }

    if (!live_host_publish(host, canonical_two, sizeof(canonical_two) - 1u)) {
        fprintf(stderr, "second host publish failed\n");
        goto cleanup;
    }
    event = wait_for_event(client, LIVE_EVENT_SNAPSHOT, PROBE_TIMEOUT_MS);
    if (!payload_is(event, canonical_two, sizeof(canonical_two) - 1u) ||
        event->revision != 9u) {
        fprintf(stderr, "second canonical snapshot was incorrect\n");
        goto cleanup;
    }
    live_event_free(event);
    event = NULL;
    if (!live_client_submit(client, stale_proposal,
                            sizeof(stale_proposal) - 1u, 8u)) {
        fprintf(stderr, "stale proposal submission failed\n");
        goto cleanup;
    }
    event = wait_for_event(host, LIVE_EVENT_CLIENT_EDIT, PROBE_TIMEOUT_MS);
    if (!payload_is(event, stale_proposal, sizeof(stale_proposal) - 1u) ||
        !event->stale || event->peer_id != origin_peer ||
        event->base_revision != 8u || event->revision != 9u) {
        fprintf(stderr, "stale proposal was not marked correctly\n");
        goto cleanup;
    }
    live_event_free(event);
    event = NULL;

    if (!live_client_submit(client, final_proposal,
                            sizeof(final_proposal) - 1u, 9u)) {
        fprintf(stderr, "final proposal could not be queued\n");
        goto cleanup;
    }
    live_stop(client);
    live_stop(client);
    event = wait_for_event(host, LIVE_EVENT_CLIENT_EDIT, PROBE_TIMEOUT_MS);
    if (!payload_is(event, final_proposal, sizeof(final_proposal) - 1u) ||
        event->stale || event->peer_id != origin_peer ||
        event->base_revision != 9u || event->revision != 9u) {
        fprintf(stderr, "queued proposal was dropped during shutdown\n");
        goto cleanup;
    }
    live_event_free(event);
    event = NULL;
    event = wait_for_event(host, LIVE_EVENT_PEER_LEFT, PROBE_TIMEOUT_MS);
    if (event == NULL || event->peer_id != origin_peer ||
        !wait_for_host_count(host, 0u, PROBE_TIMEOUT_MS)) {
        fprintf(stderr, "host did not report client leave\n");
        goto cleanup;
    }
    live_event_free(event);
    event = NULL;

    if (!live_client_join(client, invitation, "Heartbeat responder")) {
        fprintf(stderr, "heartbeat responder could not join\n");
        goto cleanup;
    }
    event = wait_for_event(client, LIVE_EVENT_SNAPSHOT, PROBE_TIMEOUT_MS);
    if (!payload_is(event, canonical_two, sizeof(canonical_two) - 1u) ||
        event->revision != 9u) {
        fprintf(stderr, "heartbeat responder did not receive a snapshot\n");
        goto cleanup;
    }
    live_event_free(event);
    event = NULL;
    idle_socket = open_unresponsive_client(status.port, token);
    if (idle_socket == INVALID_SOCKET ||
        !wait_for_host_count(host, 2u, PROBE_TIMEOUT_MS) ||
        !wait_for_host_count(host, 1u, HEARTBEAT_TIMEOUT_MS) ||
        !live_get_status(client, &status) ||
        status.state != LIVE_STATE_CONNECTED) {
        fprintf(stderr, "unresponsive authenticated peer was not evicted\n");
        goto cleanup;
    }
    closesocket(idle_socket);
    idle_socket = INVALID_SOCKET;
    live_stop(client);
    if (!wait_for_host_count(host, 0u, PROBE_TIMEOUT_MS)) {
        fprintf(stderr, "heartbeat responder did not leave cleanly\n");
        goto cleanup;
    }

    /* Fill all authenticated slots, then verify that client 33 is rejected. */
    for (index = 0u; index < LIVE_MAX_CLIENTS; ++index) {
        char name[32];
        (void)snprintf(name, sizeof(name), "Capacity client %u",
                       (unsigned int)(index + 1u));
        crowd[index] = live_create(NULL, 0u);
        if (crowd[index] == NULL ||
            !live_client_join(crowd[index], invitation, name)) {
            fprintf(stderr, "capacity client %u could not start\n",
                    (unsigned int)(index + 1u));
            goto cleanup;
        }
        event = wait_for_event(crowd[index], LIVE_EVENT_SNAPSHOT,
                               PROBE_TIMEOUT_MS);
        if (!payload_is(event, canonical_two, sizeof(canonical_two) - 1u) ||
            event->revision != 9u) {
            fprintf(stderr, "capacity client %u did not authenticate\n",
                    (unsigned int)(index + 1u));
            goto cleanup;
        }
        live_event_free(event);
        event = NULL;
    }
    if (!wait_for_host_count(host, LIVE_MAX_CLIENTS, PROBE_TIMEOUT_MS)) {
        fprintf(stderr, "host did not reach exactly 32 clients\n");
        goto cleanup;
    }
    crowd[LIVE_MAX_CLIENTS] = live_create(NULL, 0u);
    if (crowd[LIVE_MAX_CLIENTS] == NULL ||
        !live_client_join(crowd[LIVE_MAX_CLIENTS], invitation,
                          "Capacity overflow")) {
        fprintf(stderr, "overflow client could not start\n");
        goto cleanup;
    }
    event = wait_for_error(crowd[LIVE_MAX_CLIENTS], LIVE_ERROR_CLIENT_LIMIT,
                           PROBE_TIMEOUT_MS);
    if (event == NULL) {
        (void)live_get_status(crowd[LIVE_MAX_CLIENTS], &status);
        fprintf(stderr,
                "client 33 was not rejected by the exact cap "
                "(state=%d error=%d sys=%d message=%s)\n",
                (int)status.state, (int)status.last_error,
                status.last_system_error, status.error_message);
        goto cleanup;
    }
    live_event_free(event);
    event = NULL;
    if (!live_get_status(host, &status) ||
        status.client_count != LIVE_MAX_CLIENTS || status.revision != 9u) {
        fprintf(stderr, "capacity rejection changed authoritative state\n");
        goto cleanup;
    }
    for (index = 0u; index < LIVE_MAX_CLIENTS + 1u; ++index) {
        live_destroy(crowd[index]);
        crowd[index] = NULL;
    }
    if (!wait_for_host_count(host, 0u, PROBE_TIMEOUT_MS)) {
        fprintf(stderr, "capacity clients did not leave cleanly\n");
        goto cleanup;
    }

    if (!live_client_join(client, invitation, "Final flush client")) {
        fprintf(stderr, "final flush client could not join\n");
        goto cleanup;
    }
    event = wait_for_event(client, LIVE_EVENT_SNAPSHOT, PROBE_TIMEOUT_MS);
    if (!payload_is(event, canonical_two, sizeof(canonical_two) - 1u) ||
        event->revision != 9u) {
        fprintf(stderr, "final flush client did not receive canonical state\n");
        goto cleanup;
    }
    live_event_free(event);
    event = NULL;
    if (!live_host_publish(host, final_publication,
                           sizeof(final_publication) - 1u)) {
        fprintf(stderr, "final publication could not be queued\n");
        goto cleanup;
    }
    live_stop(host);
    live_stop(host);
    event = wait_for_event(client, LIVE_EVENT_SNAPSHOT, PROBE_TIMEOUT_MS);
    if (!payload_is(event, final_publication,
                    sizeof(final_publication) - 1u) ||
        event->revision != 10u) {
        fprintf(stderr, "queued publication was dropped during shutdown\n");
        goto cleanup;
    }
    live_event_free(event);
    event = NULL;
    live_stop(client);
    if (!live_get_status(host, &status) || status.role != LIVE_ROLE_NONE ||
        status.state != LIVE_STATE_STOPPED || status.worker_running ||
        status.client_count != 0u || status.peer_id != 0u) {
        fprintf(stderr, "clean host shutdown status was incorrect\n");
        goto cleanup;
    }
    printf("live_invite=ok auth=ok initial_snapshot=ok publish=ok "
           "proposal=ok stale=ok final_flush=ok resolver_cancel=ok "
           "heartbeat_evict=ok leave=ok cap32=ok shutdown=ok\n");
    result = 0;

cleanup:
    live_event_free(event);
    if (idle_socket != INVALID_SOCKET) {
        closesocket(idle_socket);
    }
    for (index = 0u; index < LIVE_MAX_CLIENTS + 1u; ++index) {
        live_destroy(crowd[index]);
    }
    live_destroy(bad_client);
    live_destroy(resolver_client);
    live_destroy(client);
    live_destroy(host);
    if (winsock_started) {
        WSACleanup();
    }
    return result;
}
