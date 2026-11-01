// Messages waiting to go out, in order, in one fixed block of RAM. The limit is in bytes, so a
// buffer of small readings holds hundreds of them; when it is full, the oldest go first.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace wbskt_core {

// When a message was made. epochMillis is -1 when the clock wasn't set yet; the time is then
// worked out from enqueuedAt (millis()) once it is.
struct Stamp {
    int64_t epochMillis;
    uint32_t enqueuedAt;
};

class Outbox {
public:
    // Each message also takes kEntryOverhead bytes of the capacity.
    static constexpr size_t kEntryOverhead = 16;

    explicit Outbox(size_t capacityBytes);

    // False, and counted as dropped, when the message is bigger than the whole buffer.
    bool push(const std::string& frame, Stamp stamp);
    bool empty() const { return count_ == 0; }
    size_t size() const { return count_; }
    size_t bytesUsed() const { return used_; }
    size_t capacity() const { return buffer_.size(); }
    size_t dropped() const { return dropped_; }
    void front(std::string& frame, Stamp& stamp) const;
    void pop();

private:
    void write(size_t at, const void* data, size_t length);
    void read(size_t at, void* data, size_t length) const;
    size_t entrySize(size_t at) const;

    std::vector<uint8_t> buffer_;
    size_t head_ = 0;  // the oldest entry
    size_t used_ = 0;
    size_t count_ = 0;
    size_t dropped_ = 0;
};

}  // namespace wbskt_core
