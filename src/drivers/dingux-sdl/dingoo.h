#ifndef __FCEU_DINGOO_H
#define __FCEU_DINGOO_H

#include <SDL/SDL.h>
#include "main.h"
//#include "dface.h"
#include "input.h"

uint16 *FCEUD_GetPaletteArray16();
int FCEUD_NetworkConnect(void);
int InitJoysticks(void);
int KillJoysticks(void);
uint32 GetBufferSize(void);
uint32 GetBufferedSound(void);
void BlitScreen(uint8 *XBuf);
int InitSound();
void WriteSound(int32 *Buffer, int Count);
int KillSound(void);
int InitVideo(FCEUGI *gi);
int KillVideo(void);
void SilenceSound(int s); /* DOS and SDL */
void DoFun(int frameskip, int);

int LoadGame(const char *path);
int CloseGame(void);

int FCEUD_LoadMovie(const char *name, char *romname);
int FCEUD_DriverReset();

void FCEUI_FDSFlip(void);

//extern int dendy;
//extern int pal_emulation;
//extern bool swapDuty;
//extern bool paldeemphswap;

#endif
