#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <linux/videodev2.h>
#include <linux/v4l2-controls.h>
#include <cstring>
#include <errno.h>
#include <cstdlib>

struct VideoBuffer {
    void* start;
    int length;

    struct v4l2_buffer inner;
    struct v4l2_plane plane;
};

bool setEncoderOption(int fd, unsigned int id, int value)
{
    struct v4l2_control control = {
        .id = id,
        .value = value
    };

    if (ioctl(fd, VIDIOC_S_CTRL, &control) < 0) {
        printf("Can't set encoder option id: %d value: %d\n", id, value);
        return false;
    }

    return true;
}

bool mapBuffer(int fd, v4l2_buf_type type, VideoBuffer *buffer)
{
    struct v4l2_buffer* inner = &buffer->inner;

    memset(inner, 0, sizeof(*inner));
    inner->type = type;
    inner->memory = V4L2_MEMORY_MMAP;
    inner->index = 0;
    inner->length = 1;
    inner->m.planes = &buffer->plane;

    if (ioctl(fd, VIDIOC_QUERYBUF, inner) < 0)
    {
        printf("Can't queue video buffer with type %d\n", type);
        return false;
    }

    buffer->length = inner->m.planes[0].length;
    buffer->start = mmap(nullptr, buffer->length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, inner->m.planes[0].m.mem_offset);


    if (ioctl(fd, VIDIOC_QBUF, inner))
    {
        printf("Can't queue video buffer with type %d\n", type);
        return false;
    }

    return true;
}

bool initBuffer(int fd, enum v4l2_buf_type type, struct VideoBuffer* buffer)
{
    struct v4l2_requestbuffers request = {0};
    request.count = 1;
    request.type = type;
    request.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_REQBUFS, &request) < 0) {
        printf("Failed to init buffer\n");
        return false;
    }

    if (request.count < 1) {
        printf("Failed to allocate buffer memory, type %d, count: %d\n", type, request.count);
        return false;
    }

    if (!mapBuffer(fd, type, buffer))
        return false;

    return true;
}

bool encodeFrame(int fd, VideoBuffer& inputBuffer, VideoBuffer& outputBuffer, void* srcData, FILE* outputFile) {
    struct v4l2_buffer input_buf = {0};
    struct v4l2_plane input_plane = {0};
    input_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    input_buf.length = 1;
    input_buf.m.planes = &input_plane;
    input_buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_DQBUF, &input_buf) < 0) {
        printf("Failed to dequeue input buffer\n");
        return false;
    }

    static time_t now = 1705597574;
    now++; // Dummy increase our "timestamp"

    struct timeval ts = {
        .tv_sec = now / 1000000,
        .tv_usec = now % 1000000,
    };

    memcpy(inputBuffer.start, srcData, inputBuffer.length);
    //memset(inputBuffer.start, 218, inputBuffer.length); // Fill the whole buffer with gray (218, 218, 218) color

    if (ioctl(fd, VIDIOC_QBUF, &input_buf) < 0) {
        printf("failed to queue input buffer\n");
        return false;
    }

    struct v4l2_buffer output_buf = {0};
    struct v4l2_plane output_plane = {0};
    output_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    output_buf.length = 1;
    output_buf.m.planes = &output_plane;
    output_buf.memory = V4L2_MEMORY_MMAP;

    // Lock the output encoder buffer
    if (ioctl(fd, VIDIOC_DQBUF, &output_buf) < 0) {
        printf("failed to dequeue output buffer\n");
        return false;
    }

    fwrite(outputBuffer.start, 1, output_buf.m.planes[0].bytesused, outputFile);

    if (ioctl(fd, VIDIOC_QBUF, &output_buf) < 0) {
        printf("failed to queue output buffer\n");
        return false;
    }

    return true;
}

bool destroyBuffer(int fd, VideoBuffer *buffer)
{
    if (munmap(buffer->start, buffer->length) < 0) {
        printf("Failed to unmap buffer %d\n", buffer->inner.type);
        return false;
    }

    *buffer = VideoBuffer();

    return true;
}

