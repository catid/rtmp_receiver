#ifndef BYTESTREAM_H
#define BYTESTREAM_H

#include <cstdint>
#include <vector>
#include <stddef.h>

class ByteStream {
public:
    ByteStream(const uint8_t* data, size_t size);

    uint8_t ReadUInt8();
    uint16_t ReadUInt16();
    uint32_t ReadUInt24(); // Custom size for RTMP specific format
    uint32_t ReadUInt32(bool big_endian = true);
    uint64_t ReadUInt64();
    double ReadDouble();
    const uint8_t* ReadData(int bytes);

    bool HasError() const;
    bool IsEndOfStream() const;
    int RemainingBytes() const;
    const uint8_t* PeekData() const;

private:
    const uint8_t* data_;
    size_t size_;
    size_t offset_;
    bool error_;
};

#endif // BYTESTREAM_H
