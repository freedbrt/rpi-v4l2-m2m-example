# rpi-v4l2-m2m-example
Raspberry pi v4l2 m2m encoder example

The application shows incorrect work of the bcm2835-codec encoder on some resolutions. 

# Build steps
```
- sudo apt install libv4l-dev
- make
```

# Testing
  For play the raw h264 frames you can use ffmpeg
  ```
  ffplay -f h264 input.h264
  ```
