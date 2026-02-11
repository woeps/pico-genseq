#pragma once

namespace hardware {

class IOutput {
public:
    virtual ~IOutput() = default;
    virtual void update() = 0;
    virtual void setValue(int value) = 0;
};

} // namespace hardware
