#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "camera_control.h"

int main()
{ 
	int flag =0;								//Flag that we will check to see if code is running ok(added for readability) 

	int fd = open("/dev/video0", O_RDWR);		/* /dev/video0 is the camera I am using */ 
	if(!fd)
	{
		printf("File could not be opened!");
		return -1;
	}

	/* Step 1: Query capabilities */
	flag = QueryCapabilities(fd);
	if(flag)
	{
		close(fd);
		return -1;
	}

	/* Step 2: Set format */
	flag = SetFormat(fd);
	if(flag)
	{
		close(fd);
		return -1;
	}

	/* Step 3: Request Buffers */
	flag = RequestBuffers(fd);
	if (flag)
	{
		close(fd);
		return -1;
	}

	/* Step 4: Query buffers */ 
	flag = QueryBuffer(fd);
	if (flag)
	{
		close(fd);
		return -1;
	}

	/*Step 5: Queue Buffer*/
	flag = QueueBuffer(fd);
	if(flag)
	{
		UnmapBuffers(WantedNumberOfBuffers);
		CleanupRequestedBuffers(fd);
		close(fd);
		return -1;
	}

	/* Step 6: Start streaming */
	flag = StartStreaming(fd);
	if (flag)
	{
		UnmapBuffers(WantedNumberOfBuffers);
		CleanupRequestedBuffers(fd);
		close(fd);
		return -1;
	}

	flag = MainLoop(fd);
    if (flag) {
    	UnmapBuffers(WantedNumberOfBuffers);
		CleanupRequestedBuffers(fd);
		close (fd);
		return -1;
    }

	/* Step 8: Stop streaming */
	StopStreaming(fd);

	printf("MADE IT OUT\n");
	
	/* Cleanup the resources */
	CleanupRequestedBuffers(fd);
	UnmapBuffers(WantedNumberOfBuffers);
	close(fd);
	
	return 0;
}