#include <iostream>
#include <thread>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include <functional>

#include "rtmp_parser.h"
#include "bytestream_writer.h"

#include <iostream>
using namespace std;


//------------------------------------------------------------------------------
// Tools

void FillRandomBuffer(uint8_t* buffer, int bytes, uint32_t seed) {
    const uint32_t a = 1664525;
    const uint32_t c = 1013904223;
    uint32_t randomValue = seed;

    for (size_t i = 0; i < bytes; ++i) {
        randomValue = a * randomValue + c; // Generate the next pseudo-random value
        buffer[i] = static_cast<uint8_t>(randomValue >> 24); // Use the most significant byte
    }
}

static void WriteUInt32(uint8_t* buffer, uint32_t value) {
    buffer[0] = static_cast<uint8_t>(value >> 24);
    buffer[1] = static_cast<uint8_t>(value >> 16);
    buffer[2] = static_cast<uint8_t>(value >> 8);
    buffer[3] = static_cast<uint8_t>(value);
}

static uint64_t GetMsec() {
    using namespace std::chrono;
    milliseconds ms = duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()
    );
    return ms.count();
}

class AutoClose {
public:
    AutoClose(std::function<void()> f) : f(f) {}
    ~AutoClose() {
        f();
    }
private:
    std::function<void()> f;
};

enum LimitType {
    LIMIT_HARD = 0,
    LIMIT_SOFT = 1,
    LIMIT_DYNAMIC = 2
};


//------------------------------------------------------------------------------
// RTMPServer

class RTMPServer {
private:
    int serverSocket = -1;
    int clientSocket = -1;
    std::vector<uint8_t> buffer;
    std::thread serverThread;
    bool running = false;

public:
    ~RTMPServer() {
        stop();
    }

    void start(int port) {
        running = true;
        serverThread = std::thread(&RTMPServer::run, this, port);
    }

    void stop() {
        running = false;
        if (serverThread.joinable()) {
            serverThread.join();
        }
        if (clientSocket != -1) {
            close(clientSocket);
            clientSocket = -1;
        }
        if (serverSocket != -1) {
            close(serverSocket);
            serverSocket = -1;
        }
    }

    const std::vector<uint8_t>& getVideoData() const {
        return buffer;
    }

private:
    RollingBuffer Buffer; // Keep left-overs from previous chunks
    uint8_t packetData[2048];

    void run(int port) {
        serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocket == -1) {
            std::cout << "Failed to create server socket" << std::endl;
            return;
        }

        AutoClose serverSocketCloser([&]() {
            close(serverSocket);
            serverSocket = -1;
        });

