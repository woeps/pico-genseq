#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "PersistableConfig.h"  // the plain snapshot struct (see Data Models)

namespace persistence {

// Save_Record header constants. `inline constexpr` gives the array a single
// definition across every translation unit that includes this header (ODR-safe
// under C++17), which plain `constexpr` at namespace scope would not guarantee
// if its address were taken in more than one TU.
inline constexpr uint8_t MAGIC[2] = {'G', 'S'};  // "GS"
inline constexpr uint8_t FORMAT_VERSION = 1;
// magic (2) + version (1) + reserved (1) + payloadLength (4) + crc32 (4) = 12
inline constexpr std::size_t HEADER_SIZE = 2 + 1 + 1 + 4 + 4;

// Outcome of parsing/validating a raw byte span as a Save_Record.
enum class LoadStatus {
    OK,
    ABSENT,        // erased flash / magic never written
    BAD_MAGIC,
    BAD_VERSION,
    TRUNCATED,     // shorter than header, or shorter than declared payload
    CRC_MISMATCH,
};

// Serialize a snapshot into a full Save_Record (header + payload). Deterministic.
std::vector<uint8_t> serialize(const PersistableConfig& cfg);

// Parse+validate a raw byte span. On OK, writes the decoded config to `out`.
// On any failure, `out` is left untouched (Req 3.6, 3.7, 6.x).
LoadStatus deserialize(const uint8_t* bytes, std::size_t len,
                       PersistableConfig& out);

// Standard CRC-32 (IEEE 802.3, polynomial 0xEDB88320, reflected), table-driven,
// computed entirely in RAM. Chosen over DMA-sniff CRC to keep the codec SDK-free
// and host-testable.
uint32_t crc32(const uint8_t* data, std::size_t len);

}  // namespace persistence
