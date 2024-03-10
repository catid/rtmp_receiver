#ifndef RTMP_TOOLS_H
#define RTMP_TOOLS_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>


//------------------------------------------------------------------------------
// Tools

void FillRandomBuffer(uint8_t* buffer, int bytes, uint32_t seed);

void WriteUInt32(uint8_t* buffer, uint32_t value);

void WriteUInt24(uint8_t* buffer, uint32_t value);

uint64_t GetMsec();

void PrintFirst64BytesAsHex(const uint8_t* data, size_t size);

void AppendDataToVector(std::vector<uint8_t>& vec, const uint8_t* data, int bytes);

std::string CreateStringFromBytes(const uint8_t* data, size_t length);


//------------------------------------------------------------------------------
// AutoClose

class AutoClose {
public:
    AutoClose(std::function<void()> f) : f(f) {}
    ~AutoClose() {
        f();
    }
private:
    std::function<void()> f;
};

#endif // RTMP_TOOLS_H
