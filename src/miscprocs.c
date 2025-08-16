/*
** This file is part of the Matrix Brandy Basic VI Interpreter.
** Copyright (C) 2000-2014 David Daniels
** Copyright (C) 2018-2024 Michael McConnell and contributors
**
** Brandy is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2, or (at your option)
** any later version.
**
** Brandy is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with Brandy; see the file COPYING.  If not, write to
** the Free Software Foundation, 59 Temple Place - Suite 330,
** Boston, MA 02111-1307, USA.
**
**
**      This module contains a selection of miscellaneous functions
**
** 06-Jan-2019 JGH: secure_tmpnam() tweeked.
**
*/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include "common.h"
#include "target.h"
#include "basicdefs.h"
#include "tokens.h"
#include "errors.h"
#include "keyboard.h"
#include "screen.h"
#include "statement.h"
#include "miscprocs.h"

#ifdef TARGET_RISCOS
#include "swis.h"
#else
# include <sys/stat.h>
#endif

#ifdef TARGET_RISCOS
int64 llabs(int64 i) {
  return (i >= 0) ? i : i * (int64)-1;
}
#endif

/*
** 'get_integer' returns the four byte integer found at offset
** 'offset' in the Basic workspace. This is used to return the
** value pointed at by an indirection operator
*/
int32 get_integer(size_t offset) {
  return basicvars.memory[offset]+(basicvars.memory[offset+1]<<BYTESHIFT)+
   (basicvars.memory[offset+2]<<(2*BYTESHIFT))+(basicvars.memory[offset+3]<<(3*BYTESHIFT));
}

/*
** 'get_int64' returns the eight byte integer found at offset
** 'offset' in the Basic workspace. This is used to return the
** value pointed at by an indirection operator
*/
int64 get_int64(size_t offset) {
  return ((int64)get_integer(offset) & 0xFFFFFFFFl) + (((int64)(get_integer(offset+4)) & 0xFFFFFFFFl) << 32);
}

/*
** 'skip_blanks' skips white space characters. The difference between
** this and 'skip' is that this one works on 'char' data whilst 'skip'
** deals with 'byte' data
*/
char *skip_blanks(char *p) {
  while (*p==' ' || *p==asc_TAB) p++;
  return p;
}

/*
** 'skip' is used to skip the 'white space' characters in a tokenised line
*/
byte *skip(byte *p) {
  while (*p==' ' || *p==asc_TAB) p++;
  return p;
}

/*
** 'get_float' returns the eight byte floating point value found
** at offset 'offset' in the Basic workspace. This is used to
** return the value pointed at by an indirection operator
*/
float64 get_float(size_t offset) {
  float64 value;
  memmove(&value, (void *)offset, sizeof(float64));
  return value;
}


/*
** 'store_integer' is called to save an integer value at an arbitrary
** offset within the basic workspace. 'offset' is the location at
** which the value is to be stored
*/
void store_integer(size_t offset, int32 value) {
  basicvars.memory[offset] = value;
  basicvars.memory[offset+1] = value>>BYTESHIFT;
  basicvars.memory[offset+2] = value>>(2*BYTESHIFT);
  basicvars.memory[offset+3] = value>>(3*BYTESHIFT);
}

/*
** 'store_in64' is called to save a 64-bit int value at an arbitrary
** offset within the basic workspace. 'offset' is the location at
** which the value is to be stored
*/
void store_int64(size_t offset, int64 value) {
  basicvars.memory[offset] = value;
  basicvars.memory[offset+1] = value>>BYTESHIFT;
  basicvars.memory[offset+2] = value>>(2*BYTESHIFT);
  basicvars.memory[offset+3] = value>>(3*BYTESHIFT);
  basicvars.memory[offset+4] = value>>(4*BYTESHIFT);
  basicvars.memory[offset+5] = value>>(5*BYTESHIFT);
  basicvars.memory[offset+6] = value>>(6*BYTESHIFT);
  basicvars.memory[offset+7] = value>>(7*BYTESHIFT);
}

/*
** 'store_float' is called to save a floating point value at an
** arbitrary offset within the basic workspace. 'offset' is the
** location at which the value is to be stored
*/
void store_float(size_t offset, float64 value) {
  memmove((void *)offset, &value, sizeof(float64));
}

