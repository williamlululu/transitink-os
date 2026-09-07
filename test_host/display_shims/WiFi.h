#pragma once
constexpr int WL_CONNECTED = 3;
struct HostWiFi { int status() const { return connected ? WL_CONNECTED : 0; } bool connected = true; };
inline HostWiFi WiFi;
