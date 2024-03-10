#include "rtmp_parser.h"

#include "bytestream.h"
#include "rtmp_tools.h"

#include <cstring>
#include <iostream>
#include <iomanip>
using namespace std;

//#define ENABLE_DEBUG_LOGS

#ifdef ENABLE_DEBUG_LOGS
# define LOG(x) x
#else
# define LOG(x)
#endif


//------------------------------------------------------------------------------
// Tools

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

    //LOG(cout << "RollingBuffer: Continue: BufferIndex=" << BufferIndex << ", bytes=" << bytes << " prev_buffer.size=" << prev_buffer.size() << endl;)

    // Continue from previous buffer if available
    if (prev_buffer.size() > 0) {
        if (bytes > 0) {
            AppendDataToVector(prev_buffer, data, bytes);
        }
        data = prev_buffer.data();
        bytes = prev_buffer.size();
    }
}

void RollingBuffer::StoreRemaining(const uint8_t* data, int bytes)
{
    BufferIndex ^= 1;

    std::vector<uint8_t>& next_buffer = Buffers[BufferIndex];

    next_buffer.clear();
    AppendDataToVector(next_buffer, data, bytes);

    //LOG(cout << "RollingBuffer: StoreRemaining: BufferIndex=" << BufferIndex << ", bytes=" << bytes << " next_buffer.size=" << next_buffer.size() << endl;)
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

bool RTMPSession::ParseChunk(const void* data, int bytes)
{
    const uint8_t* buffer = reinterpret_cast<const uint8_t*>(data);

    // Continue from previous buffer if available
    Buffer->Continue(buffer, bytes);

    ByteStream stream(buffer, bytes);

    //LOG(std::cout << "Received chunk bytes: " << bytes << std::endl;)
    //LOG(PrintFirst64BytesAsHex(buffer, bytes);)

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
                    head.stream_id = stream.ReadUInt32(false/*this is the only field...*/);
                } else if (prev_chunk) {
                    head.stream_id = prev_chunk->header.stream_id;
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

        LOG(std::cout << "Chunk: fmt=" << (int)head.fmt << " cs=" << head.cs_id << " len=" << head.length << " type=" << (int)head.type_id << " stream=" << head.stream_id << std::endl;)

        // If message fits in a single chunk, then attempt to read it directly.
        int expected_bytes = head.length;
        if (expected_bytes > ChunkSize) {
            if (prev_chunk) {
                expected_bytes -= static_cast<int>( prev_chunk->AccumulatedData.size() );
            }
            if (expected_bytes > ChunkSize) {
                expected_bytes = ChunkSize;
            }
        }
        const uint8_t* chunk_data = stream.ReadData(expected_bytes);
        if (stream.HasError()) {
            //LOG(std::cout << "Received chunk partial (waiting for more) on cs=" << head.cs_id << std::endl;)
            // Have not finished receiving the current chunk so save until more data arrives.
            Buffer->StoreRemaining(stream_start, stream_remaining);
            return false;
        }

        // Accumulate bytes processed in this chunk
        ReceivedBytes += stream.RemainingBytes() - stream_remaining;
        if (ReceivedBytes > WindowAckSize) {
            Handler->OnNeedAck(ReceivedBytes);
            ReceivedBytes = 0;
        }

        if (!prev_chunk) {
            prev_chunk = std::make_shared<RTMPChunk>();
            chunk_streams[head.cs_id] = prev_chunk;
        }
        prev_chunk->header = head; // Store header info for decoding the next chunk header

        const uint8_t* message_data = chunk_data;

        if (head.length > ChunkSize) {
            AppendDataToVector(prev_chunk->AccumulatedData, chunk_data, expected_bytes);
            message_data = prev_chunk->AccumulatedData.data();

            if (head.length > static_cast<int>( prev_chunk->AccumulatedData.size() )) {
                //LOG(std::cout << "Received message partial (waiting for more) on cs=" << head.cs_id << std::endl;)
                continue;
            }
        }

        OnMessage(head, message_data, head.length);

        prev_chunk->AccumulatedData.clear();
    }

    Buffer->Clear();
    return false;
}

