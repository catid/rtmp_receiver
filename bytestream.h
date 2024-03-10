#ifndef BYTESTREAM_H
#define BYTESTREAM_H

#include <cstdint>
#include <vector>
#include <cstring> // for memcpy
#include <string>


//------------------------------------------------------------------------------
// ByteStreamWriter

class ByteStreamWriter {
public:
    const uint8_t* GetData() const;
    size_t GetLength() const;

    void WriteUInt8(uint8_t value);
    void WriteUInt16(uint16_t value);
    void WriteUInt24(uint32_t value);
    void WriteUInt32(uint32_t value);
    void WriteUInt64(uint64_t value);
    void WriteDouble(double value);
    void WriteData(const void* data, size_t length);

    void WriteAmf0String(const std::string& value);

private:
    std::vector<uint8_t> buffer_;
};


//------------------------------------------------------------------------------
// ByteStream

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
