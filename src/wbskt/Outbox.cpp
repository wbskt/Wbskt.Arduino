#include "Outbox.h"

#include <utility>

namespace wbskt {

void Outbox::push(std::string frame) {
    if (capacity_ == 0) {
        ++dropped_;
        return;
    }
    if (frames_.size() >= capacity_) {
        frames_.pop_front();
        ++dropped_;
    }
    frames_.push_back(std::move(frame));
}

}  // namespace wbskt
