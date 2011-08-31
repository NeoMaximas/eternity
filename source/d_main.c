// Emacs style mode select   -*- C -*- vi:ts=3 sw=3:
//-----------------------------------------------------------------------------
//
// Copyright(C) 2000 James Haley
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
//--------------------------------------------------------------------------
//
// DESCRIPTION:
//  DOOM main program (D_DoomMain) and game loop, plus functions to
//  determine game mode (shareware, registered), parse command line
//  parameters, configure game parameters (turbo), and call the startup
//  functions.
//
//-----------------------------------------------------------------------------

#ifndef LINUX
#include <sys/types.h>
#endif
#include <sys/stat.h>
#include <fcntl.h>

// haleyjd 10/28/04: Win32-specific repair for D_DoomExeDir
// haleyjd 08/20/07: POSIX opendir needed for autoload functionality
#ifdef _MSC_VER
#include "Win32/i_fnames.h"
#include "Win32/i_opndir.h"
#else
#include <dirent.h>
#endif

#include "z_zone.h"

#include "d_io.h"  // SoM 3/12/2002: moved unistd stuff into d_io.h
#include "doomdef.h"
#include "doomstat.h"
#include "dstrings.h"
#include "d_diskfile.h"
#include "sounds.h"
#include "c_runcmd.h"
#include "c_io.h"
#include "c_net.h"
#include "w_wad.h"
#include "s_sound.h"
#include "v_misc.h"
#include "v_video.h"
#include "f_finale.h"
#include "f_wipe.h"
#include "m_argv.h"
#include "m_misc.h"
#include "mn_engin.h"
#include "i_system.h"
#include "i_sound.h"
#include "i_video.h"
#include "g_game.h"
#include "hu_stuff.h"
#include "st_stuff.h"
#include "am_map.h"
#include "p_setup.h"
#include "p_chase.h"
#include "p_info.h"
#include "r_draw.h"
#include "r_main.h"
#include "d_main.h"
#include "d_deh.h"  // Ty 04/08/98 - Externalizations
#include "g_bind.h" // haleyjd
#include "d_dialog.h"
#include "d_gi.h"
#include "in_lude.h"
#include "a_small.h"
#include "acs_intr.h"
#include "g_gfs.h"
#include "g_dmflag.h"
#include "e_edf.h"
#include "e_player.h"
#include "e_fonts.h"

// [CG] Added.

#include "cs_config.h"
#include "cs_hud.h"
#include "cs_netid.h"
#include "cs_main.h"
#include "cs_master.h"
#include "cs_team.h"
#include "cs_demo.h"
#include "cs_wad.h"
#include "cl_main.h"
#include "sv_main.h"

// haleyjd 11/09/09: wadfiles made a structure.
// note: needed extern in g_game.c
wfileadd_t *wadfiles;

// killough 10/98: preloaded files
#define MAXLOADFILES 2
char *wad_files[MAXLOADFILES], *deh_files[MAXLOADFILES];
// haleyjd: allow two auto-loaded console scripts
char *csc_files[MAXLOADFILES];

int textmode_startup = 0;  // sf: textmode_startup for old-fashioned people
int use_startmap = -1;     // default to -1 for asking in menu
boolean devparm;           // started game with -devparm

// jff 1/24/98 add new versions of these variables to remember command line
boolean clnomonsters;   // checkparm of -nomonsters
boolean clrespawnparm;  // checkparm of -respawn
boolean clfastparm;     // checkparm of -fast
// jff 1/24/98 end definition of command line version of play mode switches

int r_blockmap = false;       // -blockmap command line

boolean nomonsters;     // working -nomonsters
boolean respawnparm;    // working -respawn
boolean fastparm;       // working -fast

boolean singletics = false; // debug flag to cancel adaptiveness

//jff 1/22/98 parms for disabling music and sound
boolean nosfxparm;
boolean nomusicparm;

//jff 4/18/98
extern boolean inhelpscreens;

skill_t startskill;
int     startepisode;
int     startmap;
char    *startlevel;
boolean autostart;

boolean advancedemo;

extern boolean timingdemo, singledemo, demoplayback, fastdemo; // killough

char    *basedefault = NULL;      // default file
char    *baseiwad = NULL;         // jff 3/23/98: iwad directory
char    *basesavegame = NULL;     // killough 2/16/98: savegame directory

char    *basepath;                // haleyjd 11/23/06: path of "base" directory
char    *basegamepath = NULL;     // haleyjd 11/23/06: path of game directory

// set from iwad: level to start new games from
char firstlevel[9] = "";

void D_CheckNetGame(void);
void D_ProcessEvents(void);
void G_BuildTiccmd(ticcmd_t* cmd);
void D_DoAdvanceDemo(void);

//sf:
void startupmsg(char *func, char *desc)
{
   // add colours in console mode
   usermsg(in_textmode ? "%s: %s" : FC_HI "%s: " FC_NORMAL "%s",
           func, desc);
}

//=============================================================================
//
// EVENT HANDLING
//
// Events are asynchronous inputs generally generated by the game user.
// Events can be discarded if no responder claims them
//

event_t events[MAXEVENTS];
int eventhead, eventtail;

//
// D_PostEvent
// Called by the I/O functions when input is detected
//
void D_PostEvent(event_t *ev)
{
   events[eventhead++] = *ev;
   eventhead &= MAXEVENTS-1;
}

//
// D_ProcessEvents
// Send all the events of the given timestamp down the responder chain
//
void D_ProcessEvents(void)
{
   // IF STORE DEMO, DO NOT ACCEPT INPUT
   // sf: I don't think SMMU is going to be played in any store any
   //     time soon =)
   // if (gamemode != commercial || W_CheckNumForName("map01") >= 0)

   for(; eventtail != eventhead; eventtail = (eventtail+1) & (MAXEVENTS-1))
   {
      event_t *evt = events + eventtail;

      // [CG] Headless servers don't use either the menu or graphical console,
      //      so just skip right to G_Responder in this case.
      if(CS_HEADLESS)
      {
         G_Responder(evt);
      }
      else
      {
         if(!MN_Responder(evt))
            if(!C_Responder(evt))
               G_Responder(evt);
      }
   }
}

//=============================================================================
//
// Display
//
// All drawing starts here.
//

// wipegamestate can be set to -1 to force a wipe on the next draw

gamestate_t    oldgamestate = -1;  // sf: globaled
gamestate_t    wipegamestate = GS_DEMOSCREEN;
void           R_ExecuteSetViewSize(void);
camera_t       *camera;
extern boolean setsizeneeded;
boolean        redrawsbar;      // sf: globaled
boolean        redrawborder;    // sf: cleaned up border redraw
int            wipewait;        // haleyjd 10/09/07

boolean        d_drawfps;       // haleyjd 09/07/10: show drawn fps

//
// D_showFPS
//
static void D_showDrawnFPS(void)
{
   static unsigned int lastms, accms, frames;
   unsigned int curms;
   static int lastfps;
   vfont_t *font;
   char msg[64];

   accms += (curms = I_GetTicks()) - lastms;
   lastms = curms;
   ++frames;

   if(accms >= 1000)
   {
      lastfps = frames * 1000 / accms;
      frames = 0;
      accms -= 1000;
   }

   font = E_FontForName("ee_smallfont");
   psnprintf(msg, 64, "DFPS: %d", lastfps);
   V_FontWriteText(font, msg, 5, 20);
}

//
// D_Display
//  draw current display, possibly wiping it from the previous
//
void D_Display(void)
{
   if(nodrawers)                // for comparative timing / profiling
      return;

   if(setsizeneeded)            // change the view size if needed
   {
      R_ExecuteSetViewSize();
      R_FillBackScreen();       // redraw backscreen
   }

   // save the current screen if about to wipe
   // no melting consoles
   if(gamestate != wipegamestate &&
      !(wipegamestate == GS_CONSOLE && gamestate != GS_LEVEL))
      Wipe_StartScreen();

   if(inwipe || c_moving || menuactive)
      redrawsbar = redrawborder = true;   // redraw status bar and border

   // haleyjd: optimization for fullscreen menu drawing -- no
   // need to do all this if the menus are going to cover it up :)
   if(!MN_CheckFullScreen())
   {
      switch(gamestate)                // do buffered drawing
      {
      case GS_LEVEL:
         // see if the border needs to be initially drawn
         if(oldgamestate != GS_LEVEL)
            R_FillBackScreen();    // draw the pattern into the back screen
         HU_Erase();

         if(automapactive)
         {
            AM_Drawer();
         }
         else
         {
            // see if the border needs to be updated to the screen
            if(redrawborder)
               R_DrawViewBorder();    // redraw border
            R_RenderPlayerView (&players[displayplayer], camera);
         }

         ST_Drawer(scaledviewheight == 200, redrawsbar);  // killough 11/98
         HU_Drawer();
         break;
      case GS_INTERMISSION:
         IN_Drawer();
         if(clientserver)
         {
            CS_DrawChatWidget();
         }
         break;
      case GS_FINALE:
         F_Drawer();
         break;
      case GS_DEMOSCREEN:
         D_PageDrawer();
         break;
      case GS_CONSOLE:
         break;
      default:
         break;
      }

      redrawsbar = false; // reset this now
      redrawborder = false;

      // clean up border stuff
      if(gamestate != oldgamestate && gamestate != GS_LEVEL)
         I_SetPalette(W_CacheLumpName("PLAYPAL", PU_CACHE));

      oldgamestate = wipegamestate = gamestate;

      // draw pause pic
      if(paused && !walkcam_active) // sf: not if walkcam active for
      {                             // frads taking screenshots
         const char *lumpname = GameModeInfo->pausePatch;

         // haleyjd 03/12/03: changed to work
         // in heretic, and with user pause patches
         patch_t *patch = (patch_t *)W_CacheLumpName(lumpname, PU_CACHE);
         int width = SwapShort(patch->width);
         int x = (SCREENWIDTH - width) / 2 + patch->leftoffset;
         // SoM 2-4-04: ANYRES
         int y = 4 + (automapactive ? 0 : scaledwindowy);

         V_DrawPatch(x, y, &vbscreen, patch);
      }

      if(inwipe)
      {
         boolean wait = (wipewait == 1 || (wipewait == 2 && demoplayback));

         // about to start wiping; if wipewait is enabled, save everything
         // that was just drawn
         if(wait)
         {
            Wipe_SaveEndScreen();

            do
            {
               int starttime = I_GetTime();
               int tics = 0;

               Wipe_Drawer();

               do
               {
                  tics = I_GetTime() - starttime;

                  // haleyjd 06/16/09: sleep to avoid hogging 100% CPU
                  I_Sleep(1);
               }
               while(!tics);

               Wipe_Ticker();

               C_Drawer();
               MN_Drawer();
               NetUpdate();
               if(v_ticker)
                  V_FPSDrawer();
               I_FinishUpdate();

               if(inwipe)
                  Wipe_BlitEndScreen();
            }
            while(inwipe);
         }
         else
            Wipe_Drawer();
      }

      C_Drawer();

   } // if(!MN_CheckFullScreen())

   // menus go directly to the screen
   MN_Drawer();         // menu is drawn even on top of everything
   NetUpdate();         // send out any new accumulation

   //sf : now system independent
   if(v_ticker)
      V_FPSDrawer();

   if(d_drawfps)
      D_showDrawnFPS();

   // sf: wipe changed: runs alongside the rest of the game rather
   //     than in its own loop

   I_FinishUpdate();              // page flip or blit buffer
}

//=============================================================================
//
//  DEMO LOOP
//

static int demosequence;         // killough 5/2/98: made static
static int pagetic;
static const char *pagename;

//
// D_PageTicker
// Handles timing for warped projection
//
void D_PageTicker(void)
{
   // killough 12/98: don't advance internal demos if a single one is
   // being played. The only time this matters is when using -loadgame with
   // -fastdemo, -playdemo, or -timedemo, and a consistency error occurs.

   if (/*!singledemo &&*/ --pagetic < 0)
      D_AdvanceDemo();
}

//
// D_PageDrawer
//
// killough 11/98: add credits screen
//
void D_PageDrawer(void)
{
   int l;
   byte *t;

   if(pagename && (l = W_CheckNumForName(pagename)) != -1)
   {
      t = W_CacheLumpNum(l, PU_CACHE);

      // haleyjd 08/15/02: handle Heretic pages
      V_DrawFSBackground(&vbscreen, t, W_LumpLength(l));

      if(GameModeInfo->flags & GIF_HASADVISORY && demosequence == 1)
      {
         l = W_GetNumForName("ADVISOR");
         t = W_CacheLumpNum(l, PU_CACHE);
         V_DrawPatch(4, 160, &vbscreen, (patch_t *)t);
      }
   }
   else
      MN_DrawCredits();
}

//
// D_AdvanceDemo
// Called after each demo or intro demosequence finishes
//
void D_AdvanceDemo(void)
{
   advancedemo = true;
}

// killough 11/98: functions to perform demo sequences

static void D_SetPageName(const char *name)
{
   pagename = name;
}

static void D_DrawTitle(const char *name)
{
   S_StartMusic(GameModeInfo->titleMusNum);
   pagetic = GameModeInfo->titleTics;

   if(GameModeInfo->missionInfo->flags & MI_CONBACKTITLE)
      D_SetPageName(GameModeInfo->consoleBack);
   else
      D_SetPageName(name);
}

static void D_DrawTitleA(const char *name)
{
   pagetic = GameModeInfo->advisorTics;
   D_SetPageName(name);
}

// killough 11/98: tabulate demo sequences

const demostate_t demostates_doom[] =
{
   { D_DrawTitle,       "TITLEPIC" }, // shareware, registered
   { G_DeferedPlayDemo, "DEMO1"    },
   { D_SetPageName,     NULL       },
   { G_DeferedPlayDemo, "DEMO2"    },
   { D_SetPageName,     "HELP2"    },
   { G_DeferedPlayDemo, "DEMO3"    },
   { NULL }
};

const demostate_t demostates_doom2[] =
{
   { D_DrawTitle,       "TITLEPIC" }, // commercial
   { G_DeferedPlayDemo, "DEMO1"    },
   { D_SetPageName,     NULL       },
   { G_DeferedPlayDemo, "DEMO2"    },
   { D_SetPageName,     "CREDIT"   },
   { G_DeferedPlayDemo, "DEMO3"    },
   { NULL }
};

