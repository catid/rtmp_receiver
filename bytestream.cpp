#include "bytestream.h"
#include <cstring> // For memcpy


//------------------------------------------------------------------------------
// ByteStreamWriter

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


//------------------------------------------------------------------------------
// ByteStream

ByteStream::ByteStream(const uint8_t* data, size_t size)
    : data_(data), size_(size), offset_(0), error_(false) {}

uint8_t ByteStream::ReadUInt8() {
    uint8_t value = 0;
    if (offset_ + 1 <= size_) {
        value = data_[offset_];
        offset_ += 1;
    } else {
        error_ = true;
    }
    return value;
}

uint16_t ByteStream::ReadUInt16() {
    uint16_t value = 0;
    if (offset_ + 2 <= size_) {
        value = (static_cast<uint16_t>(data_[offset_]) << 8) |
                (static_cast<uint16_t>(data_[offset_ + 1]));
        offset_ += 2;
    } else {
        error_ = true;
    }
    return value;
}

uint32_t ByteStream::ReadUInt24() {
    uint32_t value = 0;
    if (offset_ + 3 <= size_) {
        value = (static_cast<uint32_t>(data_[offset_]) << 16) |
                (static_cast<uint32_t>(data_[offset_ + 1]) << 8) |
                (static_cast<uint32_t>(data_[offset_ + 2]));
        offset_ += 3;
    } else {
        error_ = true;
    }
    return value;
}

uint32_t ByteStream::ReadUInt32(bool big_endian) {
    uint32_t value = 0;
    if (offset_ + 4 <= size_) {
        if (big_endian) {
            value = (static_cast<uint32_t>(data_[offset_]) << 24) |
                    (static_cast<uint32_t>(data_[offset_ + 1]) << 16) |
                    (static_cast<uint32_t>(data_[offset_ + 2]) << 8) |
                    (static_cast<uint32_t>(data_[offset_ + 3]));
        } else {
            value = (static_cast<uint32_t>(data_[offset_ + 3]) << 24) |
                    (static_cast<uint32_t>(data_[offset_ + 2]) << 16) |
                    (static_cast<uint32_t>(data_[offset_ + 1]) << 8) |
                    (static_cast<uint32_t>(data_[offset_]));
        }
        offset_ += 4;
    } else {
        error_ = true;
    }
    return value;
}

uint64_t ByteStream::ReadUInt64() {
    uint64_t value = 0;
    if (offset_ + 8 <= size_) {
        value = (static_cast<uint64_t>(data_[offset_]) << 56) |
                (static_cast<uint64_t>(data_[offset_ + 1]) << 48) |
                (static_cast<uint64_t>(data_[offset_ + 2]) << 40) |
                (static_cast<uint64_t>(data_[offset_ + 3]) << 32) |
                (static_cast<uint64_t>(data_[offset_ + 4]) << 24) |
                (static_cast<uint64_t>(data_[offset_ + 5]) << 16) |
                (static_cast<uint64_t>(data_[offset_ + 6]) << 8) |
                (static_cast<uint64_t>(data_[offset_ + 7]));
        offset_ += 8;
    } else {
        error_ = true;
    }
    return value;
}

double ByteStream::ReadDouble() {
    uint64_t value = ReadUInt64();
    return *reinterpret_cast<double*>(&value);
}

bool ByteStream::HasError() const {
    return error_;
}

bool ByteStream::IsEndOfStream() const {
    return RemainingBytes() <= 0;
}

int ByteStream::RemainingBytes() const {
    return static_cast<int>( size_ - offset_ );
}

const uint8_t* ByteStream::ReadData(int bytes) {
    const uint8_t* data = data_ + offset_;
    if (offset_ + bytes <= size_) {
        offset_ += bytes;
    } else {
        error_ = true;
    }
    return data;
}

const uint8_t* ByteStream::PeekData() const {
    return data_ + offset_;
}
