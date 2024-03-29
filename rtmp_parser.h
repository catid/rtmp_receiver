#ifndef RTMP_PARSER_H
#define RTMP_PARSER_H

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <unordered_map>


//------------------------------------------------------------------------------
// Definitions

// Reference: https://rtmp.veriskope.com/docs/spec/

enum RTMPPacketType {
    CHUNK_SIZE = 1,
    ABORT = 2,
    ACK = 3,
    USER_CONTROL = 4,
    WINDOW_ACK_SIZE = 5,
    SET_PEER_BANDWIDTH = 6,
    AUDIO = 8,
    VIDEO = 9,
    DATA_AMF3 = 15,
    SHARED_OBJECT_AMF3 = 16,
    COMMAND_AMF3 = 17,
    DATA_AMF0 = 18,
    SHARED_OBJECT_AMF0 = 19,
    COMMAND_AMF0 = 20,
    AGGREGATE = 22
};

enum AMF0Type {
    NumberMarker = 0x00,
    BooleanMarker = 0x01,
    StringMarker = 0x02,
    ObjectMarker = 0x03,
    NullMarker = 0x05,
    UndefinedMarker = 0x06,
    ReferenceMarker = 0x07,
    ECMAArrayMarker = 0x08,
    ObjectEndMarker = 0x09
};

enum EventType {
    EVENT_STREAM_BEGIN = 0,
    EVENT_STREAM_EOF = 1,
    EVENT_STREAM_DRY = 2,
    EVENT_STREAM_ERROR = 3,
    EVENT_ABORT = 4,
    EVENT_SET_BUFFER_LENGTH = 5,
    EVENT_USER_CONTROL = 6,
    EVENT_PING = 7,
    EVENT_PONG = 8,
};

enum LimitType {
    LIMIT_HARD = 0,
    LIMIT_SOFT = 1,
    LIMIT_DYNAMIC = 2
};

enum VideoFrameType {
    VIDEO_FRAME_TYPE_KEY = 1,
    VIDEO_FRAME_TYPE_INTER = 2,
    VIDEO_FRAME_TYPE_DISPOSABLE = 3,
    VIDEO_FRAME_TYPE_GENERATED = 4,
    VIDEO_FRAME_TYPE_COMMAND = 5,
};

enum VideoCodec {
    VIDEO_CODEC_VLC1 = 2,
    VIDEO_CODEC_SCREEN_VIDEO = 3,
    VIDEO_CODEC_VP6 = 4,
    VIDEO_CODEC_VP6A = 5,
    VIDEO_CODEC_SCREEN_VIDEO2 = 6,
    VIDEO_CODEC_H264 = 7,
};

enum AvcPacketType {
    AVC_SEQUENCE_HEADER = 0,
    AVC_NALU = 1,
};

static const int kRtmpS0ServerVersion = 3;


//------------------------------------------------------------------------------
// Tools

const char* GetPacketTypeName(int type_id);

class RollingBuffer {
public:
    void Continue(const uint8_t* &data, int &bytes);
    void StoreRemaining(const uint8_t* data, int bytes);
    void Clear();

protected:
    std::vector<uint8_t> Buffers[2];
    int BufferIndex = 0;
};


//------------------------------------------------------------------------------
// Parser Helpers

struct HandshakeState {
    int Round = 0;

    // Round 0
    int ClientVersion = -1;

    // Round 1
    uint32_t ClientTime1 = 0;
    uint8_t ClientRandom[1536 - 8];

    // Round 2
    uint32_t ClientTime2 = 0;
    uint32_t ClientTime22 = 0;
    uint8_t ClientEcho[1536 - 8];
};

class RTMPHandshake {
public:
    RollingBuffer* Buffer = nullptr;

    void ParseMessage(const void* data, int bytes);

    HandshakeState State;
};

struct RTMPHeader {
    uint8_t fmt = 0; // Chunk format
    uint32_t cs_id = 0; // Chunk stream ID
    uint32_t timestamp = 0; // Timestamp
    uint32_t length = 0; // Message length
    uint8_t type_id = 0; // Message type ID
    uint32_t stream_id = 0; // Message stream ID
};

struct RTMPChunk {
    RTMPHeader header;

    // Accumulated data from previous ChunkSize chunks
    std::vector<uint8_t> AccumulatedData;
};

class RTMPHandler {
public:
    // Server should send a chunk acknowledgement
    virtual void OnNeedAck(uint32_t bytes) = 0;

    // Server should send a COMMAND_AMF0 acknowledgement
    virtual void OnMessage(const std::string& name, double number) = 0;

    virtual void OnAvccVideo(bool keyframe, uint32_t stream, uint32_t timestamp, const uint8_t* data, int bytes) = 0;
};

class RTMPSession {
public:
    RollingBuffer* Buffer = nullptr;
    RTMPHandler* Handler = nullptr;

    bool ParseChunk(const void* data, int bytes);

    void OnMessage(const RTMPHeader& header, const uint8_t* data, int bytes);

    uint32_t ChunkSize = 128; // default chunk size
    uint32_t AckSequenceNumber = 0; // default ack sequence number
    uint32_t WindowAckSize = 2500000; // default window ack size
    uint32_t MaxUnackedBytes = 0;
    int LimitType = 0;

private:
    std::unordered_map<uint32_t, std::shared_ptr<RTMPChunk>> chunk_streams; // Active chunk streams

    uint32_t ReceivedBytes = 0;
};

#endif // RTMP_PARSER_H
