#include "avcc_parser.h"
#include "rtmp_tools.h"

#include <iostream>


//------------------------------------------------------------------------------
// Tools

static const uint8_t kStartCode[] = {0x00, 0x00, 0x00, 0x01};
static const uint8_t kPrefixCode[] = {0x00, 0x00, 0x03};

static void ConvertToAnnexB(const uint8_t* data, size_t size, std::vector<uint8_t>& out_buffer) {
    out_buffer.insert(out_buffer.end(), kStartCode, kStartCode + sizeof(kStartCode));

    while (size > 0) {
        const uint8_t ch = data[0];
        if (size >= 3 && ch == 0 && data[1] == 0 && data[2] == 0) {
            out_buffer.insert(out_buffer.end(), kPrefixCode, kPrefixCode + sizeof(kPrefixCode));
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
    Video.clear();

    ByteStream stream(data, size);

    int type = stream.ReadUInt8();

    if (type == 0) {
        parseExtradata(stream);
    } else if (type == 1) {
        if (Extradata.size() > 0) {
            AppendDataToVector(Video, Extradata.data(), Extradata.size());
            Extradata.clear();
        }
        parseCodedVideo(stream);
    } else {
        std::cout << "Unsupported AVCC type " << type << std::endl;
    }
    if (stream.HasError()) {
        std::cout << "Truncated parsing AVCC" << std::endl;
    }
}

void AVCCParser::parseExtradata(ByteStream& stream) {
    int skip = stream.ReadUInt24();
    int configVersion = stream.ReadUInt8();
    int profile = stream.ReadUInt8();
    int profileCompatibility = stream.ReadUInt8();
    int level = stream.ReadUInt8();
    VideoSizeBytes = (stream.ReadUInt8() & 0x03) + 1;

    int numSPS = stream.ReadUInt8() & 0x1F;
    for (int i = 0; i < numSPS; ++i) {
        int paramSize = stream.ReadUInt16();
        const uint8_t* paramData = stream.ReadData(paramSize);
        if (stream.HasError()) {
            std::cout << "Truncated while reading SPS" << std::endl;
            return;
        }
        ConvertToAnnexB(paramData, paramSize, Extradata);
    }

    int numPPS = stream.ReadUInt8();
    for (int i = 0; i < numPPS; ++i) {
        int paramSize = stream.ReadUInt16();
        const uint8_t* paramData = stream.ReadData(paramSize);
        if (stream.HasError()) {
            std::cout << "Truncated while reading PPS" << std::endl;
            return;
        }
        ConvertToAnnexB(paramData, paramSize, Extradata);
    }
}

void AVCCParser::parseCodedVideo(ByteStream& stream) {
    int skip = stream.ReadUInt24();

    while (!stream.IsEndOfStream()) {
        uint32_t nalSize = 0;
        if (VideoSizeBytes == 1) {
            nalSize = stream.ReadUInt8();
        } else if (VideoSizeBytes == 2) {
            nalSize = stream.ReadUInt16();
        } else if (VideoSizeBytes == 3) {
            nalSize = stream.ReadUInt24();
        } else {
            nalSize = stream.ReadUInt32();
        }

        const uint8_t* nalData = stream.ReadData(nalSize);
        if (stream.HasError()) {
            std::cout << "Truncated while reading NALU" << std::endl;
            return;
        }

        ConvertToAnnexB(nalData, nalSize, Video);
    }
}