const demostate_t demostates_udoom[] =
{
   { D_DrawTitle,       "TITLEPIC" }, // retail
   { G_DeferedPlayDemo, "DEMO1"    },
   { D_SetPageName,     NULL       },
   { G_DeferedPlayDemo, "DEMO2"    },
   { D_DrawTitle,       "TITLEPIC" },
   { G_DeferedPlayDemo, "DEMO3"    },
   { D_SetPageName,     "CREDIT"   },
   { G_DeferedPlayDemo, "DEMO4"    },
   { NULL }
};

const demostate_t demostates_hsw[] =
{
   { D_DrawTitle,       "TITLE" }, // heretic shareware
   { D_DrawTitleA,      "TITLE" },
   { G_DeferedPlayDemo, "DEMO1" },
   { D_SetPageName,     "ORDER" },
   { G_DeferedPlayDemo, "DEMO2" },
   { D_SetPageName,     NULL    },
   { G_DeferedPlayDemo, "DEMO3" },
   { NULL }
};

const demostate_t demostates_hreg[] =
{
   { D_DrawTitle,       "TITLE"  }, // heretic registered/sosr
   { D_DrawTitleA,      "TITLE"  },
   { G_DeferedPlayDemo, "DEMO1"  },
   { D_SetPageName,     "CREDIT" },
   { G_DeferedPlayDemo, "DEMO2"  },
   { D_SetPageName,     NULL     },
   { G_DeferedPlayDemo, "DEMO3"  },
   { NULL }
};

const demostate_t demostates_unknown[] =
{
   { D_SetPageName, NULL }, // indetermined - haleyjd 04/01/08
   { NULL }
};

//
// D_DoAdvanceDemo
//
// This cycles through the demo sequences.
// killough 11/98: made table-driven
//
void D_DoAdvanceDemo(void)
{
   const demostate_t *demostates = GameModeInfo->demoStates;
   const demostate_t *state;

   players[consoleplayer].playerstate = PST_LIVE;  // not reborn
   advancedemo = usergame = paused = false;
   gameaction = ga_nothing;

   pagetic = GameModeInfo->pageTics;
   gamestate = GS_DEMOSCREEN;

   // haleyjd 10/08/06: changed to allow DEH/BEX replacement of
   // demo state resource names
   state = &(demostates[++demosequence]);

   if(!state->func) // time to wrap?
   {
      demosequence = 0;
      state = &(demostates[0]);
   }

   state->func(DEH_String(state->name));

   C_InstaPopup();       // make console go away
}

//
// D_StartTitle
//
void D_StartTitle(void)
{
   gameaction = ga_nothing;
   demosequence = -1;
   D_AdvanceDemo();
}

//=============================================================================
//
// WAD File Loading
//

static int numwadfiles, numwadfiles_alloc;

//
// D_AddFile
//
// Rewritten by Lee Killough
//
// killough 11/98: remove limit on number of files
// haleyjd 05/28/10: added f and baseoffset parameters for subfile support.
//
void D_AddFile(const char *file, int li_namespace, FILE *fp, size_t baseoffset,
               int privatedir)
{
   // sf: allocate for +2 for safety
   if(numwadfiles + 2 >= numwadfiles_alloc)
   {
      numwadfiles_alloc = numwadfiles_alloc ? numwadfiles_alloc * 2 : 8;

      wadfiles = realloc(wadfiles, numwadfiles_alloc * sizeof(*wadfiles));
   }

   wadfiles[numwadfiles].filename     = strdup(file);
   wadfiles[numwadfiles].li_namespace = li_namespace;
   wadfiles[numwadfiles].f            = fp;
   wadfiles[numwadfiles].baseoffset   = baseoffset;
   wadfiles[numwadfiles].privatedir   = privatedir;

   wadfiles[numwadfiles+1].filename = NULL; // sf: always NULL at end

   numwadfiles++;
}

//sf: console command to list loaded files
void D_ListWads(void)
{
   int i;
   C_Printf(FC_HI "Loaded WADs:\n");

   for(i = 0; i < numwadfiles; i++)
      C_Printf("%s\n", wadfiles[i].filename);
}

//=============================================================================
//
// EXE Path / Name Functions
//

//
// D_DoomExeDir
//
// Return the path where the executable lies -- Lee Killough
//
// FIXME: call an I_DoomExeDir function to get rid of #ifndef
//
char *D_DoomExeDir(void)
{
   static char *base = NULL;

   if(!base) // cache multiple requests
   {
#ifndef _MSC_VER

      size_t len = strlen(myargv[0]) + 1;

      base = malloc(len);

      // haleyjd 03/09/03: generalized
      M_GetFilePath(myargv[0], base, len);
#else
      // haleyjd 10/28/04: the above is not sufficient for all versions
      // of Windows. There is an API function which takes care of this,
      // however.  See i_fnames.c in the Win32 subdirectory.
      base = malloc(PATH_MAX + 1);

      WIN_GetExeDir(base, PATH_MAX + 1);
#endif
   }

   return base;
}

//
// D_DoomExeName
//
// killough 10/98: return the name of the program the exe was invoked as
//
char *D_DoomExeName(void)
{
   static char *name;    // cache multiple requests

   if(!name)
   {
      char *p = myargv[0] + strlen(myargv[0]);
      int i = 0;

      while(p > myargv[0] && p[-1] != '/' && p[-1] != '\\' && p[-1] != ':')
         p--;
      while(p[i] && p[i] != '.')
         i++;

      name = calloc(1, i + 1);

      strncpy(name, p, i);
   }
   return name;
}

//=============================================================================
//
// Base and Game Path Determination Code
//

//
// D_ExpandTilde
//     expand tilde in base path name for linux home dir
//
static char *D_ExpandTilde(char *basedir)
{
   if(basedir[0] == '~')
   {
      char *home = strdup(getenv("HOME"));
      char *newalloc = NULL;

      M_StringAlloca(&newalloc, 2, 0, home, basedir);

      strcpy(newalloc, home);
      strcpy(newalloc + strlen(home), basedir + 1);

      if(home)
         free(home);

      return newalloc;
   }

   return Z_Strdupa(basedir);
}

// return codes for D_CheckBasePath
enum
{
   BASE_ISGOOD,
   BASE_NOTEXIST,
   BASE_NOTDIR,
};

//
// D_CheckBasePath
//
// Checks a provided path to see that it both exists and that it is a directory
// and not a plain file.
//
static int D_CheckBasePath(const char *path)
{
   int ret;
   struct stat sbuf;

   if(!stat(path, &sbuf)) // check for existence
   {
      if(S_ISDIR(sbuf.st_mode)) // check that it's a directory
         ret = BASE_ISGOOD;
      else
         ret = BASE_NOTDIR;
   }
   else
      ret = BASE_NOTEXIST;

   return ret;
}

// basepath sources
enum
{
   BASE_CMDLINE,
   BASE_ENVIRON,
   BASE_WORKING,
   BASE_EXEDIR,
   BASE_NUMBASE
};

//
// D_SetBasePath
//
// haleyjd 11/23/06: Sets the path to the "base" folder, where Eternity stores
// all of its data.
//
static void D_SetBasePath(void)
{
   int p, res = BASE_NOTEXIST, source = BASE_NUMBASE;
   char *s;
   char *basedir = NULL;

   // Priority:
   // 1. Command-line argument "-base"
   // 2. Environment variable "ETERNITYBASE"
   // 3. /base under working directory
   // 4. /base under DoomExeDir

   // check command-line
   if((p = M_CheckParm("-base")) && p < myargc - 1)
   {
      basedir = D_ExpandTilde(myargv[p + 1]);

      if((res = D_CheckBasePath(basedir)) == BASE_ISGOOD)
         source = BASE_CMDLINE;
   }

   // check environment
   if(res != BASE_ISGOOD && (s = getenv("ETERNITYBASE")))
   {
      basedir = D_ExpandTilde(s);

      if((res = D_CheckBasePath(basedir)) == BASE_ISGOOD)
         source = BASE_ENVIRON;
   }

   // check working dir
   if(res != BASE_ISGOOD)
   {
      basedir = Z_Strdupa("./base");

      if((res = D_CheckBasePath(basedir)) == BASE_ISGOOD)
         source = BASE_WORKING;
   }

   // check exe dir
   if(res != BASE_ISGOOD)
   {
      const char *exedir = D_DoomExeDir();

      size_t len = M_StringAlloca(&basedir, 1, 6, exedir);

      psnprintf(basedir, len, "%s/base", D_DoomExeDir());

      if((res = D_CheckBasePath(basedir)) == BASE_ISGOOD)
         source = BASE_EXEDIR;
      else
      {
         // final straw.
         I_Error("D_SetBasePath: base path %s.\n",
                 res == BASE_NOTDIR ? "is not a directory" : "does not exist");
      }
   }

   basepath = strdup(basedir);
   M_NormalizeSlashes(basepath);

   switch(source)
   {
   case BASE_CMDLINE:
      s = "by command line";
      break;
   case BASE_ENVIRON:
      s = "by environment";
      break;
   case BASE_WORKING:
      s = "to working directory";
      break;
   case BASE_EXEDIR:
      s = "to executable directory";
      break;
   default:
      s = "to God only knows what"; // ???
      break;
   }

   printf("Base path set %s.\n", s);
}

// haleyjd 8/18/07: if true, the game path has been set
static boolean gamepathset;

// haleyjd 8/19/07: index of the game name as specified on the command line
static int gamepathparm;

//
// D_CheckGamePathParam
//
// haleyjd 08/18/07: This function checks for the -game command-line parameter.
// If it is set, then its value is saved and gamepathset is asserted.
//
static void D_CheckGamePathParam(void)
{
   int p;
   struct stat sbuf;
   char *gamedir = NULL;

   if((p = M_CheckParm("-game")) && p < myargc - 1)
   {
      size_t len = M_StringAlloca(&gamedir, 2, 2, basepath, myargv[p + 1]);

      psnprintf(gamedir, len, "%s/%s", basepath, myargv[p + 1]);

      gamepathparm = p + 1;

      if(!stat(gamedir, &sbuf)) // check for existence
      {
         if(S_ISDIR(sbuf.st_mode)) // check that it's a directory
         {
            basegamepath = strdup(gamedir);
            M_NormalizeSlashes(basegamepath);
            gamepathset = true;
         }
         else
            I_Error("Game path %s is not a directory.\n", gamedir);
      }
      else
         I_Error("Game path %s does not exist.\n", gamedir);
   }
}

//
// D_SetGamePath
//
// haleyjd 11/23/06: Sets the game path under the base path when the gamemode has
// been determined by the iwad in use.
//
static void D_SetGamePath(void)
{
   struct stat sbuf;
   char *gamedir = NULL;
   size_t len;
   const char *mstr = GameModeInfo->missionInfo->gamePathName;

   len = M_StringAlloca(&gamedir, 2, 2, basepath, mstr);

   psnprintf(gamedir, len, "%s/%s", basepath, mstr);

   if(!stat(gamedir, &sbuf)) // check for existence
   {
      if(S_ISDIR(sbuf.st_mode)) // check that it's a directory
      {
         if(basegamepath != NULL)
         {
            free(basegamepath);
         }
         basegamepath = strdup(gamedir);
         M_NormalizeSlashes(basegamepath);
      }
      else
         I_Error("Game path %s is not a directory.\n", gamedir);
   }
   else
      I_Error("Game path %s does not exist.\n", gamedir);
}

//
// D_CheckGameEDF
//
// Looks for an optional root.edf file in base/game
//
static char *D_CheckGameEDF(void)
{
   struct stat sbuf;
   static char *game_edf;
   size_t len = strlen(basegamepath) + 10;

   game_edf = malloc(len);

   psnprintf(game_edf, len, "%s/root.edf", basegamepath);

   if(!stat(game_edf, &sbuf)) // check for existence
   {
      if(!S_ISDIR(sbuf.st_mode)) // check that it's NOT a directory
         return game_edf;        // return the filename
   }

   return NULL; // return NULL to indicate the file doesn't exist
}

//=============================================================================
//
// Gamepath Autoload Directory Handling
//

// haleyjd 08/20/07: gamepath autload directory structure
static DIR *autoloads;
static char *autoload_dirname;

//
// D_EnumerateAutoloadDir
//
// haleyjd 08/20/07: this function enumerates the base/game/autoload directory.
//
static void D_EnumerateAutoloadDir(void)
{
   if(!autoloads && !M_CheckParm("-noload")) // don't do if -noload is used
   {
      size_t len = strlen(basegamepath) + 10;
      autoload_dirname = malloc(len);

      psnprintf(autoload_dirname, len, "%s/autoload", basegamepath);

      autoloads = opendir(autoload_dirname);
   }
}

//
// D_GameAutoloadWads
//
// Loads all wad files in the base/game/autoload directory.
//
static void D_GameAutoloadWads(void)
{
   char *fn = NULL;

   // haleyjd 09/30/08: not in shareware gamemodes, otherwise having any wads
   // in your base/game/autoload directory will make shareware unplayable
   if(GameModeInfo->flags & GIF_SHAREWARE)
   {
      startupmsg("D_GameAutoloadWads", "ignoring base/game/autoload wad files");
      return;
   }

   if(autoloads)
   {
      struct dirent *direntry;

      while((direntry = readdir(autoloads)))
      {
         if(strstr(direntry->d_name, ".wad"))
         {
            size_t len = M_StringAlloca(&fn, 2, 2, autoload_dirname,
                                        direntry->d_name);

            psnprintf(fn, len, "%s/%s", autoload_dirname, direntry->d_name);
            M_NormalizeSlashes(fn);
            D_AddFile(fn, ns_global, NULL, 0, 0);
         }
      }

      rewinddir(autoloads);
   }
}

//
// D_GameAutoloadDEH
//
// Queues all deh/bex files in the base/game/autoload directory.
//
static void D_GameAutoloadDEH(void)
{
   char *fn = NULL;

   if(autoloads)
   {
      struct dirent *direntry;

      while((direntry = readdir(autoloads)))
      {
         if(strstr(direntry->d_name, ".deh") ||
            strstr(direntry->d_name, ".bex"))
         {
            size_t len = M_StringAlloca(&fn, 2, 2, autoload_dirname,
                                        direntry->d_name);

            psnprintf(fn, len, "%s/%s", autoload_dirname, direntry->d_name);
            M_NormalizeSlashes(fn);
            D_QueueDEH(fn, 0);
         }
      }

      rewinddir(autoloads);
   }
}

