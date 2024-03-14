#ifndef AVCC_PARSER_H
#define AVCC_PARSER_H

#include <vector>
#include <cstdint>
#include <string>

#include "bytestream.h"

//------------------------------------------------------------------------------
// AVCCParser

class AVCCParser {
public:
    void parseAvcc(const uint8_t* data, size_t size);

    std::vector<uint8_t> Video;

private:
    int VideoSizeBytes = 0;

    std::vector<uint8_t> Extradata;

    void parseExtradata(ByteStream& stream);
    void parseCodedVideo(ByteStream& stream);
};

#endif // AVCC_PARSER_H
