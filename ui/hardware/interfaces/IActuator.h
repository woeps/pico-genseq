#pragma once

namespace hardware {

class IActuator {
public:
    virtual ~IActuator() = default;
    virtual void update() = 0;
    virtual void setValue(int value) = 0;
};

} // namespace hardware
