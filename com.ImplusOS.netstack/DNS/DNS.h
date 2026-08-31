#pragma once

#include <stdint.h>

uint32_t dns_resolve(const char *hostname);
uint32_t dns_resolve_with_server(const char *hostname, uint32_t dns_server_ip);
void dns_set_default_server(uint32_t dns_server_ip);