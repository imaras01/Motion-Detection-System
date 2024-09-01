#include "camera_control.h"
#include "send_mail.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <sys/types.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <signal.h>

struct v4l2_requestbuffers reqbuf = {0};
struct frames *buffers = NULL; 

volatile sig_atomic_t isActive = 1;

// Signal handler for SIGINT (Ctrl+C)
void handle_sigint(int sig) {
    isActive = 0;
}

//wrapper around the ioctl call which will be filling the buffers we send with the appropriate operation
static int xioctl(int fh, int request, void *arg)
{
        int r;
        do{
            r = ioctl(fh, request, arg);
        } while (-1 == r && EINTR == errno);

        return r;
}

//This function can be used to query what kind of actions can be taken with our camera.
//It doesn't need to be used
int QueryCapabilities(int fd)
{
	struct v4l2_capability cap = {0};				//initialize all values to 0

	if (-1 == ioctl(fd, VIDIOC_QUERYCAP, &cap)) {
		perror("Query capabilites");
		return -1;
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		fprintf(stderr, "Device is no video capture device\\n");
		return -1;
	}

	if (!(cap.capabilities & V4L2_CAP_READWRITE)) {
		fprintf(stderr, "Device does not support read i/o\\n");
		return -1;
	}

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		fprintf(stderr, "Devices does not support streaming i/o\\n");
		return -1;
	}

	return 0;
}

int CheckBufferAllocation(int allocatedBuffers)
{
	if ( allocatedBuffers < WantedNumberOfBuffers) {
		printf("Not enough buffers allocated!\n");
		return -1;
	}
	return 0;
}

int CleanupRequestedBuffers(int fd)
{
	reqbuf.count = 0;  									// Set count to 0 to release allocated buffers
    if (ioctl(fd, VIDIOC_REQBUFS, &reqbuf) == -1) 
	{
        perror("Error freeing requested buffers");
        return -1;
    }

	return 0;
}

/*
Before even starting the streaming process we need to determine the format of data that will be exchanged
between the driver and the application(in our case the image format)
*/
int SetFormat(int fd)
{
	//I chose these values for easier comparison that will need to be done
	struct v4l2_format format = {0};
	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	format.fmt.pix.width = WIDTH;
	format.fmt.pix.height = HEIGHT;
	//NV12 format was chosen because it takes the least amount of bits per pixel(12 bits) out of all the formats available to me
	format.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
	format.fmt.pix.field = V4L2_FIELD_NONE;

	if(ioctl(fd, VIDIOC_S_FMT, &format) == -1) {
		perror("Could not set format");
		return -1;
	}

	//Ensuring that the format we wanted is applied
	char format_code[5] = {0};
  	strncpy(format_code, (char*)&format.fmt.pix.pixelformat, 5);
  	printf(
    "Set format:\n"
    " Width: %d\n"
    " Height: %d\n"
    " Pixel format: %s\n"
    " Field: %d\n\n",
	format.fmt.pix.width,
    format.fmt.pix.height,
    format_code,
    format.fmt.pix.field);

	return 0;
}

//Allocate buffers for storing images
int RequestBuffers(int fd)
{
	reqbuf.count = WantedNumberOfBuffers;								
	reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	reqbuf.memory = V4L2_MEMORY_MMAP;

	if (ioctl(fd, VIDIOC_REQBUFS, &reqbuf) == -1) {
		perror("Requesting Buffer");
		return -1;
	}

	if(CheckBufferAllocation(reqbuf.count))						//Check if we got the ammount of buffers we wanted
	{					
		if(CleanupRequestedBuffers(fd))							//Free the allocated buffers in case we didn't get enough
			return -1;											//return -1 if something went wrong
	}
	
	return 0;
}

//After the buffers are allocated by the kernel, we have to query the physical address of each allocated buffer in order to mmap() those.
int QueryBuffer(int fd)
{
	struct v4l2_buffer buff = {0};
	buffers=NULL;

	buffers = calloc(reqbuf.count, sizeof(*buffers));
	if(!buffers)
	{
		printf("Error allocating memory with calloc!");
		CleanupRequestedBuffers(fd);
		return -1;
	}

	for (unsigned int i = 0; i < reqbuf.count; i++) {
    	memset(&buff, 0, sizeof(buff));
    	buff.type = reqbuf.type;
    	buff.memory = V4L2_MEMORY_MMAP;
    	buff.index = i;

    	// Note: VIDIOC_QUERYBUF, not VIDIOC_QBUF, is used here!
    	if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buff)) 
		{
      		perror("VIDIOC_QUERYBUF");
      		CleanupRequestedBuffers(fd);
			return -1;
    	}

    	buffers[i].length = buff.length;
    	buffers[i].start = mmap(NULL, buff.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buff.m.offset);

    	if (MAP_FAILED == buffers[i].start) {
			perror("mmap");
			UnmapBuffers(i);										//Unmap buffers in case some of them were mapped thus far
      		CleanupRequestedBuffers(fd);
			return -1;
		}
	}

	return 0;
}

//Before the buffers can be filled with data, the buffers has to be enqueued.
int QueueBuffer(int fd)
{
	struct v4l2_buffer bufd = {0};
	for (unsigned int i = 0; i < reqbuf.count; i++)
	{
		memset(&bufd,0,sizeof(bufd));
		bufd.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		bufd.memory = V4L2_MEMORY_MMAP;
		bufd.index = i;

		if (ioctl(fd, VIDIOC_QBUF, &bufd) == -1) {
			perror("Queue Buffer");
			return -1;
		}
	}

	return 0;
}

