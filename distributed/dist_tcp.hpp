// dist_tcp.hpp - minimal framed TCP transport for coordinator/worker/client.
#pragma once

#include "kernelcache/distributed.hpp"

#include <cstdint>
#include <string>
#include <vector>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET kc_socket_t;
#define KC_INVALID_SOCKET INVALID_SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
typedef int kc_socket_t;
#define KC_INVALID_SOCKET (-1)
static const int closesocket = ::close;
#endif

namespace kernelcache {
namespace dist {

inline bool sock_init() {
#ifdef _WIN32
  static bool done = []() { WSADATA w; return WSAStartup(MAKEWORD(2,2), &w) == 0; }();
  return done;
#else
  return true;
#endif
}

struct TcpListener {
  kc_socket_t fd = KC_INVALID_SOCKET;
  bool listen(std::uint16_t port) {
    sock_init();
#ifdef _WIN32
    fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == KC_INVALID_SOCKET) return false;
    BOOL yes = TRUE; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_ANY); addr.sin_port = htons(port);
    if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) { closesocket(fd); fd = KC_INVALID_SOCKET; return false; }
    if (::listen(fd, 16) == SOCKET_ERROR) { closesocket(fd); fd = KC_INVALID_SOCKET; return false; }
    return true;
#else
    fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    int yes = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_ANY); addr.sin_port = htons(port);
    if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) { ::close(fd); fd = -1; return false; }
    if (::listen(fd, 16) < 0) { ::close(fd); fd = -1; return false; }
    return true;
#endif
  }
  // Accept a client. Returns new socket or KC_INVALID_SOCKET.
  kc_socket_t accept() {
#ifdef _WIN32
    return ::accept(fd, nullptr, nullptr);
#else
    return ::accept(fd, nullptr, nullptr);
#endif
  }
  void close() { if (fd != KC_INVALID_SOCKET) { closesocket(fd); fd = KC_INVALID_SOCKET; } }
};

struct TcpConnection {
  kc_socket_t fd = KC_INVALID_SOCKET;
  bool connect(const std::string& host, std::uint16_t port) {
    sock_init();
#ifdef _WIN32
    fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == KC_INVALID_SOCKET) return false;
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    return ::connect(fd, (sockaddr*)&addr, sizeof(addr)) != SOCKET_ERROR;
#else
    fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    return ::connect(fd, (sockaddr*)&addr, sizeof(addr)) == 0;
#endif
  }
  bool close() { if (fd != KC_INVALID_SOCKET) { closesocket(fd); fd = KC_INVALID_SOCKET; return true; } return false; }

  // Read exactly n bytes.
  bool read_exact(void* buf, std::size_t n) {
    char* p = static_cast<char*>(buf);
    std::size_t got = 0;
    while (got < n) {
#ifdef _WIN32
      int r = ::recv(fd, p + got, static_cast<int>(n - got), 0);
#else
      ssize_t r = ::recv(fd, p + got, n - got, 0);
#endif
      if (r <= 0) return false;
      got += static_cast<std::size_t>(r);
    }
    return true;
  }
  // Write all bytes.
  bool write_all(const void* buf, std::size_t n) {
    const char* p = static_cast<const char*>(buf);
    std::size_t sent = 0;
    while (sent < n) {
#ifdef _WIN32
      int r = ::send(fd, p + sent, static_cast<int>(n - sent), 0);
#else
      ssize_t r = ::send(fd, p + sent, n - sent, 0);
#endif
      if (r <= 0) return false;
      sent += static_cast<std::size_t>(r);
    }
    return true;
  }
  // Read one frame (header + payload). Returns message type and payload.
  bool read_frame(std::uint16_t& type, std::vector<std::uint8_t>& payload) {
    std::uint8_t hdr[8];
    if (!read_exact(hdr, 8)) return false;
    std::uint32_t len = (static_cast<std::uint32_t>(hdr[0]) << 24) | (static_cast<std::uint32_t>(hdr[1]) << 16) | (static_cast<std::uint32_t>(hdr[2]) << 8) | hdr[3];
    std::uint16_t ver = (static_cast<std::uint16_t>(hdr[4]) << 8) | hdr[5];
    type = (static_cast<std::uint16_t>(hdr[6]) << 8) | hdr[7];
    if (ver != 1) return false;
    if (len < 8 || len > kDistMaxFrameBytes) return false;
    std::size_t plen = len - 8;
    payload.resize(plen);
    if (plen > 0 && !read_exact(payload.data(), plen)) return false;
    return true;
  }
  bool write_frame(std::uint16_t type, const std::vector<std::uint8_t>& payload) {
    std::uint32_t len = 8u + static_cast<std::uint32_t>(payload.size());
    std::uint8_t hdr[8];
    hdr[0] = static_cast<std::uint8_t>((len >> 24) & 0xff); hdr[1] = static_cast<std::uint8_t>((len >> 16) & 0xff);
    hdr[2] = static_cast<std::uint8_t>((len >> 8) & 0xff); hdr[3] = static_cast<std::uint8_t>(len & 0xff);
    hdr[4] = 0; hdr[5] = 1;  // proto version 1
    hdr[6] = static_cast<std::uint8_t>((type >> 8) & 0xff); hdr[7] = static_cast<std::uint8_t>(type & 0xff);
    if (!write_all(hdr, 8)) return false;
    return payload.empty() || write_all(payload.data(), payload.size());
  }
};

}  // namespace dist
}  // namespace kernelcache