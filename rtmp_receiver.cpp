#include "rtmp_receiver.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>

#include "rtmp_parser.h"
#include "bytestream.h"
#include "rtmp_tools.h"

#include <iostream>
#include <iomanip>
using namespace std;


//------------------------------------------------------------------------------
// Tools

static void SetNonBlocking(int s) {
    int flags = fcntl(s, F_GETFL, 0);
    if (flags < 0) {
        perror("fcntl failed");
        return;
    }
    flags |= O_NONBLOCK;
    if (fcntl(s, F_SETFL, flags) < 0) {
        perror("fcntl failed");
        return;
    }
}


//------------------------------------------------------------------------------
// RTMPReceiver

bool RTMPReceiver::Start(RTMPCallback callback, int port, bool enable_logging) {
    Callback = callback;
    Port = port;
    EnableLogging = enable_logging;

    // Allocate receive buffer on heap
    RecvBuffer.resize(2048 * 16);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, ControlSock) < 0) {
        perror("socketpair failed");
        return false;
    }
    SetNonBlocking(ControlSock[1]); // Set write end non-blocking

    Terminated = false;
    Thread = std::make_shared<std::thread>(&RTMPReceiver::Loop, this);

    return true;
}

void RTMPReceiver::Stop() {
    Terminated = true;

    char stop = 's';
    if (write(ControlSock[1], &stop, sizeof(stop)) < 0) {
        perror("write failed");
    }

    // Wait for the thread to exit
    if (Thread && Thread->joinable()) {
        Thread->join();
    }
    Thread = nullptr;

    close(ControlSock[0]);
    close(ControlSock[1]);
}

void RTMPReceiver::Loop() {
    // Keep running until the thread is stopped
    while (!Terminated) {
        RunServer();

        // Avoid busy-looping
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void RTMPReceiver::RunServer() {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("socket failed");
        return;
    }

    AutoClose serverSocketCloser([&]() {
        close(s);
    });

    int optval = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("setsockopt failed");
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(Port);

    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind failed");
        return;
    }

    if (listen(s, 1) < 0) {
        perror("listen failed");
        return;
    }

    if (EnableLogging) {
        cout << "RTMP server listening on port " << Port << endl;
    }

    while (!Terminated) {
        // Reset for each new connection
        video_streams.clear();

        if (WaitForConnection(s)) {
            HandleNextClient(s);
        }
    }
}

bool RTMPReceiver::WaitForConnection(int server_socket) {
    fd_set readfds;
    const int maxfd = max(server_socket, ControlSock[0]);

    FD_ZERO(&readfds);
    FD_SET(server_socket, &readfds);
    FD_SET(ControlSock[0], &readfds);

    if (select(maxfd + 1, &readfds, nullptr, nullptr, nullptr) < 0) {
        perror("select failed");
        return false;
    }

    if (FD_ISSET(ControlSock[0], &readfds)) {
        char stop = 0;
        if (read(ControlSock[0], &stop, sizeof(stop)) < 0) {
            perror("read failed");
        }
        return false;
    }

    if (FD_ISSET(server_socket, &readfds) && !Terminated) {
        return true; // Connection accepted
    }

    return false;
}

