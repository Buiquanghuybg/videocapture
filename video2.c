#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <linux/videodev2.h>

#define BUFFER_COUNT 2


int main(int argc, char **argv)
{
    int i;
    // Open the video capture device 
    int fd = open(argv[1], O_RDWR |O_NONBLOCK);
   
    if (fd < 0) {
        perror("Failed to open video capture device");
        exit(1);
    }

    // Setting the video capture device format and parameters
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 1280;
    fmt.fmt.pix.height = 720;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_H264; 	// for raw format
    fmt.fmt.pix.field =V4L2_FIELD_ANY; 		// for raw format

    // Set frame rate
    struct v4l2_streamparm params;
    memset(&params, 0, sizeof(params));
    params.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_G_PARM, &params) == -1) {
        perror("Failed to get stream parameters");
        close(fd);
        return 1;
    }
    params.parm.capture.timeperframe.numerator = 1;
    params.parm.capture.timeperframe.denominator = 30;
    if (ioctl(fd, VIDIOC_S_PARM, &params) == -1) {
        perror("Failed to set stream parameters");
        close(fd);
        return 1;
    }
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("Failed to set video format");
        exit(1);
    }

    // The output device,  i'm creating a new file to output it 
	char file_out[128] = {0};
	sprintf(file_out, "%s_out.h264", argv[2]);
    int out_fd = open(file_out, O_CREAT | O_WRONLY | O_TRUNC |O_NONBLOCK, 0644); // it can be /dev/video1 or out.raw
    if (out_fd < 0) {
        perror("Failed to open output device");
        exit(1);
    }

    // Allocate buffers for the video capture device
    struct v4l2_requestbuffers req = {0};
    req.count = BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  // allocate device buffers 
    req.memory = V4L2_MEMORY_MMAP;  	    // memory mapped buffers allocated in kernel
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("Failed to allocate buffers");
        exit(1);
    }

    //  buffers specifications and map them into their space with mmap() 
    struct buffer {
        void *start;
        size_t length;
    } *buffers;
    buffers = calloc(req.count, sizeof(*buffers));
    for (i = 0; i < req.count; i++) {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("Failed to query buffer");
            exit(1);
        }
        buffers[i].length = buf.length;
        buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (buffers[i].start == MAP_FAILED) {
            perror("Failed to map buffer");
            exit(1);
        }
    }

    // Enqueue the video buffers before processing
    for (i = 0; i < req.count; i++) {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("Failed to enqueue buffer");
            exit(1);
        }
    }

    // Start capturing video frames
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("Failed to start capturing");
        exit(1);
    }

    // Use poll to wait for video capture events 
    struct pollfd poll_fds[1];
    poll_fds[0].fd = fd;
    poll_fds[0].events = POLLIN;

    while (1) {
        int ret = poll(poll_fds, 1, 1000); // wait up to 1 second for events 
        				  
        if (ret < 0) {
            perror("Failed to poll");
            exit(1);
        } else if (ret == 0) {
            printf("Poll timeout\n");
            continue;
        }

        if (poll_fds[0].revents & POLLIN) {
            // Dequeue the video buffer for sending it to the output/display
            struct v4l2_buffer buf = {0};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
                perror("Failed to dequeue buffer");
                exit(1);
            }

            // Write the video data to the output device
            write(out_fd, buffers[buf.index].start, buf.bytesused);

            // Enqueue the video buffer again
            if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
                perror("Failed to enqueue buffer");
                exit(1);
            }
        }
		
		
    }

    // Stop capturing video frames
    if (ioctl(fd, VIDIOC_STREAMOFF, &type) < 0) {
        perror("Failed to stop capturing");
        exit(1);
    }

    // Unmap/free the video buffers
    for (i = 0; i < req.count; i++) {
        munmap(buffers[i].start, buffers[i].length);
    }

    // Close the video capture device and the output device
    close(fd);
    close(out_fd);

    return 0;
}




