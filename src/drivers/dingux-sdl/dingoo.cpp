#include <math.h>
#include <errno.h>
#include <stdio.h>
#include <fstream>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <iostream>
#include <limits.h>
#include <SDL/SDL.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "main.h"
#include "input.h"
#include "config.h"
#include "dingoo.h"
#include "gui/gui.h"
#include "throttle.h"
#include "dingoo-video.h"
#include "dummy-netplay.h"

#include "../../fceu.h"
#include "../../types.h"
#include "../../movie.h"
#include "../../version.h"
#include "../../oldmovie.h"
#include "../common/cheat.h"
#include "../common/configSys.h"

static void DriverKill(void);
static int DriverInitialize(FCEUGI *gi);

extern double fps_scale;

int show_fps = 0;

static int frameskip = 0;
static int init_state = 0;
static int fps_throttle = 0;
static int is_rom_loaded=0;

Config *g_config;

int LoadGame(const char *path)
{
  CloseGame();
  if (!FCEUI_LoadGame(path, 1))
  {
    return 0;
  }
  ParseGIInput(GameInfo);
  RefreshThrottleFPS();

  // Reload game config or default config
  g_config->reload(FCEU_MakeFName(FCEUMKF_CFG, 0, 0));

#ifdef FRAMESKIP
  // Update frameskip value
  g_config->getOption("SDL.Frameskip", &frameskip);
#endif

  if (!DriverInitialize(GameInfo))
  {
    return (0);
  }

  // set pal/ntsc
  int id;
  g_config->getOption("SDL.PAL", &id);
  if (id)
    FCEUI_SetVidSystem(1);
  else
    FCEUI_SetVidSystem(0);

  std::string filename;
  g_config->getOption("SDL.Sound.RecordFile", &filename);
  if (filename.size())
  {
    if (!FCEUI_BeginWaveRecord(filename.c_str()))
    {
      g_config->setOption("SDL.Sound.RecordFile", "");
    }
  }

  // Set mouse cursor's movement speed
  //g_config->getOption("SDL.MouseSpeed", &mousespeed);
  //g_config->getOption("SDL.ShowMouseCursor", &showmouse);

  // Show or not to show fps, that is the cuestion ...
  g_config->getOption("SDL.ShowFPS", &show_fps);
  g_config->getOption("SDL.FPSThrottle", &fps_throttle);

  is_rom_loaded = 1;

  FCEUD_NetworkConnect();
  return 1;
}

/**
 * Closes a game.  Frees memory, and deinitializes the drivers.
 */
int CloseGame(void)
{
  std::string filename;

  if (!is_rom_loaded)
  {
    return (0);
  }

  FCEUI_CloseGame();
  DriverKill();
  is_rom_loaded = 0;
  GameInfo = 0;

  g_config->getOption("SDL.Sound.RecordFile", &filename);
  if (filename.size())
  {
    FCEUI_EndWaveRecord();
  }

  InputUserActiveFix();
  return (1);
}

void FCEUD_Update(uint8 *XBuf, int32 *Buffer, int Count);

static void DoFun(int fskip)
{
  uint8 *gfx;
  int32 *sound;
  int32 ssize;
  extern uint8 PAL;
  int done = 0, timer = 0, ticks = 0, tick = 0, fps = 0;
  unsigned int frame_limit = 60, frametime = 16667;

  while (GameInfo)
  {
    /* Frameskip decision based on the audio buffer */
    if (!fps_throttle)
    {
      // Fill up the audio buffer with up to 6 frames dropped.
      int FramesSkipped = 0;
      while (GameInfo
             && GetBufferedSound() < GetBufferSize() * 3 / 2
             && ++FramesSkipped < 6)
      {
        FCEUI_Emulate(&gfx, &sound, &ssize, 1);
        FCEUD_Update(NULL, sound, ssize);
      }

      // Force at least one frame to be displayed.
      if (GameInfo)
      {
        FCEUI_Emulate(&gfx, &sound, &ssize, 0);
        FCEUD_Update(gfx, sound, ssize);
      }

      // Then render all frames while audio is sufficient.
      while (GameInfo
             && GetBufferedSound() > GetBufferSize() * 3 / 2)
      {
        FCEUI_Emulate(&gfx, &sound, &ssize, 0);
        FCEUD_Update(gfx, sound, ssize);
      }
    }
    else
    {
      FCEUI_Emulate(&gfx, &sound, &ssize, 0);
      FCEUD_Update(gfx, sound, ssize);
    }
  }

}

