#include "DNS.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../../API/Source/Network.h"
#include "../../../API/Source/Process.h"
#include "../../../API/Source/WiFi.h"
#include "../../../../libc/I_libc/Source/include/string.h"

#define DNS_REMOTE_PORT 53u
#define DNS_EPHEMERAL_PORT_FIRST 49152u
#define DNS_EPHEMERAL_PORT_COUNT 16384u

#define DNS_QTYPE_A   1u
#define DNS_QCLASS_IN 1u

#define DNS_MAX_QUERY_BYTES 512u
#define DNS_MAX_UDP_RECV_BYTES (8u + DNS_MAX_QUERY_BYTES)
#define DNS_MAX_TCP_RECV_BYTES 4096u
#define DNS_CACHE_SIZE 16u
#define DNS_HOSTNAME_MAX 253u
#define DNS_REQUEST_TIMEOUT_MS 1000u
#define DNS_TCP_TIMEOUT_MS 3000u

typedef struct {
    bool used;
    char hostname[DNS_HOSTNAME_MAX + 1u];
    uint32_t server_ip;
    uint32_t address;
    uint64_t expires_ms;
} dns_cache_entry_t;

typedef struct {
    uint32_t address;
    uint32_t ttl_seconds;
    bool truncated;
} dns_parse_result_t;

static uint32_t g_default_dns_server = 0x0A000203u;
static uint64_t g_dns_random_state;
static dns_cache_entry_t g_dns_cache[DNS_CACHE_SIZE];
static uint32_t g_dns_cache_replace;

static uint16_t dns_read_u16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static uint32_t dns_read_u32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static bool dns_name_equal(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        char a = *left++;
        char b = *right++;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return *left == *right;
}

static uint64_t dns_random_next(void)
{
    uint64_t x = g_dns_random_state;
    if (x == 0u) {
        uint64_t stack_address = (uint64_t)(uintptr_t)&x;
        x = get_uptime_ms() ^
            ((uint64_t)(uint32_t)process_get_current_pid() << 32) ^
            stack_address ^
            0xd1b54a32d192ed03ULL;
    }
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g_dns_random_state = x;
    return x * 0x2545f4914f6cdd1dULL;
}

static uint16_t dns_rand_txid(void)
{
    return (uint16_t)(dns_random_next() >> 16);
}

static uint16_t dns_encode_name(uint8_t *out, uint16_t out_max, const char *name)
{
    uint16_t pos = 0u;
    while (*name != '\0') {
        const char *dot = name;
        while (*dot != '\0' && *dot != '.') dot++;

        uint16_t label_len = (uint16_t)(dot - name);
        if (label_len == 0u || label_len > 63u ||
            (uint32_t)pos + 1u + label_len + 1u > out_max) {
            return 0u;
        }
        out[pos++] = (uint8_t)label_len;
        memcpy(out + pos, name, label_len);
        pos = (uint16_t)(pos + label_len);
        name = (*dot == '.') ? dot + 1 : dot;
    }
    if (pos + 1u > out_max) return 0u;
    out[pos++] = 0u;
    return pos;
}

static uint32_t dns_parse_dotted_ipv4(const char *str)
{
    uint32_t ip = 0u;
    int parts = 0;
    const char *p = str;
    while (*p != '\0' && parts < 4) {
        uint32_t val = 0u;
        int digits = 0;
        while (*p >= '0' && *p <= '9') {
            val = val * 10u + (uint32_t)(*p - '0');
            p++;
            digits++;
            if (val > 255u) return 0u;
        }
        if (digits == 0) return 0u;
        ip = (ip << 8) | val;
        parts++;
        if (*p == '.') p++;
        else if (*p != '\0') return 0u;
    }
    return (parts == 4 && *p == '\0') ? ip : 0u;
}

