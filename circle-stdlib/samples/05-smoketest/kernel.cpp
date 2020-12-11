//
// kernel.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2014-2020  R. Stange <rsta2@o2online.de>
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
#include "kernel.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;

namespace
{
    const char FromKernel[] = "Smoke Test";
}

CKernel::CKernel (void)
    : CStdlibAppStdio ("05-smoketest")
{
    mActLED.Blink (5);  // show we are alive
}

CStdlibApp::TShutdownMode
CKernel::Run (void)
{
    mLogger.Initialize (&m_LogFile);

    mLogger.Write (GetKernelName (), LogNotice,
                    "Compile time: " __DATE__ " " __TIME__);

    mLogger.Write (GetKernelName (), LogNotice, "A timer will stop the loop");

    bool volatile nTimerFired = false;

    // start timer to elapse after 5 seconds
    mTimer.StartKernelTimer (5 * HZ, TimerHandler,
                              const_cast<bool*> (&nTimerFired));

    // generate a log message every second
    unsigned nTime = mTimer.GetTime ();
    while (!nTimerFired)
    {
        while (nTime == mTimer.GetTime ())
        {
            // just wait a second
        }

        nTime = mTimer.GetTime ();

        mLogger.Write (GetKernelName (), LogNotice, "Time is %u", nTime);
    }

    IoTest ();

    mLogger.Write (GetKernelName (), LogNotice, "Shutting down...");

    return ShutdownHalt;
}

void
CKernel::Report(const char *s)
{
    mLogger.Write (GetKernelName (), LogNotice, "%s", s);
}

void
CKernel::PErrorExit(const char *s)
{
    perror (s);
    mLogger.Write (GetKernelName (), LogError, "error '%s', exiting with code 1...");
    exit (1);
}

void
CKernel::IoTest (void)
{
    Report ("Test file operations...");

    string const stdio_filename = "stdio.txt";

    FILE *fp = fopen (stdio_filename.c_str (), "w");

    if (fp == nullptr)
    {
        PErrorExit ("Cannot open file for writing with fopen ()");
    }

    fprintf (fp, "Opened file with (FILE *) %p\n", fp);
    fclose (fp);

    fp = fopen (stdio_filename.c_str (), "r");
    if (fp == nullptr)
    {
         PErrorExit ("Cannot open file for reading with fopen()");
    }

    Report ("fopen () test succeeded");

    char buf[200];
    char *p;

    while ((p = fgets (buf, sizeof(buf), fp)) != nullptr)
    {
        size_t const len = strlen (buf);
        if (len > 0 && buf[len - 1] == '\n')
        {
            buf[len - 1] = 0;
        }
        printf ("Read from file: '%s'\n", p);
    }

    // Read the blank after "Opened" by seeking
    if (fseek (fp, 6L, SEEK_SET) != 0)
    {
        PErrorExit ("fseek () failed");
    }

    Report ("fseek () test succeeded");

    char c;
    if (fread (&c, 1, 1, fp) != 1)
    {
        PErrorExit ("fread failed");
    }

    Report ("fread () test succeeded");

    if (c != ' ')
    {
        fprintf (stderr, "Bad read result, expected ' ', got '%c'\n", c);
        exit (1);
    }

    Report ("fseek () test succeeded");

    if (fclose (fp) != 0)
    {
        PErrorExit ("fclose () failed");
    }

    Report ("fclose () test succeeded");

    Report ("Test directory operations...");

    string const dirname = "subdir";

    if (mkdir (("/" + dirname).c_str(), 0) != 0)
    {
        PErrorExit ("mkdir () failed");
    }

    Report ("mkdir () test succeeded");

    DIR * const dir = opendir ("/");
    if (dir == nullptr)
    {
        PErrorExit ("opendir (\"/\") failed");
    }

    Report ("opendir () test succeeded");

    bool found = false;
    while (true)
    {
        errno = 0;
        struct dirent const *const dp = readdir (dir);
        if (dp != nullptr)
        {
            printf ("\t%s\n", dp->d_name);
            found = found || dp->d_name == dirname;
        }
        else
        {
            if (errno != 0 && errno != ENOENT)
            {
                PErrorExit ("readdir () failed");
            }

            break;
        }
    }

    if (!found)
    {
        PErrorExit (("Did not find directory '" + dirname + "'").c_str ());
    }

    Report ("readdir () test succeeded");

    printf ("Rewinding directory...\n");
    rewinddir (dir);

    Report ("rewinddir () test succeeded");

    printf ("Listing \".\" directory with readdir_r:\n");
    while (true)
    {
        struct dirent de;
        struct dirent *dep;
        int error = readdir_r (dir, &de, &dep);
        if (error != 0)
        {
            PErrorExit ("readdir_r () failed");
        }
        if (dep != nullptr)
        {
            printf ("\t%s\n", dep->d_name);
        }
        else
        {
            break;
        }
    }

    printf ("Closing directory...\n");
    if (closedir (dir) != 0)
    {
        PErrorExit ("closedir() failed ");
    }

    Report ("closedir () test succeeded");

    string const subdir_filename = "/" + dirname + "/file.txt";

    fp = fopen (subdir_filename.c_str (), "w");
    if (fp == nullptr)
    {
        PErrorExit ("fopen () failed");
    }

    if (fprintf (fp, "bla bla") < 0)
    {
        PErrorExit ("fprintf () failed");
    }

    if (fclose (fp) != 0)
    {
        PErrorExit ("fclose () failed");
    }

    string const filename2 = "/file2.txt";

    if (rename (subdir_filename.c_str (), filename2.c_str ()) < 0)
    {
        PErrorExit ("rename () failed");
    }

    Report ("rename () test succeeded");

    if (unlink (filename2.c_str ()) < 0)
    {
        PErrorExit ("unlink () failed");
    }

    Report ("unlink () test succeeded");
}

void
CKernel::TimerHandler (TKernelTimerHandle, void *pParam, void*)
{
    bool *pTimerFired = static_cast<bool*> (pParam);
    *pTimerFired = true;
}
