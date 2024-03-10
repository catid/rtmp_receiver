#ifndef RTMP_RECEIVER_H
#define RTMP_RECEIVER_H

#include "rtmp_parser.h"

#include <thread>
#include <vector>
#include <functional>
#include <atomic>
#include <string>
#include <cstdint>


//------------------------------------------------------------------------------
// RTMPServer

using RTMPCallback = std::function<void(
    bool new_stream,
    bool keyframe,
    uint32_t stream,
    uint32_t timestamp,
    const uint8_t* data,
    int bytes)>;

class RTMPServer : protected RTMPHandler {
public:
    ~RTMPServer() {
        Stop();
    }

    bool Start(RTMPCallback callback, int port = 1935, bool enable_logging = false);
    void Stop();

private:
    int Port = 1935;
    RTMPCallback Callback;
    bool EnableLogging = false;

    // Shutdown control socket
    int ControlSock[2];

    std::atomic<bool> Terminated = ATOMIC_VAR_INIT(false);
    std::shared_ptr<std::thread> Thread;

    int ClientSocket = -1;

    std::vector<uint8_t> RecvBuffer;
    uint8_t Handshake[1 + 1536];
    uint8_t RandomEcho[1536];

    void Loop();
    void RunServer();
    bool WaitForConnection(int server_socket);
    void HandleNextClient(int server_socket);

    bool SendS0S1();
    bool SendS2(uint32_t peer_time, const void* client_random);
    bool CheckC2(const void* echo);
    void ParseRTMPPacket(const uint8_t* data, size_t size);

    void OnNeedAck(uint32_t bytes) override;
    bool SendChunkAck(uint32_t ack_bytes);

    void OnMessage(const std::string& name, double number) override;

    bool SendConnectResult(
        uint32_t window_ack_size,
        uint32_t max_unacked_bytes,
        int limit_type,
        uint32_t chunk_size);

    bool SendNullResult(double command_number);

    void OnAvccVideo(bool keyframe, uint32_t stream, uint32_t timestamp, const uint8_t* data, int bytes) override;

    std::unordered_map<uint32_t, bool> seen_streams;
};

#endif // RTMP_RECEIVER_H
