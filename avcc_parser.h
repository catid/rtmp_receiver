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

    void parseExtradata(ByteStream& stream);
    void parseCodedVideo(ByteStream& stream);

    void convertToAnnexB(const uint8_t* data, size_t size);
};

#endif // AVCC_PARSER_H
