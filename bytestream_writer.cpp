#include "bytestream_writer.h"

const uint8_t* ByteStreamWriter::GetData() const {
    return buffer_.data();
}

size_t ByteStreamWriter::GetLength() const {
    return buffer_.size();
}

void ByteStreamWriter::WriteUInt8(uint8_t value) {
    buffer_.push_back(value);
}

void ByteStreamWriter::WriteUInt16(uint16_t value) {
    uint8_t data[2] = {
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value)
    };
    WriteData(data, sizeof(data));
}

void ByteStreamWriter::WriteUInt24(uint32_t value) {
    uint8_t data[3] = {
        static_cast<uint8_t>(value >> 16),
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value)
    };
    WriteData(data, sizeof(data));
}

void ByteStreamWriter::WriteUInt32(uint32_t value) {
    uint8_t data[4] = {
        static_cast<uint8_t>(value >> 24),
        static_cast<uint8_t>(value >> 16),
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value)
    };
    WriteData(data, sizeof(data));
}

void ByteStreamWriter::WriteUInt64(uint64_t value) {
    uint8_t data[8] = {
        static_cast<uint8_t>(value >> 56),
        static_cast<uint8_t>(value >> 48),
        static_cast<uint8_t>(value >> 40),
        static_cast<uint8_t>(value >> 32),
        static_cast<uint8_t>(value >> 24),
        static_cast<uint8_t>(value >> 16),
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value)
    };
    WriteData(data, sizeof(data));
}

void ByteStreamWriter::WriteDouble(double value) {
    static_assert(sizeof(double) == sizeof(uint64_t), "Double is not 8 bytes.");
    uint64_t bits = *reinterpret_cast<const uint64_t*>(&value);
    WriteUInt64(bits);
}

void ByteStreamWriter::WriteData(const void* data, size_t length) {
    if (data != nullptr && length > 0) {
        buffer_.insert(buffer_.end(), (char*)data, (char*)data + length);
    }
}

void ByteStreamWriter::WriteAmf0String(const std::string& value) {
    WriteUInt16(value.length());
    WriteData(value.c_str(), value.length());
}

