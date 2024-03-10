#include "rtmp_receiver.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "rtmp_parser.h"
#include "bytestream_writer.h"
#include "rtmp_tools.h"

#include <iostream>
#include <iomanip>
using namespace std;


//------------------------------------------------------------------------------
// RTMPServer

void RTMPServer::Start(RTMPCallback callback, int port, bool enable_logging) {
    Callback = callback;
    Port = port;
    EnableLogging = enable_logging;

    ServerSocket = -1;
    ClientSocket = -1;

    Terminated = false;
    Thread = std::make_shared<std::thread>(&RTMPServer::Loop, this);
}

void RTMPServer::Stop() {
    Terminated = true;

    // Close the sockets if they are open, forcing the thread to exit
    if (ClientSocket != -1) {
        close(ClientSocket);
        ClientSocket = -1;
    }
    if (ServerSocket != -1) {
        close(ServerSocket);
        ServerSocket = -1;
    }

    // Wait for the thread to exit
    if (Thread && Thread->joinable()) {
        Thread->join();
    }
    Thread = nullptr;
}

void RTMPServer::Loop() {
    // Keep running until the thread is stopped
    while (!Terminated) {
        RunServer();

        // Avoid busy-looping
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void RTMPServer::RunServer() {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == -1) {
        cout << "Failed to create server socket" << endl;
        return;
    }
    ServerSocket = s;

    AutoClose serverSocketCloser([&]() {
        close(ServerSocket.load());
        ServerSocket = -1;
    });

    int optval = 1;
    int r = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    if (r == -1) {
        cout << "Failed to set SO_REUSEADDR on server socket" << endl;
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(Port);

    r = bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (r == -1) {
        cout << "Failed to bind server socket" << endl;
        return;
    }

    r = listen(s, 1);
    if (r == -1) {
        cout << "Failed to listen on server socket" << endl;
        return;
    }

    if (EnableLogging) {
        cout << "RTMP server listening on port " << Port << endl;
    }

    while (!Terminated) {
        HandleClients();
    }
}

void RTMPServer::HandleClients() {
    RollingBuffer Buffer; // Keep left-overs from previous chunks

    ClientSocket = accept(ServerSocket, nullptr, nullptr);
    if (ClientSocket == -1) {
        cout << "Failed to accept client connection" << endl;
        return;
    }

    AutoClose clientSocketCloser([&]() {
        close(ClientSocket);
        ClientSocket = -1;
    });

    if (EnableLogging) {
        cout << "Client connected" << endl;
    }

    RTMPHandshake handshake;
    handshake.Buffer = &Buffer;
    bool sent_s0s1 = false;
    bool sent_s2 = false;

    while (!Terminated) {
        ssize_t recv_bytes = recv(ClientSocket, RecvBuffer, sizeof(RecvBuffer), 0);
        //cout << "Handshake: Received " << recv_bytes << " bytes of data from client" << endl;
        if (recv_bytes > 0) {
            handshake.ParseMessage(RecvBuffer, recv_bytes);
        } else if (recv_bytes <= 0) {
            if (EnableLogging) {
                cout << "Client disconnected" << endl;
            }
            return;
        }

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

    while (!Terminated) {
        // We parse first because there may be some data left over from the handshake
        while (parser.ParseChunk(parse_data, bytesRead))
        {
            // Pass null next time to continue parsing the same buffer
            parse_data = nullptr;
            bytesRead = 0;
        }

        parse_data = RecvBuffer;
        bytesRead = recv(ClientSocket, RecvBuffer, sizeof(RecvBuffer), 0);
        //cout << "Session: Received " << bytesRead << " bytes of data from client" << endl;
        if (bytesRead <= 0) {
            if (EnableLogging) {
                cout << "Client disconnected" << endl;
            }
            return;
        }
    }
}

bool RTMPServer::SendS0S1() {
    Handshake[0] = kRtmpS0ServerVersion;
    WriteUInt32(Handshake + 1, GetMsec());
    FillRandomBuffer(Handshake + 1 + 4, 1536 - 4, GetMsec());
    ssize_t bytes = send(ClientSocket.load(), Handshake, sizeof(Handshake), MSG_NOSIGNAL);
    return bytes == sizeof(Handshake);
}

bool RTMPServer::SendS2(uint32_t peer_time, const void* client_random) {
    WriteUInt32(RandomEcho, peer_time);
    WriteUInt32(RandomEcho + 4, 0);
    memcpy(RandomEcho + 8, client_random, 1536 - 8);
    ssize_t bytes = send(ClientSocket.load(), RandomEcho, sizeof(RandomEcho), MSG_NOSIGNAL);
    return bytes == sizeof(RandomEcho);
}

bool RTMPServer::CheckC2(const void* echo) {
    if (0 != memcmp(Handshake + 1 + 4 + 4, echo, 1536 - 8)) {
        return false;
    }
    return true;
}

void RTMPServer::OnNeedAck(uint32_t bytes) {
    SendChunkAck(bytes);
}

bool RTMPServer::SendChunkAck(uint32_t ack_bytes) {
    uint32_t timestamp = 0;

    ByteStreamWriter msg;

    msg.WriteUInt8(3); // cs_id = 3, fmt = 0
    msg.WriteUInt24(timestamp);
    msg.WriteUInt24(4/*length*/);
    msg.WriteUInt8(COMMAND_AMF0);
    msg.WriteUInt32(0/*stream_id*/);
        msg.WriteUInt32(ack_bytes);

    ssize_t bytes = send(ClientSocket.load(), msg.GetData(), msg.GetLength(), MSG_NOSIGNAL);
    return bytes == msg.GetLength();
}

void RTMPServer::OnMessage(const std::string& name, double number) {
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

bool RTMPServer::SendConnectResult(
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
    params.WriteUInt24(amf.GetLength()/*length*/);
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

    ssize_t bytes = send(ClientSocket.load(), params.GetData(), params.GetLength(), MSG_NOSIGNAL);
    return bytes == params.GetLength();
}

bool RTMPServer::SendNullResult(double command_number) {
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
    msg.WriteUInt24(amf.GetLength()/*length*/);
    msg.WriteUInt8(COMMAND_AMF0);
    msg.WriteUInt32(0/*stream_id*/);
        msg.WriteData(amf.GetData(), amf.GetLength());

    ssize_t bytes = send(ClientSocket.load(), msg.GetData(), msg.GetLength(), MSG_NOSIGNAL);
    return bytes == msg.GetLength();
}

void RTMPServer::OnAvccVideo(
    bool keyframe,
    uint32_t stream,
    uint32_t timestamp,
    const uint8_t* data,
    int bytes)
{
    Callback(keyframe, stream, timestamp, data, bytes);
}
