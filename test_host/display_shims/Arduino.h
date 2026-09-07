#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <type_traits>
#define PROGMEM
#ifdef _WIN32
inline std::tm* localtime_r(const time_t* value, std::tm* result) {
    return localtime_s(result, value) == 0 ? result : nullptr;
}
inline std::tm* gmtime_r(const time_t* value, std::tm* result) {
    return gmtime_s(result, value) == 0 ? result : nullptr;
}
#endif
inline double radians(double degrees) { return degrees * 3.14159265358979323846 / 180.0; }
class __FlashStringHelper;
class String {
    std::string value_;
public:
    String() = default;
    String(const char* value) : value_(value ? value : "") {}
    String(const std::string& value) : value_(value) {}
    String(char value) : value_(1, value) {}
    template<class T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>
    String(T value) : value_(std::to_string(value)) {}
    const char* c_str() const { return value_.c_str(); }
    std::size_t length() const { return value_.length(); }
    bool isEmpty() const { return value_.empty(); }
    void toLowerCase() { for (char& c : value_) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    int indexOf(const char* text, unsigned from = 0) const { auto p = value_.find(text, from); return p == std::string::npos ? -1 : static_cast<int>(p); }
    int indexOf(char text, unsigned from = 0) const { auto p = value_.find(text, from); return p == std::string::npos ? -1 : static_cast<int>(p); }
    String substring(unsigned begin, unsigned end) const { return value_.substr(begin, end - begin); }
    String substring(unsigned begin) const { return value_.substr(begin); }
    String& operator+=(const String& other) { value_ += other.value_; return *this; }
    friend String operator+(const String& a, const String& b) { return a.value_ + b.value_; }
    friend bool operator==(const String& a, const String& b) { return a.value_ == b.value_; }
};
inline void yield() {}
struct HostSerial { void println(const char*) {} };
inline HostSerial Serial;
