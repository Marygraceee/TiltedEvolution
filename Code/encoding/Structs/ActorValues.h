#pragma once

#include <cmath>
#include <cstdint>

struct ActorValues
{
    static constexpr uint32_t kValueCount = 164;

    ActorValues() = default;
    ~ActorValues() = default;

    static bool IsValidValue(uint32_t aId, float aValue) noexcept { return aId < kValueCount && std::isfinite(aValue); }

    bool operator==(const ActorValues& acRhs) const noexcept;
    bool operator!=(const ActorValues& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    TiltedPhoques::Map<uint32_t, float> ActorValuesList{};
    TiltedPhoques::Map<uint32_t, float> ActorMaxValuesList{};
};
