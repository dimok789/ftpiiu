#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <malloc.h>
#include <coreinit/memfrmheap.h>
#include <coreinit/memheap.h>
#include <coreinit/screen.h>
#include <coreinit/thread.h>
#include <coreinit/cache.h>
#include <coreinit/time.h>
#include <vpad/input.h>
#include <whb/proc.h>
#include "ftp.h"
#include "virtualpath.h"
#include "net.h"

#define PORT                    21
#define MAX_CONSOLE_LINES_TV    27
#define MAX_CONSOLE_LINES_DRC   18
#define FRAME_HEAP_TAG			0x46545055

static void *sBufferTV, *sBufferDRC;
static uint32_t sBufferSizeTV, sBufferSizeDRC;

static char * consoleArrayTv[MAX_CONSOLE_LINES_TV];
static char * consoleArrayDrc[MAX_CONSOLE_LINES_DRC];

void console_printf(const char *format, ...)
{
	char * tmp = NULL;

	va_list va;
	va_start(va, format);
	if((vasprintf(&tmp, format, va) >= 0) && tmp)
	{
	    if(consoleArrayTv[0])
            free(consoleArrayTv[0]);
	    if(consoleArrayDrc[0])
            free(consoleArrayDrc[0]);

        for(int i = 1; i < MAX_CONSOLE_LINES_TV; i++)
            consoleArrayTv[i-1] = consoleArrayTv[i];

        for(int i = 1; i < MAX_CONSOLE_LINES_DRC; i++)
            consoleArrayDrc[i-1] = consoleArrayDrc[i];

        if(strlen(tmp) > 79)
            tmp[79] = 0;

        consoleArrayTv[MAX_CONSOLE_LINES_TV-1] = strdup(tmp);
        consoleArrayDrc[MAX_CONSOLE_LINES_DRC-1] = (tmp);
	}
	va_end(va);

    // Clear screens
    OSScreenClearBufferEx(SCREEN_TV, 0);
    OSScreenClearBufferEx(SCREEN_DRC, 0);

	for(int i = 0; i < MAX_CONSOLE_LINES_TV; i++)
    {
        if(consoleArrayTv[i])
            OSScreenPutFontEx(SCREEN_TV, 0, i, consoleArrayTv[i]);
    }

	for(int i = 0; i < MAX_CONSOLE_LINES_DRC; i++)
    {
        if(consoleArrayDrc[i])
            OSScreenPutFontEx(SCREEN_DRC, 0, i, consoleArrayDrc[i]);
    }

	DCFlushRange(sBufferTV, sBufferSizeTV);
	DCFlushRange(sBufferDRC, sBufferSizeDRC);
	OSScreenFlipBuffersEx(SCREEN_TV);
	OSScreenFlipBuffersEx(SCREEN_DRC);
}

/* Entry point */
int main()
{
	WHBProcInit();
	initialise_network();

	for(int i = 0; i < MAX_CONSOLE_LINES_TV; i++)
        consoleArrayTv[i] = NULL;

	for(int i = 0; i < MAX_CONSOLE_LINES_DRC; i++)
        consoleArrayDrc[i] = NULL;

    // Init screen and screen buffers
	MEMHeapHandle heap = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM1);
	MEMRecordStateForFrmHeap(heap, FRAME_HEAP_TAG);

	OSScreenInit();
	sBufferSizeTV = OSScreenGetBufferSizeEx(SCREEN_TV);
	sBufferSizeDRC = OSScreenGetBufferSizeEx(SCREEN_DRC);

	sBufferTV = MEMAllocFromFrmHeapEx(heap, sBufferSizeTV, 4);
	sBufferDRC = MEMAllocFromFrmHeapEx(heap, sBufferSizeDRC, 4);
	if (!sBufferTV || !sBufferDRC)
		goto exit;

	OSScreenSetBufferEx(SCREEN_TV, sBufferTV);
	OSScreenSetBufferEx(SCREEN_DRC, sBufferDRC);

	OSScreenEnableEx(SCREEN_TV, 1);
	OSScreenEnableEx(SCREEN_DRC, 1);


    console_printf("FTPiiU v0.4 is listening on %u.%u.%u.%u:%i", (network_gethostip() >> 24) & 0xFF, (network_gethostip() >> 16) & 0xFF, (network_gethostip() >> 8) & 0xFF, (network_gethostip() >> 0) & 0xFF, PORT);

	MountVirtualDevices();

    int serverSocket = create_server(PORT);

	int network_down = 0;

    while(WHBProcIsRunning() && serverSocket >= 0 && !network_down)
    {
        network_down = process_ftp_events(serverSocket);
        if(network_down)
        {
            break;
        }
		OSSleepTicks(OSMicrosecondsToTicks(1000));
    }

	cleanup_ftp();
	if(serverSocket >= 0)
        network_close(serverSocket);
	UnmountVirtualPaths();

    //! free memory
	for(int i = 0; i < MAX_CONSOLE_LINES_TV; i++)
    {
        if(consoleArrayTv[i])
            free(consoleArrayTv[i]);
    }

	for(int i = 0; i < MAX_CONSOLE_LINES_DRC; i++)
    {
        if(consoleArrayDrc[i])
            free(consoleArrayDrc[i]);
    }

exit:
	OSScreenShutdown();
	MEMFreeByStateToFrmHeap(heap, FRAME_HEAP_TAG);

	finalize_network();
	WHBProcShutdown();
    return 0;
}

