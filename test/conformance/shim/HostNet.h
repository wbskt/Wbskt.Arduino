// The TCP helpers the HTTP, WebSocket and WiFiClient shims share.
#pragma once

#include <stdint.h>
#include <stddef.h>

#include <string>

namespace hostnet {

std::string lower(std::string text);
std::string trim(const std::string& text);
// "host:port" into its parts; a bare host keeps the given port.
void splitHost(const std::string& text, std::string& host, uint16_t& port);
// A connected TCP socket with TCP_NODELAY, or -1.
int connectTcp(const std::string& host, uint16_t port);
bool writeAll(int fd, const void* data, size_t length);

}  // namespace hostnet
