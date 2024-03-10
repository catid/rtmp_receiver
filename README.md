# rtmp_receiver

Simple unidirectional RTMP video stream receiver.  Implements a subset of the RTMP protocol needed to receive video from a DJI RC Pro controller: You'd specify the server IP address in the DJI controller Livestream settings as `rtmp://1.2.3.4/live/stream`

Clean codebase that handles edge-cases well, such as TCP splitting packets up into different receive buffers, programmatic termination before/after/during a connection, graceful handling of disconnections, and more.  Verified with Wireshark that the protocol looks correct and the server is not sending any extraneous data.

## Testing

```
git clone https://github.com/catid/rtmp_receiver.git
cd rtmp_receiver

mkdir build
cd build
cmake ..
make -j

./rtmp_receiver_test
```

In another window run:

```
gst-launch-1.0 videotestsrc ! video/x-raw,width=640,height=480 ! x264enc ! flvmux ! rtmpsink location='rtmp://localhost/live/stream'
```

This will exercise the RTMP server to make sure it is working.

## Example Output

The following is an example of restarting the Gstreamer pipeline above.  You can see the RTMP server accepts the new connection and resumes receiving the new stream, gracefully handling the disconnection of the previous stream.  Pressing Enter will stop the server.

```
(base) ➜  build git:(main) ✗ ./rtmp_receiver_test
Press Enter to stop the server...
*** New stream 4
Received video keyframe=1 data on stream=4 ts=0 bytes=51
Received video keyframe=1 data on stream=4 ts=0 bytes=14589
Received video keyframe=0 data on stream=4 ts=0 bytes=10676
Received video keyframe=0 data on stream=4 ts=0 bytes=8785
Received video keyframe=0 data on stream=4 ts=33 bytes=8520
Received video keyframe=0 data on stream=4 ts=66 bytes=8354
Received video keyframe=0 data on stream=4 ts=100 bytes=10287
Received video keyframe=0 data on stream=4 ts=133 bytes=8657
Received video keyframe=0 data on stream=4 ts=166 bytes=8408
Received video keyframe=0 data on stream=4 ts=200 bytes=8610
*** New stream 4
Received video keyframe=1 data on stream=4 ts=0 bytes=51
Received video keyframe=1 data on stream=4 ts=0 bytes=14589
Received video keyframe=0 data on stream=4 ts=0 bytes=10676
Received video keyframe=0 data on stream=4 ts=0 bytes=8785
Received video keyframe=0 data on stream=4 ts=33 bytes=8520
Received video keyframe=0 data on stream=4 ts=66 bytes=8470
Received video keyframe=0 data on stream=4 ts=100 bytes=10293
Received video keyframe=0 data on stream=4 ts=133 bytes=8643
Received video keyframe=0 data on stream=4 ts=166 bytes=8499
Received video keyframe=0 data on stream=4 ts=200 bytes=8648
Received video keyframe=0 data on stream=4 ts=233 bytes=10566
```

## Using

To incorporate the code into your own project, simply add all the source files to your project except for `main.cpp`.  You can refer to the `main.cpp` file for an example of how to use the library.

## License

BSD 3-Clause License
