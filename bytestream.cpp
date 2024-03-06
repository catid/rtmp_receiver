#include "bytestream.h"
#include <cstring> // For memcpy

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
        value = (static_cast<uint16_t>(data_[offset_ + 1]) << 8) |
                (static_cast<uint16_t>(data_[offset_]));
        offset_ += 2;
    } else {
        error_ = true;
    }
    return value;
}

uint32_t ByteStream::ReadUInt24() {
    uint32_t value = 0;
    if (offset_ + 3 <= size_) {
        value = (static_cast<uint32_t>(data_[offset_ + 2]) << 16) |
                (static_cast<uint32_t>(data_[offset_ + 1]) << 8) |
                (static_cast<uint32_t>(data_[offset_]));
        offset_ += 3;
    } else {
        error_ = true;
    }
    return value;
}

uint32_t ByteStream::ReadUInt32() {
    uint32_t value = 0;
    if (offset_ + 4 <= size_) {
        value = (static_cast<uint32_t>(data_[offset_ + 3]) << 24) |
                (static_cast<uint32_t>(data_[offset_ + 2]) << 16) |
                (static_cast<uint32_t>(data_[offset_ + 1]) << 8) |
                (static_cast<uint32_t>(data_[offset_]));
        offset_ += 4;
    } else {
        error_ = true;
    }
    return value;
}

bool ByteStream::HasError() const {
    return error_;
}

bool ByteStream::IsEndOfStream() const {
    return RemainingBytes() <= 0;
}

int ByteStream::RemainingBytes() const {
    return size_ - offset_;
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