//
// D_GameAutoloadCSC
//
// Runs all console scripts in the base/game/autoload directory.
//
static void D_GameAutoloadCSC(void)
{
   char *fn = NULL;

   if(autoloads)
   {
      struct dirent *direntry;

      while((direntry = readdir(autoloads)))
      {
         if(strstr(direntry->d_name, ".csc"))
         {
            size_t len = M_StringAlloca(&fn, 2, 2, autoload_dirname,
                                        direntry->d_name);

            psnprintf(fn, len, "%s/%s", autoload_dirname, direntry->d_name);
            M_NormalizeSlashes(fn);
            C_RunScriptFromFile(fn);
         }
      }

      rewinddir(autoloads);
   }
}

//
// D_CloseAutoloadDir
//
// haleyjd 08/20/07: closes the base/game/autoload directory.
//
static void D_CloseAutoloadDir(void)
{
   if(autoloads)
   {
      closedir(autoloads);
      autoloads = NULL;
   }
}

//=============================================================================
//
// Disk File Handling
//
// haleyjd 05/28/10
//

// known .disk types
enum
{
   DISK_DOOM,
   DISK_DOOM2
};

static boolean havediskfile; // if true, -disk loaded a file
static boolean havediskiwad; // if true, an IWAD was found in the disk file
static const char *diskpwad; // PWAD name (or substring) to look for
static diskfile_t *diskfile; // diskfile object (see d_diskfile.c)
static diskwad_t   diskiwad; // diskwad object for iwad
static int disktype;         // type of disk file

//
// D_CheckDiskFileParm
//
// haleyjd 05/28/10: Looks for -disk and sets up diskfile loading.
//
static void D_CheckDiskFileParm(void)
{
   int p;
   const char *fn;

   if((p = M_CheckParm("-disk")) && p < myargc - 1)
   {
      havediskfile = true;

      // get diskfile name
      fn = myargv[p + 1];

      // have a pwad name as well?
      if(p < myargc - 2 && *(myargv[p + 2]) != '-')
         diskpwad = myargv[p + 2];

      // open the diskfile
      diskfile = D_OpenDiskFile(fn);
   }
}

//
// D_FindDiskFileIWAD
//
// Finds an IWAD in a disk file.
// If this fails, the disk file will be closed.
//
static void D_FindDiskFileIWAD(void)
{
   diskiwad = D_FindWadInDiskFile(diskfile, "doom");

   if(diskiwad.f)
   {
      havediskiwad = true;

      if(strstr(diskiwad.name, "doom2.wad"))
         disktype = DISK_DOOM2;
      else
         disktype = DISK_DOOM;
   }
   else
   {
      // close it up, we can't use it
      D_CloseDiskFile(diskfile, true);
      diskfile     = NULL;
      diskpwad     = NULL;
      havediskfile = false;
      havediskiwad = false;
   }
}

//
// D_LoadDiskFileIWAD
//
// Loads an IWAD from the disk file.
//
static void D_LoadDiskFileIWAD(void)
{
   if(diskiwad.f)
      D_AddFile(diskiwad.name, ns_global, diskiwad.f, diskiwad.offset, 0);
   else
      I_Error("D_LoadDiskFileIWAD: invalid file pointer\n");
}

//
// D_LoadDiskFilePWAD
//
// Loads a PWAD from the disk file.
//
static void D_LoadDiskFilePWAD(void)
{
   diskwad_t wad = D_FindWadInDiskFile(diskfile, diskpwad);

   if(wad.f)
   {
      if(!strstr(wad.name, "doom")) // do not add doom[2].wad twice
         D_AddFile(wad.name, ns_global, wad.f, wad.offset, 0);
   }
}

//
// D_metaGetLine
//
// Gets a single line of input from the metadata.txt resource.
//
static boolean D_metaGetLine(qstring_t *qstr, const char *input, int *idx)
{
   int i = *idx;

   // if empty at start, we are finished
   if(input[i] == '\0')
      return false;

   QStrClear(qstr);

   while(input[i] != '\n' && input[i] != '\0')
   {
      if(input[i] == '\\' && input[i+1] == 'n')
      {
         // make \n sequence into a \n character
         ++i;
         QStrPutc(qstr, '\n');
      }
      else if(input[i] != '\r')
         QStrPutc(qstr, input[i]);

      ++i;
   }

   if(input[i] == '\n')
      ++i;

   // write back input position
   *idx = i;

   return true;
}

//
// D_DiskMetaData
//
// Handles metadata in the disk file.
//
static void D_DiskMetaData(void)
{
   char *name  = NULL, *metatext = NULL;
   const char *slash = NULL;
   const char *endtext = NULL, *levelname = NULL, *musicname = NULL;
   int slen = 0, index = 0;
   int partime = 0, musicnum = 0;
   int exitreturn = 0, secretlevel = 0, levelnum = 1, linenum = 0;
   diskwad_t wad;
   qstring_t buffer;
   qstring_t *qstr = &buffer;

   if(!diskpwad)
      return;

   // find the wad to get the canonical resource name
   wad = D_FindWadInDiskFile(diskfile, diskpwad);

   // return if not found, or if this is metadata for the IWAD
   if(!wad.f || strstr(wad.name, "doom"))
      return;

   // construct the metadata filename
   M_StringAlloca(&name, 2, 1, wad.name, "metadata.txt");

   if(!(slash = strrchr(wad.name, '\\')))
      return;

   slen = slash - wad.name;
   ++slen;
   strncpy(name, wad.name, slen);
   strcpy(name + slen, "metadata.txt");

   // load it up
   if(!(metatext = D_CacheDiskFileResource(diskfile, name, true)))
      return;

   // parse it

   // setup qstring
   QStrInitCreate(qstr);

   // get first line, which is an episode id
   D_metaGetLine(qstr, metatext, &index);

   // get episode name
   if(D_metaGetLine(qstr, metatext, &index))
      GameModeInfo->versionName = QStrCDup(qstr, PU_STATIC);

   // get end text
   if(D_metaGetLine(qstr, metatext, &index))
      endtext = QStrCDup(qstr, PU_STATIC);

   // get next level after secret
   if(D_metaGetLine(qstr, metatext, &index))
      exitreturn = QStrAtoi(qstr);

   // skip next line (wad name)
   D_metaGetLine(qstr, metatext, &index);

   // get secret level
   if(D_metaGetLine(qstr, metatext, &index))
      secretlevel = QStrAtoi(qstr);

   // get levels
   while(D_metaGetLine(qstr, metatext, &index))
   {
      switch(linenum)
      {
      case 0: // levelname
         levelname = QStrCDup(qstr, PU_STATIC);
         break;
      case 1: // music number
         musicnum = mus_runnin + QStrAtoi(qstr) - 1;

         if(musicnum > GameModeInfo->musMin && musicnum < GameModeInfo->numMusic)
            musicname = S_music[musicnum].name;
         else
            musicname = "";
         break;
      case 2: // partime (final field)
         partime = QStrAtoi(qstr);

         // create a metainfo object for LevelInfo
         P_CreateMetaInfo(levelnum, levelname, partime, musicname,
                          levelnum == secretlevel ? exitreturn : 0,
                          levelnum == exitreturn - 1 ? secretlevel : 0,
                          levelnum == secretlevel - 1,
                          (levelnum == secretlevel - 1) ? endtext : NULL);
         break;
      }
      ++linenum;

      if(linenum == 3)
      {
         levelnum++;
         linenum = 0;
      }
   }

   // done with qstring buffer
   QStrFree(qstr);

   // done with metadata resource
   free(metatext);
}

//=============================================================================
//
// IWAD Detection / Verification Code
//

int iwad_choice; // haleyjd 03/19/10: remember choice

// variable-for-index lookup for D_DoIWADMenu
static char **iwadVarForNum[] =
{
   &gi_path_doomsw, &gi_path_doomreg, &gi_path_doomu,
   &gi_path_doom2,  &gi_path_tnt,     &gi_path_plut,
   &gi_path_hacx,   &gi_path_hticsw,  &gi_path_hticreg,
   &gi_path_sosr,
};

//
// D_DoIWADMenu
//
// Crazy fancy graphical IWAD choosing menu.
// Paths are stored in the system.cfg file, and only the games with paths
// stored can be picked from the menu.
// This feature is only available for SDL builds.
//
static const char *D_DoIWADMenu(void)
{
   const char *iwadToUse = NULL;

#ifdef _SDL_VER
   extern int I_Pick_DoPicker(boolean haveIWADs[], int startchoice);
   boolean haveIWADs[9];
   int i, choice = -1;
   boolean foundone = false;

   // populate haveIWADs array based on system.cfg variables
   for(i = 0; i < 9; ++i)
   {
      if((haveIWADs[i] = (**iwadVarForNum[i] != '\0')))
         foundone = true;
   }

   if(foundone) // at least one IWAD must be specified!
   {
      startupmsg("D_DoIWADMenu", "Init IWAD choice subsystem.");
      choice = I_Pick_DoPicker(haveIWADs, iwad_choice);
   }

   if(choice >= 0)
   {
      iwad_choice = choice;               // 03/19/10: remember selection
      iwadToUse = *iwadVarForNum[choice];
   }
#endif

   return iwadToUse;
}


// macros for CheckIWAD

#define isMapExMy(name) \
   ((name)[0] == 'E' && (name)[2] == 'M' && !(name)[4])

#define isMapMAPxy(name) \
   ((name)[0] == 'M' && (name)[1] == 'A' && (name)[2] == 'P' && !(name)[5])

#define isCAV(name) \
   ((name)[0] == 'C' && (name)[1] == 'A' && (name)[2] == 'V' && !(name)[7])

#define isMC(name) \
   ((name)[0] == 'M' && (name)[1] == 'C' && !(name)[3])

// haleyjd 10/13/05: special stuff for FreeDOOM :)
static boolean freedoom = false;

//
// CheckIWAD
//
// Verify a file is indeed tagged as an IWAD
// Scan its lumps for levelnames and return gamemode as indicated
// Detect missing wolf levels in DOOM II
//
// The filename to check is passed in iwadname, the gamemode detected is
// returned in gmode, hassec returns the presence of secret levels
//
// jff 4/19/98 Add routine to test IWAD for validity and determine
// the gamemode from it. Also note if DOOM II, whether secret levels exist
//
// killough 11/98:
// Rewritten to considerably simplify
// Added Final Doom support (thanks to Joel Murdoch)
//
static void CheckIWAD(const char *iwadname,
		      GameMode_t *gmode,
		      GameMission_t *gmission,  // joel 10/17/98 Final DOOM fix
		      boolean *hassec)
{
   FILE *fp;
   int ud = 0, rg = 0, sw = 0, cm = 0, sc = 0, tnt = 0, plut = 0, hacx = 0;
   int raven = 0, sosr = 0;
   filelump_t lump;
   wadinfo_t header;
   const char *n = lump.name;

   if(!(fp = fopen(iwadname, "rb")))
      I_Error("Can't open IWAD: %s\n",iwadname);

   // read IWAD header
   if(fread(&header, sizeof header, 1, fp) < 1 ||
      strncmp(header.identification, "IWAD", 4))
   {
      // haleyjd 06/06/09: do not error out here, due to some bad tools
      // resetting peoples' IWADs to PWADs. Only error if it is also
      // not a PWAD.
      if(strncmp(header.identification, "PWAD", 4))
         I_Error("IWAD or PWAD tag not present: %s\n", iwadname);
      else
         usermsg("Warning: IWAD tag not present: %s\n", iwadname);
   }

   fseek(fp, SwapLong(header.infotableofs), SEEK_SET);

   // Determine game mode from levels present
   // Must be a full set for whichever mode is present
   // Lack of wolf-3d levels also detected here

   header.numlumps = SwapLong(header.numlumps);

   for(; header.numlumps; header.numlumps--)
   {
      if(!fread(&lump, sizeof(lump), 1, fp))
         break;

      if(isMapExMy(n))
      {
         if(n[1] == '4')
            ++ud;
         else if(n[1] == '3' || n[1] == '2')
            ++rg;
         else if(n[1] == '1')
            ++sw;
      }
      else if(isMapMAPxy(n))
      {
         ++cm;
         sc += (n[3] == '3' && (n[4] == '1' || n[4] == '2'));
      }
      else if(isCAV(n))
         ++tnt;
      else if(isMC(n))
         ++plut;
      else if(!strncmp(n, "ADVISOR",  7) ||
              !strncmp(n, "TINTTAB",  7) ||
              !strncmp(n, "SNDCURVE", 8))
      {
         ++raven;
      }
      else if(!strncmp(n, "EXTENDED", 8))
         ++sosr;
      else if(!strncmp(n, "FREEDOOM", 8))
         freedoom = true;
      else if(!strncmp(n, "HACX-R", 6))
         ++hacx;
   }

   fclose(fp);

   *hassec = false;

   // haleyjd 10/09/05: "Raven mode" detection
   if(raven == 3)
   {
      // TODO: Hexen
      *gmission = heretic;

      if(rg >= 18)
      {
         // require both E4 and EXTENDED lump for SoSR
         if(sosr && ud >= 9)
            *gmission = hticsosr;
         *gmode = hereticreg;
      }
      else if(sw >= 9)
         *gmode = hereticsw;
      else
         *gmode = indetermined;
   }
   else
   {
      *gmission = doom;

      if(cm >= 30 || (cm && !rg))
      {
         if(freedoom) // FreeDoom is meant to be Doom II, not TNT
            *gmission = doom2;
         else
         {
            if(tnt >= 4)
               *gmission = pack_tnt;
            else if(plut >= 8)
               *gmission = pack_plut;
            else if(hacx)
               *gmission = pack_hacx;
            else
               *gmission = doom2;
         }
         *hassec = (sc >= 2) || hacx;
         *gmode = commercial;
      }
      else if(ud >= 9)
         *gmode = retail;
      else if(rg >= 18)
         *gmode = registered;
      else if(sw >= 9)
         *gmode = shareware;
      else
         *gmode = indetermined;
   }
}

//
// WadFileStatus
//
// jff 4/19/98 Add routine to check a pathname for existence as
// a file or directory. If neither append .wad and check if it
// exists as a file then. Else return non-existent.
//
static boolean WadFileStatus(char *filename, boolean *isdir)
{
   struct stat sbuf;
   int i;

   *isdir = false;                //default is directory to false
   if(!filename || !*filename)    //if path NULL or empty, doesn't exist
      return false;

   if(!stat(filename,&sbuf))      //check for existence
   {
      *isdir=S_ISDIR(sbuf.st_mode); //if it does, set whether a dir or not
      return true;                  //return does exist
   }

   i = strlen(filename);          //get length of path
   if(i >= 4)
      if(!strncasecmp(filename + i - 4, ".wad", 4))
         return false;            //if already ends in .wad, not found

   strcat(filename,".wad");       //try it with .wad added
   if(!stat(filename,&sbuf))      //if it exists then
   {
      if(S_ISDIR(sbuf.st_mode))   //but is a dir, then say we didn't find it
         return false;
      return true;                //otherwise return file found, w/ .wad added
   }
   filename[i] = 0;               //remove .wad
   return false;                  //and report doesn't exist
}