/**
 * Initialize all of the subsystem drivers: video, audio, and joystick.
 */
static int DriverInitialize(FCEUGI *gi)
{
  if (InitVideo(gi) < 0)
    return 0;
  init_state |= 4;

  if (InitSound())
    init_state |= 1;

  if (InitJoysticks())
    init_state |= 2;

  //int fourscore = 0;
  //g_config->getOption("SDL.FourScore", &fourscore);
  //eoptions &= ~EO_FOURSCORE;
  //if (fourscore)
  //eoptions |= EO_FOURSCORE;

  InitInputInterface();

  FCEUGUI_Reset(gi);
  return 1;
}

/**
 * Resets sound and video subsystem drivers.
 */
int FCEUD_DriverReset()
{
  // Save game config file
  g_config->save(FCEU_MakeFName(FCEUMKF_CFG, 0, 0));

#ifdef FRAMESKIP
  // Update frameskip value
  g_config->getOption("SDL.Frameskip", &frameskip);
#endif

  // Kill drivers first
  if (init_state & 4)
    KillVideo();
  if (init_state & 1)
    KillSound();

  if (InitVideo(GameInfo) < 0)
    return 0;
  init_state |= 4;

  if (InitSound())
    init_state |= 1;

  // Set mouse cursor's movement speed
  //g_config->getOption("SDL.MouseSpeed", &mousespeed);
  //g_config->getOption("SDL.ShowMouseCursor", &showmouse);

  // Set showfps variable and throttle
  g_config->getOption("SDL.ShowFPS", &show_fps);
  g_config->getOption("SDL.FPSThrottle", &fps_throttle);

  return 1;
}

/**
 * Shut down all of the subsystem drivers: video, audio, and joystick.
 */
static void DriverKill(void)
{
  // Save only game config file
  g_config->save(FCEU_MakeFName(FCEUMKF_CFG, 0, 0));

  if (init_state & 2)
    KillJoysticks();
  if (init_state & 4)
    KillVideo();
  if (init_state & 1)
    KillSound();
  init_state = 0;
}

/**
 * Update the video, audio, and input subsystems with the provided
 * video (XBuf) and audio (Buffer) information.
 */
void FCEUD_Update(uint8 *XBuf, int32 *Buffer, int Count)
{
  // Write the audio before the screen, because writing the screen induces
  // a delay after double-buffering.
  if (Count) WriteSound(Buffer, Count);

  if (XBuf && (init_state & 4)) BlitScreen(XBuf);

  FCEUD_UpdateInput();
}

/**
 * Opens a file to be read a byte at a time.
 */
EMUFILE_FILE* FCEUD_UTF8_fstream(const char *fn, const char *m)
{
  std::ios_base::openmode mode = std::ios_base::binary;
  if(!strcmp(m,"r") || !strcmp(m,"rb"))
    mode |= std::ios_base::in;
  else if(!strcmp(m,"w") || !strcmp(m,"wb"))
    mode |= std::ios_base::out | std::ios_base::trunc;
  else if(!strcmp(m,"a") || !strcmp(m,"ab"))
    mode |= std::ios_base::out | std::ios_base::app;
  else if(!strcmp(m,"r+") || !strcmp(m,"r+b"))
    mode |= std::ios_base::in | std::ios_base::out;
  else if(!strcmp(m,"w+") || !strcmp(m,"w+b"))
    mode |= std::ios_base::in | std::ios_base::out | std::ios_base::trunc;
  else if(!strcmp(m,"a+") || !strcmp(m,"a+b"))
    mode |= std::ios_base::in | std::ios_base::out | std::ios_base::app;
  return new EMUFILE_FILE(fn, m);
}


/**
 * Opens a file, C++ style, to be read a byte at a time.
 */
FILE *FCEUD_UTF8fopen(const char *fn, const char *mode)
{
  return (fopen(fn, mode));
}

static char *s_linuxCompilerString = "g++ " __VERSION__;
/**
 * Returns the compiler string.
 */
const char *FCEUD_GetCompilerString()
{
  return (const char *) s_linuxCompilerString;
}

/**
 * Unimplemented.
 */
void FCEUD_DebugBreakpoint()
{
  return;
}

/**
 * Unimplemented.
 */
