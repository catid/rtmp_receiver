#include <iostream>
#include <fstream>
#include <vector>

#include "rtmp_receiver.h"
#include "rtmp_tools.h"
#include "bytestream.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

// Global variables
AVCodecContext* codecContext = nullptr;
const AVCodec* codec = nullptr;
AVFrame* frame = nullptr;
AVPacket* packet = nullptr;
std::vector<uint8_t> avccExtradata;
int VideoSizeBytes = 0;

void rtmpSetupCallback(
    uint32_t stream,
    RTMPSetupResult& result)
{
    std::cout << "*** New stream " << stream << std::endl;
    std::cout << "Extradata size: " << result.ExtradataSize << std::endl;
    std::cout << "SPS count: " << result.SPS.size() << std::endl;
    std::cout << "PPS count: " << result.PPS.size() << std::endl;
    std::cout << "Video size bytes: " << result.VideoSizeBytes << std::endl;
    VideoSizeBytes = result.VideoSizeBytes;

    // Find the decoder for H.264
    codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (codec == nullptr) {
        std::cout << "Unsupported codec." << std::endl;
        return;
    }

    // Create the codec context
    codecContext = avcodec_alloc_context3(codec);
    if (codecContext == nullptr) {
        std::cout << "Failed to allocate codec context." << std::endl;
        return;
    }

    codecContext->extradata = (uint8_t*)av_malloc(result.ExtradataSize);
    if(!codecContext->extradata){
        std::cout << "Failed to allocate extradata." << std::endl;
        return;
    }
    codecContext->extradata_size = result.ExtradataSize;
    memcpy(codecContext->extradata, result.Extradata, result.ExtradataSize);

    // Open the codec
    if (avcodec_open2(codecContext, codec, nullptr) < 0) {
        std::cout << "Failed to open the codec." << std::endl;
        return;
    }

    // Allocate the video frame
    frame = av_frame_alloc();
    if (frame == nullptr) {
        std::cout << "Failed to allocate the video frame." << std::endl;
        return;
    }

    // Allocate the packet
    packet = av_packet_alloc();
    if (packet == nullptr) {
        std::cout << "Failed to allocate the packet." << std::endl;
        return;
    }

    std::cout << "Initialized video decoder" << std::endl;
}

void rtmpVideoCallback(
    uint32_t stream,
    bool keyframe,
    uint32_t timestamp,
    const uint8_t* data,
    int bytes)
{
    if (!packet) {
        std::cout << "Codec not initialized." << std::endl;
        return;
    }

    std::cout << "Received video keyframe=" << keyframe << " data on stream=" << stream << " ts=" << timestamp << " bytes=" << bytes << std::endl;

    ByteStream avcc_stream(data, bytes);

    std::vector<uint8_t> annex_b_buffer;

    while (!avcc_stream.IsEndOfStream()) {
        uint32_t nalSize = 0;
        if (VideoSizeBytes == 1) {
            nalSize = avcc_stream.ReadUInt8();
        } else if (VideoSizeBytes == 2) {
            nalSize = avcc_stream.ReadUInt16();
        } else if (VideoSizeBytes == 3) {
            nalSize = avcc_stream.ReadUInt24();
        } else {
            nalSize = avcc_stream.ReadUInt32();
        }

        std::cout << "NALU size: " << nalSize << std::endl;

        const uint8_t* nalData = avcc_stream.ReadData(nalSize);
        if (avcc_stream.HasError()) {
            std::cout << "Truncated while reading NALU" << std::endl;
            return;
        }

        ConvertToAnnexB(nalData, nalSize, annex_b_buffer);
    }

    data = annex_b_buffer.data();
    bytes = annex_b_buffer.size();

    // Create a new packet from the received data
    if (av_new_packet(packet, bytes) < 0) {
        std::cout << "Error allocating packet." << std::endl;
        return;
    }
    memcpy(packet->data, data, bytes);

    // Send the packet to the decoder
    if (avcodec_send_packet(codecContext, packet) < 0) {
        std::cout << "Error sending packet for decoding." << std::endl;
        return;
    }

    // Receive the decoded frame from the decoder
    while (avcodec_receive_frame(codecContext, frame) == 0) {
        std::cout << "Decoded frame: width=" << frame->width << " height=" << frame->height << std::endl;
    }

    // Free the packet
    av_packet_unref(packet);
}

int main() {
    // Start the RTMP receiver
    RTMPReceiver server;
    server.Start(rtmpSetupCallback, rtmpVideoCallback);

    std::cout << "Press Enter to stop the server..." << std::endl;
    std::cin.get();

    // Free the allocated resources
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codecContext);

    return 0;
}