// jff 4/19/98 list of standard IWAD names
static const char *const standard_iwads[]=
{
   // Official IWADs
   "/doom2.wad",     // DOOM II
   "/doom2f.wad",    // DOOM II, French Version
   "/plutonia.wad",  // Final DOOM: Plutonia
   "/tnt.wad",       // Final DOOM: TNT
   "/doom.wad",      // Registered/Ultimate DOOM
   "/doomu.wad",     // CPhipps - allow doomu.wad
   "/doom1.wad",     // Shareware DOOM
   "/heretic.wad",   // Heretic  -- haleyjd 10/10/05
   "/heretic1.wad",  // Shareware Heretic

   // Unofficial IWADs
   "/freedoom.wad",  // Freedoom                -- haleyjd 01/31/03
   "/freedoomu.wad", // "Ultimate" Freedoom     -- haleyjd 03/07/10
   "/freedoom1.wad", // Freedoom "Demo"         -- haleyjd 03/07/10
   "/hacx.wad",      // HACX standalone version -- haleyjd 08/19/09
};

static const int nstandard_iwads = sizeof standard_iwads/sizeof*standard_iwads;

//
// FindIWADFile
//
// Search in all the usual places until an IWAD is found.
//
// The global baseiwad contains either a full IWAD file specification
// or a directory to look for an IWAD in, or the name of the IWAD desired.
//
// The global standard_iwads lists the standard IWAD names
//
// The result of search is returned in baseiwad, or set blank if none found
//
// IWAD search algorithm:
//
// Set customiwad blank
// If -iwad present set baseiwad to normalized path from -iwad parameter
//  If baseiwad is an existing file, thats it
//  If baseiwad is an existing dir, try appending all standard iwads
//  If haven't found it, and no : or / is in baseiwad,
//   append .wad if missing and set customiwad to baseiwad
//
// Look in . for customiwad if set, else all standard iwads
//
// Look in DoomExeDir. for customiwad if set, else all standard iwads
//
// If $DOOMWADDIR is an existing file
//  If customiwad is not set, thats it
//  else replace filename with customiwad, if exists thats it
// If $DOOMWADDIR is existing dir, try customiwad if set, else standard iwads
//
// If $HOME is an existing file
//  If customiwad is not set, thats it
//  else replace filename with customiwad, if exists thats it
// If $HOME is an existing dir, try customiwad if set, else standard iwads
//
// IWAD not found
//
// jff 4/19/98 Add routine to search for a standard or custom IWAD in one
// of the standard places. Returns a blank string if not found.
//
// killough 11/98: simplified, removed error-prone cut-n-pasted code
//
char *FindIWADFile(void)
{
   static const char *envvars[] = { "DOOMWADDIR", "HOME" };
   static char *iwad = NULL;
   char *customiwad = NULL;
   char *gameiwad = NULL;
   boolean isdir = false;
   int i, j;
   char *p;
   const char *basename = NULL;

   //jff 3/24/98 get -iwad parm if specified else use .
   // [CG] C/S clients and servers either have this configured in the
   //      configuration file, or sent over the wire.
   if(clientserver)
   {
      basename = cs_iwad;
   }
   else if((i = M_CheckParm("-iwad")) && i < myargc - 1)
   {
      basename = myargv[i + 1];
   }
   else
   {
      basename = G_GFSCheckIWAD(); // haleyjd 04/16/03: GFS support
   }

   // haleyjd 08/19/07: if -game was used and neither -iwad nor a GFS iwad
   // specification was used, start off by trying base/game/game.wad
   if(gamepathset && !basename)
   {
      size_t len = M_StringAlloca(&gameiwad, 2, 8, basegamepath,
                                  myargv[gamepathparm]);

      psnprintf(gameiwad, len, "%s/%s.wad", basegamepath, myargv[gamepathparm]);

      if(!access(gameiwad, R_OK)) // only if the file exists do we try to use it.
         basename = gameiwad;
   }

   //jff 3/24/98 get -iwad parm if specified else use .
   if(basename)
   {
      if(baseiwad != NULL)
      {
         free(baseiwad);
      }
      baseiwad = strdup(basename);
      M_NormalizeSlashes(baseiwad);

      iwad = calloc(1, strlen(baseiwad) + 1024);
      strcpy(iwad, baseiwad);

      if(WadFileStatus(iwad, &isdir))
      {
         if(!isdir)
            return iwad;
         else
         {
            for(i = 0; i < nstandard_iwads; i++)
            {
               int n = strlen(iwad);
               strcat(iwad, standard_iwads[i]);
               if(WadFileStatus(iwad, &isdir) && !isdir)
                  return iwad;
               iwad[n] = 0; // reset iwad length to former
            }
         }
      }
      else if(!strchr(iwad, ':') && !strchr(iwad, '/'))
      {
         M_StringAlloca(&customiwad, 1, 8, iwad);
         M_AddDefaultExtension(strcat(strcpy(customiwad, "/"), iwad), ".wad");
      }
   }
   else if(!gamepathset) // try wad picker
   {
      const char *name = D_DoIWADMenu();
      if(name && *name)
      {
         if(baseiwad != NULL)
         {
            free(baseiwad);
         }
         baseiwad = strdup(name);
         M_NormalizeSlashes(baseiwad);
         return baseiwad;
      }
   }

   for(j = 0; j < (gamepathset ? 3 : 2); j++)
   {
      switch(j)
      {
      case 0:
      case 1:
         if(iwad)
         {
            free(iwad);
            iwad = NULL;
         }
         iwad = calloc(1, strlen(D_DoomExeDir()) + 1024);
         strcpy(iwad, j ? D_DoomExeDir() : ".");
         break;
      case 2:
         // haleyjd: try basegamepath too when -game was used
         if(iwad)
         {
            free(iwad);
            iwad = NULL;
         }
         iwad = calloc(1, strlen(basegamepath) + 1024);
         strcpy(iwad, basegamepath);
         break;
      }

      M_NormalizeSlashes(iwad);

       // sf: only show 'looking in' for devparm
      if(devparm)
         printf("Looking in %s\n",iwad);   // killough 8/8/98

      if(customiwad)
      {
         strcat(iwad, customiwad);
         if(WadFileStatus(iwad, &isdir) && !isdir)
            return iwad;
      }
      else
      {
         for(i = 0; i < nstandard_iwads; i++)
         {
            int n = strlen(iwad);
            strcat(iwad, standard_iwads[i]);
            if(WadFileStatus(iwad, &isdir) && !isdir)
               return iwad;
            iwad[n] = 0; // reset iwad length to former
         }
      }
   }

   for(i = 0; i < sizeof envvars / sizeof *envvars; i++)
   {
      if((p = getenv(envvars[i])))
      {
         if(iwad)
         {
            free(iwad);
            iwad = NULL;
         }
         iwad = calloc(1, sizeof(p) + 1024);
         M_NormalizeSlashes(strcpy(iwad, p));
         if(WadFileStatus(iwad, &isdir))
         {
            if(!isdir)
            {
               if(!customiwad)
                  return printf("Looking for %s\n", iwad), iwad; // killough 8/8/98
               else if((p = strrchr(iwad,'/')))
               {
                  *p=0;
                  strcat(iwad, customiwad);
                  printf("Looking for %s\n",iwad);  // killough 8/8/98
                  if(WadFileStatus(iwad, &isdir) && !isdir)
                     return iwad;
               }
            }
            else
            {
               if(devparm)       // sf: devparm only
                  printf("Looking in %s\n",iwad);  // killough 8/8/98
               if(customiwad)
               {
                  if(WadFileStatus(strcat(iwad, customiwad), &isdir) && !isdir)
                     return iwad;
               }
               else
               {
                  for(i = 0; i < nstandard_iwads; i++)
                  {
                     int n = strlen(iwad);
                     strcat(iwad, standard_iwads[i]);
                     if(WadFileStatus(iwad, &isdir) && !isdir)
                        return iwad;
                     iwad[n] = 0; // reset iwad length to former
                  }
               } // end else (!*customiwad)
            } // end else (isdir)
         } // end if(WadFileStatus(...))
      } // end if((p = getenv(...)))
   } // end for

   *iwad = 0;
   return iwad;
}

//
// D_LoadResourceWad
//
// haleyjd 03/10/03: moved eternity.wad loading to this function
//
static void D_LoadResourceWad(void)
{
   char *filestr = NULL;
   size_t len = M_StringAlloca(&filestr, 1, 20, basegamepath);

   psnprintf(filestr, len, "%s/eternity.wad", basegamepath);

   // haleyjd 08/19/07: if not found, fall back to base/doom/eternity.wad
   if(access(filestr, R_OK))
      psnprintf(filestr, len, "%s/doom/eternity.wad", basepath);

   M_NormalizeSlashes(filestr);
   D_AddFile(filestr, ns_global, NULL, 0, 0);

   modifiedgame = false; // reset, ignoring smmu.wad etc.
}

static const char *game_name; // description of iwad

//
// D_SetGameName
//
// Sets the game_name variable for displaying what version of the game is being
// played at startup. "iwad" may be NULL. GameModeInfo must be initialized prior
// to calling this.
//
static void D_SetGameName(const char *iwad)
{
   // get appropriate name for the gamemode/mission
   game_name = GameModeInfo->versionName;

   // special hacks for localized DOOM II variants:
   if(iwad && GameModeInfo->id == commercial)
   {
      // joel 10/16/98 Final DOOM fix
      if(GameModeInfo->missionInfo->id == doom2)
      {
         int i = strlen(iwad);
         if(i >= 10 && !strncasecmp(iwad+i-10, "doom2f.wad", 10))
         {
            language = french;
            game_name = "DOOM II version, French language";
         }
         else if(!haswolflevels)
            game_name = "DOOM II version, German edition, no Wolf levels";
      }
      // joel 10/16/98 end Final DOOM fix
   }

   // haleyjd 03/07/10: Special FreeDoom overrides :)
   if(freedoom && GameModeInfo->freeVerName)
      game_name = GameModeInfo->freeVerName;

   puts(game_name);
}

//
// D_InitPaths
//
// Initializes important file paths, including the game path, config
// path, and save path.
//
static void D_InitPaths(void)
{
   int i;

   // haleyjd 11/23/06: set game path if -game wasn't used
   if(!gamepathset)
      D_SetGamePath();

   // haleyjd 11/23/06: set basedefault here, and use basegamepath.
   // get config file from same directory as executable
   // killough 10/98
   if(GameModeInfo->type == Game_DOOM && use_doom_config)
   {
      // hack for DOOM modes: optional use of /doom config
      size_t len = strlen(basepath) + strlen("/doom") +
                   strlen(D_DoomExeName()) + 8;
      if(basedefault != NULL)
      {
         free(basedefault);
      }
      basedefault = malloc(len);

      psnprintf(basedefault, len, "%s/doom/%s.cfg",
                basepath, D_DoomExeName());
   }
   else
   {
      size_t len = strlen(basegamepath) + strlen(D_DoomExeName()) + 8;

      if(basedefault != NULL)
      {
         free(basedefault);
      }
      basedefault = malloc(len);

      psnprintf(basedefault, len, "%s/%s.cfg", basegamepath, D_DoomExeName());
   }

   // haleyjd 11/23/06: set basesavegame here, and use basegamepath
   // set save path to -save parm or current dir
   if(basesavegame != NULL)
   {
      free(basesavegame);
   }
   basesavegame = strdup(basegamepath);

   if((i = M_CheckParm("-save")) && i < myargc-1) //jff 3/24/98 if -save present
   {
      struct stat sbuf; //jff 3/24/98 used to test save path for existence

      if(!stat(myargv[i+1],&sbuf) && S_ISDIR(sbuf.st_mode)) // and is a dir
      {
         if(basesavegame)
            free(basesavegame);
         basesavegame = strdup(myargv[i+1]); //jff 3/24/98 use that for savegame
      }
      else
         puts("Error: -save path does not exist, using game path");  // killough 8/8/98
   }
}

//
// IdentifyDisk
//
// haleyjd 05/31/10: IdentifyVersion subroutine for dealing with disk files.
//
static void IdentifyDisk(void)
{
   GameMode_t    gamemode;
   GameMission_t gamemission;

   printf("IWAD found: %s\n", diskiwad.name);

   // haleyjd: hardcoded for now
   if(disktype == DISK_DOOM2)
   {
      gamemode      = commercial;
      gamemission   = pack_disk;
      haswolflevels = true;
   }
   else
   {
      gamemode    = retail;
      gamemission = doom;
   }

   // setup gameModeInfo
   D_SetGameModeInfo(gamemode, gamemission);

   // haleyjd: load metadata from diskfile
   D_DiskMetaData();

   // set and display version name
   D_SetGameName(NULL);

   // initialize game/data paths
   D_InitPaths();

   // haleyjd 03/10/03: add eternity.wad before the IWAD, at request of
   // fraggle -- this allows better compatibility with new IWADs
   D_LoadResourceWad();

   // load disk IWAD
   D_LoadDiskFileIWAD();

   // haleyjd: load disk file pwad here, if one was specified
   if(diskpwad)
      D_LoadDiskFilePWAD();

   // done with the diskfile structure
   D_CloseDiskFile(diskfile, false);
   diskfile = NULL;
}

//
// IdentifyDisk
//
// haleyjd 05/31/10: IdentifyVersion subroutine for dealing with normal IWADs.
//
static void IdentifyIWAD(void)
{
   char *iwad;
   GameMode_t    gamemode;
   GameMission_t gamemission;

   iwad = FindIWADFile();

   if(iwad && *iwad)
   {
      printf("IWAD found: %s\n", iwad); //jff 4/20/98 print only if found

      CheckIWAD(iwad,
                &gamemode,
                &gamemission,   // joel 10/16/98 gamemission added
                &haswolflevels);

      // setup GameModeInfo
      D_SetGameModeInfo(gamemode, gamemission);

      // set and display version name
      D_SetGameName(iwad);

      // initialize game/data paths
      D_InitPaths();

      // haleyjd 03/10/03: add eternity.wad before the IWAD, at request of
      // fraggle -- this allows better compatibility with new IWADs
      D_LoadResourceWad();

      D_AddFile(iwad, ns_global, NULL, 0, 0);

      // done with iwad string
      free(iwad);
      iwad = NULL;
   }
   else
   {
      // haleyjd 08/20/07: improved error message for n00bs
      I_Error("\nIWAD not found!\n"
              "To specify an IWAD, try one of the following:\n"
              "* Configure IWAD file paths in base/system.cfg\n"
              "* Use -iwad\n"
              "* Set the DOOMWADDIR environment variable.\n"
              "* Place an IWAD in the working directory.\n"
              "* Place an IWAD file under the appropriate\n"
              "  game folder of the base directory and use\n"
              "  the -game parameter.");
   }
}