/* Mode 7 screen memory adjust */
#ifdef USE_SDL
size_t m7offset(size_t p) {
  if (p >= MODE7FB && p <= (MODE7FB + 1023)) {
    p = (p - MODE7FB) + (size_t)mode7frame;
  }
  return p;
}
#endif

/*
** 'save_current' saves the value of the token pointer, current.
** There is a stack of saved values used primarily for dealing
** with READ and EVAL as well as when parsing procedure and
** function definitions. The stack is of limited size but there
** should never be more than three entries on it. Nevertheless
** there is a check for overflow.
*/
void save_current(void) {
  if (basicvars.curcount==MAXCURCOUNT) {
    error(ERR_OPSTACK);
    return;
  }
  basicvars.savedcur[basicvars.curcount] = basicvars.current;
  basicvars.curcount++;
}

/*
** 'restore_current' sets 'current' back to its proper value and
** marks it as safe to use
*/
void restore_current(void) {
  basicvars.curcount--;
  basicvars.current = basicvars.savedcur[basicvars.curcount];
}

char cstring[MAXNAMELEN+4];

/*
** 'tocstring' takes a string which is either length or control-
** character terminated and returns a pointer to a copy of that
** string as a null-terminated C string. It also expands a 'PROC'
** or 'FN' token at the start of a name to its text form
*/
char *tocstring(char *cp, int32 len) {
  int32 n;
  if (len==0) return "";
  if (len>=MAXNAMELEN) len = MAXNAMELEN-1;
  switch (*CAST(cp, byte *)) {
  case BASTOKEN_PROC:
    STRLCPY(cstring, "PROC", MAXNAMELEN);
    n = 4;
    cp++;
    break;
  case BASTOKEN_FN:
    STRLCPY(cstring, "FN", MAXNAMELEN);
    n = 2;
    cp++;
    break;
  case BASTOKEN_STATICVAR: case BASTOKEN_STATINDVAR:
    cstring[0] = *(cp+1)+'@';
    cstring[1] = '%';
    cstring[2] = asc_NUL;
    return &cstring[0];
  default:
    n = 0;
  }
  while (*cp>=' ' && n<len) {
    cstring[n] = *cp;
    cp++;
    n++;
  }
  if (n==MAXNAMELEN) {  /* Put ellipsis at end of name if it has been truncated */
    cstring[n] = cstring[n+1] = cstring [n+2] = '.';
    n+=3;
  }
  cstring[n] = asc_NUL;
  return &cstring[0];
}

/*
** 'find_library' checks to see if the address 'wanted' lies
** within a library. If it does it returns a pointer to the
** library structure of that library. If not, it returns NIL.
*/
library *find_library(byte *wanted) {
  library *lp;
  lp = basicvars.liblist;       /* Check if it is in a library */
  while (lp!=NIL && (wanted<lp->libstart || wanted>=lp->libstart+lp->libsize)) lp = lp->libflink;
  if (lp==NIL) {        /* Not found. Check installed libraries */
    lp = basicvars.installist;
    while (lp!=NIL && (wanted<lp->libstart || wanted>=lp->libstart+lp->libsize)) lp = lp->libflink;
  }
  return lp;
}

/*
** 'find_linestart' finds the start of the line into which 'wanted'
** points. It returns a pointer to the start of the line or NIL if
** the pointer is out of range (and probably points at 'thisline').
** It looks in both the program in the Basic workspace and any
** libraries that have been loaded.
** There is no pointer kept to the start of the current line, nor is
** it possible to scan backwards through the line to find its start.
** All that can be done is to scan from the start of the program.
** Luckily this function is only needed in the error handling and
** trace code
*/
byte *find_linestart(byte *wanted) {
  byte *p, *last;
  library *lp;
  p = NIL;
  if (wanted>=basicvars.page && wanted<basicvars.top)   /* Address is in loaded program */
    p = basicvars.start;
  else {
    lp = find_library(wanted);  /* Check if it is in a library */
    if (lp==NIL) return NIL;    /* Could not find where address points */
    p = lp->libstart;   /* 'wanted' points into a library */
  }
  last = p;
  while (p<=wanted) {
    last = p;
    p+=GET_LINELEN(p);
  }
  return last;
}

