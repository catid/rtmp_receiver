#ifndef AVCC_PARSER_H
#define AVCC_PARSER_H

#include <vector>
#include <cstdint>
#include <string>

#include "bytestream.h"

//------------------------------------------------------------------------------
// AVCCParser

void ConvertToAnnexB(
    const uint8_t* data,
    size_t size,
    std::vector<uint8_t>& out_buffer);

//------------------------------------------------------------------------------
// AVCCParser

struct ParameterData {
    const uint8_t* Data = nullptr;
    int Size = 0;
};

struct RTMPSetupResult {
    // Raw input
    const uint8_t* Extradata = nullptr;
    int ExtradataSize = 0;

    // Parsed input
    std::vector<ParameterData> SPS, PPS;
    int VideoSizeBytes;
};

class AVCCParser {
public:
    void parseAvcc(const uint8_t* data, size_t size);

    bool HasParams = false;
    RTMPSetupResult SetupResult;

    const uint8_t* VideoData = nullptr;
    int VideoSize = 0;

private:
    void parseExtradata(ByteStream& stream);
    void parseCodedVideo(ByteStream& stream);
};

#endif // AVCC_PARSER_H