// [CG] Added for c/s.
void D_ClearFiles(void)
{
   wfileadd_t *curfile = NULL;
   for(curfile = wadfiles; curfile->filename != NULL; curfile++)
   {
      free((void *)curfile->filename);
   }
   numwadfiles = 0;
}

//
// IdentifyVersion
//
// Set the location of the defaults file and the savegame root
// Locate and validate an IWAD file
// Determine gamemode from the IWAD
//
// supports IWADs with custom names. Also allows the -iwad parameter to
// specify which iwad is being searched for if several exist in one dir.
// The -iwad parm may specify:
//
// 1) a specific pathname, which must exist (.wad optional)
// 2) or a directory, which must contain a standard IWAD,
// 3) or a filename, which must be found in one of the standard places:
//   a) current dir,
//   b) exe dir
//   c) $DOOMWADDIR
//   d) or $HOME
//
// jff 4/19/98 rewritten to use a more advanced search algorithm
//
void IdentifyVersion(void)
{
   // haleyjd 05/28/10: check for -disk parameter
   D_CheckDiskFileParm();

   // if we loaded one, try finding an IWAD in it
   if(havediskfile)
      D_FindDiskFileIWAD();

   // locate the IWAD and determine game mode from it
   if(havediskiwad)
      IdentifyDisk();
   else
      IdentifyIWAD();
}

//=============================================================================
//
// Response File Parsing
//

// MAXARGVS: a reasonable(?) limit on response file arguments

#define MAXARGVS 100

//
//
// FindResponseFile
//
// Find a Response File, identified by an "@" argument.
//
// haleyjd 04/17/03: copied, slightly modified prboom's code to
// allow quoted LFNs in response files.
//
void FindResponseFile(void)
{
   int i;

   for(i = 1; i < myargc; ++i)
   {
      if(myargv[i][0] == '@')
      {
         int size, index, indexinfile;
         byte *f;
         char *file = NULL, *firstargv;
         char **moreargs = malloc(myargc * sizeof(char *));
         char **newargv;
         char *fname = NULL;

         size_t len = M_StringAlloca(&fname, 1, 6, myargv[i]);

         strncpy(fname, &myargv[i][1], len);
         M_AddDefaultExtension(fname, ".rsp");

         // read the response file into memory
         if((size = M_ReadFile(fname, &f)) < 0)
            I_Error("No such response file: %s\n", fname);

         file = (char *)f;

         printf("Found response file %s\n", fname);

         // proff 04/05/2000: Added check for empty rsp file
         if(!size)
         {
            int k;
            printf("\nResponse file empty!\n");

            newargv = calloc(sizeof(char *), MAXARGVS);
            newargv[0] = myargv[0];
            for(k = 1, index = 1; k < myargc; k++)
            {
               if(i != k)
                  newargv[index++] = myargv[k];
            }
            myargc = index; myargv = newargv;
            return;
         }

         // keep all cmdline args following @responsefile arg
         memcpy((void *)moreargs, &myargv[i+1],
                (index = myargc - i - 1) * sizeof(myargv[0]));

         firstargv = myargv[0];
         newargv = calloc(sizeof(char *),MAXARGVS);
         newargv[0] = firstargv;

         {
            char *infile = file;
            indexinfile = 0;
            indexinfile++;  // skip past argv[0] (keep it)
            do
            {
               while(size > 0 && isspace(*infile))
               {
                  infile++;
                  size--;
               }

               if(size > 0)
               {
                  char *s = malloc(size+1);
                  char *p = s;
                  int quoted = 0;

                  while (size > 0)
                  {
                     // Whitespace terminates the token unless quoted
                     if(!quoted && isspace(*infile))
                        break;
                     if(*infile == '\"')
                     {
                        // Quotes are removed but remembered
                        infile++; size--; quoted ^= 1;
                     }
                     else
                     {
                        *p++ = *infile++; size--;
                     }
                  }
                  if(quoted)
                     I_Error("Runaway quoted string in response file\n");

                  // Terminate string, realloc and add to argv
                  *p = 0;
                  newargv[indexinfile++] = realloc(s,strlen(s)+1);
               }
            }
            while(size > 0);
         }
         free(file);

         memcpy((void *)&newargv[indexinfile],moreargs,index*sizeof(moreargs[0]));
         free((void *)moreargs);

         myargc = indexinfile+index; myargv = newargv;

         // display args
         printf("%d command-line args:\n", myargc);

         for(index = 1; index < myargc; index++)
            printf("%s\n", myargv[index]);

         break;
      }
   }
}

//=============================================================================
//
// Misc. File Stuff
// * DEHACKED Loading
// * MBF-Style Autoloads
//

// killough 10/98: moved code to separate function

static void D_ProcessDehCommandLine(void)
{
   // ty 03/09/98 do dehacked stuff
   // Note: do this before any other since it is expected by
   // the deh patch author that this is actually part of the EXE itself
   // Using -deh in BOOM, others use -dehacked.
   // Ty 03/18/98 also allow .bex extension.  .bex overrides if both exist.
   // killough 11/98: also allow -bex

   int p = M_CheckParm ("-deh");
   if(p || (p = M_CheckParm("-bex")))
   {
      // the parms after p are deh/bex file names,
      // until end of parms or another - preceded parm
      // Ty 04/11/98 - Allow multiple -deh files in a row
      // killough 11/98: allow multiple -deh parameters

      boolean deh = true;
      while(++p < myargc)
      {
         if(*myargv[p] == '-')
            deh = !strcasecmp(myargv[p],"-deh") || !strcasecmp(myargv[p],"-bex");
         else
         {
            if(deh)
            {
               char *file; // killough
               M_StringAlloca(&file, 1, 6, myargv[p]);

               M_AddDefaultExtension(strcpy(file, myargv[p]), ".bex");
               if(access(file, F_OK))  // nope
               {
                  M_AddDefaultExtension(strcpy(file, myargv[p]), ".deh");
                  if(access(file, F_OK))  // still nope
                     I_Error("Cannot find .deh or .bex file named %s\n",
                             myargv[p]);
               }
               // during the beta we have debug output to dehout.txt
               // (apparently, this was never removed after Boom beta-killough)
               //ProcessDehFile(file, D_dehout(), 0);  // killough 10/98
               // haleyjd: queue the file, process it later
               D_QueueDEH(file, 0);
            }
         }
      }
   }
   // ty 03/09/98 end of do dehacked stuff
}

// killough 10/98: support preloaded wads

static void D_ProcessWadPreincludes(void)
{
   // haleyjd 09/30/08: don't do in shareware
   if(!M_CheckParm("-noload") && !(GameModeInfo->flags & GIF_SHAREWARE))
   {
      int i;
      char *s;
      for(i = 0; i < MAXLOADFILES; ++i)
         if((s = wad_files[i]))
         {
            while(isspace(*s))
               s++;
            if(*s)
            {
               char *file = NULL;
               M_StringAlloca(&file, 1, 6, s);

               M_AddDefaultExtension(strcpy(file, s), ".wad");
               if(!access(file, R_OK))
                  D_AddFile(file, ns_global, NULL, 0, 0);
               else
                  printf("\nWarning: could not open %s\n", file);
            }
         }
   }
}

// killough 10/98: support preloaded deh/bex files

static void D_ProcessDehPreincludes(void)
{
   if(!M_CheckParm ("-noload"))
   {
      int i;
      char *s;
      for(i = 0; i < MAXLOADFILES; i++)
      {
         if((s = deh_files[i]))
         {
            while(isspace(*s))
               s++;
            if(*s)
            {
               char *file = NULL;
               M_StringAlloca(&file, 1, 6, s);

               M_AddDefaultExtension(strcpy(file, s), ".bex");
               if(!access(file, R_OK))
                  D_QueueDEH(file, 0); // haleyjd: queue it
               else
               {
                  M_AddDefaultExtension(strcpy(file, s), ".deh");
                  if(!access(file, R_OK))
                     D_QueueDEH(file, 0); // haleyjd: queue it
                  else
                     printf("\nWarning: could not open %s .deh or .bex\n", s);
               }
            } // end if(*s)
         } // end if((s = deh_files[i]))
      } // end for
   } // end if
}

// haleyjd: auto-executed console scripts

static void D_AutoExecScripts(void)
{
   // haleyjd 05/31/06: run command-line scripts first
   C_RunCmdLineScripts();

   if(!M_CheckParm("-nocscload")) // separate param from above
   {
      int i;
      char *s;
      for(i = 0; i < MAXLOADFILES; ++i)
         if((s = csc_files[i]))
         {
            while(isspace(*s))
               s++;
            if(*s)
            {
               char *file = NULL;
               M_StringAlloca(&file, 1, 6, s);

               M_AddDefaultExtension(strcpy(file, s), ".csc");
               if(!access(file, R_OK))
                  C_RunScriptFromFile(file);
               else
                  usermsg("\nWarning: could not open console script %s\n", s);
            }
         }
   }
}

// killough 10/98: support .deh from wads
//
// A lump named DEHACKED is treated as plaintext of a .deh file embedded in
// a wad (more portable than reading/writing info.c data directly in a wad).
//
// If there are multiple instances of "DEHACKED", we process each, in first
// to last order (we must reverse the order since they will be stored in
// last to first order in the chain). Passing NULL as first argument to
// ProcessDehFile() indicates that the data comes from the lump number
// indicated by the third argument, instead of from a file.

// haleyjd 10/20/03: restored to MBF semantics in order to support
// queueing of dehacked lumps without trouble because of reordering
// of the wad directory by W_InitMultipleFiles.

static void D_ProcessDehInWad(int i)
{
   if(i >= 0)
   {
      D_ProcessDehInWad(w_GlobalDir.lumpinfo[i]->next);
      if(!strncasecmp(w_GlobalDir.lumpinfo[i]->name, "DEHACKED", 8) &&
         w_GlobalDir.lumpinfo[i]->li_namespace == ns_global)
         D_QueueDEH(NULL, i); // haleyjd: queue it
   }
}

static void D_ProcessDehInWads(void)
{
   // haleyjd: start at the top of the hash chain
   lumpinfo_t *root = W_GetLumpNameChain("DEHACKED");

   D_ProcessDehInWad(root->index);
}

//=============================================================================
//
// haleyjd 03/10/03: GFS functions
//

static void D_ProcessGFSDeh(gfs_t *gfs)
{
   int i;
   char *filename = NULL;

   for(i = 0; i < gfs->numdehs; ++i)
   {
      size_t len;

      if(gfs->filepath)
      {
         len = M_StringAlloca(&filename, 2, 2, gfs->filepath, gfs->dehnames[i]);
         psnprintf(filename, len, "%s/%s", gfs->filepath, gfs->dehnames[i]);
      }
      else
      {
         len = M_StringAlloca(&filename, 1, 2, gfs->dehnames[i]);
         psnprintf(filename, len, "%s", gfs->dehnames[i]);
      }

      M_NormalizeSlashes(filename);

      if(access(filename, F_OK))
         I_Error("Couldn't open .deh or .bex %s\n", filename);

      D_QueueDEH(filename, 0); // haleyjd: queue it
   }
}

static void D_ProcessGFSWads(gfs_t *gfs)
{
   int i;
   char *filename = NULL;

   // haleyjd 09/30/08: don't load GFS wads in shareware gamemodes
   if(GameModeInfo->flags & GIF_SHAREWARE)
   {
      startupmsg("D_ProcessGFSWads", "ignoring GFS wad files");
      return;
   }

   // haleyjd 06/21/04: GFS should mark modified game when wads are added!
   if(gfs->numwads > 0)
      modifiedgame = true;

   for(i = 0; i < gfs->numwads; ++i)
   {
      size_t len;

      if(gfs->filepath)
      {
         len = M_StringAlloca(&filename, 2, 2, gfs->filepath, gfs->wadnames[i]);
         psnprintf(filename, len, "%s/%s", gfs->filepath, gfs->wadnames[i]);
      }
      else
      {
         len = M_StringAlloca(&filename, 1, 2, gfs->wadnames[i]);
         psnprintf(filename, len, "%s", gfs->wadnames[i]);
      }

      if(gfs->filepath)
         psnprintf(filename, len, "%s/%s", gfs->filepath, gfs->wadnames[i]);
      else
         psnprintf(filename, len, "%s", gfs->wadnames[i]);

      M_NormalizeSlashes(filename);

      if(access(filename, F_OK))
         I_Error("Couldn't open WAD file %s\n", filename);

      D_AddFile(filename, ns_global, NULL, 0, 0);
   }
}

static void D_ProcessGFSCsc(gfs_t *gfs)
{
   int i;
   char *filename = NULL;

   for(i = 0; i < gfs->numcsc; ++i)
   {
      size_t len;

      if(gfs->filepath)
      {
         len = M_StringAlloca(&filename, 2, 2, gfs->filepath, gfs->cscnames[i]);
         psnprintf(filename, len, "%s/%s", gfs->filepath, gfs->cscnames[i]);
      }
      else
      {
         len = M_StringAlloca(&filename, 1, 2, gfs->cscnames[i]);
         psnprintf(filename, len, "%s", gfs->cscnames[i]);
      }

      if(gfs->filepath)
         psnprintf(filename, len, "%s/%s", gfs->filepath, gfs->cscnames[i]);
      else
         psnprintf(filename, len, "%s", gfs->cscnames[i]);

      M_NormalizeSlashes(filename);

      if(access(filename, F_OK))
         I_Error("Couldn't open CSC file %s\n", filename);

      C_RunScriptFromFile(filename);
   }
}

//=============================================================================
//
// EDF Loading
//

