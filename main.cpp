#include <iostream>
#include <fstream>
#include <vector>

#include "rtmp_receiver.h"
#include "rtmp_tools.h"

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

// Callback function for RTMP receiver
void rtmpCallback(bool newStream, bool keyframe, uint32_t stream, uint32_t timestamp, const uint8_t* data, int bytes) {
    if (newStream) {
        std::cout << "*** New stream " << stream << std::endl;

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

        // Parse the AVCC extradata
        if (bytes > 6 && data[0] == 0x01) {
            int extradata_size = bytes - 5;
            avccExtradata.resize(extradata_size);
            std::copy(data + 5, data + bytes, avccExtradata.begin());
            codecContext->extradata = avccExtradata.data();
            codecContext->extradata_size = extradata_size;
        }

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
    }
    if (!packet) {
        std::cout << "Codec not initialized." << std::endl;
        return;
    }

    std::cout << "Received video keyframe=" << keyframe << " data on stream=" << stream << " ts=" << timestamp << " bytes=" << bytes << std::endl;

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
    server.Start(rtmpCallback);

    std::cout << "Press Enter to stop the server..." << std::endl;
    std::cin.get();

    // Free the allocated resources
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codecContext);

    return 0;
}
