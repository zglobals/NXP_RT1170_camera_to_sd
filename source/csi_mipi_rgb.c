/*
 * Copyright 2019 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

//#include "display_support.h"
#include "camera_support.h"
#include "fsl_pxp.h"

#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#include "fsl_debug_console.h"

#include "ff.h"
#include "diskio.h"
#include "fsl_sd_disk.h"
#include "sdmmc_config.h"


const static uint8_t header_bmp[] = {
		0x42,0x4D,0x36,0xEC,0x5E,0x00,0x00,0x00,0x00,0x00,0x36,0x00,0x00,0x00,0x28,0x00,
		0x00,0x00,0x80,0x07,0x00,0x00,0x38,0x04,0x00,0x00,0x01,0x00,0x20,0x00,0x00,0x00,
		0x00,0x00,0x00,0xEC,0x5E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00
		};

/*
 * In this example, the camera device output pixel format is RGB565, the MIPI_CSI
 * converts it to RGB888 internally and sends to CSI. In other words, the CSI input
 * data bus width is 24-bit. The CSI saves the frame as 32-bit format XRGB8888.
 *
 * The PXP is used in this example to rotate and convert the CSI output frame
 * to fit the display output.
 */

#include "fsl_soc_src.h"
/*******************************************************************************
 * Definitions
 ******************************************************************************/

/* CSI output frame buffer is XRGB8888. */
#define DEMO_CAMERA_BUFFER_BPP 4 // 4

#define DEMO_CAMERA_BUFFER_COUNT 2



/*******************************************************************************
 * Prototypes
 ******************************************************************************/
//static void DEMO_BufferSwitchOffCallback(void *param, void *switchOffBuffer);
static void DEMO_InitCamera(void);
static void DEMO_CSI_MIPI_RGB(void);
static status_t sdcardWaitCardInsert(void);

/*******************************************************************************
 * Variables
 ******************************************************************************/

AT_NONCACHEABLE_SECTION_ALIGN(static uint8_t s_cameraBuffer[DEMO_CAMERA_BUFFER_COUNT][DEMO_CAMERA_HEIGHT]
																					  [DEMO_CAMERA_WIDTH * DEMO_CAMERA_BUFFER_BPP],
																					  DEMO_CAMERA_BUFFER_ALIGN);

/*******************************************************************************
 * Code
 ******************************************************************************/

static void BOARD_ResetDisplayMix(void)
{
	/*
	 * Reset the displaymix, otherwise during debugging, the
	 * debugger may not reset the display, then the behavior
	 * is not right.
	 */
	SRC_AssertSliceSoftwareReset(SRC, kSRC_DisplaySlice);
	while (kSRC_SliceResetInProcess == SRC_GetSliceResetState(SRC, kSRC_DisplaySlice))
	{
	}
}



/*!
 * @brief Main function
 *
 * drop display
 *
 */
int main(void)
{
	BOARD_ConfigMPU();
	BOARD_InitBootPins();
	BOARD_BootClockRUN();
	BOARD_ResetDisplayMix();
	BOARD_EarlyInitCamera();
	BOARD_InitLpuartPins();
	BOARD_InitMipiPanelPins();
	BOARD_InitMipiCameraPins();
	BOARD_InitDebugConsole();
	BOARD_InitPins();

	PRINTF("CSI MIPI RGB example start...\r\n");

	memset(s_cameraBuffer, 0, sizeof(s_cameraBuffer));

	DEMO_InitCamera();

	DEMO_CSI_MIPI_RGB();

	while (1)
	{
	}
}