void RTMPSession::OnMessage(const RTMPHeader& head, const uint8_t* data, int bytes)
{
    // Note: This function only implements the subset of the RTMP protocol needed to receive video.
    // However, the chunk parsing logic above is fully-featured and can handle the complete protocol.

    LOG(std::cout << "Received message cs_id=" << head.cs_id << " stream=" << head.stream_id << " ts=" << head.timestamp << " type=" << GetPacketTypeName(head.type_id) << " len=" << head.length << std::endl;)
    //LOG(PrintFirst64BytesAsHex(data, bytes);)

    ByteStream stream(data, bytes);

    switch (head.type_id) {
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
        {
            ByteStream stream(data, bytes);

            const uint8_t type_byte = stream.ReadUInt8();
            const int frame_type = type_byte >> 4;
            const int codec = type_byte & 0xf;

            if (codec != VIDEO_CODEC_H264) {
                cout << "Received unknown video codec type=" << codec << endl;
                return;
            }

            if (frame_type != VIDEO_FRAME_TYPE_KEY && frame_type != VIDEO_FRAME_TYPE_INTER) {
                cout << "Received unknown video frame type=" << frame_type << endl;
                return;
            }
            const bool keyframe = (frame_type == VIDEO_FRAME_TYPE_KEY);

            Handler->OnAvccVideo(keyframe, head.stream_id, head.timestamp, data + 1, bytes - 1);
        }
        break;
    case DATA_AMF3:
        break;
    case SHARED_OBJECT_AMF3:
        break;
    case COMMAND_AMF3:
        break;
    case DATA_AMF0:
        {
            int object_nest_level = 0;
            while (stream.RemainingBytes() > 0) {
                if (object_nest_level > 0) {
                    uint32_t string_length = stream.ReadUInt16();
                    if (string_length == 0) {
                        LOG(std::cout << "} null string at end of object" << std::endl;)
                    } else {
                        const uint8_t* string_data = stream.ReadData(string_length);
                        std::string value = CreateStringFromBytes(string_data, string_length);
                        LOG(std::cout << "Received AMF0 object string key: " << value << std::endl;)
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
                    LOG(std::cout << "Received AMF0 number: " << value << std::endl;)
                }
                else if (amf0_type == BooleanMarker) {
                    bool value = stream.ReadUInt8() != 0;
                    LOG(std::cout << "Received AMF0 boolean: " << value << std::endl;)
                }
                else if (amf0_type == StringMarker) {
                    uint32_t string_length = stream.ReadUInt16();
                    const uint8_t* string_data = stream.ReadData(string_length);
                    std::string value = CreateStringFromBytes(string_data, string_length);

                    LOG(std::cout << "Received AMF0 string: " << value << std::endl;)
                }
                else if (amf0_type == NullMarker) {
                    LOG(std::cout << "Received AMF0 null" << std::endl;)
                }
                else if (amf0_type == UndefinedMarker) {
                    LOG(std::cout << "Received AMF0 undefined" << std::endl;)
                }
                else if (amf0_type == ReferenceMarker) {
                    uint32_t reference_id = stream.ReadUInt16();
                    LOG(std::cout << "Received AMF0 reference: " << reference_id << std::endl;)
                }
                else if (amf0_type == ECMAArrayMarker) {
                    uint32_t array_length = stream.ReadUInt32();
                    LOG(std::cout << "Received AMF0 array of length: " << array_length << std::endl;)
                    ++object_nest_level;
                }
                else if (amf0_type == ObjectMarker) {
                    LOG(std::cout << "Start AMF0 object {" << std::endl;)
                    ++object_nest_level;
                } else {
                    LOG(std::cout << "Unknown AMF0 type: " << (int)amf0_type << std::endl;)
                }
            }
        }
        break;
    case SHARED_OBJECT_AMF0:
        break;
    case COMMAND_AMF0:
        {
            std::string command_name;
            int object_nest_level = 0;
            double command_number = 0;
            bool has_command_number = false;
            while (stream.RemainingBytes() > 0) {
                if (object_nest_level > 0) {
                    uint32_t string_length = stream.ReadUInt16();
                    if (string_length == 0) {
                        LOG(std::cout << "} null string at end of object" << std::endl;)
                    } else {
                        const uint8_t* string_data = stream.ReadData(string_length);
                        std::string value = CreateStringFromBytes(string_data, string_length);
                        LOG(std::cout << "Received AMF0 object string key: " << value << std::endl;)
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
                    LOG(std::cout << "Received AMF0 number: " << value << std::endl;)
                    if (!has_command_number) {
                        command_number = value;
                        has_command_number = true;
                    }
                }
                else if (amf0_type == BooleanMarker) {
                    bool value = stream.ReadUInt8() != 0;
                    LOG(std::cout << "Received AMF0 boolean: " << value << std::endl;)
                }
                else if (amf0_type == StringMarker) {
                    uint32_t string_length = stream.ReadUInt16();
                    const uint8_t* string_data = stream.ReadData(string_length);
                    std::string value = CreateStringFromBytes(string_data, string_length);

                    if (command_name.empty()) {
                        command_name = value;
                        LOG(std::cout << "Received AMF0 command: " << value << std::endl;)
                    } else {
                        LOG(std::cout << "Received AMF0 string: " << value << std::endl;)
                    }
                }
                else if (amf0_type == NullMarker) {
                    LOG(std::cout << "Received AMF0 null" << std::endl;)
                }
                else if (amf0_type == UndefinedMarker) {
                    LOG(std::cout << "Received AMF0 undefined" << std::endl;)
                }
                else if (amf0_type == ReferenceMarker) {
                    uint32_t reference_id = stream.ReadUInt16();
                    LOG(std::cout << "Received AMF0 reference: " << reference_id << std::endl;)
                }
                else if (amf0_type == ECMAArrayMarker) {
                    uint32_t array_length = stream.ReadUInt32();
                    LOG(std::cout << "Received AMF0 array of length: " << array_length << std::endl;)
                    ++object_nest_level;
                }
                else if (amf0_type == ObjectMarker) {
                    LOG(std::cout << "Start AMF0 object {" << std::endl;)
                    ++object_nest_level;
                } else {
                    LOG(std::cout << "Unknown AMF0 type: " << (int)amf0_type << std::endl;)
                }
            }

            LOG(cout << "command_name='" << command_name << "'" << endl;)

            Handler->OnMessage(command_name, command_number);
        }
        break;
    case AGGREGATE:
        break;
    }
}