void FCEUD_TraceInstruction()
{
  return;
}

/**
 * Convert FCM movie to FM2 .
 * Returns 1 on success, otherwise 0.
 */
int FCEUD_ConvertMovie(const char *name, char *outname)
{
  int okcount = 0;
  std::string infname = name;

  // produce output filename
  std::string tmp;
  size_t dot = infname.find_last_of(".");
  if (dot == std::string::npos)
    tmp = infname + ".fm2";
  else
    tmp = infname.substr(0, dot) + ".fm2";

  MovieData md;
  EFCM_CONVERTRESULT result = convert_fcm(md, infname);

  if (result == FCM_CONVERTRESULT_SUCCESS)
  {
    okcount++;
    EMUFILE_FILE* outf = FCEUD_UTF8_fstream(tmp, "wb");
    md.dump(outf, false);
    delete outf;
    printf("Your file has been converted to FM2.\n");

    strncpy(outname, tmp.c_str(), 128);
    return 1;
  }
  else
  {
    printf("Something went wrong while converting your file...\n");
    return 0;
  }

  return 0;
}

/*
 * Loads a movie, if fcm movie will convert first.  And will
 * ask for rom too, returns 1 on success, 0 otherwise.
 */
int FCEUD_LoadMovie(const char *name, char *romname)
{
  std::string s = std::string(name);

  // Convert to fm2 if necessary ...
  if (s.find(".fcm") != std::string::npos)
  {
    char tmp[128];
    if (!FCEUD_ConvertMovie(name, tmp))
      return 0;
    s = std::string(tmp);
  }

  if (s.find(".fm2") != std::string::npos)
  {
    // WARNING: Must load rom first
    // Launch file browser to search for movies's rom file.
    const char *types[] = { ".nes", ".fds", ".zip", NULL };
    if (!RunFileBrowser(NULL, romname, types, "Select movie's rom file"))
    {
      printf("WARNING: Must load a rom to play the movie! %s\n",
             s.c_str());
      return 0;
    }

    if (LoadGame(romname) != 1)
      return -1;

    static int pauseframe;
    g_config->getOption("SDL.PauseFrame", &pauseframe);
    g_config->setOption("SDL.PauseFrame", 0);
    printf("Playing back movie located at %s\n", s.c_str());
    FCEUI_LoadMovie(s.c_str(), false, pauseframe ? pauseframe : false);
  }
  else
  {
    // Must be a rom file ...
    return 0;
  }

  return 1;
}