//
// D_LooseEDF
//
// Looks for a loose EDF file on the command line, to support
// drag-and-drop.
//
static boolean D_LooseEDF(char **buffer)
{
   int i;
   const char *dot;

   for(i = 1; i < myargc; ++i)
   {
      // stop at first param with '-' or '@'
      if(myargv[i][0] == '-' || myargv[i][0] == '@')
         break;

      // get extension (search from right end)
      dot = strrchr(myargv[i], '.');

      // check extension
      if(!dot || strncasecmp(dot, ".edf", 4))
         continue;

      *buffer = Z_Strdupa(myargv[i]);
      return true; // process only the first EDF found
   }

   return false;
}

//
// D_LoadEDF
//
// Identifies the root EDF file, and then calls E_ProcessEDF.
//
static void D_LoadEDF(gfs_t *gfs)
{
   int i;
   char *edfname = NULL;
   const char *shortname = NULL;

   // command line takes utmost precedence
   if((i = M_CheckParm("-edf")) && i < myargc - 1)
   {
      // command-line EDF file found
      edfname = Z_Strdupa(myargv[i + 1]);
   }
   else if(gfs && (shortname = G_GFSCheckEDF()))
   {
      // GFS specified an EDF file
      size_t len = M_StringAlloca(&edfname, 2, 2, gfs->filepath, shortname);
      psnprintf(edfname, len, "%s/%s", gfs->filepath, shortname);
   }
   else
   {
      // use default
      if(!D_LooseEDF(&edfname)) // check for loose files (drag and drop)
      {
         char *fn;

         // haleyjd 08/20/07: check for root.edf in base/game first
         if((fn = D_CheckGameEDF()))
         {
            edfname = Z_Strdupa(fn);
            free(fn);
         }
         else
         {
            size_t len = M_StringAlloca(&edfname, 1, 10, basepath);

            psnprintf(edfname, len, "%s/root.edf",  basepath);
         }

         // disable other game modes' definitions implicitly ONLY
         // when using the default root.edf
         // also, allow command line toggle
         if(!M_CheckParm("-edfenables"))
         {
            if(GameModeInfo->type == Game_Heretic)
               E_EDFSetEnableValue("DOOM", 0);
            else
               E_EDFSetEnableValue("HERETIC", 0);
         }
      }
   }

   M_NormalizeSlashes(edfname);

   E_ProcessEDF(edfname);

   // haleyjd FIXME: temporary hacks
   D_InitGameInfo();
   D_InitWeaponInfo();
}

//=============================================================================
//
// loose file support functions -- these enable drag-and-drop support
// for Windows and possibly other OSes
//

static void D_LooseWads(void)
{
   int i;
   const char *dot;
   char *filename;

   for(i = 1; i < myargc; ++i)
   {
      // stop at first param with '-' or '@'
      if(myargv[i][0] == '-' || myargv[i][0] == '@')
         break;

      // get extension (search from right end)
      dot = strrchr(myargv[i], '.');

      // check extension
      if(!dot || strncasecmp(dot, ".wad", 4))
         continue;

      // add it
      filename = Z_Strdupa(myargv[i]);
      M_NormalizeSlashes(filename);
      modifiedgame = true;
      D_AddFile(filename, ns_global, NULL, 0, 0);
   }
}

static void D_LooseDehs(void)
{
   int i;
   const char *dot;
   char *filename;

   for(i = 1; i < myargc; ++i)
   {
      // stop at first param with '-' or '@'
      if(myargv[i][0] == '-' || myargv[i][0] == '@')
         break;

      // get extension (search from right end)
      dot = strrchr(myargv[i], '.');

      // check extension
      if(!dot || (strncasecmp(dot, ".deh", 4) &&
                  strncasecmp(dot, ".bex", 4)))
         continue;

      // add it
      filename = Z_Strdupa(myargv[i]);
      M_NormalizeSlashes(filename);
      D_QueueDEH(filename, 0);
   }
}

static gfs_t *D_LooseGFS(void)
{
   int i;
   const char *dot;

   for(i = 1; i < myargc; ++i)
   {
      // stop at first param with '-' or '@'
      if(myargv[i][0] == '-' || myargv[i][0] == '@')
         break;

      // get extension (search from right end)
      dot = strrchr(myargv[i], '.');

      // check extension
      if(!dot || strncasecmp(dot, ".gfs", 4))
         continue;

      printf("Found loose GFS file %s\n", myargv[i]);

      // process only the first GFS found
      return G_LoadGFS(myargv[i]);
   }

   return NULL;
}

//=============================================================================
//
// Primary Initialization Routines
//

// I am not going to make an entire header just for this :P
extern void M_LoadSysConfig(const char *filename);

//
// D_LoadSysConfig
//
// Load the system config. Prerequisites: base path must be determined.
//
static void D_LoadSysConfig(void)
{
   char *filename = NULL;
   size_t len = M_StringAlloca(&filename, 2, 2, basepath, "system.cfg");

   psnprintf(filename, len, "%s/system.cfg", basepath);

   M_LoadSysConfig(filename);
}

//
// D_SetGraphicsMode
//
// sf: this is really part of D_DoomMain but I made it into
// a seperate function
// this stuff needs to be kept together
//
static void D_SetGraphicsMode(void)
{
   // set graphics mode
   I_InitGraphics();

   // set up the console to display startup messages
   gamestate = GS_CONSOLE;
   Console.current_height = SCREENHEIGHT;
   Console.showprompt = false;

   C_Puts(game_name);    // display description of gamemode
   D_ListWads();         // list wads to the console
   C_Printf("\n");       // leave a gap
}

#ifdef GAMEBAR
// print title for every printed line
static char title[128];
#endif

extern int levelTimeLimit;
extern int levelFragLimit;

//
// D_StartupMessage
//
// A little reminder for Certain People (Ichaelmay Ardyhay)
//
static void D_StartupMessage(void)
{
   puts("The Eternity Engine\n"
        "Copyright 2010 James Haley and Stephen McGranahan\n"
        "http://www.doomworld.com/eternity\n"
        "\n"
        "This program is free software distributed under the terms of\n"
        "the GNU General Public License. See the file \"COPYING\" for\n"
        "full details. Commercial sale or distribution of this product\n"
        "without its license, source code, and copyright notices is an\n"
        "infringement of US and international copyright laws.\n");
}

// haleyjd 11/12/05: in cdrom mode?
boolean cdrom_mode = false;