/*
** 'find_line' searches for line 'line' in the program. It returns
** a pointer to where that line would be found, that is, it will
** point to a line in the source (or possibly the end marker) which
** will either be an exact match for the line or have a line number
** greater than the desired value. It is up to the calling routine
** determine which of these it is.
** One complication is that a reference to a line number in a library
** must result in a search of that library, not the program in memory.
** The function checks the value of the current token pointer,
** basicvars.current, to work out where to look. If the point where
** the line number is required is in a library it checks that library
** for the line otherwise it searches the program in memory
*/
byte *find_line(int32 lineno) {
  byte *p;

  if (basicvars.runflags.running) {     /* Running program => search program or library */
    byte *cp = basicvars.current;     /* This is just to reduce the amount of typing */
    if (cp>=basicvars.page && cp<basicvars.top)         /* Check program for line */
      p = basicvars.start;
    else {      /* Check libraries */
      library *lp = find_library(cp);
      if (lp==NIL) {
        error(ERR_BROKEN, __LINE__, "misc"); /* Could not find line number anywhere */
        return NULL;
      }
      p = lp->libstart;
    }
  } else {        /* Not running a program - Line can only be in the program in memory */
    p = basicvars.start;
  }
  while (GET_LINENO(p)<lineno) p+=GET_LINELEN(p);
  return p;
}

/*
** 'show_byte' displays the contents of memory between the addresses
**'low' and 'high' as bytes of data
*/
void show_byte(size_t low, size_t high) {
  int32 n, ll, count;
  byte ch;
  if (low>high) return;
  count = high-low;
  for (n=0; n<count; n+=16) {
    int32 x = 0;
    emulate_printf("%06X  ", low);
    for (ll=0; ll<16; ll++) {
      if (n+ll>=count)
        emulate_printf("   ");
      else {
        emulate_printf("%02X ", basicvars.memory[low+ll]);
      }
      x++;
      if (x==4) {
        x = 0;
        emulate_vdu(' ');
      }
    }
    for (ll = 0; ll<16; ll++) {
      if (n+ll>=count)
        emulate_vdu('.');
      else {
        ch = basicvars.memory[low+ll];
        if (ch>=' ' && ch<='~')
          emulate_vdu(ch);
        else {
          emulate_vdu('.');
        }
      }
    }
    emulate_vdu('\r');
    emulate_vdu('\n');
    low+=16;
  }
}

/*
** 'show_word' displays the contents of memory between the addresses
**'low' and 'high' as four-byte words of data
*/
void show_word(size_t low, size_t high) {
  int32 n, ll, count;
  byte ch;
  low = ALIGN(low);
  high = ALIGN(high);
  if (low>high) return;
  count = high-low;
  for (n=0; n<count; n+=16) {
    emulate_printf("%06X  +%04X  %08X  %08X  %08X  %08X  ",
     low, n, get_integer(low), get_integer(low+4), get_integer(low+8), get_integer(low+12));
    for (ll = 0; ll<16; ll++) {
      if (n+ll>=count)
        emulate_vdu('.');
      else {
        ch = basicvars.memory[low+ll];
        if (ch>=' ' && ch<='~')
          emulate_vdu(ch);
        else {
          emulate_vdu('.');
        }
      }
    }
    emulate_vdu('\r');
    emulate_vdu('\n');
    low+=16;
  }
}

/*
** 'strip' strips trailing blanks, new line characters and so forth
** from the string passed to it
*/
static void strip(char line[]) {
  int32 n;
  n = strlen(line);
  if (n!=0) {   /* Delete trailing rubbish */
    do
      n--;
    while (n>=0 && isspace(line[n]));
    n++;
  }
  line[n] = asc_NUL;
}

/*
** 'read_line' reads a line from the keyboard or whatever stdin
** points at and returns it as a null-terminated string with
** trailing blanks and newlines removed. It returns TRUE if
** everything was okay or FALSE if the input stream returned
** an end-of-file condition (this will most likely happen if
** input is being taken from a file). Pressing escape is handled
** by this code. 'read_line' is used when the line to be read
** contains nothing
*/
boolean read_line(char line[], int32 linelen) {
  readstate result;
  line[0] = asc_NUL;
  result = kbd_readline(line, linelen, 0);
  if (result==-READ_ESC || basicvars.escape) {
    error(ERR_ESCAPE);
    return FALSE;
  }
  if (result==-READ_EOF) return FALSE;           /* Read failed - Hit EOF */
  strip(line);
  return TRUE;
}

