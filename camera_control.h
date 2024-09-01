#ifndef CAMERA_CONTROL_H
#define CAMERA_CONTROL_H

#include<stdio.h>
#include<stdbool.h>

#define HEIGHT 240
#define WIDTH 320
#define WantedNumberOfBuffers 3
#define MOVEMENT_THRESHOLD 1000 // Adjust this value to change sensitivity

struct frames{
  void *start;
  size_t length;
};

extern struct frames *buffers;

static int xioctl(int fh, int request, void *arg);
int QueryCapabilities(int fd);
int CheckBufferAllocation(int allocatedBuffers);
int CleanupRequestedBuffers(int fd);
int SetFormat(int fd);
int RequestBuffers(int fd);
int QueryBuffer(int fd);
int QueueBuffer(int fd);
int StartStreaming(int fd);
int DequeueBuffer(int fd,bool *firstTime, int *warmupPeriod, char *currentFrame, char *previousFrame);
int StopStreaming(int fd);
int MainLoop(int fd);
int UnmapBuffers(int ammountOfMappedBuffers);
int CaptureFrame(int fd, unsigned char **frame);
void SwitchFrames(char *currentFrame, char *previousFrame, int index);
bool CompareFrames(char *currentFrame, char *previousFrame);



#endif