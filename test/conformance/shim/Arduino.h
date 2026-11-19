// Just enough of the Arduino core for WbsktClient.cpp to build and run on Linux.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <string>
#include <utility>

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

    explicit String(unsigned short number) : value_(std::to_string(number)) {}
    explicit String(unsigned char number) : value_(std::to_string(number)) {}
    explicit String(char c) : value_(1, c) {}

    const char* c_str() const { return value_.c_str(); }
    // The Arduino core's String: indexes are int and -1 means "not found".
    int indexOf(char c, unsigned int from = 0) const { return found(value_.find(c, from)); }
    int indexOf(const String& text, unsigned int from = 0) const { return found(value_.find(text.value_, from)); }
    int indexOf(const char* text, unsigned int from = 0) const { return indexOf(String(text), from); }
    String substring(unsigned int from) const { return from >= value_.size() ? String() : String(value_.substr(from)); }
    String substring(unsigned int from, unsigned int to) const {
        if (from > to) {
            std::swap(from, to);
        }
        return from >= value_.size() ? String() : String(value_.substr(from, to - from));
    }
    void trim() {
        const size_t start = value_.find_first_not_of(" \t\r\n\v\f");
        const size_t end = value_.find_last_not_of(" \t\r\n\v\f");
        value_ = start == std::string::npos ? "" : value_.substr(start, end - start + 1);
    }
    void remove(unsigned int index) { value_.erase(index < value_.size() ? index : value_.size()); }
    void remove(unsigned int index, unsigned int count) { value_.erase(index < value_.size() ? index : value_.size(), count); }
    bool startsWith(const String& prefix) const { return value_.compare(0, prefix.value_.size(), prefix.value_) == 0; }
    bool equalsIgnoreCase(const String& other) const {
        return value_.size() == other.value_.size() && strncasecmp(value_.c_str(), other.value_.c_str(), value_.size()) == 0;
    }
    bool reserve(unsigned int size) {
        value_.reserve(size);
        return true;
    }
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
    static int found(size_t at) { return at == std::string::npos ? -1 : static_cast<int>(at); }

    std::string value_;
};

inline String operator+(const String& a, const String& b) {
    String out = a;
    out += b;
    return out;
}
inline String operator+(const String& a, const char* b) { return a + String(b); }
inline String operator+(const char* a, const String& b) { return String(a) + b; }
inline String operator+(const String& a, unsigned short b) { return a + String(b); }

class EspClass {
public:
    const char* getChipModel() const { return "host"; }
    uint32_t getFreeHeap() const { return 200000; }
};

extern EspClass ESP;