void RTMPReceiver::HandleNextClient(int server_socket) {

    int cs = accept(server_socket, nullptr, nullptr);
    if (cs < 0) {
        perror("accept failed");
        return;
    }
    ClientSocket = cs;

    AutoClose clientSocketCloser([&]() {
        close(cs);
    });

    if (EnableLogging) {
        cout << "Client connected" << endl;
    }

    RollingBuffer Buffer; // Keep left-overs from previous chunks

    RTMPHandshake handshake;
    handshake.Buffer = &Buffer;
    bool sent_s0s1 = false;
    bool sent_s2 = false;

    while (!Terminated)
    {
        ssize_t recv_bytes = recv(cs, RecvBuffer.data(), RecvBuffer.size(), 0);
        if (recv_bytes <= 0) {
            if (EnableLogging) {
                cout << "Client disconnected" << endl;
            }
            return;
        }

        handshake.ParseMessage(RecvBuffer.data(), static_cast<int>( recv_bytes ));

        // If we have C0 but we haven't sent S0 and S1 yet:
        if (!sent_s0s1 && handshake.State.Round >= 1) {
            if (handshake.State.ClientVersion != kRtmpS0ServerVersion) {
                cout << "Invalid version from client = " << handshake.State.ClientVersion << endl;
                return;
            }
            if (!SendS0S1()) {
                cout << "Failed to send S1 to client" << endl;
                return;
            }
            sent_s0s1 = true;
        }

        // If we have C1 but we haven't sent S2 yet:
        if (!sent_s2 && handshake.State.Round >= 2) {
            if (!SendS2(handshake.State.ClientTime1, handshake.State.ClientRandom)) {
                cout << "Failed to send random echo to client" << endl;
                return;
            }
            sent_s2 = true;
        }

        // If we have C2:
        if (handshake.State.Round >= 3) {
            if (!CheckC2(handshake.State.ClientEcho)) {
                cout << "Invalid random echo from client" << endl;
                return;
            }
            break; // Handshake complete
        }
    } // Continue handshake

    if (EnableLogging) {
        cout << "Handshake complete" << endl;
    }

    RTMPSession parser;
    parser.Buffer = &Buffer;
    parser.Handler = this;

    const uint8_t* parse_data = nullptr;
    ssize_t bytesRead = 0;

    while (!Terminated)
    {
        // We parse first because there may be some data left over from the handshake
        while (parser.ParseChunk(parse_data, static_cast<int>( bytesRead ))) {
            // Pass null next time to continue parsing the same buffer
            parse_data = nullptr;
            bytesRead = 0;
        }

        parse_data = RecvBuffer.data();
        bytesRead = recv(cs, RecvBuffer.data(), RecvBuffer.size(), 0);
        //cout << "Session: Received " << bytesRead << " bytes of data from client" << endl;
        if (bytesRead <= 0) {
            if (EnableLogging) {
                cout << "Client disconnected" << endl;
            }
            return;
        }
    }
}

bool RTMPReceiver::SendS0S1() {
    const uint32_t timestamp = static_cast<uint32_t>( GetMsec() );

    Handshake[0] = kRtmpS0ServerVersion;
    WriteUInt32(Handshake + 1, timestamp);
    FillRandomBuffer(Handshake + 1 + 4, 1536 - 4, timestamp);
    ssize_t bytes = send(ClientSocket, Handshake, sizeof(Handshake), MSG_NOSIGNAL);
    return bytes == sizeof(Handshake);
}

bool RTMPReceiver::SendS2(uint32_t peer_time, const void* client_random) {
    WriteUInt32(RandomEcho, peer_time);
    WriteUInt32(RandomEcho + 4, 0);
    memcpy(RandomEcho + 8, client_random, 1536 - 8);
    ssize_t bytes = send(ClientSocket, RandomEcho, sizeof(RandomEcho), MSG_NOSIGNAL);
    return bytes == sizeof(RandomEcho);
}

bool RTMPReceiver::CheckC2(const void* echo) {
    return 0 == memcmp(Handshake + 1 + 4 + 4, echo, 1536 - 8);
}

void RTMPReceiver::OnNeedAck(uint32_t bytes) {
    SendChunkAck(bytes);
}

bool RTMPReceiver::SendChunkAck(uint32_t ack_bytes) {
    uint32_t timestamp = 0;

    ByteStreamWriter msg;

    msg.WriteUInt8(3); // cs_id = 3, fmt = 0
    msg.WriteUInt24(timestamp);
    msg.WriteUInt24(4/*length*/);
    msg.WriteUInt8(COMMAND_AMF0);
    msg.WriteUInt32(0/*stream_id*/);
        msg.WriteUInt32(ack_bytes);

    //cout << "Sending chunk ack of " << ack_bytes << " bytes" << endl;

    ssize_t bytes = send(ClientSocket, msg.GetData(), msg.GetLength(), MSG_NOSIGNAL);
    return bytes == msg.GetLength();
}

void RTMPReceiver::OnMessage(const std::string& name, double number) {
    if (name == "connect") {
        const uint32_t window_ack_size = 2500000;
        const uint32_t max_unacked_bytes = 2500000;
        const int limit_type = LIMIT_DYNAMIC;
        const uint32_t chunk_size = 60000;

        SendConnectResult(window_ack_size, max_unacked_bytes, limit_type, chunk_size);
    } else {
        SendNullResult(number);
    }
}

