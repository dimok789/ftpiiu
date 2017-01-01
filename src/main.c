#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <malloc.h>
#include <gctypes.h>
#include <iosuhax.h>
#include <iosuhax_devoptab.h>
#include "dynamic_libs/os_functions.h"
#include "dynamic_libs/fs_functions.h"
#include "dynamic_libs/gx2_functions.h"
#include "dynamic_libs/sys_functions.h"
#include "dynamic_libs/vpad_functions.h"
#include "dynamic_libs/padscore_functions.h"
#include "dynamic_libs/socket_functions.h"
#include "dynamic_libs/ax_functions.h"
#include "fs/fs_utils.h"
#include "fs/sd_fat_devoptab.h"
#include "system/memory.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "common/common.h"
#include "ftp.h"
#include "virtualpath.h"
#include "net.h"

#define PORT                    21
#define MAX_CONSOLE_LINES_TV    27
#define MAX_CONSOLE_LINES_DRC   18

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

        consoleArrayTv[MAX_CONSOLE_LINES_TV-1] = (char*)malloc(strlen(tmp) + 1);
        if(consoleArrayTv[MAX_CONSOLE_LINES_TV-1])
            strcpy(consoleArrayTv[MAX_CONSOLE_LINES_TV-1], tmp);

        consoleArrayDrc[MAX_CONSOLE_LINES_DRC-1] = (tmp);
	}
	va_end(va);

    // Clear screens
    OSScreenClearBufferEx(0, 0);
    OSScreenClearBufferEx(1, 0);


	for(int i = 0; i < MAX_CONSOLE_LINES_TV; i++)
    {
        if(consoleArrayTv[i])
            OSScreenPutFontEx(0, 0, i, consoleArrayTv[i]);
    }

	for(int i = 0; i < MAX_CONSOLE_LINES_DRC; i++)
    {
        if(consoleArrayDrc[i])
            OSScreenPutFontEx(1, 0, i, consoleArrayDrc[i]);
    }

	OSScreenFlipBuffersEx(0);
	OSScreenFlipBuffersEx(1);
}

/* Entry point */
int Menu_Main(void)
{
    //!*******************************************************************
    //!                   Initialize function pointers                   *
    //!*******************************************************************
    //! do OS (for acquire) and sockets first so we got logging
    InitOSFunctionPointers();
    InitSocketFunctionPointers();

    log_init("192.168.178.3");
    log_print("Starting launcher\n");

    InitFSFunctionPointers();
    InitVPadFunctionPointers();

    log_print("Function exports loaded\n");

    //!*******************************************************************
    //!                    Initialize heap memory                        *
    //!*******************************************************************
    log_print("Initialize memory management\n");
    //! We don't need bucket and MEM1 memory so no need to initialize
    //memoryInitialize();

    //!*******************************************************************
    //!                        Initialize FS                             *
    //!*******************************************************************
    log_printf("Mount SD partition\n");
    mount_sd_fat("sd");

    int res = IOSUHAX_Open();
    if(res < 0)
    {
        log_printf("IOSUHAX_open failed\n");
    }

    int fsaFd = IOSUHAX_FSA_Open();
    if(fsaFd < 0)
    {
        log_printf("IOSUHAX_FSA_Open failed\n");
    }

    mount_fs("slccmpt01", fsaFd, "/dev/slccmpt01", "/vol/storage_slccmpt01");
    mount_fs("storage_odd_tickets", fsaFd, "/dev/odd01", "/vol/storage_odd_tickets");
    mount_fs("storage_odd_updates", fsaFd, "/dev/odd02", "/vol/storage_odd_updates");
    mount_fs("storage_odd_content", fsaFd, "/dev/odd03", "/vol/storage_odd_content");
    mount_fs("storage_odd_content2", fsaFd, "/dev/odd04", "/vol/storage_odd_content2");
    mount_fs("storage_slc", fsaFd, NULL, "/vol/system");
    mount_fs("storage_mlc", fsaFd, NULL, "/vol/storage_mlc01");
    mount_fs("storage_usb", fsaFd, NULL, "/vol/storage_usb01");

	for(int i = 0; i < MAX_CONSOLE_LINES_TV; i++)
        consoleArrayTv[i] = NULL;

	for(int i = 0; i < MAX_CONSOLE_LINES_DRC; i++)
        consoleArrayDrc[i] = NULL;

    VPADInit();

    // Prepare screen
    int screen_buf0_size = 0;

    // Init screen and screen buffers
    OSScreenInit();
    screen_buf0_size = OSScreenGetBufferSizeEx(0);
    OSScreenSetBufferEx(0, (void *)0xF4000000);
    OSScreenSetBufferEx(1, (void *)(0xF4000000 + screen_buf0_size));

    OSScreenEnableEx(0, 1);
    OSScreenEnableEx(1, 1);

    // Clear screens
    OSScreenClearBufferEx(0, 0);
    OSScreenClearBufferEx(1, 0);

    // Flip buffers
    OSScreenFlipBuffersEx(0);
    OSScreenFlipBuffersEx(1);

    console_printf("FTPiiU v0.4 is listening on %u.%u.%u.%u:%i", (network_gethostip() >> 24) & 0xFF, (network_gethostip() >> 16) & 0xFF, (network_gethostip() >> 8) & 0xFF, (network_gethostip() >> 0) & 0xFF, PORT);

	MountVirtualDevices();

    int serverSocket = create_server(PORT);

	int network_down = 0;
    int vpadError = -1;
    VPADData vpad;
    int vpadReadCounter = 0;

    while(serverSocket >= 0 && !network_down)
    {
        network_down = process_ftp_events(serverSocket);
        if(network_down)
        {
            break;
        }

        //! update only at 50 Hz, thats more than enough
        if(++vpadReadCounter >= 20)
        {
            vpadReadCounter = 0;

            VPADRead(0, &vpad, 1, &vpadError);

            if(vpadError == 0 && ((vpad.btns_d | vpad.btns_h) & VPAD_BUTTON_HOME))
                break;
        }

		usleep(1000);
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

    //!*******************************************************************
    //!                    Enter main application                        *
    //!*******************************************************************

    log_printf("Unmount SD\n");
    unmount_sd_fat("sd");

    unmount_fs("slccmpt01");
    unmount_fs("storage_odd_tickets");
    unmount_fs("storage_odd_updates");
    unmount_fs("storage_odd_content");
    unmount_fs("storage_odd_content2");
    unmount_fs("storage_slc");
    unmount_fs("storage_mlc");
    unmount_fs("storage_usb");
    IOSUHAX_FSA_Close(fsaFd);
    IOSUHAX_Close();

    log_printf("Release memory\n");
    //memoryRelease();
    log_deinit();

    return EXIT_SUCCESS;
}

