//
// littlevgl.h
//
// C++ wrapper for LittlevGL with mouse and touch screen support
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2019  R. Stange <rsta2@o2online.de>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#ifndef _littlevgl_littlevgl_h
#define _littlevgl_littlevgl_h

#include <littlevgl/lvgl/lvgl.h>
#include <circle/screen.h>
#include <circle/bcmframebuffer.h>
#include <circle/interrupt.h>
#include <circle/input/mouse.h>
#include <circle/input/touchscreen.h>
#include <circle/dmachannel.h>
#include <circle/types.h>
#include <assert.h>

class CLittlevGL
{
public:
	CLittlevGL (CScreenDevice *pScreen, CInterruptSystem *pInterrupt);
	CLittlevGL (CBcmFrameBuffer *pFrameBuffer, CInterruptSystem *pInterrupt);
	~CLittlevGL (void);

	boolean Initialize (void);

	void Update (void);

private:
	static void DisplayFlush (lv_disp_drv_t *pDriver, const lv_area_t *pArea,
				  lv_color_t *pBuffer);
	static void DisplayFlushComplete (unsigned nChannel, boolean bStatus, void *pParam);

	static bool PointerRead (lv_indev_drv_t *pDriver, lv_indev_data_t *pData);
	static void MouseEventHandler (TMouseEvent Event, unsigned nButtons,
				       unsigned nPosX, unsigned nPosY);
	static void TouchScreenEventHandler (TTouchScreenEvent Event, unsigned nID,
					     unsigned nPosX, unsigned nPosY);

	static void LogPrint (lv_log_level_t Level, const char *pFile, uint32_t nLine,
			      const char *pDescription);

private:
	lv_color_t *m_pBuffer1;
	lv_color_t *m_pBuffer2;

	CScreenDevice *m_pScreen;
	CBcmFrameBuffer *m_pFrameBuffer;
	CDMAChannel m_DMAChannel;
	unsigned m_nLastUpdate;

	CMouseDevice *m_pMouseDevice;
	CTouchScreenDevice *m_pTouchScreen;
	unsigned m_nLastTouchUpdate;
	lv_indev_data_t m_PointerData;

	static CLittlevGL *s_pThis;
};

#endif