//Start capturing frames
int StartStreaming(int fd)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if(ioctl(fd, VIDIOC_STREAMON, &type) == -1) {
		perror("VIDIOC_STREAMON");
		return -1;
	}

	return 0;
}

//Taking the frame from the outgoing queue and assigning it to the appropriate frame
int DequeueBuffer(int fd, bool *firstTime, int *warmupPeriod, char *currentFrame, char *previousFrame)
{
	struct v4l2_buffer bufd = {0};
	bufd.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	bufd.memory = V4L2_MEMORY_MMAP;
	bool thereIsMovement=false;

	//Taking a buffer filled with data out of the queue
	if (ioctl(fd, VIDIOC_DQBUF, &bufd) == -1) 
	{
  		perror("VIDIOC_DQBUF");
		return -1;
	}

	if(bufd.index > reqbuf.count)
	{
		perror("Buffer index bigger than total ammount of buffers!");
		return -1;
	}

	//If it is the first frame then there is no need for switching frames, just filling the currentFrame is enough
	if(*firstTime)
	{
		memcpy(currentFrame, buffers[bufd.index].start, buffers[bufd.index].length / 2);	// Copy only the Y (luminance) component
	}

	else
		SwitchFrames(currentFrame, previousFrame, bufd.index);


  	// As soon as we are done processing the buffer data, we enqueue the buffer again
  	if (-1 == xioctl(fd, VIDIOC_QBUF, &bufd)) {
    	perror("VIDIOC_QBUF");
    	return -1;
 	}

	if(*firstTime)
	{
		*firstTime = false;
		(*warmupPeriod)--;
		return 0;
	}

	//Waiting for the camera to fully calibrate
	if(*warmupPeriod > 0)
	{
		(*warmupPeriod)--;
		return 0;
	}

	thereIsMovement = CompareFrames(currentFrame, previousFrame);

	if(thereIsMovement){
		sendEmailAlert();
		*warmupPeriod = 3;				//when sleep() ends, don't compare the first couple of frames
		sleep(30);
	}

	return 0;
}

int StopStreaming(int fd)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if(ioctl(fd, VIDIOC_STREAMOFF, &type) == -1) {
		perror("VIDIOC_STREAMON");
		UnmapBuffers(WantedNumberOfBuffers);
		CleanupRequestedBuffers(fd);
		return -1;
	}
    
    return 0;
}

//Main part of the program which continously runs the camera and checks for motion detection
int MainLoop(int fd)
{

	signal(SIGINT, handle_sigint);

	int status=0;
	int warmupPeriod=4;			//We wont check the first 4 frames because when first starting camera it needs a bit to stabilize
	bool firstTtime= true;
	
	/* For motion detection we will only compare luminance(Y) component of images since it is the most important one
	and in NV12(used format in this code) that information is contained in the first half of the frame data
	That's why further in this code it will always be buffers[x].length/2 which are all the same size(printf statements below check that)
	It is the most effective and the fastest method
	*/

	char *currentFrame= (char*) calloc(1, buffers[0].length/2);
	char *previousFrame=(char*) calloc(1, buffers[0].length/2);

	printf("Half the buffer length 1 is %zu\n", buffers[0].length/2);
	printf("Half the buffer length 2 is %zu\n", buffers[1].length/2);
	printf("Half the buffer length 3 is %zu\n\n\n", buffers[2].length/2);

	while(isActive){
		
		//This part of the code sets up monitoring for file descriptor.
		//It checks for readability, writability, or exceptional conditions using the select() function.
		fd_set fds;
		struct timeval tv;
		int r;

		FD_ZERO(&fds);
		FD_SET(fd, &fds);

		tv.tv_sec = 2;
		tv.tv_usec = 0;

		r = select(fd + 1, &fds, NULL, NULL, &tv);

		if (r == -1) {
			if (errno == EINTR) {
                // If select() was interrupted by a signal, check if we should exit
                if (!isActive) 
                    break;
			}

			perror("Select error!");
			return -1;
		}

		else if (r == 0) {
			fprintf(stderr, "Select timeout!\n");
			return -1;
		}

		//If we made it here it means that file descriptor is ready and we call the function for processing frames
		status = DequeueBuffer(fd, &firstTtime, &warmupPeriod, currentFrame, previousFrame);
		if(status==-1){
			free(currentFrame);
			free(previousFrame);
			return -1;
		}

		puts("-");
	}

	free(currentFrame);
	free(previousFrame);

	return 0;
}

int UnmapBuffers(int ammountOfMappedBuffers)
{
	if(ammountOfMappedBuffers == 0)
	{
		printf("No buffer was successfully mapped.");
		return 0;
	}

	for (int i =0; i < ammountOfMappedBuffers; i++)
	{ 
		munmap(buffers[i].start, buffers[i].length);
	}

	free(buffers);
	return 0;
}

void SwitchFrames(char *currentFrame, char *previousFrame, int index)
{
	memcpy(previousFrame, currentFrame, buffers[index].length/2);
	memcpy(currentFrame, buffers[index].start, buffers[index].length / 2);
}

bool CompareFrames(char *currentFrame, char *previousFrame)
{
	size_t difference = 0;

	for (size_t i = 0; i < buffers[0].length / 2; i++) { 

		//To avoid false positives caused by overly sensitive and unstable pixels, the comparison is implemented like this 
   		if (abs( currentFrame[i] - previousFrame[i]) > 10) 
        	difference++;

	}

	if(difference > MOVEMENT_THRESHOLD){
		printf("The difference is %zu\n", difference);
		return true;
	}

	printf("The difference is %zu\n", difference);

	return false;
}