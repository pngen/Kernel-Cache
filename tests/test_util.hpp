// test_util.hpp - runtime CHECK macros (always evaluated, so Release is safe).
#pragma once
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#define kc_pid() ::GetCurrentProcessId()
#else
#include <unistd.h>
#define kc_pid() getpid()
#endif

namespace kctest {
inline int g_checks = 0;
inline void fail(const std::string& msg, int line) {
  std::fprintf(stderr, "CHECK FAILED (line %d): %s\n", line, msg.c_str());
  std::exit(1);
}
}  // namespace kctest

#define CHECK_TRUE(expr) do { ++kctest::g_checks; if (!(expr)) { std::ostringstream _o; _o << #expr; kctest::fail(_o.str(), __LINE__); } } while (0)
#define CHECK_FALSE(expr) do { ++kctest::g_checks; if ((expr)) { std::ostringstream _o; _o << "expected false: " << #expr; kctest::fail(_o.str(), __LINE__); } } while (0)
#define CHECK_EQ(a, b) do { ++kctest::g_checks; if (!((a) == (b))) { std::ostringstream _o; _o << #a " == " #b; kctest::fail(_o.str(), __LINE__); } } while (0)
#define CHECK_NE(a, b) do { ++kctest::g_checks; if (((a) == (b))) { std::ostringstream _o; _o << #a " != " #b; kctest::fail(_o.str(), __LINE__); } } while (0)
#define CHECK_ERRCODE(res, code) do { ++kctest::g_checks; if ((res).ok() || (res).error().code() != (code)) { std::ostringstream _o; _o << #res ".error().code() == " #code; kctest::fail(_o.str(), __LINE__); } } while (0)