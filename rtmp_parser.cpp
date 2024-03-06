#include "rtmp_parser.h"

#include "bytestream.h"

#include <iostream>
#include <iomanip>
#include <cstring>
#include <chrono>


//------------------------------------------------------------------------------
// Tools

static void AppendDataToVector(std::vector<uint8_t>& vec, const uint8_t* data, int bytes) {
    if (data != nullptr && bytes > 0) {
        vec.insert(vec.end(), data, data + bytes);
    }
}

static void PrintFirst64BytesAsHex(const uint8_t* data, size_t size) {
    size_t bytesToPrint = (size < 64) ? size : 64; // Limit to the first 64 bytes

    for (size_t i = 0; i < bytesToPrint; ++i) {
        // Print each byte in hex format, padded with 0 if necessary
        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]) << " ";
        
        // Optional: add a new line every 16 bytes for readability
        if ((i + 1) % 16 == 0) {
            std::cout << std::endl;
        }
    }

    std::cout << std::dec << std::endl; // Switch back to decimal for any further output
}


//------------------------------------------------------------------------------
// RollingBuffer

void RollingBuffer::Continue(const uint8_t* &data, int &bytes)
{
    std::vector<uint8_t>& prev_buffer = Buffers[BufferIndex];

    // Continue from previous buffer if available
    if (prev_buffer.size() > 0) {
        AppendDataToVector(prev_buffer, data, bytes);
        data = prev_buffer.data();
        bytes = prev_buffer.size();
    }
}

void RollingBuffer::StoreRemaining(const uint8_t* data, int bytes)
{
    ++BufferIndex;
    if (BufferIndex >= 2) {
        BufferIndex = 0;
    }

    std::vector<uint8_t>& next_buffer = Buffers[BufferIndex];

    next_buffer.clear();
    AppendDataToVector(next_buffer, data, bytes);
}

void RollingBuffer::Clear()
{
    Buffers[0].clear();
    Buffers[1].clear();
    BufferIndex = 0;
}


//------------------------------------------------------------------------------
// RTMPHandshake

void RTMPHandshake::ParseMessage(const void* data, int bytes)
{
    const uint8_t* buffer = reinterpret_cast<const uint8_t*>(data);

    // Continue from previous buffer if available
    Buffer.Continue(buffer, bytes);

    ByteStream stream(buffer, bytes);

    while (!stream.IsEndOfStream()) {
        // Store the start of this stream message so if it is truncated we can store it
        const uint8_t* stream_start = stream.PeekData();
        int stream_remaining = stream.RemainingBytes();

        if (State.Round == 0) {
            State.ClientVersion = stream.ReadUInt8();
        } else if (State.Round == 1) {
            State.ClientTime1 = stream.ReadUInt32();
            stream.ReadUInt32(); // Zero
            const uint8_t* rand_data = stream.ReadData(1536 - 8);

            if (!stream.HasError()) {
                memcpy(State.ClientRandom, rand_data, 1536 - 8);
            }
        } else if (State.Round == 2) {
            State.ClientTime2 = stream.ReadUInt32();
            State.ClientTime22 = stream.ReadUInt32();
            const uint8_t* rand_echo = stream.ReadData(1536 - 8);

            if (!stream.HasError()) {
                memcpy(State.ClientEcho, rand_echo, 1536 - 8);
            }
        }

        if (stream.HasError()) {
            Buffer.StoreRemaining(stream_start, stream_remaining);
            return;
        }

        State.Round++;
    }
}


//------------------------------------------------------------------------------
// RTMPSession

void RTMPSession::ParseChunk(const void* data, int bytes)
{
    const uint8_t* buffer = reinterpret_cast<const uint8_t*>(data);

    // Continue from previous buffer if available
    Buffer.Continue(buffer, bytes);

    ByteStream stream(buffer, bytes);

    std::cout << "Received chunk bytes: " << bytes << std::endl;
    PrintFirst64BytesAsHex(buffer, bytes);

    while (!stream.IsEndOfStream()) {
        // Store the start of this stream message so if it is truncated we can store it
        const uint8_t* stream_start = stream.PeekData();
        int stream_remaining = stream.RemainingBytes();

        RTMPHeader head;

        uint8_t basic_header = stream.ReadUInt8();
        head.fmt = (basic_header >> 6) & 0x03;
        head.cs_id = basic_header & 0x3F; // Simplified, real logic for cs_id > 64 is omitted
        if (head.cs_id == 0) {
            head.cs_id = stream.ReadUInt8() + 64;
        } else if (head.cs_id == 1) {
            head.cs_id = stream.ReadUInt16() + 64;
        }

        std::shared_ptr<RTMPChunk> prev_chunk;
        auto iter = chunk_streams.find(head.cs_id);
        if (iter != chunk_streams.end()) {
            prev_chunk = iter->second;
        }

        // Parse message header based on fmt
        head.timestamp = 0;
        bool extended_timestamp_present = false;

        if (head.fmt <= 2) {
            head.timestamp = stream.ReadUInt24();
            if (head.fmt <= 1) {
                head.length = stream.ReadUInt24();
                head.type_id = stream.ReadUInt8();
                if (head.fmt == 0) {
                    head.stream_id = stream.ReadUInt32();
                }
            }
            if (head.fmt == 2 && prev_chunk) {
                head.stream_id = prev_chunk->header.stream_id;
                head.length = prev_chunk->header.length;
                head.type_id = prev_chunk->header.type_id;
            }
        } else if (head.fmt == 3) {
            head.timestamp = 0; // delta = 0
            head.length = prev_chunk->header.length;
            head.type_id = prev_chunk->header.type_id;
            head.stream_id = prev_chunk->header.stream_id;
        }

        // Check for extended timestamp
        if (head.timestamp == 0xFFFFFF) {
            head.timestamp = stream.ReadUInt32();
        } else if (prev_chunk) {
            head.timestamp += prev_chunk->header.timestamp;
        }

        std::cout << "Received chunk: fmt=" << (int)head.fmt << ", cs_id=" << head.cs_id
            << ", ts=" << head.timestamp << ", len=" << head.length << ", type_id="
            << (int)head.type_id << ", stream_id=" << head.stream_id << std::endl;

        const uint8_t* message_data = stream.ReadData(head.length);

        // If truncated:
        if (stream.HasError()) {
            std::cout << "Error: truncated message stream.RemainingBytes()=" << stream.RemainingBytes() << ", head.length = " << head.length << std::endl;
            Buffer.StoreRemaining(stream_start, stream_remaining);
            return;
        }

        OnMessage(head, message_data, head.length);

        // Completed a message, so store the state for next time
        if (!prev_chunk) {
            prev_chunk = std::make_shared<RTMPChunk>();
            chunk_streams[head.cs_id] = prev_chunk;
        }
        prev_chunk->header = head;
    }

    Buffer.Clear();
}

void RTMPSession::OnMessage(const RTMPHeader& header, const uint8_t* data, int bytes)
{
    std::cout << "Received message with bytes: " << bytes << std::endl;
}