static bool dns_skip_name(const uint8_t *packet,
                          uint16_t packet_len,
                          uint16_t *offset)
{
    uint16_t pos = *offset;
    while (pos < packet_len) {
        uint8_t length = packet[pos];
        if ((length & 0xc0u) == 0xc0u) {
            if ((uint32_t)pos + 2u > packet_len) return false;
            *offset = (uint16_t)(pos + 2u);
            return true;
        }
        if ((length & 0xc0u) != 0u) return false;
        pos++;
        if (length == 0u) {
            *offset = pos;
            return true;
        }
        if ((uint32_t)pos + length > packet_len) return false;
        pos = (uint16_t)(pos + length);
    }
    return false;
}

static dns_parse_result_t dns_parse_response(const uint8_t *packet,
                                             uint16_t packet_len,
                                             uint16_t expected_txid)
{
    dns_parse_result_t result = {0};
    if (packet == NULL || packet_len < 12u ||
        dns_read_u16(packet) != expected_txid) {
        return result;
    }

    uint16_t flags = dns_read_u16(packet + 2u);
    if ((flags & 0x8000u) == 0u || (flags & 0x000fu) != 0u) {
        return result;
    }
    result.truncated = (flags & 0x0200u) != 0u;
    if (result.truncated) return result;

    uint16_t question_count = dns_read_u16(packet + 4u);
    uint16_t answer_count = dns_read_u16(packet + 6u);
    uint16_t offset = 12u;

    for (uint16_t i = 0u; i < question_count; ++i) {
        if (!dns_skip_name(packet, packet_len, &offset) ||
            (uint32_t)offset + 4u > packet_len) {
            return (dns_parse_result_t){0};
        }
        offset = (uint16_t)(offset + 4u);
    }

    for (uint16_t i = 0u; i < answer_count; ++i) {
        if (!dns_skip_name(packet, packet_len, &offset) ||
            (uint32_t)offset + 10u > packet_len) {
            return (dns_parse_result_t){0};
        }

        uint16_t type = dns_read_u16(packet + offset);
        uint16_t class_code = dns_read_u16(packet + offset + 2u);
        uint32_t ttl = dns_read_u32(packet + offset + 4u);
        uint16_t data_len = dns_read_u16(packet + offset + 8u);
        offset = (uint16_t)(offset + 10u);
        if ((uint32_t)offset + data_len > packet_len) {
            return (dns_parse_result_t){0};
        }

        if (type == DNS_QTYPE_A && class_code == DNS_QCLASS_IN &&
            data_len == 4u) {
            result.address = dns_read_u32(packet + offset);
            result.ttl_seconds = ttl;
            return result;
        }
        offset = (uint16_t)(offset + data_len);
    }
    return result;
}

static uint32_t dns_cache_lookup(const char *hostname, uint32_t server_ip)
{
    uint64_t now = get_uptime_ms();
    for (uint32_t i = 0u; i < DNS_CACHE_SIZE; ++i) {
        dns_cache_entry_t *entry = &g_dns_cache[i];
        if (!entry->used) continue;
        if (now >= entry->expires_ms) {
            entry->used = false;
            continue;
        }
        if (entry->server_ip == server_ip &&
            dns_name_equal(entry->hostname, hostname)) {
            return entry->address;
        }
    }
    return 0u;
}

static void dns_cache_store(const char *hostname,
                            uint32_t server_ip,
                            uint32_t address,
                            uint32_t ttl_seconds)
{
    if (ttl_seconds == 0u || strlen(hostname) > DNS_HOSTNAME_MAX) return;
    if (ttl_seconds > 86400u) ttl_seconds = 86400u;

    uint32_t slot = DNS_CACHE_SIZE;
    for (uint32_t i = 0u; i < DNS_CACHE_SIZE; ++i) {
        if (g_dns_cache[i].used &&
            g_dns_cache[i].server_ip == server_ip &&
            dns_name_equal(g_dns_cache[i].hostname, hostname)) {
            slot = i;
            break;
        }
        if (!g_dns_cache[i].used && slot == DNS_CACHE_SIZE) slot = i;
    }
    if (slot == DNS_CACHE_SIZE) {
        slot = g_dns_cache_replace++ % DNS_CACHE_SIZE;
    }

    dns_cache_entry_t *entry = &g_dns_cache[slot];
    size_t hostname_len = strlen(hostname);
    memcpy(entry->hostname, hostname, hostname_len + 1u);
    entry->server_ip = server_ip;
    entry->address = address;
    entry->expires_ms = get_uptime_ms() + (uint64_t)ttl_seconds * 1000u;
    entry->used = true;
}

