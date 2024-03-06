#ifndef RTMPPARSER_H
#define RTMPPARSER_H

#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <unordered_map>

class RollingBuffer {
public:
    void Continue(const uint8_t* &data, int &bytes);
    void StoreRemaining(const uint8_t* data, int bytes);
    void Clear();

protected:
    std::vector<uint8_t> Buffers[2];
    int BufferIndex = 0;
};

// RTMP packet types
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

const char* GetPacketTypeName(int type_id);

static const int kRtmpS0ServerVersion = 3;

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
};

class RTMPSession {
public:
    RollingBuffer* Buffer = nullptr;

    void ParseChunk(const void* data, int bytes);

    void OnMessage(const RTMPHeader& header, const uint8_t* data, int bytes);

    uint32_t ChunkSize = 128; // default chunk size
    uint32_t AckSequenceNumber = 0; // default ack sequence number
    uint32_t WindowAckSize = 2500000; // default window ack size

    bool MustAck = false;
    uint32_t MustAckBytes = 0;

    uint32_t MaxUnackedBytes = 0;
    int LimitType = 0;

private:
    std::unordered_map<uint32_t, std::shared_ptr<RTMPChunk>> chunk_streams; // Active chunk streams

    uint32_t ReceivedBytes = 0;
};

#endif // RTMPPARSER_H