int main(int argc, char *argv[])
{
  int error;

  printf("\nStarting "FCEU_NAME_AND_VERSION"...\n");
  if(SDL_Init(SDL_INIT_VIDEO))
  {
    printf("Could not initialize SDL: %s.\n", SDL_GetError());
    return(-1);
  }

  // Initialize the configuration system
  g_config = InitConfig();

  if (!g_config)
  {
    SDL_Quit();
    return -1;
  }

  // Initialize the fceu320 gui
  FCEUGUI_Init(NULL);

  // initialize the infrastructure
  error = FCEUI_Initialize();

  if (error != 1)
  {
    SDL_Quit();
    return -1;
  }

  int romIndex = g_config->parse(argc, argv);

  // This is here so that a default fceux.cfg will be created on first
  // run, even without a valid ROM to play.
  // Unless, of course, there's actually --no-config given
  // mbg 8/23/2008 - this is also here so that the inputcfg routines can
  // have a chance to dump the new inputcfg to the fceux.cfg
  // in case you didnt specify a rom filename
  //g_config->getOption("SDL.NoConfig", &noconfig);
  //if (!noconfig)
  //g_config->save();

  std::string s;
  g_config->getOption("SDL.InputCfg", &s);

  // update the input devices
  UpdateInput(g_config);

  // check for a .fcm file to convert to .fm2
  g_config->getOption("SDL.FCMConvert", &s);
  g_config->setOption("SDL.FCMConvert", "");

  if (!s.empty())
  {
    int okcount = 0;
    std::string infname = s.c_str();
    // produce output filename
    std::string outname;
    size_t dot = infname.find_last_of(".");
    if (dot == std::string::npos)
      outname = infname + ".fm2";
    else
      outname = infname.substr(0, dot) + ".fm2";

    MovieData md;
    EFCM_CONVERTRESULT result = convert_fcm(md, infname);

    if (result == FCM_CONVERTRESULT_SUCCESS)
    {
      okcount++;
      EMUFILE_FILE* outf = FCEUD_UTF8_fstream(outname, "wb");
      md.dump(outf, false);
      delete outf;
      printf("Your file has been converted to FM2.\n");
    }
    else
      printf("Something went wrong while converting your file...\n");

    DriverKill();
    SDL_Quit();
    return 0;
  }

  // check to see if movie messages are disabled
  int mm;
  g_config->getOption("SDL.MovieMsg", &mm);
  FCEUI_SetAviDisableMovieMessages(mm == 0);

  // check for a .fm2 file to rip the subtitles
  g_config->getOption("SDL.RipSubs", &s);
  g_config->setOption("SDL.RipSubs", "");
  if (!s.empty())
  {
    MovieData md;
    std::string infname;
    infname = s.c_str();
    FCEUFILE *fp = FCEU_fopen(s.c_str(), 0, "rb", 0);

    // load the movie and and subtitles
    extern bool LoadFM2(MovieData&, EMUFILE*, int, bool);
    LoadFM2(md, fp->stream, INT_MAX, false);
    LoadSubtitles(md); // fill subtitleFrames and subtitleMessages
    delete fp;

    // produce .srt file's name and open it for writing
    std::string outname;
    size_t dot = infname.find_last_of(".");
    if (dot == std::string::npos)
      outname = infname + ".srt";
    else
      outname = infname.substr(0, dot) + ".srt";
    FILE *srtfile;
    srtfile = fopen(outname.c_str(), "w");

    if (srtfile != NULL)
    {
      extern std::vector<int> subtitleFrames;
      extern std::vector<std::string> subtitleMessages;
      float fps = (md.palFlag == 0 ? 60.0988 : 50.0069); // NTSC vs PAL
      float subduration = 3; // seconds for the subtitles to be displayed
      for (int i = 0; i < subtitleFrames.size(); i++)
      {
        fprintf(srtfile, "%i\n", i + 1); // starts with 1, not 0
        double seconds, ms, endseconds, endms;
        seconds = subtitleFrames[i] / fps;
        if (i + 1 < subtitleFrames.size())   // there's another subtitle coming after this one
        {
          if (subtitleFrames[i + 1] - subtitleFrames[i] < subduration
              * fps)   // avoid two subtitles at the same time
          {
            endseconds = (subtitleFrames[i + 1] - 1) / fps; // frame x: subtitle1; frame x+1 subtitle2
          }
          else
            endseconds = seconds + subduration;
        }
        else
          endseconds = seconds + subduration;

        ms = modf(seconds, &seconds);
        endms = modf(endseconds, &endseconds);
        // this is just beyond ugly, don't show it to your kids
        fprintf(
          srtfile,
          "%02.0f:%02d:%02d,%03d --> %02.0f:%02d:%02d,%03d\n", // hh:mm:ss,ms --> hh:mm:ss,ms
          floor(seconds / 3600), (int) floor(seconds / 60) % 60,
          (int) floor(seconds) % 60, (int) (ms * 1000), floor(
            endseconds / 3600),
          (int) floor(endseconds / 60) % 60, (int) floor(
            endseconds) % 60, (int) (endms * 1000));
        fprintf(srtfile, "%s\n\n", subtitleMessages[i].c_str()); // new line for every subtitle
      }
      fclose(srtfile);
      printf("%d subtitles have been ripped.\n",
             (int) subtitleFrames.size());
    }
    else
      printf("Couldn't create output srt file...\n");

    DriverKill();
    SDL_Quit();
    return 0;
  }

  /*
   // if there is no rom specified launch the file browser
   if(romIndex <= 0) {
   ShowUsage(argv[0]);
   FCEUD_Message("\nError parsing command line arguments\n");
   SDL_Quit();
   return -1;
   }
   */

  // update the emu core
  UpdateEMUCore(g_config);

  {
    int id;
    g_config->getOption("SDL.InputDisplay", &id);
    extern int input_display;
    input_display = id;
    // not exactly an id as an true/false switch; still better than creating another int for that
    g_config->getOption("SDL.SubtitleDisplay", &id);
    extern int movieSubtitles;
    movieSubtitles = id;
  }

  // load the hotkeys from the config life
  setHotKeys();

  if (romIndex >= 0)
  {
    // load the specified game
    error = LoadGame(argv[romIndex]);
    if (error != 1)
    {
      DriverKill();
      SDL_Quit();
      return -1;
    }
  }
  else
  {
    // Launch file browser
    const char *types[] = { ".nes", ".fds", ".zip", ".fcm", ".fm2", ".nsf",
                            NULL
                          };
    char filename[128], romname[128];

    InitVideo(0);
    init_state |= 4; // Hack to init video mode before running gui

    if (!RunFileBrowser(NULL, filename, types))
    {
      DriverKill();
      SDL_Quit();
      return -1;
    }

    // Is this a movie?
    if (!(error = FCEUD_LoadMovie(filename, romname)))
      error = LoadGame(filename);

    if (error != 1)
    {
      DriverKill();
      SDL_Quit();
      return -1;
    }
  }

  // movie playback
  g_config->getOption("SDL.Movie", &s);
  g_config->setOption("SDL.Movie", "");
  if (s != "")
  {
    if (s.find(".fm2") != std::string::npos)
    {
      static int pauseframe;
      g_config->getOption("SDL.PauseFrame", &pauseframe);
      g_config->setOption("SDL.PauseFrame", 0);
      printf("Playing back movie located at %s\n", s.c_str());
      FCEUI_LoadMovie(s.c_str(), false, pauseframe ? pauseframe : false);
    }
    else
      printf("Sorry, I don't know how to play back %s\n", s.c_str());
  }

  {
    int id;
    g_config->getOption("SDL.NewPPU", &id);
    if (id)
      newppu = 1;
  }

  g_config->getOption("SDL.Frameskip", &frameskip);

  // loop playing the game
  DoFun(frameskip);

  CloseGame();

  // exit the infrastructure
  FCEUI_Kill();
  SDL_Quit();
  return 0;
}