static uint16_t dns_bind_ephemeral_udp_port(void)
{
    uint32_t start = (uint32_t)(dns_random_next() % DNS_EPHEMERAL_PORT_COUNT);
    for (uint32_t i = 0u; i < DNS_EPHEMERAL_PORT_COUNT; ++i) {
        uint16_t port = (uint16_t)(DNS_EPHEMERAL_PORT_FIRST +
                                   ((start + i) % DNS_EPHEMERAL_PORT_COUNT));
        if (udp_bind_port(port) == 0) return port;
    }
    return 0u;
}

static int dns_tcp_read_exact(int32_t connection,
                              uint8_t *buffer,
                              uint16_t length,
                              uint64_t deadline)
{
    uint16_t total = 0u;
    while (total < length) {
        int32_t received = tcp_recv(connection,
                                    buffer + total,
                                    (uint16_t)(length - total));
        if (received > 0) {
            total = (uint16_t)(total + (uint16_t)received);
            continue;
        }
        int32_t state = tcp_get_state(connection);
        if (state != TCP_STATE_ESTABLISHED &&
            state != TCP_STATE_CLOSE_WAIT) {
            return -1;
        }
        if (get_uptime_ms() >= deadline) return -1;
        process_yield();
    }
    return 0;
}

static dns_parse_result_t dns_query_tcp(uint32_t server_ip,
                                        const uint8_t *query,
                                        uint16_t query_len,
                                        uint16_t txid)
{
    dns_parse_result_t result = {0};
    int32_t connection = -1;
    uint32_t start = (uint32_t)(dns_random_next() % DNS_EPHEMERAL_PORT_COUNT);
    for (uint32_t i = 0u; i < 64u; ++i) {
        uint16_t local_port =
            (uint16_t)(DNS_EPHEMERAL_PORT_FIRST +
                       ((start + i) % DNS_EPHEMERAL_PORT_COUNT));
        connection = tcp_connect(server_ip, DNS_REMOTE_PORT, local_port);
        if (connection >= 0) break;
    }
    if (connection < 0) return result;

    uint64_t deadline = get_uptime_ms() + DNS_TCP_TIMEOUT_MS;
    while (tcp_get_state(connection) == TCP_STATE_SYN_SENT) {
        if (get_uptime_ms() >= deadline) {
            tcp_close(connection);
            return result;
        }
        process_yield();
    }
    if (tcp_get_state(connection) != TCP_STATE_ESTABLISHED) {
        tcp_close(connection);
        return result;
    }

    uint8_t framed_query[DNS_MAX_QUERY_BYTES + 2u];
    framed_query[0] = (uint8_t)(query_len >> 8);
    framed_query[1] = (uint8_t)query_len;
    memcpy(framed_query + 2u, query, query_len);

    uint16_t sent = 0u;
    uint16_t framed_len = (uint16_t)(query_len + 2u);
    while (sent < framed_len) {
        int32_t written = tcp_send(connection,
                                   framed_query + sent,
                                   (uint16_t)(framed_len - sent));
        if (written <= 0 || get_uptime_ms() >= deadline) {
            tcp_close(connection);
            return result;
        }
        sent = (uint16_t)(sent + (uint16_t)written);
    }

    uint8_t length_bytes[2];
    if (dns_tcp_read_exact(connection, length_bytes, 2u, deadline) < 0) {
        tcp_close(connection);
        return result;
    }
    uint16_t response_len = dns_read_u16(length_bytes);
    if (response_len < 12u || response_len > DNS_MAX_TCP_RECV_BYTES) {
        tcp_close(connection);
        return result;
    }

    uint8_t response[DNS_MAX_TCP_RECV_BYTES];
    if (dns_tcp_read_exact(connection, response, response_len, deadline) == 0) {
        result = dns_parse_response(response, response_len, txid);
    }
    tcp_close(connection);
    return result;
}