//
// D_DoomInit
//
// Broke D_DoomMain into two functions in order to keep
// initialization stuff off the main line of execution.
//
static void D_DoomInit(void)
{
   int p, slot;
   int dmtype = 0;             // haleyjd 04/14/03
   boolean haveGFS = false;    // haleyjd 03/10/03
   gfs_t *gfs = NULL;

   gamestate = GS_STARTUP; // haleyjd 01/01/10

   D_StartupMessage();

   FindResponseFile(); // Append response file arguments to command-line

   // haleyjd 11/12/05: moved -cdrom check up and made status global
#ifdef EE_CDROM_SUPPORT
   if(M_CheckParm("-cdrom"))
      cdrom_mode = true;
#endif

   // haleyjd 08/18/07: set base path
   D_SetBasePath();

   // haleyjd 08/19/07: check for -game parameter first
   D_CheckGamePathParam();

   // [CG] Check for c/s command-line args.
   if(M_CheckParm("-solo-net") && M_CheckParm("-csjoin"))
   {
      I_Error("Cannot specify both -solo-net and -csjoin.\n");
   }
   if(M_CheckParm("-solo-net") && M_CheckParm("-csserve"))
   {
      I_Error("Cannot specify both -solo-net and -csserve.\n");
   }
   if(M_CheckParm("-solo-net") && M_CheckParm("-csplaydemo"))
   {
      I_Error("Cannot specify both -solo-net and -csplaydemo.\n");
   }
   if(M_CheckParm("-csjoin") && M_CheckParm("-csserve"))
   {
      I_Error("Cannot specify both -csjoin and -csserve.\n");
   }
   if(M_CheckParm("-csjoin") && M_CheckParm("-csplaydemo"))
   {
      I_Error("Cannot specify both -csjoin and -csplaydemo");
   }
   if(M_CheckParm("-csserve") && M_CheckParm("-csplaydemo"))
   {
      I_Error("Cannot specify both -csserve and -csplaydemo");
   }

   if((p = M_CheckParm("-csjoin")) && p < myargc - 1)
   {
      printf("CS_Init: Initializing as c/s client.\n");
      clientside  = true;
      serverside  = false;
      clientserver = true;
      // [CG] We can afford to be pretty dumb about things in CS_CLIENT mode
      dmflags = 0;
      dmflags2 = 0;
      memset(comp, 0, sizeof(comp));
      GameType = DefaultGameType = gt_coop;
   }
   else if(M_CheckParm("-csserve"))
   {
      if(!M_CheckParm("-showserverwindow"))
      {
         printf("CS_Init: Initializing as headless c/s server.\n");
         CS_HEADLESS = true;
      }
      else
      {
         printf(
            "CS_Init: Initializing as c/s server with server window.\n"
         );
         CS_HEADLESS = false;
      }
      clientside  = false;
      serverside  = true;
      clientserver = true;
   }
   else if(M_CheckParm("-csplaydemo"))
   {
      printf("CS_Init: Initializing as c/s demo viewer.\n");
      clientserver = true;
      cs_demo_playback = true;
      clientside = serverside = false; // [CG] This will be changed later.
   }
   else
   {
      printf("CS_Init: Initializing as singleplayer client.\n");
      clientside  = true;
      serverside  = true;
      clientserver = false;
   }

   // [CG] Allocate various players-related arrays.
   players = calloc(MAXPLAYERS, sizeof(player_t));
   playeringame = calloc(MAXPLAYERS, sizeof(boolean));
   clients = malloc(MAXPLAYERS * sizeof(client_t));

   playeringame[0] = true;

   if(clientserver)
   {
      consoleplayer = displayplayer = 0;
      clients[consoleplayer].spectating = true;

      // [CG] Initialize libcurl.
      CS_InitCurl();
   }

   // haleyjd 03/05/09: load system config as early as possible
   D_LoadSysConfig();

   // [CG] Load c/s configurations and networking.
   if(CS_DEMO)
   {
      if(!CS_PlayDemo(myargv[M_CheckParm("-csplaydemo") + 1]))
      {
         I_Error("Error playing demo: %s\n", CS_GetDemoErrorMessage());
      }
      CL_InitPlayDemoMode();
      atexit(CS_StopDemo); // [CG] Ensure we clean up after ourselves.
   }
   else if(CS_CLIENT)
   {
      CL_Init(myargv[M_CheckParm("-csjoin") + 1]);
   }
   else if(CS_SERVER)
   {
      SV_LoadConfig();
      SV_Init();
   }

   // [CG] Initialize NetID stacks.
   CS_InitNetIDs();

   // [CG] Can't use GFS in c/s.
   if(!clientserver)
   {
      // haleyjd 03/10/03: GFS support
      // haleyjd 11/22/03: support loose GFS on the command line too
      if((p = M_CheckParm("-gfs")) && p < myargc - 1)
      {
         char *fn = NULL;
         M_StringAlloca(&fn, 1, 6, myargv[p + 1]);

         // haleyjd 01/19/05: corrected use of AddDefaultExtension
         M_AddDefaultExtension(strcpy(fn, myargv[p + 1]), ".gfs");
         if(access(fn, F_OK))
            I_Error("GFS file %s not found\n", fn);

         printf("Parsing GFS file %s\n", fn);

         gfs = G_LoadGFS(fn);
         haveGFS = true;
      }
      else if((gfs = D_LooseGFS()))
      {
         // ^^ look for a loose GFS for drag-and-drop support (comment moved by
         //    [CG])
         haveGFS = true;
      }
      else if(gamepathset)
      {
         // haleyjd 08/19/07: look for default.gfs in specified game path
         char *fn = NULL;
         size_t len = M_StringAlloca(&fn, 1, 14, basegamepath);

         psnprintf(fn, len, "%s/default.gfs", basegamepath);
         if(!access(fn, R_OK))
         {
            gfs = G_LoadGFS(fn);
            haveGFS = true;
         }
      }
   }

   // haleyjd: init the dehacked queue (only necessary the first time)
   D_DEHQueueInit();

   // haleyjd 11/22/03: look for loose DEH files (drag and drop)
   D_LooseDehs();

   // killough 10/98: process all command-line DEH's first
   // haleyjd  09/03: this just queues them now
   D_ProcessDehCommandLine();

   // haleyjd 09/11/03: queue GFS DEH's
   if(haveGFS)
      D_ProcessGFSDeh(gfs);

   // killough 10/98: set default savename based on executable's name
   // haleyjd 08/28/03: must be done BEFORE bex hash chain init!
   savegamename = Z_Malloc(16, PU_STATIC, NULL);
   psnprintf(savegamename, 16, "%.4ssav", D_DoomExeName());

   devparm = !!M_CheckParm("-devparm");         //sf: move up here

   if(!CS_DEMO)
   {
      IdentifyVersion();
   }
   printf("\n"); // gap

   modifiedgame = false;

   // jff 1/24/98 set both working and command line value of play parms
   // sf: make boolean for console
   // [CG] This is already done by this point in c/s (we've already processed
   //      the configuration file).
   if(!clientserver)
   {
      nomonsters  = clnomonsters  = !!M_CheckParm("-nomonsters");
      respawnparm = clrespawnparm = !!M_CheckParm("-respawn");
      fastparm    = clfastparm    = !!M_CheckParm("-fast");
   }
   // jff 1/24/98 end of set to both working and command line value

   DefaultGameType = gt_single;

   // [CG] Game type is set before this (and elsewhere) in c/s.
   if(!clientserver)
   {
      if(M_CheckParm("-deathmatch"))
      {
         DefaultGameType = gt_dm;
         dmtype = 1;
      }
      if(M_CheckParm("-altdeath"))
      {
         DefaultGameType = gt_dm;
         dmtype = 2;
      }
      if(M_CheckParm("-trideath"))  // deathmatch 3.0!
      {
         DefaultGameType = gt_dm;
         dmtype = 3;
      }

      GameType = DefaultGameType;
      G_SetDefaultDMFlags(dmtype, true);
   }

#ifdef GAMEBAR
   psnprintf(title, sizeof(title), "%s", GameModeInfo->startupBanner);
   printf("%s\n", title);
   printf("%s\nBuilt on %s at %s\n", title, version_date,
          version_time);    // killough 2/1/98
#else
   // haleyjd: always provide version date/time
   printf("Built on %s at %s\n", version_date, version_time);
#endif /* GAMEBAR */

   if(devparm)
   {
      printf(D_DEVSTR);
      v_ticker = true;  // turn on the fps ticker
   }

#ifdef EE_CDROM_SUPPORT
   // sf: ok then, this is broken under linux.
   // haleyjd: FIXME
   if(cdrom_mode)
   {
      size_t len;

#ifndef DJGPP
      mkdir("c:/doomdata");
#else
      mkdir("c:/doomdata", 0);
#endif
      // killough 10/98:
      if(basedefault)
         free(basedefault);

      len = strlen(D_DoomExeName()) + 18;

      basedefault = malloc(len);

      psnprintf(basedefault, len, "c:/doomdata/%s.cfg", D_DoomExeName());
   }
#endif

   // [CG] Don't load any WADs (other than the IWAD) that aren't explicitly
   //      listed by the server or in the server's configuration file.
   if(!clientserver)
   {
      // haleyjd 03/10/03: Load GFS Wads
      // 08/08/03: moved first, so that command line overrides
      if(haveGFS)
         D_ProcessGFSWads(gfs);

      // haleyjd 11/22/03: look for loose wads (drag and drop)
      D_LooseWads();

      // add any files specified on the command line with -file wadfile
      // to the wad list

      // killough 1/31/98, 5/2/98: reload hack removed, -wart same as -warp now.

      if((p = M_CheckParm("-file")))
      {
         // the parms after p are wadfile/lump names,
         // until end of parms or another - preceded parm
         // killough 11/98: allow multiple -file parameters

         boolean file = modifiedgame = true; // homebrew levels
         while(++p < myargc)
         {
            if(*myargv[p] == '-')
            {
               file = !strcasecmp(myargv[p], "-file");
            }
            else
            {
               if(file)
                  D_AddFile(myargv[p], ns_global, NULL, 0, 0);
            }
         }
      }

      if(!(p = M_CheckParm("-playdemo")) || p >= myargc-1)   // killough
      {
         if((p = M_CheckParm("-fastdemo")) && p < myargc-1)  // killough
            fastdemo = true;            // run at fastest speed possible
         else
            p = M_CheckParm("-timedemo");
      }

      if(p && p < myargc - 1)
      {
         char *file = NULL;
         size_t len = M_StringAlloca(&file, 1, 6, myargv[p + 1]);

         strncpy(file, myargv[p + 1], len);

         M_AddDefaultExtension(file, ".lmp");     // killough
         D_AddFile(file, ns_demos, NULL, 0, 0);
         usermsg("Playing demo %s\n",file);
      }

      // get skill / episode / map from parms

      // jff 3/24/98 was sk_medium, just note not picked
      startskill = sk_none;
      startepisode = 1;
      startmap = 1;
      autostart = false;

      if((p = M_CheckParm("-skill")) && p < myargc - 1)
      {
         startskill = myargv[p+1][0]-'1';
         autostart = true;
      }

      if((p = M_CheckParm("-episode")) && p < myargc - 1)
      {
         startepisode = myargv[p+1][0]-'0';
         startmap = 1;
         autostart = true;
      }

      // haleyjd: deatchmatch-only options
      if(GameType == gt_dm)
      {
         if((p = M_CheckParm("-timer")) && p < myargc-1)
         {
            int time = atoi(myargv[p+1]);

            usermsg("Levels will end after %d minute%s.\n",
               time, time > 1 ? "s" : "");
            levelTimeLimit = time;
         }

         // sf: moved from p_spec.c
         // See if -frags has been used
         if((p = M_CheckParm("-frags")) && p < myargc-1)
         {
            int frags = atoi(myargv[p+1]);

            if(frags <= 0)
               frags = 10;  // default 10 if no count provided
            levelFragLimit = frags;
         }

         if((p = M_CheckParm("-avg")) && p < myargc-1)
         {
            levelTimeLimit = 20 * 60 * TICRATE;
            usermsg("Austin Virtual Gaming: Levels will end after 20 minutes");
         }
      }

      if(((p = M_CheckParm("-warp")) ||      // killough 5/2/98
          (p = M_CheckParm("-wart"))) && p < myargc - 1)
      {
         // 1/25/98 killough: fix -warp xxx from crashing Doom 1 / UD
         if(GameModeInfo->flags & GIF_MAPXY)
         {
            startmap = atoi(myargv[p + 1]);
            autostart = true;
         }
         else if(p < myargc - 2)
         {
            startepisode = atoi(myargv[++p]);
            startmap = atoi(myargv[p + 1]);
            autostart = true;
         }
      }
   }

   // [CG] C/S servers default to -nosound, -nodraw and -noblit unless
   //      explicitly overridden with -showserverwindow, in which case
   //      arguments are applied as normal.
   if(CS_HEADLESS)
   {
      nomusicparm = true;
      nosfxparm = true;
      nodrawers = true;
      noblit = true;
   }
   else
   {
      //jff 1/22/98 add command line parms to disable sound and music
      {
         boolean nosound = !!M_CheckParm("-nosound");
         nomusicparm = nosound || M_CheckParm("-nomusic");
         nosfxparm   = nosound || M_CheckParm("-nosfx");
      }
      //jff end of sound/music command line parms

      // killough 3/2/98: allow -nodraw -noblit generally
      nodrawers = !!M_CheckParm("-nodraw");
      noblit    = !!M_CheckParm("-noblit");
   }

   // haleyjd: need to do this before M_LoadDefaults
   C_InitPlayerName();

   startupmsg("M_LoadDefaults", "Load system defaults.");
   M_LoadDefaults();              // load before initing other systems

   // haleyjd 01/11/09: process affinity mask stuff
#if defined(_WIN32) || defined(HAVE_SCHED_SETAFFINITY)
   {
      extern void I_SetAffinityMask(void);

      I_SetAffinityMask();
   }
#endif

   bodyquesize = default_bodyquesize; // killough 10/98

   G_ReloadDefaults();    // killough 3/4/98: set defaults just loaded.
   // jff 3/24/98 this sets startskill if it was -1

   // 1/18/98 killough: Z_Init call moved to i_main.c

   if(!clientserver)
   {
      // killough 10/98: add preincluded wads at the end
      D_ProcessWadPreincludes();

      // haleyjd 08/20/07: also, enumerate and load wads from
      //                   base/game/autoload
      D_EnumerateAutoloadDir();
      D_GameAutoloadWads();
   }

   startupmsg("W_Init", "Init WADfiles.");
   W_InitMultipleFiles(&w_GlobalDir, wadfiles);
   usermsg("");  // gap

   // [CG] I feel like it's fair that to play c/s Doom you need to either buy
   //      Doom/Doom II from id Software or use Freedoom.  Shareware IWAD won't
   //      cut it.
   if(clientserver && (GameModeInfo->flags & GIF_SHAREWARE))
   {
      I_Error(
         "\nYou cannot -csjoin/-csserve with the shareware version.  "
         "Register!\n"
      );
   }

   // Check for -file in shareware
   //
   // haleyjd 03/22/03: there's no point in trying to detect fake IWADs,
   // especially after user wads have already been linked in, so I've removed
   // that kludge
   if(modifiedgame && (GameModeInfo->flags & GIF_SHAREWARE))
      I_Error("\nYou cannot -file with the shareware version. Register!\n");

   // haleyjd 11/12/09: Initialize post-W_InitMultipleFiles GameModeInfo
   // overrides and adjustments here.
   D_InitGMIPostWads();

   // haleyjd 10/20/03: use D_ProcessDehInWads again
   D_ProcessDehInWads();

   // killough 10/98: process preincluded .deh files
   // haleyjd  09/03: this just queues them now
   D_ProcessDehPreincludes();

   // haleyjd 08/20/07: queue autoload dir dehs
   D_GameAutoloadDEH();

   // haleyjd 09/11/03: All EDF and DeHackEd processing is now
   // centralized here, in order to allow EDF to load from wads.
   // As noted in comments, the other DEH functions above now add
   // their files or lumps to the queue in d_dehtbl.c -- the queue
   // is processed here to parse all files/lumps at once.

   // Init bex hash chaining before EDF
   D_BuildBEXHashChains();

   // Identify root EDF file and process EDF
   D_LoadEDF(gfs);

   // Build BEX tables (some are EDF-dependent)
   D_BuildBEXTables();

   // Process the DeHackEd queue, then free it
   D_ProcessDEHQueue();

   // Process the deferred wad sounds queue, after the above stuff.
   S_ProcDeferredSounds();

   V_InitColorTranslation(); //jff 4/24/98 load color translation lumps

   if(!clientserver)
   {
      // haleyjd: moved down turbo to here for player class support
      if((p = M_CheckParm("-turbo")))
      {
         extern int turbo_scale;

         if(p < myargc - 1)
            turbo_scale = atoi(myargv[p + 1]);
         if(turbo_scale < 10)
            turbo_scale = 10;
         if(turbo_scale > 400)
            turbo_scale = 400;
         printf("turbo scale: %i%%\n",turbo_scale);
         E_ApplyTurbo(turbo_scale);
      }
   }

   // killough 2/22/98: copyright / "modified game" / SPA banners removed

   // haleyjd 11/13/05: moved down CD-ROM mode notice and changed to
   // use BEX string like it really always should have. Because it was
   // previously before the point where DEH/BEX was loaded, it couldn't
   // be done.
   if(cdrom_mode)
      puts(DEH_String("D_CDROM"));

   // Ty 04/08/98 - Add 5 lines of misc. data, only if nonblank
   // The expectation is that these will be set in a .bex file

   // haleyjd: in order for these to play the appropriate role, they
   //  should appear in the console if not in text mode startup

   if(textmode_startup)
   {
      if(*startup1) puts(startup1);
      if(*startup2) puts(startup2);
      if(*startup3) puts(startup3);
      if(*startup4) puts(startup4);
      if(*startup5) puts(startup5);
   }
   // End new startup strings

   startupmsg("V_InitMisc","Init miscellaneous video patches.");
   V_InitMisc();

   startupmsg("C_Init","Init console.");
   C_Init();

   startupmsg("I_Init","Setting up machine state.");
   I_Init();

   // devparm override of early set graphics mode
   if(!textmode_startup && !devparm)
   {
      startupmsg("D_SetGraphicsMode", "Set graphics mode");
      D_SetGraphicsMode();
   }

   startupmsg("R_Init","Init DOOM refresh daemon");
   R_Init();

   startupmsg("P_Init","Init Playloop state.");
   P_Init();

   startupmsg("HU_Init","Setting up heads up display.");
   HU_Init();

   startupmsg("ST_Init","Init status bar.");
   ST_Init();

   startupmsg("MN_Init","Init menu.");
   MN_Init();

   startupmsg("F_Init", "Init finale.");
   F_Init();

   startupmsg("S_Init","Setting up sound.");
   S_Init(snd_SfxVolume, snd_MusicVolume);

   //
   // NETCODE_FIXME: Netgame check.
   //

   startupmsg("D_CheckNetGame","Check netgame status.");
   D_CheckNetGame();

   // haleyjd 04/10/03: set coop gametype
   // haleyjd 04/01/10: support -solo-net parameter
   if((netgame || M_CheckParm("-solo-net")) && GameType == gt_single)
   {
      GameType = DefaultGameType = gt_coop;
      G_SetDefaultDMFlags(0, true);
   }

   if(!clientserver)
   {
      // check for command-line override of dmflags
      if((p = M_CheckParm("-dmflags")) && p < myargc-1)
      {
         dmflags = default_dmflags = (unsigned int)atoi(myargv[p+1]);
      }
   }

   // haleyjd: this SHOULD be late enough...
   startupmsg("G_LoadDefaults", "Init keybindings.");
   G_LoadDefaults();

   //
   // CONSOLE_FIXME: This may not be the best time for scripts.
   // Reconsider this as is appropriate.
   //

   // haleyjd: AFTER keybindings for overrides
   startupmsg("D_AutoExecScripts", "Executing console scripts.");
   D_AutoExecScripts();

   // haleyjd 08/20/07: autoload dir csc's
   D_GameAutoloadCSC();

   // haleyjd 03/10/03: GFS csc's
   if(haveGFS)
   {
      D_ProcessGFSCsc(gfs);

      // this is the last GFS action, so free the gfs now
      G_FreeGFS(gfs);
      haveGFS = false;
   }

   // haleyjd: GFS is no longer valid from here!

   // haleyjd 08/20/07: done with base/game/autoload directory
   D_CloseAutoloadDir();

   if(devparm) // we wait if in devparm so the user can see the messages
   {
      printf("devparm: press a key..\n");
      getchar();
   }

   ///////////////////////////////////////////////////////////////////
   //
   // Must be in Graphics mode by now!
   //

   // check

   if(in_textmode)
      D_SetGraphicsMode();

#ifndef EE_NO_SMALL_SUPPORT
   // initialize Small, load game scripts; 01/07/07: init ACS
   SM_InitSmall();
   SM_InitGameScript();
#endif
   ACS_Init();

   // haleyjd: updated for eternity
   C_Printf("\n");
   C_Separator();
   C_Printf("\n"
            FC_HI "The Eternity Engine\n"
            FC_NORMAL "By James Haley and Stephen McGranahan\n"
            "http://doomworld.com/eternity/ \n"
            "Version %i.%02i.%02i '%s' \n\n",
            version/100, version%100, SUBVERSION, version_name);

#if defined(TOKE_MEMORIAL)
   // haleyjd 08/30/06: for v3.33.50 Phoenix: RIP Toke
   C_Printf(FC_GREEN "Dedicated to the memory of our friend\n"
            "Dylan 'Toke' McIntosh  Jan 14 1983 - Aug 19 2006\n");
#elif defined(ASSY_MEMORIAL)
   // haleyjd 08/29/08
   C_Printf(FC_GREEN "Dedicated to the memory of our friend\n"
            "Jason 'Amaster' Masihdas 12 Oct 1981 - 14 Jun 2007\n");
#endif

   // haleyjd: if we didn't do textmode startup, these didn't show up
   //  earlier, so now is a cool time to show them :)
   // haleyjd: altered to prevent printf attacks
   if(!textmode_startup)
   {
      if(*startup1) C_Printf("%s", startup1);
      if(*startup2) C_Printf("%s", startup2);
      if(*startup3) C_Printf("%s", startup3);
      if(*startup4) C_Printf("%s", startup4);
      if(*startup5) C_Printf("%s", startup5);
   }

   if(!textmode_startup && !devparm)
      C_Update();

   idmusnum = -1; //jff 3/17/98 insure idmus number is blank

#if 0
   // check for a driver that wants intermission stats
   if((p = M_CheckParm("-statcopy")) && p < myargc-1)
   {
      // for statistics driver
      extern void *statcopy;

      // killough 5/2/98: this takes a memory
      // address as an integer on the command line!

      statcopy = (void *)atoi(myargv[p+1]);
      usermsg("External statistics registered.");
   }
#endif

   if(clientserver)
   {
      if(!CS_DEMO)
         CS_LoadWADs();
      if(M_CheckParm("-csplaydemo") || M_CheckParm("-record"))
      {
         if(M_CheckParm("-csplaydemo") && M_CheckParm("-record"))
         {
            I_Error("Cannot specify both -csplaydemo and -record.\n");
         }

         if(CS_SERVER)
         {
            I_Error(
               "Cannot record or playback demos from the command-line in c/s "
               "server mode.\n"
            );
         }

         if(M_CheckParm("-record"))
         {
            printf("CL_Init: Recording new c/s demo.\n");
            if(!CS_RecordDemo())
            {
               I_Error("Error recording demo: %s\n", CS_GetDemoErrorMessage());
            }
         }
         else if(p < myargc - 2)
         {
            cs_demo_archive_path = strdup(myargv[p + 1]);
            cs_demo_playback = true;
         }
         else
         {
            I_Error("CL_Init: No demo file specified.\n");
         }
      }
   }
   else
   {
      // sf: -blockmap option as a variable now
      if(M_CheckParm("-blockmap"))
      {
         r_blockmap = true;
      }

      // start the appropriate game based on parms

      // killough 12/98:
      // Support -loadgame with -record and reimplement -recordfrom.
      if((slot = M_CheckParm("-recordfrom")) && (p = slot+2) < myargc)
      {
         G_RecordDemo(myargv[p]);
      }
      else
      {
         slot = M_CheckParm("-loadgame");
         if((p = M_CheckParm("-record")) && ++p < myargc)
         {
            autostart = true;
            G_RecordDemo(myargv[p]);
         }
      }

      if((p = M_CheckParm ("-fastdemo")) && ++p < myargc)
      {                                 // killough
         fastdemo = true;                // run at fastest speed possible
         timingdemo = true;              // show stats after quit
         G_DeferedPlayDemo(myargv[p]);
         singledemo = true;              // quit after one demo
      }
      else if((p = M_CheckParm("-timedemo")) && ++p < myargc)
      {
         // haleyjd 10/16/08: restored to MBF status
         singletics = true;
         timingdemo = true;            // show stats after quit
         G_DeferedPlayDemo(myargv[p]);
         singledemo = true;            // quit after one demo
      }
      else if((p = M_CheckParm("-playdemo")) && ++p < myargc)
      {
         G_DeferedPlayDemo(myargv[p]);
         singledemo = true;          // quit after one demo
      }
   }

   if(CS_DEMO)
   {
   }
   else if(CS_SERVER)
   {
      CS_InitNew();
   }
   else if(CS_CLIENT)
   {
      D_StartTitle();
      C_SetConsole();
      CL_Connect();
   }
   else
   {
      startlevel = strdup(G_GetNameForMap(startepisode, startmap));
      if(slot && ++slot < myargc)
      {
         char *file = NULL;
         size_t len = M_StringAlloca(&file, 2, 26, basesavegame, savegamename);
         slot = atoi(myargv[slot]);       // killough 3/16/98: add slot info
         G_SaveGameName(file, len, slot); // killough 3/22/98
         G_LoadGame(file, slot, true);    // killough 5/15/98: add command flag
      }
      else if(!singledemo) // killough 12/98
      {
         if(autostart || netgame)
         {
            G_InitNewNum(startskill, startepisode, startmap);
            if(demorecording)
            {
               G_BeginRecording();
            }
         }
         else
         {
            D_StartTitle(); // start up intro loop
         }
   }

      /*
      if(netgame)
      {
         //
         // NETCODE_FIXME: C_SendNetData.
         //
         C_SendNetData();

         if(demorecording)
            G_BeginRecording();
      }
      */
   }

   // a lot of alloca calls are made during startup; kill them all now.
   Z_FreeAlloca();
}

