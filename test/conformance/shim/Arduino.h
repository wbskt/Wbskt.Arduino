// Just enough of the Arduino core for WbsktClient.cpp to build and run on Linux.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

unsigned long millis();
void delay(unsigned long ms);
void configTime(long gmtOffset, int daylightOffset, const char* server);

#define log_e(format, ...) fprintf(stderr, "E " format "\n", ##__VA_ARGS__)
#define log_w(format, ...) fprintf(stderr, "W " format "\n", ##__VA_ARGS__)
#define log_i(format, ...) fprintf(stderr, "I " format "\n", ##__VA_ARGS__)

class String {
public:
    String() = default;
    String(const char* text) : value_(text ? text : "") {}
    String(const std::string& text) : value_(text) {}
    explicit String(int number) : value_(std::to_string(number)) {}
    explicit String(unsigned int number) : value_(std::to_string(number)) {}
    explicit String(long number) : value_(std::to_string(number)) {}
    explicit String(unsigned long number) : value_(std::to_string(number)) {}

    String& operator=(const char* text) {
        value_ = text ? text : "";
        return *this;
    }

    const char* c_str() const { return value_.c_str(); }
    size_t length() const { return value_.size(); }
    bool isEmpty() const { return value_.empty(); }
    long toInt() const { return strtol(value_.c_str(), nullptr, 10); }
    bool concat(const char* text) {
        value_ += text ? text : "";
        return true;
    }
    bool concat(char c) {
        value_ += c;
        return true;
    }
    char operator[](size_t index) const { return value_[index]; }
    String& operator+=(const String& other) {
        value_ += other.value_;
        return *this;
    }
    String& operator+=(const char* text) { return *this += String(text); }
    bool operator==(const String& other) const { return value_ == other.value_; }
    bool operator==(const char* text) const { return value_ == (text ? text : ""); }
    bool operator!=(const String& other) const { return value_ != other.value_; }

    const std::string& str() const { return value_; }

private:
    std::string value_;
};

inline String operator+(const String& a, const String& b) {
    String out = a;
    out += b;
    return out;
}
inline String operator+(const String& a, const char* b) { return a + String(b); }
inline String operator+(const char* a, const String& b) { return String(a) + b; }

class EspClass {
public:
    const char* getChipModel() const { return "host"; }
};

extern EspClass ESP;