/*
** 'amend_line' reads a line from the keyboard or whatever stdin
** points at and returns it as a null-terminated string with
** trailing blanks and newlines removed. It returns TRUE if
** everything was okay or FALSE if the input stream returned
** an end-of-file condition (this will most likely happen if
** input is being taken from a file). Pressing escape is handled
** by this code. 'amend_line' is used when the line to be read
** is prefilled with a string
*/
boolean amend_line(char line[], int32 linelen) {
  readstate result;
  result = kbd_readline(line, linelen,0);
  if (result==-READ_ESC || basicvars.escape) {
    error(ERR_ESCAPE);
    return FALSE;
  }
  if (result==-READ_EOF) return FALSE;           /* Read failed - Hit EOF */
  strip(line);
  return TRUE;
}

/*
** 'secure_tmpnam' generates a temporary filename and opens it securely.
** Returns a FILE * pointer on success, NULL on failure.
** ** THE FILENAME BUFFER MUST BE OF SUFFICIENT SIZE FOR USE BY tmpnam.
** ** NO CHECK IS MADE.
*/
FILE *secure_tmpnam(char *name) {
#ifdef TARGET_MINGW
  FILE *fdes;
  fdes=tmpfile();
  if (!fdes) return NULL;
  return fdes;
#else
#if defined(BODGEMGW) | defined(BODGESDL) | defined(__TARGET_SCL__)
  STRLCPY(name, "/tmp/.brandy.XXXXXX", MAXNAMELEN);
  return fopen(name, "w+");
#else
  int fdes;
  STRLCPY(name, "/tmp/.brandy.XXXXXX", MAXNAMELEN);
  fdes=mkstemp(name);
  if (!fdes) return NULL;
  return fdopen(fdes, "w+");
#endif
#endif /* TARGET_MINGW */
}

/* These are reimplemented from macros */
int32 TOINT(float64 fltmp) {
  if ((fltmp >= 2147483648.0) || (fltmp < -2147483648.0)) error(ERR_RANGE);
  return (int32)fltmp;
}

int64 TOINT64(float64 fltmp) {
  if ((fltmp > MAXINT64FLT) || (fltmp < MININT64FLT)) {
    error(ERR_RANGE);
    return 0;
  }
  if ((sgni((int64)fltmp) != 0) && (sgnf(fltmp) != sgni((int64)fltmp))) error(ERR_RANGE);
  return (int64)fltmp;
}

size_t TONATIVEADDR(float64 fltmp) {
#ifdef MATRIX64BIT
  return TOINT64(fltmp);
#else
  return TOINT(fltmp);
#endif /* MATRIX64BIT */
}

/* This function reduces a 64-bit int to 32-bit int, with range checking.
** The first, commented out line is strict range checking, the second one allows the 32nd bit to be set,
** to allow negative 32-bit ints to be entered as hex.
*/
int32 INT64TO32(int64 itmp) {
  if ((itmp > 4294967295ll) || (itmp < -2147483648ll)) error(ERR_RANGE); /* Allow &80000000-FFFFFFFF */
  return (int32)itmp;
}

void set_fpu(void) {
#if defined(__i386__) || defined(__x86_64__)
  unsigned int mode = 0x37F;
  asm ("fldcw %0" : : "m" (*&mode));
#endif
}

void decimaltocomma(char *numstring, int32 len) {
  int32 i = 0;
  for(i = 0; i < len; i++)
    if (numstring[i] == '.') numstring[i] = ',';
}

/* Return the sign of the number */
int32 sgni(int64 v) {
  if (v>0) {
    return(1);
  } else if (v == 0) {
    return(0);
  } else {
    return(-1);
  }
}

int32 sgnf(float64 f) {
  if (f>0.0) {
    return(1);
  } else if (f == 0.0) {
    return(0);
  } else {
    return(-1);
  }
}

/* Converts a \r or \n terminated string to a \0 terminated string */
void string_zeroterm(char *buffer) {
  int32 p, i;

  p=strlen(buffer);
  if(p==0) return;
  for(i=0; i<p; i++) {
    if ((buffer[i] == '\r') || (buffer[i] == '\n')) buffer[i]='\0';
  }
}