//=============================================================================
//
// Main Routine
//

//
// D_DoomMain
//
void D_DoomMain(void)
{
   int i;

   D_DoomInit();

   oldgamestate = wipegamestate = gamestate;

   // haleyjd 02/23/04: fix problems with -warp
   if(autostart)
   {
      oldgamestate = -1;
      redrawborder = true;
   }

   // [CG] Servers should advertise and send a state update themselves at this
   //      point.
   if(CS_SERVER)
   {
      SV_MasterAdvertise();
      // [CG] Add update requests for each master.
      for(i = 0; i < master_server_count; i++)
      {
         SV_AddUpdateRequest(&master_servers[i]);
      }
      SV_MasterUpdate();
      // [CG] C/S server can never wait for screen wiping.
      wipewait = 0;
   }

   // killough 12/98: inlined D_DoomLoop

   while(1)
   {
      // frame synchronous IO operations
      I_StartFrame();

      // [CG] TryRunTics has the vanilla netgame model woven into it pretty
      //      tightly, so rather than mess with it I've just created a c/s
      //      version of the function.
      if(clientserver)
      {
         // [CG] CL_TryRunTics (which is called by CS_TryRunTics if clientside)
         //      will *wait* at least one TIC, but it won't actually run one
         //      until it gets the go-ahead from the server.  So it's possible
         //      for this function to return without having built a ticcmd,
         //      without having ran G_Ticker, and without having incremented
         //      gametic.  This is intended, as other things like the console
         //      and menu need to continue ticking whether or not the server
         //      sends any messages.
         CS_TryRunTics();
      }
      else
      {
         TryRunTics(); // will run at least one tic
      }

      // killough 3/16/98: change consoleplayer to displayplayer
      S_UpdateSounds(players[displayplayer].mo); // move positional sounds

      // Update display, next frame, with current state.
      D_Display();

      // Sound mixing for the buffer is synchronous.
      I_UpdateSound();

      // Synchronous sound output is explicitly called.
      // Update sound output.
      I_SubmitSound();

      // haleyjd 12/06/06: garbage-collect all alloca blocks
      Z_FreeAlloca();
   }
}

//=============================================================================
//
// SMMU Runtime WAD File Loading
//

// re init everything after loading a wad

void D_ReInitWadfiles(void)
{
   R_FreeData();
   E_ProcessNewEDF();   // haleyjd 03/24/10: process any new EDF lumps
   D_ProcessDEHQueue(); // haleyjd 09/12/03: run any queued DEHs
   if(clientserver)
   {
      C_InitBackdrop();
      V_InitColorTranslation();
      V_InitBox();
      I_SetPalette(W_CacheLumpName("PLAYPAL", PU_CACHE));
      E_UpdateFonts();
   }
   R_Init();
   P_Init();
   if(clientserver)
   {
      HU_Init();
      C_ReloadFont();
      ST_Init();
      MN_Init();
   }
}

// FIXME: various parts of this routine need tightening up
void D_NewWadLumps(FILE *handle, int sound_update_type)
{
   int i, format;
   char wad_firstlevel[9];

   memset(wad_firstlevel, 0, 9);

   for(i = 0; i < w_GlobalDir.numlumps; ++i)
   {
      if(w_GlobalDir.lumpinfo[i]->file != handle)
         continue;

      // haleyjd: changed check for "THINGS" lump to a fullblown
      // P_CheckLevel call -- this should fix some problems with
      // some crappy wads that have partial levels sitting around

      if((format = P_CheckLevel(&w_GlobalDir, i)) != LEVEL_FORMAT_INVALID) // a level
      {
         char *name = w_GlobalDir.lumpinfo[i]->name;

         // ignore ones called 'start' as these are checked elsewhere
         if((!*wad_firstlevel && strcmp(name, "START")) ||
            strncmp(name, wad_firstlevel, 8) < 0)
            strncpy(wad_firstlevel, name, 8);

         // haleyjd: move up to the level's last lump
         i += (format == LEVEL_FORMAT_HEXEN ? 11 : 10);
         continue;
      }

      // new sound
      if(!strncmp(w_GlobalDir.lumpinfo[i]->name, "DSCHGUN",8)) // chaingun sound
      {
         S_Chgun();
         continue;
      }

      if(!strncmp(w_GlobalDir.lumpinfo[i]->name, "DS", 2))
      {
         switch(sound_update_type)
         {
         case 0: // called during startup, defer processing
            S_UpdateSoundDeferred(i);
            break;
         case 1: // called during gameplay
            S_UpdateSound(i);
            break;
         }
         continue;
      }

      // new music -- haleyjd 06/17/06: should be strncasecmp, not strncmp
      if(!strncasecmp(w_GlobalDir.lumpinfo[i]->name, GameModeInfo->musPrefix,
                      strlen(GameModeInfo->musPrefix)))
      {
         S_UpdateMusic(i);
         continue;
      }

      // skins
      if(!strncmp(w_GlobalDir.lumpinfo[i]->name, "S_SKIN", 6))
      {
         P_ParseSkin(i);
         continue;
      }
   }

   if(*wad_firstlevel && (!*firstlevel ||
      strncmp(wad_firstlevel, firstlevel, 8) < 0)) // a new first level?
      strcpy(firstlevel, wad_firstlevel);
}

void usermsg(const char *s, ...)
{
   static char msg[1024];
   va_list v;

   va_start(v,s);
   pvsnprintf(msg, sizeof(msg), s, v); // print message in buffer
   va_end(v);

   if(in_textmode)
   {
      puts(msg);
   }
   else
   {
      C_Puts(msg);
      C_Update();
   }
}

// add a new .wad file
// returns true if successfully loaded

boolean D_AddNewFile(const char *s)
{
   Console.showprompt = false;
   if(W_AddNewFile(&w_GlobalDir, s))
      return false;
   modifiedgame = true;
   D_AddFile(s, ns_global, NULL, 0, 0);   // add to the list of wads
   C_SetConsole();
   D_ReInitWadfiles();
   return true;
}

//============================================================================
//
// Console Commands
//

VARIABLE_TOGGLE(d_drawfps, NULL, onoff);
CONSOLE_VARIABLE(d_drawfps, d_drawfps, 0) {}

void D_AddCommands(void)
{
   C_AddCommand(d_drawfps);
}

//----------------------------------------------------------------------------
//
// $Log: d_main.c,v $
// Revision 1.47  1998/05/16  09:16:51  killough
// Make loadgame checksum friendlier
//
// Revision 1.46  1998/05/12  10:32:42  jim
// remove LEESFIXES from d_main
//
// Revision 1.45  1998/05/06  15:15:46  jim
// Documented IWAD routines
//
// Revision 1.44  1998/05/03  22:26:31  killough
// beautification, declarations, headers
//
// Revision 1.43  1998/04/24  08:08:13  jim
// Make text translate tables lumps
//
// Revision 1.42  1998/04/21  23:46:01  jim
// Predefined lump dumper option
//
// Revision 1.39  1998/04/20  11:06:42  jim
// Fixed print of IWAD found
//
// Revision 1.37  1998/04/19  01:12:19  killough
// Fix registered check to work with new lump namespaces
//
// Revision 1.36  1998/04/16  18:12:50  jim
// Fixed leak
//
// Revision 1.35  1998/04/14  08:14:18  killough
// Remove obsolete adaptive_gametics code
//
// Revision 1.34  1998/04/12  22:54:41  phares
// Remaining 3 Setup screens
//
// Revision 1.33  1998/04/11  14:49:15  thldrmn
// Allow multiple deh/bex files
//
// Revision 1.32  1998/04/10  06:31:50  killough
// Add adaptive gametic timer
//
// Revision 1.31  1998/04/09  09:18:17  thldrmn
// Added generic startup strings for BEX use
//
// Revision 1.30  1998/04/06  04:52:29  killough
// Allow demo_insurance=2, fix fps regression wrt redrawsbar
//
// Revision 1.29  1998/03/31  01:08:11  phares
// Initial Setup screens and Extended HELP screens
//
// Revision 1.28  1998/03/28  15:49:37  jim
// Fixed merge glitches in d_main.c and g_game.c
//
// Revision 1.27  1998/03/27  21:26:16  jim
// Default save dir offically . now
//
// Revision 1.26  1998/03/25  18:14:21  jim
// Fixed duplicate IWAD search in .
//
// Revision 1.25  1998/03/24  16:16:00  jim
// Fixed looking for wads message
//
// Revision 1.23  1998/03/24  03:16:51  jim
// added -iwad and -save parms to command line
//
// Revision 1.22  1998/03/23  03:07:44  killough
// Use G_SaveGameName, fix some remaining default.cfg's
//
// Revision 1.21  1998/03/18  23:13:54  jim
// Deh text additions
//
// Revision 1.19  1998/03/16  12:27:44  killough
// Remember savegame slot when loading
//
// Revision 1.18  1998/03/10  07:14:58  jim
// Initial DEH support added, minus text
//
// Revision 1.17  1998/03/09  07:07:45  killough
// print newline after wad files
//
// Revision 1.16  1998/03/04  08:12:05  killough
// Correctly set defaults before recording demos
//
// Revision 1.15  1998/03/02  11:24:25  killough
// make -nodraw -noblit work generally, fix ENDOOM
//
// Revision 1.14  1998/02/23  04:13:55  killough
// My own fix for m_misc.c warning, plus lots more (Rand's can wait)
//
// Revision 1.11  1998/02/20  21:56:41  phares
// Preliminarey sprite translucency
//
// Revision 1.10  1998/02/20  00:09:00  killough
// change iwad search path order
//
// Revision 1.9  1998/02/17  06:09:35  killough
// Cache D_DoomExeDir and support basesavegame
//
// Revision 1.8  1998/02/02  13:20:03  killough
// Ultimate Doom, -fastdemo -nodraw -noblit support, default_compatibility
//
// Revision 1.7  1998/01/30  18:48:15  phares
// Changed textspeed and textwait to functions
//
// Revision 1.6  1998/01/30  16:08:59  phares
// Faster end-mission text display
//
// Revision 1.5  1998/01/26  19:23:04  phares
// First rev with no ^Ms
//
// Revision 1.4  1998/01/26  05:40:12  killough
// Fix Doom 1 crashes on -warp with too few args
//
// Revision 1.3  1998/01/24  21:03:04  jim
// Fixed disappearence of nomonsters, respawn, or fast mode after demo play or IDCLEV
//
// Revision 1.1.1.1  1998/01/19  14:02:53  rand
// Lee's Jan 19 sources
//
//----------------------------------------------------------------------------