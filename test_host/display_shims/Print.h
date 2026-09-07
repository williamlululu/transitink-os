#pragma once
#include "Arduino.h"
class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t value) = 0;
    size_t print(const String& value) { size_t n = 0; for (size_t i = 0; i < value.length(); ++i) n += write(static_cast<uint8_t>(value.c_str()[i])); return n; }
};
