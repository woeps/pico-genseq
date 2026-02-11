#pragma once

#include <functional>

namespace hardware {

class ISensor {
public:
    virtual ~ISensor() = default;
    virtual void update() = 0;
    virtual void setValueChangeCallback(std::function<void(int)> callback) = 0;
};

} // namespace hardware
