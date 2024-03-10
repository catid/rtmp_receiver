#include "rtmp_tools.h"

#include <chrono>
#include <iostream>
#include <iomanip>
using namespace std;


//------------------------------------------------------------------------------
// Tools

void FillRandomBuffer(uint8_t* buffer, int bytes, uint32_t seed) {
    const uint32_t a = 1664525;
    const uint32_t c = 1013904223;
    uint32_t randomValue = seed;

    for (size_t i = 0; i < bytes; ++i) {
        randomValue = a * randomValue + c; // Generate the next pseudo-random value
        buffer[i] = static_cast<uint8_t>(randomValue >> 24); // Use the most significant byte
    }
}

void WriteUInt32(uint8_t* buffer, uint32_t value) {
    buffer[0] = static_cast<uint8_t>(value >> 24);
    buffer[1] = static_cast<uint8_t>(value >> 16);
    buffer[2] = static_cast<uint8_t>(value >> 8);
    buffer[3] = static_cast<uint8_t>(value);
}

void WriteUInt24(uint8_t* buffer, uint32_t value) {
    buffer[0] = static_cast<uint8_t>(value >> 16);
    buffer[1] = static_cast<uint8_t>(value >> 8);
    buffer[2] = static_cast<uint8_t>(value);
}

uint64_t GetMsec() {
    using namespace std::chrono;
    milliseconds ms = duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()
    );
    return ms.count();
}

void PrintFirst64BytesAsHex(const uint8_t* data, size_t size) {
    size_t bytesToPrint = (size < 512) ? size : 512; // Limit to the first 64 bytes

    for (size_t i = 0; i < bytesToPrint; ++i) {
        // Print each byte in hex format, padded with 0 if necessary
        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]) << " ";
        
        // Optional: add a new line every 16 bytes for readability
        if ((i + 1) % 16 == 0) {
            std::cout << std::endl;
        }
    }

    std::cout << std::dec << std::endl; // Switch back to decimal for any further output
}

void AppendDataToVector(std::vector<uint8_t>& vec, const uint8_t* data, int bytes) {
    if (data != nullptr && bytes > 0) {
        vec.insert(vec.end(), data, data + bytes);
    }
}

std::string CreateStringFromBytes(const uint8_t* data, size_t length) {
    // Create a string from the given data and length.
    // The std::string constructor will copy 'length' characters and
    // automatically handle null-termination for C-string access.
    std::string result(reinterpret_cast<const char*>(data), length);
    return result;
}