/**
 * Get the time in ticks.
 */
uint64 FCEUD_GetTime()
{
  return SDL_GetTicks();
}

/**
 * Get the tick frequency in Hz.
 */
uint64 FCEUD_GetTimeFreq(void)
{
  return 1000;
}

/**
 * Shows an error message in a message box.
 * (For now: prints to stderr.)
 *
 * @param errormsg Text of the error message.
 **/
void FCEUD_PrintError(const char *errormsg)
{
  fprintf(stderr, "%s\n", errormsg);
}

// dummy functions

#define DUMMY(__f) void __f(void) {printf("%s\n", #__f); FCEU_DispMessage("Not implemented.", 0);}
DUMMY(FCEUD_HideMenuToggle)
DUMMY(FCEUD_MovieReplayFrom)
DUMMY(FCEUD_ToggleStatusIcon)
DUMMY(FCEUD_AviRecordTo)
DUMMY(FCEUD_AviStop)
void FCEUI_AviVideoUpdate(const unsigned char* buffer)
{
}
int FCEUD_ShowStatusIcon(void)
{
  return 0;
}
bool FCEUI_AviIsRecording(void)
{
  return false;
}
void FCEUI_UseInputPreset(int preset)
{
}
bool FCEUD_PauseAfterPlayback()
{
  return false;
}
// These are actually fine, but will be unused and overriden by the current UI code.
void FCEUD_TurboOn(void)
{
  NoWaiting |= 1;
}
void FCEUD_TurboOff(void)
{
  NoWaiting &= ~1;
}
void FCEUD_TurboToggle(void)
{
  NoWaiting ^= 1;
}
FCEUFILE* FCEUD_OpenArchiveIndex(ArchiveScanRecord& asr, std::string &fname,
                                 int innerIndex)
{
  return 0;
}
FCEUFILE* FCEUD_OpenArchive(ArchiveScanRecord& asr, std::string& fname,
                            std::string* innerFilename)
{
  return 0;
}
ArchiveScanRecord FCEUD_ScanArchive(std::string fname)
{
  return ArchiveScanRecord();
}

extern uint8 SelectDisk, InDisk;
extern int FDSSwitchRequested;

void FCEUI_FDSFlip(void)
{
  /* Taken from fceugc
     the commands shouldn't be issued in parallel so
   * we'll delay them so the virtual FDS has a chance
   * to process them
  */
  if(FDSSwitchRequested == 0)
    FDSSwitchRequested = 1;
}

bool enableHUDrecording = false;
bool FCEUI_AviEnableHUDrecording()
{
  if (enableHUDrecording)
    return true;

  return false;
}
