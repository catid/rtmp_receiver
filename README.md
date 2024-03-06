# rtmp_receiver (Work in Progress)

Simple unidirectional RTMP server receiver.

This is useful for receiving an RTMP stream from a DJI RC Pro controller (you'd specify the server IP address in the DJI controller settings).

## Usage

```
git clone https://github.com/catid/rtmp_receiver.git
cd rtmp_receiver

mkdir build
cd build
cmake ..
make -j

./rtmp_receiver
```

In another window run:

```
gst-launch-1.0 videotestsrc ! video/x-raw,width=640,height=480 ! x264enc ! flvmux ! rtmpsink location='rtmp://localhost/live/stream'
```

This will exercise the RTMP server to make sure it is working.

## License

BSD 3-Clause License
