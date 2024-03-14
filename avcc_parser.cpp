#include "avcc_parser.h"
#include "rtmp_tools.h"

#include <iostream>


//------------------------------------------------------------------------------
// AVCCParser

static const uint8_t kStartCode[] = {0x00, 0x00, 0x00, 0x01};
static const uint8_t kPrefixCode[] = {0x00, 0x00, 0x03};

void ConvertToAnnexB(
    const uint8_t* data,
    size_t size,
    std::vector<uint8_t>& out_buffer)
{
    AppendDataToVector(out_buffer, kStartCode, sizeof(kStartCode));

    while (size > 0) {
        const uint8_t ch = data[0];
        if (size >= 3 && ch == 0 && data[1] == 0 && data[2] == 0) {
            AppendDataToVector(out_buffer, kPrefixCode, sizeof(kPrefixCode));
            data += 3;
            size -= 3;
        } else {
            out_buffer.push_back(ch);
            ++data;
            --size;
        }
    }
}


//------------------------------------------------------------------------------
// AVCCParser

void AVCCParser::parseAvcc(const uint8_t* data, size_t size) {
    VideoData = nullptr;
    VideoSize = 0;

    ByteStream stream(data, size);

    int type = stream.ReadUInt8();
    int skip = stream.ReadUInt24();
    UNUSED(skip);

    if (type == 0) {
        parseExtradata(stream);
    } else if (type == 1) {
        parseCodedVideo(stream);
    } else {
        std::cout << "Unsupported AVCC type " << type << std::endl;
    }

    if (stream.HasError()) {
        std::cout << "Truncated parsing AVCC" << std::endl;
    }
}

void AVCCParser::parseExtradata(ByteStream& stream) {
    SetupResult.Extradata = stream.PeekData();
    SetupResult.ExtradataSize = stream.RemainingBytes();

    int configVersion = stream.ReadUInt8();
    UNUSED(configVersion);
    int profile = stream.ReadUInt8();
    UNUSED(profile);
    int profileCompatibility = stream.ReadUInt8();
    UNUSED(profileCompatibility);
    int level = stream.ReadUInt8();
    UNUSED(level);
    SetupResult.VideoSizeBytes = (stream.ReadUInt8() & 0x03) + 1;

    int numSPS = stream.ReadUInt8() & 0x1F;
    for (int i = 0; i < numSPS; ++i) {
        int paramSize = stream.ReadUInt16();
        const uint8_t* paramData = stream.ReadData(paramSize);
        if (stream.HasError()) {
            std::cout << "Truncated while reading SPS" << std::endl;
            return;
        }
        ParameterData data;
        data.Data = paramData;
        data.Size = paramSize;
        SetupResult.SPS.push_back(data);
    }

    int numPPS = stream.ReadUInt8();
    for (int i = 0; i < numPPS; ++i) {
        int paramSize = stream.ReadUInt16();
        const uint8_t* paramData = stream.ReadData(paramSize);
        if (stream.HasError()) {
            std::cout << "Truncated while reading PPS" << std::endl;
            return;
        }
        ParameterData data;
        data.Data = paramData;
        data.Size = paramSize;
        SetupResult.PPS.push_back(data);
    }

    if (stream.HasError()) {
        std::cout << "Truncated while reading parameters" << std::endl;
        return;
    }

    HasParams = true;
}

void AVCCParser::parseCodedVideo(ByteStream& stream) {
    if (stream.IsEndOfStream()) {
        return;
    }

    VideoSize = stream.RemainingBytes();
    VideoData = stream.ReadData(VideoSize);
}