        int optval = 1;
        if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
            std::cout << "Failed to set SO_REUSEADDR on server socket" << std::endl;
            return;
        }

        sockaddr_in serverAddress{};
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_addr.s_addr = INADDR_ANY;
        serverAddress.sin_port = htons(port);

        if (bind(serverSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) == -1) {
            std::cout << "Failed to bind server socket" << std::endl;
            return;
        }

        if (listen(serverSocket, 1) == -1) {
            std::cout << "Failed to listen on server socket" << std::endl;
            return;
        }

        std::cout << "RTMP server listening on port " << port << std::endl;

        clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket == -1) {
            std::cout << "Failed to accept client connection" << std::endl;
            return;
        }

        AutoClose clientSocketCloser([&]() {
            close(clientSocket);
            clientSocket = -1;
        });

        std::cout << "Client connected" << std::endl;

        RTMPHandshake handshake;
        handshake.Buffer = &Buffer;
        bool sent_s0s1 = false;
        bool sent_s2 = false;

        while (running) {
            ssize_t bytesRead = recv(clientSocket, packetData, sizeof(packetData), 0);
            //std::cout << "Handshake: Received " << bytesRead << " bytes of data from client" << std::endl;
            if (bytesRead > 0) {
                handshake.ParseMessage(packetData, bytesRead);
            } else if (bytesRead == 0) {
                std::cout << "Client disconnected" << std::endl;
                break;
            } else {
                std::cout << "Failed to receive data from client" << std::endl;
                break;
            }

            // If we have C0 but we haven't sent S0 and S1 yet:
            if (!sent_s0s1 && handshake.State.Round >= 1) {
                if (handshake.State.ClientVersion != kRtmpS0ServerVersion) {
                    std::cout << "Invalid version from client" << std::endl;
                    return;
                }
                if (!sendS0S1()) {
                    std::cout << "Failed to send S1 to client" << std::endl;
                    return;
                }
                sent_s0s1 = true;
            }

            // If we have C1 but we haven't sent S2 yet:
            if (!sent_s2 && handshake.State.Round >= 2) {
                if (!sendS2(handshake.State.ClientTime1, handshake.State.ClientRandom)) {
                    std::cout << "Failed to send random echo to client" << std::endl;
                    return;
                }
                sent_s2 = true;
            }

            // If we have C2:
            if (handshake.State.Round >= 3) {
                if (!checkC2(handshake.State.ClientEcho)) {
                    std::cout << "Invalid random echo from client" << std::endl;
                    return;
                }
                break; // Handshake complete
            }
        } // Continue handshake

        cout << "Handshake complete" << endl;

        RTMPSession parser;
        parser.Buffer = &Buffer;

        const uint8_t* parse_data = nullptr;
        ssize_t bytesRead = 0;

        while (running) {
            // We parse first because there may be some data left over from the handshake
            while (parser.ParseChunk(parse_data, bytesRead))
            {
                // Pass null next time to continue parsing the same buffer
                parse_data = nullptr;
                bytesRead = 0;

                if (parser.MustSetParams) {
                    parser.MustSetParams = false;
                    sendWindowAckSize(2500000);
                    sendChannelParams(2500000, LIMIT_DYNAMIC, 60000);
                    sendConnectResult();
                }
                else if (parser.NeedsNullResponse) {
                    parser.NeedsNullResponse = false;
                    sendNullResponse(parser.NullResponseNumber);
                }
            }

            parse_data = packetData;
            bytesRead = recv(clientSocket, packetData, sizeof(packetData), 0);
            //std::cout << "Session: Received " << bytesRead << " bytes of data from client" << std::endl;
            if (bytesRead == 0) {
                std::cout << "Client disconnected" << std::endl;
                break;
            } else if (bytesRead < 0) {
                std::cout << "Failed to receive data from client" << std::endl;
                break;
            }
        }
    }

    uint8_t Handshake[1 + 1536];

    bool sendS0S1() {
        Handshake[0] = kRtmpS0ServerVersion;
        WriteUInt32(Handshake + 1, GetMsec());
        FillRandomBuffer(Handshake + 1 + 4, 1536 - 4, GetMsec());
        ssize_t bytes = send(clientSocket, Handshake, sizeof(Handshake), 0);
        return bytes == sizeof(Handshake);
    }

    uint8_t RandomEcho[1536];

    bool sendS2(uint32_t peer_time, const void* client_random) {
        WriteUInt32(RandomEcho, peer_time);
        WriteUInt32(RandomEcho + 4, 0);
        memcpy(RandomEcho + 8, client_random, 1536 - 8);
        ssize_t bytes = send(clientSocket, RandomEcho, sizeof(RandomEcho), 0);
        return bytes == sizeof(RandomEcho);
    }

    bool checkC2(const void* echo) {
        if (0 != memcmp(Handshake + 1 + 4 + 4, echo, 1536 - 8)) {
            return false;
        }
        return true;
    }

    void parseRTMPPacket(const uint8_t* data, size_t size) {
        // Implement RTMP protocol parsing logic here
        // Extract video data from the RTMP packet and append it to the buffer
        // You can refer to the RTMP specification for the packet structure and parsing rules
        // This is a simplified example, assuming the video data is contained in the payload of the RTMP packet
        buffer.insert(buffer.end(), data, data + size);
    }

    bool sendWindowAckSize(uint32_t window_ack_size) {
        uint32_t timestamp = 0;

        ByteStreamWriter params;
        params.WriteUInt8(2); // cs_id = 2, fmt = 0
        params.WriteUInt24(timestamp);
        params.WriteUInt24(4/*length*/);
        params.WriteUInt8(WINDOW_ACK_SIZE);
        params.WriteUInt32(0/*stream_id*/);
            params.WriteUInt32(window_ack_size);

        ssize_t bytes = send(clientSocket, params.GetData(), params.GetLength(), 0);
        return bytes == params.GetLength();
    }

    bool sendChannelParams(uint32_t max_unacked_bytes, int limit_type, uint32_t chunk_size) {
        uint32_t timestamp = 0;

        ByteStreamWriter params;

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
#if 0
        params.WriteUInt8(2); // cs_id = 2, fmt = 0
        params.WriteUInt24(timestamp);
        params.WriteUInt24(6/*length*/);
        params.WriteUInt8(USER_CONTROL);
        params.WriteUInt32(0/*stream_id*/);
            params.WriteUInt16(EVENT_STREAM_BEGIN);
            params.WriteUInt32(0);
#endif
        ssize_t bytes = send(clientSocket, params.GetData(), params.GetLength(), 0);
        cout << "SEND sendChannelParams" << endl;
        return bytes == params.GetLength();
    }

    bool sendConnectResult() {
        uint32_t timestamp = 0;

        ByteStreamWriter msg;

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

        msg.WriteUInt8(3); // cs_id = 3, fmt = 0
        msg.WriteUInt24(timestamp);
        msg.WriteUInt24(amf.GetLength()/*length*/);
        msg.WriteUInt8(COMMAND_AMF0);
        msg.WriteUInt32(0/*stream_id*/);
            msg.WriteData(amf.GetData(), amf.GetLength());

        cout << "Sent connect response" << endl;

        ssize_t bytes = send(clientSocket, msg.GetData(), msg.GetLength(), 0);
        return bytes == msg.GetLength();
    }

    bool sendNullResponse(double command_number) {
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

        cout << "Sent null response" << endl;

        ssize_t bytes = send(clientSocket, msg.GetData(), msg.GetLength(), 0);
        return bytes == msg.GetLength();
    }
};

int main() {
    RTMPServer server;

    // Start the RTMP server on a specific port
    int port = 1935; // Default RTMP port
    server.start(port);

    // Wait for user input to stop the server
    std::cout << "Press Enter to stop the server..." << std::endl;
    std::cin.get();

    // Stop the RTMP server
    server.stop();

    // Process the received video data
    const std::vector<uint8_t>& videoData = server.getVideoData();
    if (!videoData.empty()) {
        // Process the video data as needed
        // For example, you can save it to a file or perform further analysis
        std::cout << "Received " << videoData.size() << " bytes of video data" << std::endl;
        // Add your video processing logic here
    }

    return 0;
}
