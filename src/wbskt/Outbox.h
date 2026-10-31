// Messages waiting for the connection, sent in order once it is back. When full, the oldest goes.
#pragma once

#include <stddef.h>

#include <deque>
#include <string>

namespace wbskt {

class Outbox {
public:
    explicit Outbox(size_t capacity) : capacity_(capacity) {}

    void push(std::string frame);
    bool empty() const { return frames_.empty(); }
    size_t size() const { return frames_.size(); }
    size_t dropped() const { return dropped_; }
    const std::string& front() const { return frames_.front(); }
    void pop() { frames_.pop_front(); }

private:
    size_t capacity_;
    size_t dropped_ = 0;
    std::deque<std::string> frames_;
};

}  // namespace wbskt