static char fnbuf[FNAMESIZE+4];

static char _chrflip(char c) {
  if(c=='.') c='/';
    else if (c=='/') c='.';
  return(c);
}

/* On RISC OS, translates a Linux-style filename to RISC OS format,
 * on other platforms, translates a RISC OS-style filename to Linux.
 * (Windows can directly handle a Linux-format file name courtesy of
 * the MinGW C libraries. */
char *translatefname(char *fn) {
  int i=0, p=0;
  
  memset(fnbuf, 0, FNAMESIZE+4);
  p=strlen(fn);
  if (p>FNAMESIZE) p=FNAMESIZE;
  for(i=0;i<p;i++) fnbuf[i]=_chrflip(fn[i]);
#ifdef TARGET_RISCOS
  if (fnbuf[0]=='.') {
    memmove(fnbuf+1, fnbuf, FNAMESIZE);
    fnbuf[0]='$';
  }
#else
  if (fnbuf[0]=='$') memmove(fnbuf, fnbuf+1, FNAMESIZE);
#endif
  return(fnbuf);
}

int32 get_listo(void) {
  return (basicvars.list_flags.space | basicvars.list_flags.indent<<1
        | basicvars.list_flags.split<<2 | basicvars.list_flags.noline<<3
        | basicvars.list_flags.lower<<4 | basicvars.list_flags.showpage<<5
        | basicvars.list_flags.expand<<6
#ifdef DEBUG
        | basicvars.debug_flags.debug<<8
        | basicvars.debug_flags.tokens<<9
        | basicvars.debug_flags.variables<<10
        | basicvars.debug_flags.strings<<11
        | basicvars.debug_flags.stats<<12
        | basicvars.debug_flags.stack<<13
        | basicvars.debug_flags.allstack<<14
        | basicvars.debug_flags.functions<<15
        | basicvars.debug_flags.vdu<<16
        | basicvars.debug_flags.sound<<17
#endif
        );
}

void set_listoption(int32 listopts) {
  DEBUGFUNCMSGIN;
  basicvars.list_flags.space = (listopts & LIST_SPACE) != 0;
  basicvars.list_flags.indent = (listopts & LIST_INDENT) != 0;
  basicvars.list_flags.split = (listopts & LIST_SPLIT) != 0;
  basicvars.list_flags.noline = (listopts & LIST_NOLINE) != 0;
  basicvars.list_flags.lower = (listopts & LIST_LOWER) != 0;
  basicvars.list_flags.showpage = (listopts & LIST_PAGE) != 0;
  basicvars.list_flags.expand = (listopts & LIST_EXPAND) != 0;
#ifdef DEBUG
/* Internal debugging options */
  basicvars.debug_flags.debug = (listopts & DEBUG_DEBUG) != 0;
  basicvars.debug_flags.tokens = (listopts & DEBUG_TOKENS) != 0;
  basicvars.debug_flags.variables = (listopts & DEBUG_VARIABLES) != 0;
  basicvars.debug_flags.strings = (listopts & DEBUG_STRINGS) != 0;
  basicvars.debug_flags.stats = (listopts & DEBUG_STATS) != 0;
  basicvars.debug_flags.stack = (listopts & DEBUG_STACK) != 0;
  basicvars.debug_flags.allstack = (listopts & DEBUG_ALLSTACK) != 0;
  basicvars.debug_flags.functions = (listopts & DEBUG_FUNCTIONS) != 0;
  basicvars.debug_flags.vdu = (listopts & DEBUG_VDU) != 0;
  basicvars.debug_flags.sound = (listopts & DEBUG_SOUND) != 0;
#endif
  DEBUGFUNCMSGOUT;
}

#ifndef BRANDY_HAS_STRL_FUNCTIONS
/* strncpy() does not gurarantee to include a \0 byte at the end of a string
 * if the number of characters copied is equal to the number copied. OpenBSD
 * strlcpy() copies one less, and ensures there is always a zero byte at the
 * end. This attempts to emulate that. */
char *my_strlcpy(char *dest, const char *src, size_t n) {
  size_t i;

  for (i = 0; i < n-1 && src[i] != '\0'; i++)
    dest[i] = src[i];
  dest[i]='\0';
  return dest;
}

#endif
