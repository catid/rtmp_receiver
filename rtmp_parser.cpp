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

static std::string CreateStringWithTrailingNull(const uint8_t* data, size_t length) {
    // Create a string from the given data and length
    std::string result(reinterpret_cast<const char*>(data), length);
    
    // Explicitly append a null character to ensure the string is null-terminated
    result.push_back('\0');
    
    return result;
}

const char* GetPacketTypeName(int type_id) {
    switch (type_id) {
        case CHUNK_SIZE: return "CHUNK_SIZE";
        case ABORT: return "ABORT";
        case ACK: return "ACK";
        case USER_CONTROL: return "USER_CONTROL";
        case WINDOW_ACK_SIZE: return "WINDOW_ACK_SIZE";
        case SET_PEER_BANDWIDTH: return "SET_PEER_BANDWIDTH";
        case AUDIO: return "AUDIO";
        case VIDEO: return "VIDEO";
        case DATA_AMF3: return "DATA_AMF3";
        case SHARED_OBJECT_AMF3: return "SHARED_OBJECT_AMF3";
        case COMMAND_AMF3: return "COMMAND_AMF3";
        case DATA_AMF0: return "DATA_AMF0";
        case SHARED_OBJECT_AMF0: return "SHARED_OBJECT_AMF0";
        case COMMAND_AMF0: return "COMMAND_AMF0";
        case AGGREGATE: return "AGGREGATE";
        default: return "UNKNOWN";
    }
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
    Buffer->Continue(buffer, bytes);

    if (State.Round >= 3) {
        return; // Handshake complete
    }

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
            Buffer->StoreRemaining(stream_start, stream_remaining);
            return;
        }

        State.Round++;

        if (State.Round >= 3) {
            Buffer->StoreRemaining(stream.PeekData(), stream.RemainingBytes());
            return;
        }
    }
}


//------------------------------------------------------------------------------
// RTMPSession

void RTMPSession::ParseChunk(const void* data, int bytes)
{
    const uint8_t* buffer = reinterpret_cast<const uint8_t*>(data);

    // Continue from previous buffer if available
    Buffer->Continue(buffer, bytes);

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
            Buffer->StoreRemaining(stream_start, stream_remaining);
            return;
        }

        // Accumulate bytes processed in this chunk
        ReceivedBytes += stream.RemainingBytes() - stream_remaining;
        if (ReceivedBytes > WindowAckSize) {
            MustAck = true;
            MustAckBytes = ReceivedBytes;
            ReceivedBytes = 0;
        }

        OnMessage(head, message_data, head.length);

        // Completed a message, so store the state for next time
        if (!prev_chunk) {
            prev_chunk = std::make_shared<RTMPChunk>();
            chunk_streams[head.cs_id] = prev_chunk;
        }
        prev_chunk->header = head;
    }

    Buffer->Clear();
}

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
    // Add other markers as needed
};

void RTMPSession::OnMessage(const RTMPHeader& header, const uint8_t* data, int bytes)
{
    std::cout << "Received message (" << GetPacketTypeName(header.type_id) << ") with bytes: " << bytes << std::endl;
    PrintFirst64BytesAsHex(data, bytes);

    ByteStream stream(data, bytes);

    switch (header.type_id) {
    case CHUNK_SIZE:
        ChunkSize = stream.ReadUInt32();
        return;
    case ABORT:
        {
            uint32_t cs_id = stream.ReadUInt32();
            auto iter = chunk_streams.find(cs_id);
            if (iter != chunk_streams.end()) {
                chunk_streams.erase(iter);
            }
        }
        return;
    case ACK:
        AckSequenceNumber = stream.ReadUInt32();
        return;
    case USER_CONTROL:
        break;
    case WINDOW_ACK_SIZE:
        WindowAckSize = stream.ReadUInt32();
        return;
    case SET_PEER_BANDWIDTH:
        MaxUnackedBytes = stream.ReadUInt32();
        LimitType = stream.ReadUInt8();
        return;
    case AUDIO:
        break;
    case VIDEO:
        break;
    case DATA_AMF3:
        break;
    case SHARED_OBJECT_AMF3:
        break;
    case COMMAND_AMF3:
        break;
    case DATA_AMF0:
        break;
    case SHARED_OBJECT_AMF0:
        break;
    case COMMAND_AMF0:
        {
            bool has_command = false;
            int object_nest_level = 0;
            while (stream.RemainingBytes() > 0) {
                if (object_nest_level > 0) {
                    uint32_t string_length = stream.ReadUInt16();
                    if (string_length == 0) {
                        std::cout << "} null string at end of object" << std::endl;
                    } else {
                        const uint8_t* string_data = stream.ReadData(string_length);
                        std::string value = CreateStringWithTrailingNull(string_data, string_length);
                        std::cout << "Received AMF0 object string key: " << value << std::endl;
                    }
                }
                uint32_t amf0_type = stream.ReadUInt8();
                if (amf0_type == ObjectEndMarker) {
                    --object_nest_level;
                    if (object_nest_level < 0) {
                        break;
                    }
                }
                else if (amf0_type == NumberMarker) {
                    double value = stream.ReadDouble();
                    std::cout << "Received AMF0 number: " << value << std::endl;
                }
                else if (amf0_type == BooleanMarker) {
                    bool value = stream.ReadUInt8() != 0;
                    std::cout << "Received AMF0 boolean: " << value << std::endl;
                }
                else if (amf0_type == StringMarker) {
                    uint32_t string_length = stream.ReadUInt16();
                    const uint8_t* string_data = stream.ReadData(string_length);
                    std::string value = CreateStringWithTrailingNull(string_data, string_length);

                    if (!has_command) {
                        has_command = true;
                        std::cout << "Received AMF0 command: " << value << std::endl;
                    } else {
                        std::cout << "Received AMF0 string: " << value << std::endl;
                    }
                }
                else if (amf0_type == NullMarker) {
                    std::cout << "Received AMF0 null" << std::endl;
                }
                else if (amf0_type == UndefinedMarker) {
                    std::cout << "Received AMF0 undefined" << std::endl;
                }
                else if (amf0_type == ReferenceMarker) {
                    uint32_t reference_id = stream.ReadUInt16();
                    std::cout << "Received AMF0 reference: " << reference_id << std::endl;
                }
                else if (amf0_type == ECMAArrayMarker) {
                    uint32_t array_length = stream.ReadUInt16();
                    std::cout << "Received AMF0 array of length: " << array_length << std::endl;
                    // FIXME: Handle array elements
                }
                else if (amf0_type == ObjectMarker) {
                    std::cout << "Start AMF0 object {" << std::endl;
                    ++object_nest_level;
                } else {
                    std::cout << "Unknown AMF0 type: " << (int)amf0_type << std::endl;
                }
            }
        }
        break;
    case AGGREGATE:
        break;
    }
}