static void DEMO_InitCamera(void)
{
	camera_config_t cameraConfig;

	memset(&cameraConfig, 0, sizeof(cameraConfig));

	BOARD_InitCameraResource();

	/* CSI input data bus is 24-bit, and save as XRGB8888.. */
	cameraConfig.pixelFormat                = kVIDEO_PixelFormatXRGB8888;
	cameraConfig.bytesPerPixel              = DEMO_CAMERA_BUFFER_BPP;
	cameraConfig.resolution                 = FSL_VIDEO_RESOLUTION(DEMO_CAMERA_WIDTH, DEMO_CAMERA_HEIGHT);
	cameraConfig.frameBufferLinePitch_Bytes = DEMO_CAMERA_WIDTH * DEMO_CAMERA_BUFFER_BPP;
	cameraConfig.interface                  = kCAMERA_InterfaceGatedClock;
	cameraConfig.controlFlags               = DEMO_CAMERA_CONTROL_FLAGS;
	cameraConfig.framePerSec                = DEMO_CAMERA_FRAME_RATE;

	CAMERA_RECEIVER_Init(&cameraReceiver, &cameraConfig, NULL, NULL);

	BOARD_InitMipiCsi();

	cameraConfig.pixelFormat   = kVIDEO_PixelFormatRGB565;
	cameraConfig.bytesPerPixel = 2;
	cameraConfig.resolution    = FSL_VIDEO_RESOLUTION(DEMO_CAMERA_WIDTH, DEMO_CAMERA_HEIGHT);
	cameraConfig.interface     = kCAMERA_InterfaceMIPI;
	cameraConfig.controlFlags  = DEMO_CAMERA_CONTROL_FLAGS;
	cameraConfig.framePerSec   = DEMO_CAMERA_FRAME_RATE;
	cameraConfig.csiLanes      = DEMO_CAMERA_MIPI_CSI_LANE;
	CAMERA_DEVICE_Init(&cameraDevice, &cameraConfig);

	CAMERA_DEVICE_Start(&cameraDevice);

	/* Submit the empty frame buffers to buffer queue. */
	for (uint32_t i = 0; i < DEMO_CAMERA_BUFFER_COUNT; i++)
	{
		CAMERA_RECEIVER_SubmitEmptyBuffer(&cameraReceiver, (uint32_t)(s_cameraBuffer[i]));
	}
}

static FATFS g_fileSystem; /* File system object */
const TCHAR driverNumberBuffer[3U] = {SDDISK + '0', ':', '/'};

#define COUNT_SCREEN 4

static void DEMO_CSI_MIPI_RGB(void)
{
	uint32_t cameraReceivedFrameAddr;

	PRINTF("start receive\n");
	CAMERA_RECEIVER_Start(&cameraReceiver);

	while (1)
	{
		for(int i = 0; i < COUNT_SCREEN; i++) {
			while (kStatus_Success != CAMERA_RECEIVER_GetFullBuffer(&cameraReceiver, &cameraReceivedFrameAddr))
			{
			}
			CAMERA_RECEIVER_SubmitEmptyBuffer(&cameraReceiver, (uint32_t)cameraReceivedFrameAddr);
		}

		PRINTF("get full buffer\n");

		while (kStatus_Success != CAMERA_RECEIVER_GetFullBuffer(&cameraReceiver, &cameraReceivedFrameAddr))
		{
		}

		CAMERA_RECEIVER_Stop(&cameraReceiver);

		PRINTF("stop receive\n");

		if (sdcardWaitCardInsert() != kStatus_Success) {
			PRINTF("Error sd card.\r\n");
		}


		if (f_mount(&g_fileSystem, driverNumberBuffer, 0U) != FR_OK) {
			PRINTF("Mount volume failed.\r\n");
		}

		FIL file0;

		f_open(&file0, _T("2:/image0.bmp"), FA_WRITE | FA_READ | FA_CREATE_ALWAYS);

		//PRINTF("write header\n");
		//f_write(&file0, header_bmp, sizeof(header_bmp), NULL);
		PRINTF("write raw\n");
		f_write(&file0,  (void*)cameraReceivedFrameAddr, DEMO_CAMERA_HEIGHT * DEMO_CAMERA_WIDTH * DEMO_CAMERA_BUFFER_BPP, NULL);

		f_close(&file0);

		PRINTF("photo cmpl\n");

		while(1) {
			asm("nop");
		}
	}
}

static status_t sdcardWaitCardInsert(void)
{
	BOARD_SD_Config(&g_sd, NULL, BOARD_SDMMC_SD_HOST_IRQ_PRIORITY, NULL);

	/* SD host init function */
	if (SD_HostInit(&g_sd) != kStatus_Success)
	{
		PRINTF("\r\nSD host init fail\r\n");
		return kStatus_Fail;
	}

	/* wait card insert */
	if (SD_PollingCardInsert(&g_sd, kSD_Inserted) == kStatus_Success)
	{
		PRINTF("\r\nCard inserted.\r\n");
		/* power off card */
		SD_SetCardPower(&g_sd, false);
		/* power on the card */
		SD_SetCardPower(&g_sd, true);
	}
	else
	{
		PRINTF("\r\nCard detect fail.\r\n");
		return kStatus_Fail;
	}

	return kStatus_Success;
}
