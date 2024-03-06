#include <iostream>
#include <thread>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include <functional>

#include "rtmp_parser.h"

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
        bool sent_s0s1 = false;
        bool sent_s2 = false;

        while (running) {
            ssize_t bytesRead = recv(clientSocket, packetData, sizeof(packetData), 0);
            std::cout << "Handshake: Received " << bytesRead << " bytes of data from client" << std::endl;
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
            if (handshake.State.Round >= 1) {
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

        while (running) {
            ssize_t bytesRead = recv(clientSocket, packetData, sizeof(packetData), 0);
            std::cout << "Session: Received " << bytesRead << " bytes of data from client" << std::endl;
            if (bytesRead > 0) {
                parser.ParseChunk(packetData, bytesRead);
            } else if (bytesRead == 0) {
                std::cout << "Client disconnected" << std::endl;
                break;
            } else {
                std::cout << "Failed to receive data from client" << std::endl;
                break;
            }
        }
    }

    uint8_t Handshake[1 + 1536];

    bool sendS0S1() {
        Handshake[0] = kRtmpS0ServerVersion;
        WriteUInt32(Handshake + 1, GetMsec());
        WriteUInt32(Handshake + 5, 0);
        FillRandomBuffer(Handshake + 1 + 4 + 4, 1536 - 8, GetMsec());
        ssize_t bytes = send(clientSocket, Handshake, sizeof(Handshake), 0);
        return bytes == sizeof(Handshake);
    }

    uint8_t RandomEcho[1536];

    bool sendS2(uint32_t peer_time, const void* client_random) {
        WriteUInt32(RandomEcho, peer_time);
        WriteUInt32(RandomEcho + 4, GetMsec());
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