void dns_set_default_server(uint32_t dns_server_ip)
{
    g_default_dns_server =
        dns_server_ip != 0u ? dns_server_ip : 0x0A000203u;
}

uint32_t dns_resolve(const char *hostname)
{
    /* Prefer whatever DNS server DHCP actually leased us (real network,
     * including AX900 Wi-Fi once associated) over the hardcoded
     * QEMU-user-mode-NAT default below -- see Kernel/Network/network_main.c.
     * dns_set_default_server() still wins when the caller explicitly set
     * one and no DHCP lease exists yet. */
    uint32_t dhcp_dns = net_get_dhcp_dns_server();
    uint32_t server = dhcp_dns != 0u ? dhcp_dns : g_default_dns_server;
    return dns_resolve_with_server(hostname, server);
}

uint32_t dns_resolve_with_server(const char *hostname, uint32_t dns_server_ip)
{
    if (hostname == NULL || hostname[0] == '\0' ||
        strlen(hostname) > DNS_HOSTNAME_MAX || dns_server_ip == 0u) {
        return 0u;
    }

    uint32_t dotted = dns_parse_dotted_ipv4(hostname);
    if (dotted != 0u) return dotted;

    uint32_t cached = dns_cache_lookup(hostname, dns_server_ip);
    if (cached != 0u) return cached;

    uint8_t query[DNS_MAX_QUERY_BYTES];
    memset(query, 0, sizeof(query));
    uint16_t txid = dns_rand_txid();
    query[0] = (uint8_t)(txid >> 8);
    query[1] = (uint8_t)txid;
    query[2] = 0x01u;
    query[5] = 0x01u;

    uint16_t query_len = 12u;
    uint16_t name_len = dns_encode_name(query + query_len,
                                        DNS_MAX_QUERY_BYTES - query_len - 4u,
                                        hostname);
    if (name_len == 0u) return 0u;
    query_len = (uint16_t)(query_len + name_len);
    query[query_len++] = 0u;
    query[query_len++] = (uint8_t)DNS_QTYPE_A;
    query[query_len++] = 0u;
    query[query_len++] = (uint8_t)DNS_QCLASS_IN;

    uint16_t local_port = dns_bind_ephemeral_udp_port();
    if (local_port == 0u) return 0u;

    dns_parse_result_t result = {0};
    for (uint32_t attempt = 0u; attempt < 3u && result.address == 0u; ++attempt) {
        if (!udp_send(dns_server_ip,
                      local_port,
                      DNS_REMOTE_PORT,
                      query,
                      query_len)) {
            continue;
        }

        uint64_t deadline = get_uptime_ms() + DNS_REQUEST_TIMEOUT_MS;
        while (get_uptime_ms() < deadline) {
            uint8_t buffer[DNS_MAX_UDP_RECV_BYTES];
            int32_t received = udp_recv(local_port, buffer, sizeof(buffer));
            if (received <= 8) {
                process_yield();
                continue;
            }

            uint16_t payload_len =
                (uint16_t)((uint16_t)buffer[6] |
                           ((uint16_t)buffer[7] << 8));
            if ((uint32_t)payload_len + 8u > (uint32_t)received) continue;

            result = dns_parse_response(buffer + 8u, payload_len, txid);
            if (result.truncated) {
                result = dns_query_tcp(dns_server_ip, query, query_len, txid);
            }
            if (result.address != 0u || result.truncated) break;
        }
    }

    udp_unbind_port(local_port);
    if (result.address != 0u) {
        dns_cache_store(hostname,
                        dns_server_ip,
                        result.address,
                        result.ttl_seconds);
    }
    return result.address;
}