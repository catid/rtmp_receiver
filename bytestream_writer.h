#ifndef BYTESTREAM_WRITER_H
#define BYTESTREAM_WRITER_H

#include <cstdint>
#include <vector>
#include <cstring> // for memcpy
#include <string>

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

#endif // BYTESTREAM_WRITER_H
