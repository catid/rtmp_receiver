# rtmp_receiver

Simple unidirectional RTMP server receiver.  Implements a subset of the RTMP protocol needed to receive video from a DJI RC Pro controller (you'd specify the server IP address in the DJI controller settings).

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

```

```

## Using

To incorporate the code into your own project, simply add all the source files to your project except for `main.cpp`.  You can refer to the `main.cpp` file for an example of how to use the library.

## License

BSD 3-Clause License
