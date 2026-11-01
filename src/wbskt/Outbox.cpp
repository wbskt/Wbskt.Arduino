#include "Outbox.h"

#include <string.h>

namespace wbskt_core {

// An entry is its length (4 bytes), epochMillis (8), enqueuedAt (4), then the frame. Entries wrap
// around the end of the buffer.

Outbox::Outbox(size_t capacityBytes) : buffer_(capacityBytes) {}

void Outbox::write(size_t at, const void* data, size_t length) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    const size_t first = length < buffer_.size() - at ? length : buffer_.size() - at;
    memcpy(&buffer_[at], bytes, first);
    memcpy(&buffer_[0], bytes + first, length - first);
}

void Outbox::read(size_t at, void* data, size_t length) const {
    uint8_t* bytes = static_cast<uint8_t*>(data);
    const size_t first = length < buffer_.size() - at ? length : buffer_.size() - at;
    memcpy(bytes, &buffer_[at], first);
    memcpy(bytes + first, &buffer_[0], length - first);
}

size_t Outbox::entrySize(size_t at) const {
    uint32_t length = 0;
    read(at, &length, sizeof(length));
    return kEntryOverhead + length;
}

bool Outbox::push(const std::string& frame, Stamp stamp) {
    const size_t needed = kEntryOverhead + frame.size();
    if (needed > buffer_.size()) {
        ++dropped_;
        return false;
    }
    while (buffer_.size() - used_ < needed) {
        pop();
        ++dropped_;
    }
    size_t at = (head_ + used_) % buffer_.size();
    const uint32_t length = static_cast<uint32_t>(frame.size());
    write(at, &length, sizeof(length));
    at = (at + sizeof(length)) % buffer_.size();
    write(at, &stamp.epochMillis, sizeof(stamp.epochMillis));
    at = (at + sizeof(stamp.epochMillis)) % buffer_.size();
    write(at, &stamp.enqueuedAt, sizeof(stamp.enqueuedAt));
    at = (at + sizeof(stamp.enqueuedAt)) % buffer_.size();
    write(at, frame.data(), frame.size());
    used_ += needed;
    ++count_;
    return true;
}

void Outbox::front(std::string& frame, Stamp& stamp) const {
    uint32_t length = 0;
    size_t at = head_;
    read(at, &length, sizeof(length));
    at = (at + sizeof(length)) % buffer_.size();
    read(at, &stamp.epochMillis, sizeof(stamp.epochMillis));
    at = (at + sizeof(stamp.epochMillis)) % buffer_.size();
    read(at, &stamp.enqueuedAt, sizeof(stamp.enqueuedAt));
    at = (at + sizeof(stamp.enqueuedAt)) % buffer_.size();
    frame.resize(length);
    if (length > 0) {
        read(at, &frame[0], length);
    }
}

void Outbox::pop() {
    if (count_ == 0) {
        return;
    }
    const size_t size = entrySize(head_);
    head_ = (head_ + size) % buffer_.size();
    used_ -= size;
    --count_;
    if (count_ == 0) {
        head_ = 0;
        used_ = 0;
    }
}

}  // namespace wbskt_core