int runTest(int width, int height) {
    int fd = open("/dev/video11", O_RDWR);
    if (fd < 0)
    {
        printf("Failed to open /dev/video11\n");
        return 1;
    }

    setEncoderOption(fd, V4L2_CID_MPEG_VIDEO_BITRATE, 1024 * 1024 * 8);
    setEncoderOption(fd, V4L2_CID_MPEG_VIDEO_H264_I_PERIOD, 30);
    setEncoderOption(fd, V4L2_CID_MPEG_VIDEO_H264_PROFILE, V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE);
    setEncoderOption(fd, V4L2_CID_MPEG_VIDEO_H264_LEVEL, V4L2_MPEG_VIDEO_H264_LEVEL_4_0);
    setEncoderOption(fd, V4L2_CID_MPEG_VIDEO_REPEAT_SEQ_HEADER, 1);
    setEncoderOption(fd, V4L2_CID_MPEG_VIDEO_H264_MIN_QP, 2);
    setEncoderOption(fd, V4L2_CID_MPEG_VIDEO_H264_MAX_QP, 8);

    // Setup "input" format

    struct v4l2_format inputFormat = {0};
    inputFormat.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    inputFormat.fmt.pix_mp.width = width;
    inputFormat.fmt.pix_mp.height = height;
    inputFormat.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_YUV420;
    inputFormat.fmt.pix_mp.field = V4L2_FIELD_ANY;
    inputFormat.fmt.pix_mp.colorspace = V4L2_COLORSPACE_DEFAULT;
    inputFormat.fmt.pix_mp.quantization = V4L2_QUANTIZATION_FULL_RANGE;
    inputFormat.fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
    inputFormat.fmt.pix_mp.num_planes = 1;

    if (ioctl(fd, VIDIOC_S_FMT, &inputFormat) < 0) {
        printf("Failed to set input format\n");
        return 1;
    }

    // Setup output format
    struct v4l2_format outputFormat = {0};
    outputFormat.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    outputFormat.fmt.pix_mp.width = width;
    outputFormat.fmt.pix_mp.height = height;
    outputFormat.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
    outputFormat.fmt.pix_mp.field = V4L2_FIELD_ANY;
    outputFormat.fmt.pix_mp.colorspace = V4L2_COLORSPACE_DEFAULT;
    outputFormat.fmt.pix_mp.quantization = V4L2_QUANTIZATION_FULL_RANGE;
    outputFormat.fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT;
    outputFormat.fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
    outputFormat.fmt.pix_mp.num_planes = 1;
    outputFormat.fmt.pix_mp.plane_fmt[0].sizeimage = (1024 + 512) << 10;

    if (ioctl(fd, VIDIOC_S_FMT, &outputFormat) < 0) {
        printf("Failed to set output format\n");
        return 1;
    }

    // Allocate buffers

    VideoBuffer inputBuffer;
    VideoBuffer outputBuffer;

    initBuffer(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, &inputBuffer);
    initBuffer(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, &outputBuffer);

    printf("Input buffer length: %i bytes, stride: %i, width: %i height %i\n", 
        inputBuffer.length, inputFormat.fmt.pix_mp.plane_fmt[0].bytesperline, inputFormat.fmt.pix_mp.width, inputFormat.fmt.pix_mp.height);

    // Write the output frame data to the output file
    
    char str[80];
    memset(str, 0, 80);

    sprintf(str, "output_%i_%i.h264", width, height);

    FILE *outputFile = fopen(str, "wba\n");
    if (!outputFile) {
        perror("Error opening output file\n");
        return 1;
    }

    if (ioctl(fd, VIDIOC_STREAMON, &inputBuffer.inner.type) < 0) {
        printf("Can't set VIDIOC_STREAMON for input buffer\n");
        return 1;
    }

    if (ioctl(fd, VIDIOC_STREAMON, &outputBuffer.inner.type) < 0) {
        printf("Can't set VIDIOC_STREAMON for output buffer\n");
        return 1;
    }

    void* srcData = malloc(inputBuffer.length);
    memset(srcData, 128, inputBuffer.length); // Fill the whole frame with gray color

    int yStride = inputFormat.fmt.pix_mp.plane_fmt[0].bytesperline;
    int uvStride = inputFormat.fmt.pix_mp.plane_fmt[0].bytesperline / 2;
    int h = inputFormat.fmt.pix_mp.height;

    unsigned char* yData = (unsigned char*)srcData;
    unsigned char* uData = yData + yStride * h;
    unsigned char* vData = uData + uvStride * h / 2;

    unsigned int planeYsize = yStride * h;
    unsigned int planeUsize = uvStride * h / 2;
    unsigned int planeVsize = uvStride * h / 2;

    printf("plane Y (offset %u, size %u), plane U (offset %u, size %u), plane V (offset %u, size %u), total (input buffer length %u, calculated length %u)\n", 
            0, planeYsize, 
            planeYsize, planeUsize, 
            planeYsize + planeUsize, planeVsize,
            inputBuffer.length, planeYsize + planeUsize + planeVsize);

    // Rectangle coords
    int rectX = 50; 
    int rectY = 50;
    int rectW = 150;
    int rectH = 250;

    // RGB blue (39, 115, 255) -> YUV (111, 222, 77)

    // Y plane has double size comparable to UV
    for (int y = rectY * 2; y < rectY * 2 + rectH * 2; y++)
    {
        memset((yData + rectX * 2) + (y * yStride), 111, rectW * 2); // Fill the Y plane
    }

    for (int y = rectY; y < rectY + rectH; y++)
    {
        memset((uData + rectX) + (y * uvStride), 222, rectW); // Fill the U plane
        memset((vData + rectX) + (y * uvStride), 77, rectW); // Fill the V plane
    }

    encodeFrame(fd, inputBuffer, outputBuffer, srcData, outputFile);
    encodeFrame(fd, inputBuffer, outputBuffer, srcData, outputFile);
    encodeFrame(fd, inputBuffer, outputBuffer, srcData, outputFile);
    encodeFrame(fd, inputBuffer, outputBuffer, srcData, outputFile);
    encodeFrame(fd, inputBuffer, outputBuffer, srcData, outputFile);

    free(srcData);

    // Shoutdown the stream

    if (ioctl(fd, VIDIOC_STREAMOFF, &inputBuffer.inner.type) < 0) {
        printf("Can't set VIDIOC_STREAMON for input buffer\n");
        return 1;
    }

    if (ioctl(fd, VIDIOC_STREAMOFF, &outputBuffer.inner.type) < 0) {
        printf("Can't set VIDIOC_STREAMON for output buffer\n");
        return 1;
    }

    destroyBuffer(fd, &inputBuffer);
    destroyBuffer(fd, &outputBuffer);

    fclose(outputFile);
    close(fd);

    return 0;
}

int main(int argc, char* argv[])
{
    if (runTest(1920, 1080)) // GOOD
        return 1;

    if (runTest(1579, 889)) // BAD
        return 1;

    if (runTest(1579, 888)) // GOOD
        return 1;

    if (runTest(1579, 887)) // BAD
        return 1;

    if (runTest(1579, 886)) // GOOD
        return 1;

    printf("done\n");

    return 0;
}