bool RTMPReceiver::SendConnectResult(
    uint32_t window_ack_size,
    uint32_t max_unacked_bytes,
    int limit_type,
    uint32_t chunk_size)
{
    uint32_t timestamp = 0;

    ByteStreamWriter params;

    params.WriteUInt8(2); // cs_id = 2, fmt = 0
    params.WriteUInt24(timestamp);
    params.WriteUInt24(4/*length*/);
    params.WriteUInt8(WINDOW_ACK_SIZE);
    params.WriteUInt32(0/*stream_id*/);
        params.WriteUInt32(window_ack_size);

    params.WriteUInt8(2); // cs_id = 2, fmt = 0
    params.WriteUInt24(timestamp);
    params.WriteUInt24(5/*length*/);
    params.WriteUInt8(SET_PEER_BANDWIDTH);
    params.WriteUInt32(0/*stream_id*/);
        params.WriteUInt32(max_unacked_bytes);
        params.WriteUInt8(limit_type);

    params.WriteUInt8(2); // cs_id = 2, fmt = 0
    params.WriteUInt24(timestamp);
    params.WriteUInt24(4/*length*/);
    params.WriteUInt8(CHUNK_SIZE);
    params.WriteUInt32(0/*stream_id*/);
        params.WriteUInt32(chunk_size);

    ByteStreamWriter amf;
    amf.WriteUInt8(StringMarker);
    amf.WriteAmf0String("_result");
    amf.WriteUInt8(NumberMarker);
    amf.WriteDouble(1.0);
    amf.WriteUInt8(NullMarker);
    amf.WriteUInt8(ObjectMarker);
        amf.WriteAmf0String("level");
        amf.WriteUInt8(StringMarker);
        amf.WriteAmf0String("status");

        amf.WriteAmf0String("code");
        amf.WriteUInt8(StringMarker);
        amf.WriteAmf0String("NetConnection.Connect.Success");

        amf.WriteAmf0String("description");
        amf.WriteUInt8(StringMarker);
        amf.WriteAmf0String("Connection succeeded.");

        amf.WriteUInt16(0);
    amf.WriteUInt8(ObjectEndMarker);

    params.WriteUInt8(3); // cs_id = 3, fmt = 0
    params.WriteUInt24(timestamp);
    params.WriteUInt24(static_cast<int>( amf.GetLength() )/*length*/);
    params.WriteUInt8(COMMAND_AMF0);
    params.WriteUInt32(0/*stream_id*/);
        params.WriteData(amf.GetData(), amf.GetLength());

    params.WriteUInt8(2); // cs_id = 2, fmt = 0
    params.WriteUInt24(timestamp);
    params.WriteUInt24(6/*length*/);
    params.WriteUInt8(USER_CONTROL);
    params.WriteUInt32(0/*stream_id*/);
        params.WriteUInt16(EVENT_STREAM_BEGIN);
        params.WriteUInt32(0);

    ssize_t bytes = send(ClientSocket, params.GetData(), params.GetLength(), MSG_NOSIGNAL);
    return bytes == params.GetLength();
}

bool RTMPReceiver::SendNullResult(double command_number) {
    uint32_t timestamp = 0;

    ByteStreamWriter msg;

    ByteStreamWriter amf;
    amf.WriteUInt8(StringMarker);
    amf.WriteAmf0String("_result");
    amf.WriteUInt8(NumberMarker);
    amf.WriteDouble(command_number);
    amf.WriteUInt8(NullMarker);
    amf.WriteUInt8(UndefinedMarker);

    msg.WriteUInt8(3); // cs_id = 3, fmt = 0
    msg.WriteUInt24(timestamp);
    msg.WriteUInt24(static_cast<int>( amf.GetLength() )/*length*/);
    msg.WriteUInt8(COMMAND_AMF0);
    msg.WriteUInt32(0/*stream_id*/);
        msg.WriteData(amf.GetData(), amf.GetLength());

    ssize_t bytes = send(ClientSocket, msg.GetData(), msg.GetLength(), MSG_NOSIGNAL);
    return bytes == msg.GetLength();
}

void RTMPReceiver::OnAvccVideo(
    bool keyframe,
    uint32_t stream,
    uint32_t timestamp,
    const uint8_t* data,
    int bytes)
{
    // Check if this is a new stream
    auto iter = video_streams.find(stream);
    std::shared_ptr<VideoStreamState> stream_state;

    if (iter == video_streams.end()) {
        stream_state = std::make_shared<VideoStreamState>();
        video_streams[stream] = stream_state;
    } else {
        stream_state = iter->second;
    }

    stream_state->avccParser.parseAvcc(data, bytes);

    const uint8_t* annexb_data = stream_state->avccParser.Video.data();
    int annexb_bytes = static_cast<int>( stream_state->avccParser.Video.size() );

    if (annexb_bytes == 0) {
        return;
    }

    Callback(stream_state->NewStream, keyframe, stream, timestamp, annexb_data, annexb_bytes);

    stream_state->NewStream = false;
}
