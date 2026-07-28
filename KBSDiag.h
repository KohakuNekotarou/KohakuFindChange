//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  A diagnostic channel that can be read from OUTSIDE the running application.
//
//  Some of what KBS needs to verify is invisible from every angle otherwise: the scripting DOM
//  cannot see a panel's status line, an InDesign panel's widgets have no HWND for Win32 to read
//  (only the panel frame does), and a screen capture needs the application to be frontmost and a
//  person to look at it. This writes those values to a plain file, so whoever is driving a test -
//  a script, or a person, or an assistant - can simply read them.
//
//  OFF unless the marker file exists. Without the marker a call costs one getenv and one failed
//  fopen and writes nothing, which is cheap enough to leave in the shipping plug-in rather than
//  fencing it behind a build flag that then never gets built when it is wanted.
//
//      switch on   create  %TEMP%\kbs-diag.on      (TMPDIR on the Mac)
//      read                %TEMP%\kbs-diag.log     (appended to, never truncated here)
//      switch off  delete the marker; the log stays until it is deleted
//
//========================================================================================

#ifndef __KBSDiag_h__
#define __KBSDiag_h__

#include "PMString.h"

namespace KBSDiag
{
	/** Is the channel switched on (does the marker file exist)? Worth asking before building an
	    expensive message; a plain Log call checks this itself. */
	bool IsOn();

	/** Append one line. A newline is added here - callers pass the text alone. */
	void Log(const char* line);

	/** As above, for text that came out of the application (a step name, a chapter name). Written
	    as UTF-8, so a Japanese chapter name survives the trip out. */
	void Log(const PMString& line);
}

#endif // __KBSDiag_h__
