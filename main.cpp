#include <iostream>
using namespace std;

#include "rtmp_receiver.h"

int main() {
    RTMPReceiver server;

    auto fn = [](
        bool new_stream,
        bool keyframe,
        uint32_t stream,
        uint32_t timestamp,
        const uint8_t* data,
        int bytes)
    {
        if (new_stream) {
            cout << "*** New stream " << stream << endl;
        }
        cout << "Received video keyframe=" << keyframe << " data on stream=" << stream << " ts=" << timestamp << " bytes=" << bytes << endl;
    };

    server.Start(fn);

    cout << "Press Enter to stop the server..." << endl;
    cin.get();

    return 0;
}
