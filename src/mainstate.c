/*
** This file is part of the Matrix Brandy Basic VI Interpreter.
** Copyright (C) 2000-2014 David Daniels
** Copyright (C) 2018-2021 Michael McConnell and contributors
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
**	This file contains the bulk of the Basic interpreter itself
**
** 20-Mar-2014 JGH: DEF correctly executed as a REM - skips to next line
**
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "common.h"
#include "target.h"
#include "basicdefs.h"
#include "tokens.h"
#include "variables.h"
#include "stack.h"
#include "heap.h"
#include "strings.h"
#include "errors.h"
#include "statement.h"
#include "evaluate.h"
#include "convert.h"
#include "miscprocs.h"
#include "editor.h"
#include "mos.h"
#include "screen.h"
#include "lvalue.h"
#include "fileio.h"
#include "mainstate.h"
#include "keyboard.h"

#define MAXWHENS 500		/* maximum number of WHENs allowed per CASE statement */

/* Replacement for memmove where we dedupe pairs of double quotes */
static int memcpydedupe(char *dest, const unsigned char *src, size_t len, char dedupe) {
  int sptr = 0, dptr=0, shorten=0;
  
  while (sptr < len) {
    *(dest+dptr) = *(src+sptr);
    if (*(src+sptr)==dedupe && *(src+sptr+1)==dedupe) {
      sptr++;
      shorten++;
    }
    sptr++;
    dptr++;
  }
  return(shorten);
}

/*
** 'exec_assembler' is invoked when a '[' is found. This version of
** the interpreter does not include an assembler
*/
void exec_assembler(void) {
  error(ERR_UNSUPPORTED);
}

/*
** 'exec_asmend' is called with a ']' is found. This version of
** the interpreter does not include an assembler
*/
void exec_asmend(void) {
  error(ERR_UNSUPPORTED);
}

/*
** 'exec_oscmd' deals with '*' commands. The text of the '*' is
** retrieved from the source part of the line and passed to the
** operating system as a command. The effect of a command that
** overwrites the Basic program or the interpreter is undefined.
*/
void exec_oscmd(void) {
  char *p;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_oscmd\n");
#endif /* DEBUG */
  p = CAST(get_srcaddr(basicvars.current), char *);	/* Get the address of the command text */
  mos_oscli(p, FALSE, NULL);		/* Run command but do not capture output */
  basicvars.current+=1+SIZESIZE;	/* Skip the '*' and the offset after it */
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_oscmd\n");
#endif /* DEBUG */
}

/*
** 'exec_call' deals with the Basic 'CALL' statement. This is not
** supported at the moment
*/
void exec_call(void) {
  int32 address, parmcount, parameters[1];
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_call\n");
#endif /* DEBUG */
  basicvars.current++;
  parmcount = 0;
  address = eval_integer();
  check_ateol();
  mos_call(address, parmcount, parameters);
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_call\n");
#endif /* DEBUG */
}

/*
** 'exec_case' deals with a 'CASE' statement.
** The way 'CASE' statements are handled is to build a table of pointers to
** expressions and statement sequences the first time the statement is seen.
** This eliminates the need to search for the 'WHEN' clauses each time the
** statement is executed (at the expense of some extra memory).
*/
void exec_case(void) {
  stackitem casetype, whentype;
  int32 n, intcase = 0;
  uint8 uint8case = 0;
  int64 int64case = 0;
  static float64 floatcase = 0;
  basicstring casestring = {0, NULL}, whenstring;
  casetable *cp;
  boolean found;
  byte *here;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_case\n");
#endif /* DEBUG */
  here = basicvars.current;
  cp = GET_ADDRESS(basicvars.current, casetable *);	/* Fetch address of 'CASE' table */
  basicvars.current+=1+LOFFSIZE;		/* Skip 'CASE' token and pointer */
  expression();
  casetype = GET_TOPITEM;
  switch (casetype) {	/* Check the type of the 'CASE' expression */
  case STACK_INT:    intcase = pop_int(); break;
  case STACK_UINT8:  uint8case = pop_uint8(); break;
  case STACK_INT64:  int64case = pop_int64(); break;
  case STACK_FLOAT:  floatcase = pop_float(); break;
  case STACK_STRING: case STACK_STRTEMP: casestring = pop_string(); break;
  default: error(ERR_VARNUMSTR);
  }
/*
** Now go through the case table and try to find a 'WHEN' case that
** matches
*/
  found = FALSE;
  for (n=0; n<cp->whencount; n++) {
    basicvars.current = cp->whentable[n].whenexpr;	/* Point at the WHEN expression */
    if (basicvars.traces.lines) trace_line(get_lineno(find_linestart(basicvars.current)));
    while (TRUE) {
      expression();
      whentype = GET_TOPITEM;
      if (casetype == STACK_INT) {	/* Go by type of 'case' expression */
        switch(whentype) {		/* Then by type of 'WHEN' expression */
          case STACK_INT: case STACK_UINT8: case STACK_INT64:
            found = pop_anyint() == intcase; break;
          case STACK_FLOAT: found = pop_float() == TOFLOAT(intcase); break;
          default: error(ERR_TYPENUM);
        }
      }
      else if (casetype == STACK_UINT8) {	/* Go by type of 'case' expression */
        switch(whentype) {		/* Then by type of 'WHEN' expression */
          case STACK_INT: case STACK_UINT8: case STACK_INT64:
            found = pop_anyint() == uint8case; break;
          case STACK_FLOAT: found = pop_float() == TOFLOAT(uint8case); break;
          default: error(ERR_TYPENUM);
        }
      }
      else if (casetype == STACK_INT64) {	/* Go by type of 'case' expression */
        switch(whentype) {		/* Then by type of 'WHEN' expression */
          case STACK_INT: case STACK_UINT8: case STACK_INT64:
            found = pop_anyint() == int64case; break;
          case STACK_FLOAT: found = pop_float() == TOFLOAT(int64case); break;
          default: error(ERR_TYPENUM);
        }
      }
      else if (casetype == STACK_FLOAT) {		/* 'case' expression is a floating point value */
        found = pop_anynumfp() == floatcase;
      }
      else {	/* This leaves just strings */
        if (whentype != STACK_STRING && whentype != STACK_STRTEMP) error(ERR_TYPESTR);
        whenstring = pop_string();
        if (whenstring.stringlen != casestring.stringlen)
          found = FALSE;
        else if (whenstring.stringlen == 0)
          found = TRUE;
        else {
          found = memcmp(whenstring.stringaddr, casestring.stringaddr, whenstring.stringlen) == 0;
        }
        if (whentype == STACK_STRTEMP) free_string(whenstring);
      }
      if (found || *basicvars.current == ':' || *basicvars.current == asc_NUL) break;	/* Found a match or end of WHEN expression list so escape from loop */
      if (*basicvars.current == ',')	/* No match - Another value follows for this CASE */
        basicvars.current++;
      else {
        error(ERR_SYNTAX);
      }
    }
    if (found) break;	/* Match found - Escape from outer loop */
  }
  if (casetype == STACK_STRTEMP) free_string(casestring);
  if (found) {	/* Case value matched */
    if (basicvars.traces.branches) trace_branch(here, cp->whentable[n].whenaddr);
    basicvars.current = cp->whentable[n].whenaddr;
  }
  else {	/* Case value not matched - Use 'OTHERWISE' entry */
    if (basicvars.traces.branches) trace_branch(here, cp->defaultaddr);
    basicvars.current = cp->defaultaddr;
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_case\n");
#endif /* DEBUG */
}

/*
** 'exec_xcase' is called the first time a case statement is seen to go
** through the statement and build a case table for it. Each entry of
** this consists of pair of addresses, one for an expression and one for
** the code after the 'WHEN'. On entry, 'current' points at the address
** of the 'XCASE' token
*/
void exec_xcase(void) {
  byte *tp, *lp, *defaultaddr;
  int32 whencount, depth, n;
  casetable *cp;
  whenvalue whentable[MAXWHENS];
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_xcase\n");
#endif /* DEBUG */
  lp = basicvars.current;
  do {	/* Find the end of the current line */
    tp = lp;
    lp = skip_token(lp);
  } while (*lp != asc_NUL);
  if (*tp != BASIC_TOKEN_OF) error(ERR_OFMISS);		/* Last item on line must be 'OF' */
  lp++;		/* Point at the start of the line after the 'CASE' */
  whencount = 0;
  defaultaddr = NIL;
  depth = 1;
  while (depth>0) {
    if (AT_PROGEND(lp)) error(ERR_ENDCASE);	/* No ENDCASE found for this CASE */
    tp = FIND_EXEC(lp);		/* Find the first executable token */
    switch (*tp) {
    case BASIC_TOKEN_XWHEN: case BASIC_TOKEN_WHEN:	/* Have found a 'WHEN' */
      tp+=(1+OFFSIZE);	/* Skip token and the offset after it */
      if (depth == 1) {	/* Only want WHENs from CASE at this level */
        if (whencount == MAXWHENS) error(ERR_WHENCOUNT);
        whentable[whencount].whenexpr = tp;
        while (*tp != asc_NUL && *tp != ':') tp = skip_token(tp);	/* Find code after ':' */
        if (*tp == ':') tp++;
        if (*tp == asc_NUL) {	/* At end of line - Skip to next line */
          tp++;
          tp = FIND_EXEC(tp);
        }
        whentable[whencount].whenaddr = tp;
        whencount++;
      }
      break;
    case BASIC_TOKEN_XOTHERWISE: case BASIC_TOKEN_OTHERWISE:	/* Have found an 'OTHERWISE' */
      if (depth == 1) {
        tp+=(1+OFFSIZE);	/* Skip token and the offset after it */
        if (*tp == ':') tp++;
        if (*tp == asc_NUL) {	/* 'OTHERWISE' is at end of line */
          tp++;	/* Move to start of next line */
          if (AT_PROGEND(tp)) error(ERR_ENDCASE);
          tp = FIND_EXEC(tp);
        }
        defaultaddr = tp;
      }
      break;
    case BASIC_TOKEN_ENDCASE:	/* Have reached the end of the statement */
      depth--;
      if (depth == 0 && defaultaddr == NIL) defaultaddr = tp+1;
      break;
    }
/* See if a nested CASE statement starts on this line */
    if (depth>0) {
      tp = FIND_EXEC(lp);
      while (*tp != asc_NUL && *tp != BASIC_TOKEN_XCASE) tp = skip_token(tp);
      if (*tp == BASIC_TOKEN_XCASE) depth++;
      lp+=GET_LINELEN(lp);
    }
  }
/* Create 'CASE' table */
  cp = allocmem(sizeof(casetable)+whencount*sizeof(whenvalue), 1);	/* Hacksville, Tennessee */
  cp->whencount = whencount;
  cp->defaultaddr = defaultaddr;
  for (n=0; n<whencount; n++) cp->whentable[n] = whentable[n];
  *basicvars.current = BASIC_TOKEN_CASE;
  set_address(basicvars.current, cp);
  exec_case();	/* Now go and process the CASE statement */
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_xcase\n");
#endif /* DEBUG */
}

/*
** 'exec_chain' deals with the Basic 'CHAIN' statement.
*/
void exec_chain(void) {
  basicstring namedesc;
  stackitem stringtype;
  char *filename;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_chain\n");
#endif /* DEBUG */
  basicvars.current++;
  expression();
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) error(ERR_TYPESTR);
  namedesc = pop_string();
  filename = tocstring(namedesc.stringaddr, namedesc.stringlen);
  if (stringtype == STACK_STRTEMP) free_string(namedesc);
  check_ateol();
  read_basic(filename);
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exiting function mainstate.c:exec_chain by calling run_program()\n");
#endif /* DEBUG */
  run_program(NIL);
}

/*
** 'exec_clear' is called to clear all the variables defined in the
** program and remove all references to them from the tokenised program.
** It also clears the Basic stack so it is not a good idea to use it
** in a procedure or function. It probably will not crash the interpreter
** but there is no guarantee of this.
*/
void exec_clear(void) {
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_clear\n");
#endif /* DEBUG */
  basicvars.current++;	/* Move past token */
  if ((*basicvars.current == 0xFF) && (*(basicvars.current+1) == BASIC_TOKEN_HIMEM)) {
    basicvars.current+=2;
    exec_clear_himem();
  } else {
    check_ateol();
    clear_offheaparrays();
    clear_varptrs();
    clear_varlists();
    clear_strings();
    clear_heap();
    clear_stack();
    init_expressions();
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_clear\n");
#endif /* DEBUG */
}

/*
** 'exec_data' hendles the 'DATA' statement when one of these is
** encountered in normal program execution. The DATA token will
** always be the last thing on the line. It moves the current
** pointer to the NUL at the end of the line
*/
void exec_data(void) {
  basicvars.current = skip_token(basicvars.current);
}

/*
** 'exec_def' processes 'DEF'-type statements. A DEF is executed
** identically to a REM - execution skips to the next line.
** (which is why we check for 0 instead of using the ateol table)
*/
void exec_def(void) {
  while (*basicvars.current != '\0') {
    basicvars.current = skip_token(basicvars.current);
  }
}

/*
** define_byte_array - Called to handle a DIM of the form:
**   DIM <name> <size>
*/
static void define_byte_array(variable *vp, boolean offheap) {
  boolean isindref, islocal;
  int64 offset = 0, highindex;
  byte *ep = NULL;
/*
** Allocate a byte *array* (index range 0..highindex) of the requested
** size. The address stored is in fact the byte offset of the array
** from the start of the Basic workspace
*/
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:define_byte_array\n");
#endif
  if (vp->varflags == VAR_UINT8) error(ERR_UNSUITABLEVAR);
  if (vp->varflags  !=  VAR_INTWORD && vp->varflags  !=  VAR_INTLONG && vp->varflags  !=  VAR_FLOAT) error(ERR_VARNUM);
  isindref = *basicvars.current == '!';
  if (isindref) {
/* Deal with indirection operator in DIM <variable>!<offset> <size> */
    basicvars.current++;
    if (vp->varflags == VAR_INTWORD)
      offset = vp->varentry.varinteger + eval_intfactor();
    else if (vp->varflags == VAR_INTLONG)
      offset = vp->varentry.var64int + eval_intfactor();
    else {
      offset = TOINT64(vp->varentry.varfloat) + eval_intfactor();
    }
  } else if (offheap) {
    if (vp->varflags == VAR_INTWORD)
      offset = vp->varentry.varinteger;
    else if (vp->varflags == VAR_INTLONG)
      offset = vp->varentry.var64int;
    else {
      offset = TOINT64(vp->varentry.varfloat);
    }
  }
  islocal = *basicvars.current == BASIC_TOKEN_LOCAL;
  if (islocal) {	/* Allocating block on stack */
    if (basicvars.procstack == NIL) error(ERR_LOCAL);	/* LOCAL found outside a PROC or FN */
    basicvars.current++;
    highindex = eval_int64();
    if (highindex < -1) error(ERR_NEGBYTEDIM, vp->varname);	/* Dimension is out of range */
    ep = alloc_stackmem(highindex + 1);			/* Allocate memory on the stack */
    if (ep == NIL) error(ERR_BADBYTEDIM, vp->varname);	/* Not enough memory left */
  }
  else {	/* Allocating block from heap or malloc*/
    highindex = eval_int64();
    if (highindex < -1) error(ERR_NEGBYTEDIM, vp->varname);	/* Dimension is out of range */
    if (offheap) {
      ep = (byte *)(size_t)offset;
      if (highindex == -1) {
        free(ep);
        ep = 0;
      } else {
        ep = realloc(ep, highindex+1);
        if (!ep) error(ERR_BADBYTEDIM); /* Not enough memory left */
#ifdef MATRIX64BIT
        if ((vp->varflags == VAR_INTWORD) && ((int64)ep > 0xFFFFFFFFll)) {
          free(ep); /* Can't store the address in the variable type given so free it before complaining */
          error(ERR_ADDRESS);
        }
#endif
      }
    } else {
      if (highindex == -1) {	/* Treat size of -1 as special case, although it does not have to be */
        ep = basicvars.vartop;
#ifdef MATRIX64BIT
        if ((vp->varflags == VAR_INTWORD) && ((int64)ep > 0xFFFFFFFFll)) error(ERR_ADDRESS);
#endif
      } else {
#ifdef MATRIX64BIT
        if ((vp->varflags == VAR_INTWORD) && ((int64)(basicvars.stacklimit.bytesp+highindex+1) > 0xFFFFFFFFll)) error(ERR_ADDRESS);
#endif
        ep = allocmem(highindex+1, 0);
        if (ep == NIL) error(ERR_BADBYTEDIM, vp->varname);	/* Not enough memory left */
      }
    }
  }
  if (isindref)
    store_integer(offset, (size_t)ep);
  else if (vp->varflags == VAR_INTWORD)
    vp->varentry.varinteger = (size_t)ep;
  else if (vp->varflags == VAR_INTLONG)
    vp->varentry.var64int = (size_t)ep;
  else {
    vp->varentry.varfloat = TOFLOAT((size_t)ep);
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:define_byte_array\n");
#endif
}

/*
** 'exec_dim' is called to handle DIM statements
*/
void exec_dim(void) {
  byte *base, *ep;
  variable *vp;
  boolean offheap = 0;		/* TRUE if allocating a block of memory off the heap */
  boolean blockdef;		/* TRUE if we are allocating a block of memory and not creating an array */

#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_dim\n");
#endif
  do {
    boolean islocal = FALSE;	/* TRUE if we are defining a local array on the stack */
    basicvars.current++;	/* Skip 'DIM' token or ',' */
    /* Is the next token HIMEM? If so we're doing an off-heap memory block */
    if ((*basicvars.current == 0xFF) && (*(basicvars.current+1) == BASIC_TOKEN_HIMEM)) {
      offheap = TRUE;
      basicvars.current+=2;
    }
/* Must always have a variable name next */
    if (*basicvars.current != BASIC_TOKEN_STATICVAR && *basicvars.current != BASIC_TOKEN_XVAR) error(ERR_NAMEMISS);
    if (*basicvars.current == BASIC_TOKEN_STATICVAR) {	/* Found a static variable */
      vp = &basicvars.staticvars[*(basicvars.current+1)];
      base = basicvars.current;
      basicvars.current+=2;
      blockdef = TRUE;	/* Can only be defining a block of memory */
    }
    else {	/* Found dynamic variable or array name */
      base = get_srcaddr(basicvars.current);	/* Point 'base' at start of array name */
      ep = skip_name(base);			/* Point ep at byte after name */
      basicvars.current+=1+LOFFSIZE;		/* Skip the pointer to the name */
      blockdef = *(ep-1) != '(' && *(ep-1) != '[';
      vp = find_variable(base, ep-base);
      if (blockdef) {	/* Defining a block of memory (byte array) */
        if (vp == NIL) {		/* Variable does not exist */
          if (*basicvars.current == '!')	/* Variable name followed by indirection operator */
            error(ERR_VARMISS, tocstring(CAST(base, char *), ep-base));
          else {	/* Reference to variable only - Create the variable */
            vp = create_variable(base, ep-base, NIL);
          }
        }
      }
      else if (vp == NIL)	/* Defining an array - Array does not exist yet */
        vp = create_variable(base, ep-base, NIL);
      else {	/* Array name exists */
        if (vp->varentry.vararray != NIL) error(ERR_DUPLDIM, vp->varname);	/* Array aleady defined */
        islocal = TRUE;	/* Name exists but definition does not. Assume a local array */
      }
    }
    if (blockdef)	/* Defining a block of memory */
      define_byte_array(vp, offheap);
    else {	/* Defining a normal array */
      define_array(vp, islocal, offheap);
    }
  } while (*basicvars.current == ',');
  check_ateol();
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_dim\n");
#endif
}

/*
** 'start_blockif' returns 'TRUE' if the line starting at 'tp' marks the
** start of a block 'IF' statement
*/
static boolean start_blockif(byte *tp) {
  while (*tp != asc_NUL) {
    if (*tp == BASIC_TOKEN_THEN && *(tp+1) == asc_NUL) return TRUE;
    tp = skip_token(tp);
  }
  return FALSE;
}

/*
** 'exec_elsewhen' deals with an ELSE keyword for both block and single
** line IF statements as well as the 'WHEN' and 'OTHERWISE' keywords in
** a 'CASE' statement. In all cases, the keyword marks the end of one of
** more statements and a branch to another part of the code (the code
** after the ENDIF or ENDCASE) is needed.
** On entry, basicvars.current points at the token for the keyword. The
** offset is in the two bytes that follow. 'GET_DEST' differs from most
** of the other macros used to manipulate offsets and pointers in that
** the argument has to point at the offset and not at the byte before it.
** The reason for this is that there can be more than one offset in
** a row, for example, after an 'IF' token
*/
void exec_elsewhen(void) {
  byte *p;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_elsewhen\n");
#endif /* DEBUG */
  p = basicvars.current+1;	/* Point at offset */
  p = GET_DEST(p);
  if (basicvars.traces.enabled) {
    if (basicvars.traces.lines) trace_line(get_lineno(find_linestart(p)));
    if (basicvars.traces.branches) trace_branch(basicvars.current, p);
  }
  basicvars.current = p;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_elsewhen\n");
#endif /* DEBUG */
}

/*
** 'exec_xelse' deals with the first reference to an ELSE in a single line
** IF statement. It fills in the offset to reach the next line
*/
void exec_xelse(void) {
  byte *p;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_xelse\n");
#endif /* DEBUG */
  *basicvars.current = BASIC_TOKEN_ELSE;
  p = basicvars.current+1+OFFSIZE;	/* Start at the token after the offset */
  do
    p = skip_token(p);
  while (*p != asc_NUL);
  p++;	/* Point at start of next line */
  set_dest(basicvars.current+1, FIND_EXEC(p));
  exec_elsewhen();
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_xelse\n");
#endif /* DEBUG */
}

/*
** 'exec_xlhelse' deals with the first reference to an 'ELSE' that is part
** of a block 'IF' statement. It will be encountered after executing the
** statements following the 'THEN' part of the 'IF'. It locates the 'ENDIF'
** and fills in the offset to the next statement after the 'ENDIF' after
** the 'ELSE' token. Note that it is possible for there to be a statement
** on the same line as the 'ELSE' so the search starts there. The 'ENDIF'
** has to be the first token on a line.
*/
void exec_xlhelse(void) {
  int32 depth;
  byte *lp, *lp2;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_xlhelse\n");
#endif /* DEBUG */
  lp = find_linestart(basicvars.current);	/* Find the start of the line with the 'ELSE' */
  lp2 = basicvars.current;	/* Ensure the code won't find an ENDIF first thing */
  depth = 1;
  do {	/* Look for the matching ENDIF */
    if (*lp2 == BASIC_TOKEN_ENDIF) depth--;
    if (start_blockif(lp2)) depth++;	/* Scan line to see if another block 'IF' starts in it */
    if (depth == 0) break;
    lp+=GET_LINELEN(lp);
    if (AT_PROGEND(lp)) error(ERR_ENDIF);	/* No ENDIF found */
    lp2 = FIND_EXEC(lp);
  } while (TRUE);
  lp2++;	/* Skip the ENDIF token */
  if (*lp2 == asc_NUL) {	/* There is nothing else on the line after the ENDIF */
    lp2++;	/* Move to start of next line */
    if (basicvars.traces.lines) trace_line(get_lineno(lp2));
    lp2 = FIND_EXEC(lp2);	/* Find the first executable token */
  }
  *basicvars.current = BASIC_TOKEN_LHELSE;
  set_dest(basicvars.current+1, lp2);	/* Set destination of branch */
  exec_elsewhen();
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_xlhelse\n");
#endif /* DEBUG */
}

/*
** 'exec_end' executes an 'END' statement. The normal use of 'END'
** is to end a program, but under RISC OS the form 'END=<value>' can
** be used to extend the memory available to the program above the
** top of the Basic stack to give a user-controlled heap. In this
** version of the interpreter, this statement form is recognised
** but has no effect
**
** If being used to finish a program, all it does is return to the
** command loop after closing any open files (if that option is
** in effect)
*/
void exec_end(void) {
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_end\n");
#endif /* DEBUG */
  basicvars.current++;		/* Skip END token */
  if (*basicvars.current == '=') {	/* Have got 'END=' version */
    int32 newend = 0;
    basicvars.current++;
    expression();
    check_ateol();
    newend = pop_anynum32();
    mos_setend(newend);
  }
  else {	/* Normal 'END' statement */
    check_ateol();
    end_run();
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_end\n");
#endif /* DEBUG */
}

/*
** 'exec_endifcase' is called when an 'ENDCASE' or an 'ENDIF' statement
** is found. In the normal case of events these are skipped automatically
** by the 'WHEN' and 'ELSE' code but it is not an error if they are
** encountered during normal program execution. They act as 'no op's in
** this case
*/
void exec_endifcase(void) {
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_endifcase\n");
#endif /* DEBUG */
  basicvars.current++;		/* Skip token */
  if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
  if (*basicvars.current == ':') basicvars.current++;	/* Skip ':' */
  if (*basicvars.current == asc_NUL) {	/* Token is at end of line */
    basicvars.current++;		/* Move to start of next line */
    if (basicvars.traces.lines) trace_line(get_lineno(basicvars.current));
    basicvars.current = FIND_EXEC(basicvars.current);
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_endifcase\n");
#endif /* DEBUG */
}

/*
** 'exec_endproc' deals with returns from procedures.
** The stack layout at this point should be:
**	<stuff to chuck away> <local vars> <return block> <arguments>
** Everything here has to be discarded.
*/
void exec_endproc(void) {
  fnprocinfo returnblock;
  stackitem item;

#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_endproc\n");
#endif /* DEBUG */
  basicvars.errorislocal = 0;
  if (basicvars.procstack == NIL) error(ERR_ENDPROC);	/* ENDPROC found outside a PROC */
  item = stack_unwindlocal();
  if (item == STACK_ERROR) basicvars.error_handler = pop_error();
  if (GET_TOPITEM != STACK_PROC) empty_stack(STACK_PROC);	/* Throw away unwanted entries on Basic stack */
  returnblock = pop_proc();	/* Fetch return address and so forth */
  if (returnblock.parmcount != 0) restore_parameters(returnblock.parmcount);	/* Procedure had parameters - Restore old values */
  if (basicvars.traces.enabled) {
    if (basicvars.traces.procs) trace_proc(returnblock.fnprocname, FALSE);
    if (basicvars.traces.branches) trace_branch(basicvars.current, returnblock.retaddr);
  }
  basicvars.current = returnblock.retaddr;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_endproc\n");
#endif /* DEBUG */
}

/*
** 'exec_fnreturn' deals with function returns.
** One unfortunate point is that if the result is a string and the string
** came from a variable, that is, the type is STACK_STRING, it is necessary
** to make a copy of the string in case the string came from a local
** variable. The reason for this is that the local variable will be
** destroyed when the function returns and the string returned to the
** heap
*/
void exec_fnreturn(void) {
  stackitem resultype, item;
  int32 intresult = 0;
  int64 int64result = 0;
  uint8 uint8result = 0;
  static float64 fpresult;
  basicstring stresult = {0, NULL};
  char *sp;
  fnprocinfo returnblock;

#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_fnreturn\n");
#endif /* DEBUG */
  basicvars.errorislocal = 0;
  if (basicvars.procstack == NIL) error(ERR_FNRETURN);	/* '=<expr>' found outside a FN */
  basicvars.current++;
  expression();
  resultype = GET_TOPITEM;
  if (resultype == STACK_INT)	/* Pop result from stack and ensure type is legal */
    intresult = pop_int();
  else if (resultype == STACK_UINT8)	/* Pop result from stack and ensure type is legal */
    uint8result = pop_uint8();
  else if (resultype == STACK_INT64)
    int64result = pop_int64();
  else if (resultype == STACK_FLOAT)
    fpresult = pop_float();
  else if (resultype == STACK_STRING) {	/* Have to make a copy of the string for safety */
    stresult = pop_string();
    sp = alloc_string(stresult.stringlen);
    if (stresult.stringlen != 0) memmove(sp, stresult.stringaddr, stresult.stringlen);
    stresult.stringaddr = sp;
    resultype = STACK_STRTEMP;
  }
  else if (resultype == STACK_STRTEMP)
    stresult = pop_string();
  else {
    error(ERR_VARNUMSTR);
  }
  item = stack_unwindlocal();
  if (item == STACK_ERROR) basicvars.error_handler = pop_error();
  empty_stack(STACK_FN);	/* Throw away unwanted entries on Basic stack */
  returnblock = pop_fn();	/* Fetch return address and so forth */
  if (returnblock.parmcount != 0) restore_parameters(returnblock.parmcount);	/* Procedure had arguments - restore old values */
  if (resultype == STACK_INT) {	/* Lastly, put the result back on the stack */
    push_int(intresult);
  }
  else if (resultype == STACK_UINT8) {	/* Lastly, put the result back on the stack */
    push_uint8(uint8result);
  }
  else if (resultype == STACK_INT64) {
    push_int64(int64result);
  }
  else if (resultype == STACK_FLOAT) {
    push_float(fpresult);
  }
  else if (resultype == STACK_STRING) {
    push_string(stresult);
  }
  else if (resultype == STACK_STRTEMP) {
    push_strtemp(stresult.stringlen, stresult.stringaddr);
  }
  if (basicvars.traces.enabled) {
    if (basicvars.traces.procs) trace_proc(returnblock.fnprocname, FALSE);
    if (basicvars.traces.branches) trace_branch(basicvars.current, returnblock.retaddr);
  }
  basicvars.current = returnblock.retaddr;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_fnreturn\n");
#endif /* DEBUG */
}

/*
** 'exec_endwhile' deals with the ENDWHILE statement. It is in fact
** more important than the 'WHILE' in that whether to continue with
** the loop or to simply move on to the next statement is decided here.
**
** One point to note is that Basic V/VI allows loops to be nested
** incorrectly in that if one loop is not terminated within another,
** the inner one is automatically terminated by the outer one, for
** example:
**    WHILE X%<10
**      REPEAT ...
**    ENDWHILE
** Here, the 'REPEAT' loop is automatically terminated when then
** 'ENDWHILE' is reached. The code mimics this behaviour (in
** 'get_while').
*/
void exec_endwhile(void) {
  byte *tp;
  stack_while *wp;
  int64 result = 0;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_endwhile\n");
#endif /* DEBUG */
  tp = basicvars.current+1;
  if (!ateol[*tp]) error(ERR_SYNTAX);
  if (GET_TOPITEM == STACK_WHILE) 	/* WHILE control block is top of stack */
    wp = basicvars.stacktop.whilesp;
  else {	/* Discard contents of stack as far as WHILE block */
    wp = get_while();
  }
  if (wp == NIL) error(ERR_NOTWHILE);	/* Not in a WHILE loop */
  if (kbd_escpoll()) error(ERR_ESCAPE);
  basicvars.current = wp->whilexpr;
  expression();
  result = pop_anynum64();
  if (result != BASFALSE) {	/* Condition still true - Continue with loop */
    if (basicvars.traces.branches) trace_branch(tp, wp->whileaddr);
    basicvars.current = wp->whileaddr;
  }
  else {	/* Escape from loop - Remove WHILE control block from stack */
    pop_while();
    if (*tp == ':') tp++;		/* Continue at next token after ENDWHILE */
    if (*tp == asc_NUL) {
      tp++;		/* Move to the start of the next line */
      if (basicvars.traces.lines) trace_line(get_lineno(tp));
      tp = FIND_EXEC(tp);	/* Skip to start of the executable tokens */
    }
    basicvars.current = tp;
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_endwhile\n");
#endif /* DEBUG */
}

/*
** 'exec_error' deals with the 'ERROR' statement, which halts a
** program with a user-defined error.
** Note that the 'ERROR EXT' statement is not supported yet
*/
void exec_error(void) {
  int32 errnumber;
  stackitem stringtype;
  basicstring descriptor;
  char *errtext;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_error\n");
#endif /* DEBUG */
  basicvars.current++;	/* Skip the ERROR token*/
  errnumber = eval_integer();
  if (*basicvars.current != ',') error(ERR_COMISS);	/* Comma missing */
  basicvars.current++;
  expression();
  check_ateol();
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) error(ERR_TYPESTR);
  descriptor = pop_string();
  errtext = tocstring(descriptor.stringaddr, descriptor.stringlen);
  if (stringtype == STACK_STRTEMP) free_string(descriptor);
  show_error(errnumber, errtext);	/* Report the error */
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_error\n");
#endif /* DEBUG */
}

/*
** 'exec_for' deals with the 'FOR' statement at the start of a 'FOR' loop.
** It sets up the control block needed and starts the loop but that is all.
** Everything else is done by the 'NEXT' statement code.
*/
void exec_for(void) {
  boolean isinteger=0;
  lvalue forvar;
  int64 intlimit = 0, intstep = 1;
  float64 floatlimit = 0.0, floatstep = 1.0;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_for\n");
#endif /* DEBUG */
  basicvars.current++;	/* Skip the 'FOR' token */
  get_lvalue(&forvar);
  if ((forvar.typeinfo & VAR_ARRAY) != 0) error(ERR_VARNUM);	/* Numeric variable required */
  switch(forvar.typeinfo & TYPEMASK) {
    case VAR_INTWORD: case VAR_INTLONG: case VAR_UINT8: isinteger=1; break;
    case VAR_FLOAT: isinteger=0; break;
    default: error(ERR_VARNUM);
  }
  if (*basicvars.current != '=') error(ERR_EQMISS);	/* '=' is missing */
  basicvars.current++;
  expression();		/* Get the control variable's initial value */
  if (*basicvars.current != BASIC_TOKEN_TO) error(ERR_TOMISS);
  basicvars.current++;
  switch (forvar.typeinfo) {	/* Assign control variable's initial value */
  case VAR_UINT8: forvar.typeinfo = VAR_INTWORD;
  case VAR_INTWORD:
    *forvar.address.intaddr = pop_anynum32();
    break;
  case VAR_INTLONG:
    *forvar.address.int64addr = pop_anynum64();
    break;
  case VAR_FLOAT:
    *forvar.address.floataddr = pop_anynumfp();
    break;
  case VAR_INTBYTEPTR:
    basicvars.memory[forvar.address.offset] = pop_anynum32();
    break;
  case VAR_INTWORDPTR:
    store_integer(forvar.address.offset, pop_anynum32());
    break;
  case VAR_FLOATPTR:
    store_float(forvar.address.offset, pop_anynumfp());
    break;
  default:
    error(ERR_BROKEN, __LINE__, "mainstate");		/* Bad variable type found */
  }

/* Now evaluate the control variable's final value */

  expression();
  if (isinteger) {	/* Loop is an integer loop */
    intlimit = pop_anynum64();
  } else {	/* Loop is a floating point loop */
    floatlimit = pop_anynumfp();
  }
  if (*basicvars.current == BASIC_TOKEN_STEP) {
    basicvars.current++;
    expression();
    if (isinteger) {	/* Loop is an integer loop */
      intstep=pop_anynum64();
      if (intstep == 0) error(ERR_SILLY);
    } else {	/* Loop is a floating point loop */
      floatstep = pop_anynumfp();
      if (floatstep == 0.0) error(ERR_SILLY);
    }
  }
  if (!ateol[*basicvars.current]) error(ERR_SYNTAX);		/* Ensure there is nothing left on the line */
  if (*basicvars.current == ':') basicvars.current++;	/* Find the start of the statements in the loop */
  if (*basicvars.current == asc_NUL) {	/* Not on this line - Try the next */
    basicvars.current++;
    if (basicvars.traces.lines) trace_line(get_lineno(basicvars.current));
    basicvars.current = FIND_EXEC(basicvars.current);
  }
  if (isinteger) {	/* Finally, set up the loop control block on the stack and end */
    boolean simple = forvar.typeinfo == VAR_INTWORD && intstep == 1;
    switch(forvar.typeinfo) {
      case VAR_INTWORD: push_intfor(forvar, basicvars.current, intlimit, intstep, simple); break;
      case VAR_INTLONG: push_int64for(forvar, basicvars.current, intlimit, intstep, simple); break;
      default: error(ERR_BROKEN, __LINE__, "mainstate");
    }
  }
  else {
    push_floatfor(forvar, basicvars.current, floatlimit, floatstep, FALSE);
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_for\n");
#endif /* DEBUG */
}

/*
** 'set_linedest' is called to locate the line to which a line number refers
** and fill in its address in the 'BASIC_TOKEN_LINENUM' token. The address stored
** is that of the first token on the destination line. It returns a pointer
** to that token
*/
static byte *set_linedest(byte *tp) {
  int32 line;
  byte *dest;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:set_linedest\n");
#endif /* DEBUG */
  line = get_linenum(tp);
  dest = find_line(line);	/* Find the line refered to */
  if (get_lineno(dest) != line) error(ERR_LINEMISS, line);
  dest = FIND_EXEC(dest);	/* Find the first executable token */
  *tp = BASIC_TOKEN_LINENUM;
  set_address(tp, dest);
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:set_linedest\n");
#endif /* DEBUG */
  return dest;
}

/*
** 'exec_gosub' deals with the Basic 'GOSUB' statement
*/
void exec_gosub(void) {
  byte *dest = NULL;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_gosub\n");
#endif /* DEBUG */
  if (kbd_escpoll()) error(ERR_ESCAPE);
  basicvars.current++;		/* Slip GOSUB token */
  if (*basicvars.current == BASIC_TOKEN_LINENUM) {
    dest = GET_ADDRESS(basicvars.current, byte *);
    basicvars.current+=1+LOFFSIZE;	/* Skip 'line number' token */
  }
  else if (*basicvars.current == BASIC_TOKEN_XLINENUM) {	/* GOSUB destination not filled in yet */
    dest = set_linedest(basicvars.current);
    basicvars.current+=1+LOFFSIZE;	/* Skip 'line number' token */
  }
  else {	/* Destination line number is given by an expression */
    int32 line = eval_integer();
    if (line<0 || line>MAXLINENO) error(ERR_LINENO);	/* Line number is out of range */
    dest = find_line(line);	/* Find start of destination line */
    if (get_lineno(dest) != line) error(ERR_LINEMISS, line);
    dest = FIND_EXEC(dest);		/* Move from start of line to first token */
  }
  check_ateol();
  push_gosub();
  if (basicvars.traces.branches) trace_branch(basicvars.current, dest);
  basicvars.current = dest;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_gosub\n");
#endif /* DEBUG */
}

/*
** 'exec_goto' handles the Basic 'GOTO' statement
*/
void exec_goto(void) {
  byte *dest = NULL;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_goto\n");
#endif /* DEBUG */
  if (kbd_escpoll()) error(ERR_ESCAPE);
  basicvars.current++;		/* Skip 'GOTO' token */
  if (*basicvars.current == BASIC_TOKEN_LINENUM) {
    dest = GET_ADDRESS(basicvars.current, byte *);
    basicvars.current+=1+LOFFSIZE;	/* Skip 'line number' token */
  }
  else if (*basicvars.current == BASIC_TOKEN_XLINENUM) {	/* GOTO destination not filled in yet */
    dest = set_linedest(basicvars.current);
    basicvars.current+=1+LOFFSIZE;	/* Skip 'line number' token */
  }
  else {	/* Destination line number is given by an expression */
    int32 line = eval_integer();
    if (line<0 || line>MAXLINENO) error(ERR_LINENO);	/* Line number is out of range */
    dest = find_line(line);
    if (get_lineno(dest) != line) error(ERR_LINEMISS, line);
    dest = FIND_EXEC(dest);
  }
  check_ateol();
  if (basicvars.traces.branches) trace_branch(basicvars.current, dest);
  basicvars.current = dest;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_goto\n");
#endif /* DEBUG */
}

/*
** 'exec_blockif' is called to handle block 'IF' statements.
** The layout of an 'IF' statement is:
**   <IF token> <offset of THEN part> <offset of ELSE part> <expression> ...
*/
void exec_blockif(void) {
  byte *dest;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_blockif\n");
#endif /* DEBUG */
  dest = basicvars.current+1;		/* Point at the 'THEN' offset */
  basicvars.current+=1+2*OFFSIZE;	/* Skip IF token and THEN and ELSE offsets */
  expression();
  if (pop_anynum64() == BASFALSE) dest+=OFFSIZE;	/* Point at offset to 'ELSE' part */
  if (basicvars.traces.enabled) {	/* Branch after dealing with debug info */
    if (basicvars.traces.lines) trace_line(get_lineno(find_linestart(GET_DEST(dest))));
    if (basicvars.traces.branches) trace_branch(dest, GET_DEST(dest));
  }
  basicvars.current = GET_DEST(dest);		/* Branch to the 'THEN' or 'ELSE' code */
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_blockif\n");
#endif /* DEBUG */
}

/*
** 'exec_singlif' is called to deal with single line 'IF' statements
*/
void exec_singlif(void) {
  byte *dest, *here;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_singlif\n");
#endif /* DEBUG */
  here = dest = basicvars.current+1;	/* Point at the 'THEN' offset */
  basicvars.current+=1+2*OFFSIZE;	/* Skip IF token and THEN and ELSE offsets */
  expression();
  if (pop_anynum64() == BASFALSE) dest+=OFFSIZE;	/* Cond was false - Point at offset to 'ELSE' part */
  dest = GET_DEST(dest);	/* Find code after the 'THEN' or 'ELSE' */
  if (*dest == BASIC_TOKEN_LINENUM)	/* There is a line number there */
    dest = GET_ADDRESS(dest, byte *);
  else if (*dest == BASIC_TOKEN_XLINENUM) {	/* Address of line is not filled in */
    dest = set_linedest(dest);	/* Find line and fill in its address */
  }
  if (basicvars.traces.enabled) {	/* Deal with any trace info needed */
    if (basicvars.traces.lines) {
      int32 destline = get_lineno(find_linestart(dest));
      if (get_lineno(here) != destline) trace_line(destline);
    }
    if (basicvars.traces.branches) trace_branch(here, dest);
  }
  basicvars.current = dest;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_singlif\n");
#endif /* DEBUG */
}

/*
** 'exec_xif' is called the first time an 'IF' statement is encountered
** to identify the type of 'IF' and to fill in the offsets to the
** 'THEN' and 'ELSE' parts of the statement.
*/
void exec_xif(void) {
  byte *lp2 = NULL, *lp3 = NULL, *dest, *ifplace, *thenplace, *elseplace;
  int64 result = 0;
  int32 depth;
  boolean single = 0;
  boolean cascade = 0;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_xif\n");
#endif /* DEBUG */
  ifplace = basicvars.current; 		/* Set up a pointer to the 'IF' token */
  thenplace = ifplace+1;		/* Set up addresses where offsets will be stored */
  elseplace = ifplace+1+OFFSIZE;
  basicvars.current+=1+2*OFFSIZE;
  expression();
  result = pop_anynum64();
  single = *basicvars.current != BASIC_TOKEN_THEN;	/* No 'THEN' = single line if */
  if (*basicvars.current == BASIC_TOKEN_THEN) {
    lp2 = basicvars.current+1;	/* Skip the 'THEN' and see if it is the last item on the line */
    single = *lp2 != asc_NUL;		/* A 'null' here means that this is the start of a block 'IF' */
  }
  if (single) {		/* Dealing with a single line 'IF' */
    *ifplace = BASIC_TOKEN_SINGLIF;
    if (*basicvars.current == BASIC_TOKEN_XELSE) {	/* Got 'IF <expression> ELSE ...' i.e. there is no 'THEN' part */
      lp2 = basicvars.current+1+OFFSIZE;	/* Find the token after the 'ELSE' */
      set_dest(elseplace, lp2);
      while (*lp2 != asc_NUL) lp2 = skip_token(lp2);		/* Find next line for dummy 'THEN' part */
      lp2++;	/* Move to the start of the next line */
      set_dest(thenplace, FIND_EXEC(lp2));
    }
    else {
/*
** There are two cases to deal with here:
** 1)  Statement has a 'THEN'
** 2)  Statement omits the 'THEN'
** In the first case, 'lp2' already points at the first token after the
** 'THEN'. If the 'THEN' is missing, 'basicvars.current' points at the token
** of interest. Once the 'THEN' part has been dealt with, the 'ELSE' has to
** be found. Of course, there might not be an 'ELSE' in which case the 'ELSE'
** offset just points at the next line
*/
      if (start_blockif(basicvars.current)) cascade = 1;
      if (*basicvars.current != BASIC_TOKEN_THEN) lp2 = basicvars.current;
      set_dest(thenplace, lp2);
      if (cascade && matrixflags.cascadeiftweak) {
        /* Scan the line for a trailing THEN. If so, we need to look for
         * an ENDIF token and set the location of that to elseplace. */
        while (*lp2 != asc_NUL) {
          lp3 = lp2;
          lp2 = skip_token(lp2);
        }
        if (*lp3 != BASIC_TOKEN_THEN) {
          /* Not a block IF */
          lp2++;
          lp2 = FIND_EXEC(lp2);
        } else {
/* START ENDIF SEARCH LOOP */
          depth = 1;
          while (depth > 0) {
            if (AT_PROGEND(lp2)) {
              error(ERR_ENDIF);
            } else if (*lp2 == BASIC_TOKEN_ENDIF) {
              depth--;
            } else if ((*lp2 == BASIC_TOKEN_THEN) && start_blockif(lp2)) {
              depth++;
            } else if ((depth == 1) && (*lp2 == BASIC_TOKEN_XLHELSE)) {
              depth--;
            } 
            lp2 = skip_token(lp2);
            if (*lp2 == asc_NUL) {
              lp2++;
              lp2 = FIND_EXEC(lp2);
            }
          }
/* END ENDIF SEARCH LOOP */
        }
        set_dest(elseplace,lp2);
        /* End of Cascaded IF handler */
      } else {
        while (*lp2 != asc_NUL && *lp2 != BASIC_TOKEN_XELSE) lp2 = skip_token(lp2);
        if (*lp2 == BASIC_TOKEN_XELSE) lp2+=1+OFFSIZE;	/* Find the token after the 'ELSE' */
        if (*lp2 == asc_NUL) {	/* Find the first token on the next line */
          lp2++;
          lp2 = FIND_EXEC(lp2);
        }
        set_dest(elseplace, lp2);
      }
    }
  }
  else {	/* Dealing with a block 'IF' */
    *ifplace = BASIC_TOKEN_BLOCKIF;
/*
** Now find the 'ELSE' or 'ENDIF' that matches this 'IF' to fill in the
** 'ELSE' offset. A couple of points to note here:
** 1) lp2 points at the NULL after the 'THEN' token here
** 2) If no 'ELSE' or 'ENDIF' is found then it depends whether the result
**    of the 'IF' expression is true or false whether or not this is
**    flagged as an error. If it is false then it is a syntax error.
**    If true the error is ignored.
*/
    basicvars.current = lp2+1;		/* Move to the start of the next line */
    set_dest(thenplace, FIND_EXEC(basicvars.current));
    depth = 1;
    while (depth>0) {	/* Look for an ELSE or ENDIF */
      if (AT_PROGEND(basicvars.current)) {
        if (result == BASFALSE)
          error(ERR_ENDIF);	/* Result is 'false' but no ELSE or ENDIF found */
        else {	/* Otherwise we pretend we have found the end of the block IF */
          break;
        }
      }
      lp2 = FIND_EXEC(basicvars.current);	/* Find the first executable token */
      if (*lp2 == BASIC_TOKEN_ENDIF)
        depth--;
      else if (*lp2 == BASIC_TOKEN_XLHELSE) {
        if (depth == 1) depth = 0;	/* ELSE can only decrement depth if at top level of nest */
      }
      else if (start_blockif(lp2)) {	/* There is a block IF nested here */
        depth++;
      }
      if (depth>0) basicvars.current+=GET_LINELEN(basicvars.current);
    }
    if (AT_PROGEND(basicvars.current))		/* No 'ELSE' or 'ENDIF' found */
      lp2 = FIND_EXEC(basicvars.current);	/* Fake an address for the 'ELSE' code */
    else {	/* ELSE or ENDIF found */
      if (*lp2 == BASIC_TOKEN_XLHELSE)	/* Move past ELSE and offset */
        lp2+=1+OFFSIZE;
      else {	/* Move past ENDIF */
        lp2++;
      }
      if (*lp2 == asc_NUL) {	/* There is nothing else on the line - Skip to next line */
        lp2++;
        lp2 = FIND_EXEC(lp2);
      }
    }
    set_dest(elseplace, lp2);
  }
/*
** Finally, execute the 'IF' statement. The 'IF' expression has had to be
** evalued in order to see what followed it so the action of the statement
** has to be carried out here rather than calling one of the other 'IF'
** functions
*/
  if (result != BASFALSE)
    dest = GET_DEST(thenplace);	/* Result is 'true' - Go to 'THEN' code */
  else {
    dest = GET_DEST(elseplace);
  }
  if (single) {
    if (*dest == BASIC_TOKEN_XLINENUM)	/* Unresolved line number follows THEN or ELSE */
      dest = set_linedest(dest);
    else if (*dest == BASIC_TOKEN_LINENUM) {	/* Resolved line number follows THEN or ELSE */
      dest = GET_ADDRESS(dest, byte *);
    }
  }
  if (basicvars.traces.lines) {
    int32 destline = get_lineno(find_linestart(dest));
    if (get_lineno(basicvars.current) != destline) trace_line(destline);
  }
  if (basicvars.traces.branches) trace_branch(ifplace, dest);
  basicvars.current = dest;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_xif\n");
#endif /* DEBUG */
}

/*
** 'exec_library' deals with the Basic 'LIBRARY' statement.
** This version allows a list of library names to be specified
** after the keyword 'LIBRARY'
*/
void exec_library(void) {
  basicstring name;
  char *libname;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_library\n");
#endif /* DEBUG */
  basicvars.current++;
  if (*basicvars.current == BASIC_TOKEN_LOCAL) error(ERR_NOLIBLOC);	/* 'LIBRARY LOCAL' not allowed */
  do {
    stackitem stringtype;
    expression();	/* Get a library name */
    stringtype = GET_TOPITEM;
    if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) error(ERR_TYPESTR);
    name = pop_string();
    if (name.stringlen>0) {	/* Ignore non-existant library names */
      libname = tocstring(name.stringaddr, name.stringlen);
      if (stringtype == STACK_STRTEMP) free_string(name);
      read_library(libname, LOAD_LIBRARY);	/* Save library on Basic heap */
    }
    if (*basicvars.current != ',') break;		/* Escape if there is nothing more to do */
    basicvars.current++;
  } while (TRUE);
  check_ateol();
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_library\n");
#endif /* DEBUG */
}

/*
** 'def_locvar' handles the 'LOCAL <variable>' statement and
** creates local variables. If the variable already exists it
** just its value on the stack and resets the variable to zero.
** If it does not exist, a new variable is created.
*/
static void def_locvar(void) {
  basicstring descriptor;
  lvalue locvar;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:def_locvar\n");
#endif /* DEBUG */
  if (basicvars.procstack == NIL) error(ERR_LOCAL);	/* LOCAL found outside a PROC or FN */
  basicvars.runflags.make_array = TRUE;	/* Create arrays, do not flag errors if missing in 'get_lvalue' */
  do {
    get_lvalue(&locvar);
    switch (locvar.typeinfo) {	/* Now to save the variable and set it to its new initial value */
    case VAR_INTWORD:
      save_int(locvar, *locvar.address.intaddr);
      *locvar.address.intaddr = 0;
      break;
    case VAR_UINT8:
      save_uint8(locvar, *locvar.address.uint8addr);
      *locvar.address.uint8addr = 0;
      break;
    case VAR_INTLONG:
      save_int64(locvar, *locvar.address.int64addr);
      *locvar.address.int64addr = 0;
      break;
    case VAR_FLOAT:
      save_float(locvar, *locvar.address.floataddr);
      *locvar.address.floataddr = 0.0;
      break;
    case VAR_STRINGDOL:
      save_string(locvar, *locvar.address.straddr);
      locvar.address.straddr->stringlen = 0;
      locvar.address.straddr->stringaddr = nullstring;	/* 'nullstring' is defined in variables */
      break;
    case VAR_INTBYTEPTR:
      save_int(locvar, basicvars.memory[locvar.address.offset]);
      basicvars.memory[locvar.address.offset] = 0;
      break;
    case VAR_INTWORDPTR:
      save_int(locvar, get_integer(locvar.address.offset));
      store_integer(locvar.address.offset, 0);
      break;
    case VAR_FLOATPTR:
      save_float(locvar, get_float(locvar.address.offset));
      store_float(locvar.address.offset, 0.0);
      break;
    case VAR_DOLSTRPTR:
      descriptor.stringlen = get_stringlen(locvar.address.offset)+1;	/* +1 for CR at end */
      descriptor.stringaddr = alloc_string(descriptor.stringlen);
      memmove(descriptor.stringaddr, &basicvars.memory[locvar.address.offset], descriptor.stringlen);
      save_string(locvar, descriptor);
      basicvars.memory[locvar.address.offset] = asc_CR;
      break;
    case VAR_INTARRAY: case VAR_UINT8ARRAY: case VAR_FLOATARRAY: case VAR_STRARRAY:
      save_array(locvar);
      *locvar.address.arrayaddr = NIL;
      break;
    default:
      error(ERR_BROKEN, __LINE__, "mainstate");	/* Just in case something gets clobbered */
    }
    if (*basicvars.current != ',') break;	/* Escape if there is nothing more to do */
    basicvars.current++;	/* Skip ',' token */
  } while(TRUE);
  basicvars.runflags.make_array = FALSE;
  check_ateol();
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:def_locvar\n");
#endif /* DEBUG */
}

/*
** 'exec_local' deals with the Basic 'LOCAL' statement. There are three
** versions of this: 'LOCAL <variable>', 'LOCAL ERROR' and 'LOCAL DATA'
*/
void exec_local(void) {
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_local\n");
#endif /* DEBUG */
  basicvars.current++;	/* Skip LOCAL token */
  switch (*basicvars.current) {
  case BASIC_TOKEN_ERROR:	/* Got 'LOCAL ERROR' */
    basicvars.current = skip_token(basicvars.current);
    check_ateol();
    push_error(basicvars.error_handler);
    basicvars.errorislocal = 1;
    break;
  case BASIC_TOKEN_DATA:	/* Got 'LOCAL DATA' */
    basicvars.current = skip_token(basicvars.current);
    check_ateol();
    push_data(basicvars.datacur);
    break;
  default:	/* Defining local variables */
    def_locvar();
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_local\n");
#endif /* DEBUG */
}

/*
** 'exec_next' handles what is really the business end of a 'FOR' loop.
*/

static stack_for *find_for(void)
{
  stack_for *fp;

  if (GET_TOPITEM == STACK_INTFOR || GET_TOPITEM == STACK_INT64FOR || GET_TOPITEM == STACK_FLOATFOR) /* FOR control block is top of stack */
    fp = basicvars.stacktop.forsp;
  else {	/* Discard entries until FOR control block is found */
    fp = get_for();
  }
  if (fp == NIL) error(ERR_NOTFOR);	/* Not in a FOR loop */
  return fp;
}
  
void exec_next(void) {
  stack_for *fp;
  lvalue nextvar;
  boolean contloop = FALSE;
  int32 intvalue;
  int64 int64value;
  uint8 uint8value;
  static float64 floatvalue;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_next\n");
#endif /* DEBUG */
  if (kbd_escpoll()) error(ERR_ESCAPE);
  do {
    fp = find_for();
    basicvars.current++;	/* Skip NEXT token */
    if (!ateol[*basicvars.current]) {	/* There is a control variable (or two) here */
      if (*basicvars.current != ',') {
        get_lvalue(&nextvar);
        while (nextvar.address.intaddr != fp->forvar.address.intaddr) {
          /* top for loop is an inner one - pop the stack to find the one that matches the NEXT */
          pop_for();
          fp = find_for();
        }          
      }
    }
/*
** The 'simplefor' flag is set to true for the most common type of FOR loop,
** that is, the loop control variable is an integer variable and the step
** is +1. Deal with this case first and anything else later
*/
    if (fp->simplefor) {
      intvalue = *fp->forvar.address.intaddr+=1;
      if (intvalue<=fp->fortype.intfor.intlimit) {	/* Continue with loop */
        if (basicvars.traces.branches) trace_branch(basicvars.current, fp->foraddr);
        basicvars.current = fp->foraddr;
        return;
      }
      contloop = FALSE;	/* Escape from loop */
    }
    else {
      switch (fp->forvar.typeinfo) {	/* Right, let's bump up the 'FOR' variable */
      case VAR_INTWORD:		/* 32-bit integer value */
        intvalue = *fp->forvar.address.intaddr+fp->fortype.intfor.intstep;
        *fp->forvar.address.intaddr = intvalue;
        if (fp->fortype.intfor.intstep>0)
          contloop = intvalue<=fp->fortype.intfor.intlimit;
        else {
          contloop = intvalue>=fp->fortype.intfor.intlimit;
        }
        break;
      case VAR_INTLONG:		/* 64-bit integer value */
        int64value = *fp->forvar.address.int64addr+fp->fortype.int64for.int64step;
        *fp->forvar.address.int64addr = int64value;
        if (fp->fortype.int64for.int64step>0)
          contloop = int64value<=fp->fortype.int64for.int64limit;
        else {
          contloop = int64value>=fp->fortype.int64for.int64limit;
        }
        break;
      case VAR_UINT8:		/* Unsigned 8-bit integer value */
        uint8value = *fp->forvar.address.uint8addr+fp->fortype.uint8for.uint8step;
        *fp->forvar.address.uint8addr = uint8value;
        if (fp->fortype.uint8for.uint8step>0)
          contloop = uint8value<=fp->fortype.uint8for.uint8limit;
        else {
          contloop = uint8value>=fp->fortype.uint8for.uint8limit;
        }
        break;
      case VAR_FLOAT:		/* Word-aligned floating point value */
        floatvalue = *fp->forvar.address.floataddr+fp->fortype.floatfor.floatstep;
        *fp->forvar.address.floataddr = floatvalue;
        if (fp->fortype.floatfor.floatstep>0)
          contloop = floatvalue<=fp->fortype.floatfor.floatlimit;
        else {
          contloop = floatvalue>=fp->fortype.floatfor.floatlimit;
        }
        break;
      case VAR_INTBYTEPTR:	/* Pointer to a byte-aligned byte-sized integer */
        intvalue = basicvars.memory[fp->forvar.address.offset]+fp->fortype.intfor.intstep;
        basicvars.memory[fp->forvar.address.offset] = intvalue;
        if (fp->fortype.intfor.intstep>0)
          contloop = intvalue<=fp->fortype.intfor.intlimit;
        else {
          contloop = intvalue>=fp->fortype.intfor.intlimit;
        }
        break;
      case VAR_INTWORDPTR:	/* Pointer to a byte-aligned word-sized integer */
        intvalue = get_integer(fp->forvar.address.offset)+fp->fortype.intfor.intstep;
        store_integer(fp->forvar.address.offset, intvalue);
        if (fp->fortype.intfor.intstep>0)
          contloop = intvalue<=fp->fortype.intfor.intlimit;
        else {
          contloop = intvalue>=fp->fortype.intfor.intlimit;
        }
        break;
      case VAR_FLOATPTR:	/* Pointer to byte-aligned floating point value */
        floatvalue = get_float(fp->forvar.address.offset)+fp->fortype.floatfor.floatstep;
        store_float(fp->forvar.address.offset, floatvalue);
        if (fp->fortype.floatfor.floatstep>0)
          contloop = floatvalue<=fp->fortype.floatfor.floatlimit;
        else {
          contloop = floatvalue>=fp->fortype.floatfor.floatlimit;
        }
        break;
      default:
        error(ERR_BROKEN, __LINE__, "mainstate");
      }
    }
    if (contloop) {	/* Continue with loop */
      if (basicvars.traces.branches) trace_branch(basicvars.current, fp->foraddr);
      basicvars.current = fp->foraddr;
      return;
    }
    pop_for();	/* Loop has finished - Discard loop control block */
  } while (*basicvars.current == ',');
  check_ateol();
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_next\n");
#endif /* DEBUG */
}

/*
** 'exec_onerror' deals with the Basic 'ON ERROR' statement
*/
static void exec_onerror(void) {
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_onerror\n");
#endif /* DEBUG */
  basicvars.current++;		/* Skip ON token */
  switch (*basicvars.current) {
  case BASIC_TOKEN_OFF:	/* Got 'ON ERROR OFF' */
    clear_error();
    basicvars.current++;
    check_ateol();
    break;
  case BASIC_TOKEN_LOCAL:	/* Got 'ON ERROR LOCAL' */
    basicvars.current++;
    push_error(basicvars.error_handler);
    set_local_error();
    while (*basicvars.current != asc_NUL) basicvars.current = skip_token(basicvars.current);
    break;
  default: /* Got 'ON ERROR <statements>' */
    if (basicvars.errorislocal) {
      push_error(basicvars.error_handler);
      set_local_error();
    } else {
      set_error();
    }
    while (*basicvars.current != asc_NUL) basicvars.current = skip_token(basicvars.current);
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_onerror\n");
#endif /* DEBUG */
}

/*
** 'find_else' is called to locate an 'ELSE' clause in a 'ON' statement.
** If it finds one, control is passed to the statement after the 'ELSE'
** otherwise an error is flagged.
**'ateol' checks for both an end of line and an 'ELSE' so it is used here.
** Note that the code does not check the syntax of the statement along the
** way. All it is interested in is finding an 'ELSE' (this matches what the
** Acorn interpreter does).
*/
static void find_else(byte *tp, int32 index) {
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:find_else\n");
#endif /* DEBUG */
  while (!ateol[*tp]) tp = skip_token(tp);
  if (*tp == BASIC_TOKEN_XELSE) {
    if (basicvars.traces.branches) trace_branch(basicvars.current, tp);
/*
** Note that the 'ELSE' token is followed by an offset. Need to
** skip this as well
*/
    basicvars.current = tp+1+OFFSIZE;
  }
  else {	/* No 'ELSE' clause found - Flag an 'ON' range error */
    error(ERR_ONRANGE, index);
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:find_else\n");
#endif /* DEBUG */
}

/*
** 'find_onentry' looks for entry number 'wanted' in an 'ON' statement.
** It returns a pointer to the 'wanted'th item or the 'ELSE' token of
** an 'ELSE' clause if no entry is found. If there is no entry to match
** the value passed to it and no 'ELSE' clause, an error is flagged.
** The function takes into account any expressions in brackets it comes
** across. These could be procedure or function calls or array references.
** ('ON' statements allow general expressions instead of just simple line
** numbers.)
*/
static byte *find_onentry(byte *tp, int32 wanted) {
  int32 brackets, count;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:find_onentry\n");
#endif /* DEBUG */
  count = 1;
  brackets = 0;
  do {
    while (*tp != ':' && *tp != asc_NUL && *tp != BASIC_TOKEN_XELSE && (*tp != ',' || brackets != 0)) {
      tp = skip_token(tp);
      if (*tp == '(')
        brackets++;
      else if (*tp == ')') {
        brackets--;
      }
    }
    if (*tp == BASIC_TOKEN_XELSE) break;	/* Check this first to avoid clash with ATEOL */
    if (ateol[*tp]) error(ERR_ONRANGE, wanted);
    count++;
    if (count == wanted) break;
    if (*tp != ',') error(ERR_COMISS);
    tp++;	/* Skip the ',' */
  } while (TRUE);
  if (*tp == ',') tp++;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:find_onentry\n");
#endif /* DEBUG */
  return tp;
}

/*
** 'exec_onbranch' handles the 'ON ... GOTO', 'ON ... GOSUB' and 'ON ... PROC'
** statements.
** This code is strictly interpreted. It would be better if a table of
** pointers to the line numbers was constructed to allow the statement to be
** processed more quickly but this code will do for now. The 'ON' statement
** is not that important in Basic V/VI and is mainly here for compatibility
*/
static void exec_onbranch(void) {
  int32 index;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_onbranch\n");
#endif /* DEBUG */
  index = eval_integer();
  if (index<1)	/* 'ON' index is out of range */
    find_else(basicvars.current, index);
  else {
    byte onwhat = *basicvars.current;
    if (onwhat == BASIC_TOKEN_GOTO || onwhat == BASIC_TOKEN_GOSUB) {
      byte *dest;
      basicvars.current++;	/* Skip the 'GOTO' or 'GOSUB' token */
      if (index>1) basicvars.current = find_onentry(basicvars.current, index);
      if (*basicvars.current == BASIC_TOKEN_XELSE) {
        basicvars.current+=1+OFFSIZE;	/* Find statement after 'ELSE' */
        if (*basicvars.current == BASIC_TOKEN_XLINENUM) error(ERR_SYNTAX);	/* Line number is not allowed here */
      }
      else {	/* Try to find a line number */
        if (*basicvars.current == BASIC_TOKEN_LINENUM)		/* GOTO/GOSUB destination is known */
          dest = GET_ADDRESS(basicvars.current, byte *);
        else if (*basicvars.current == BASIC_TOKEN_XLINENUM)	/* GOTO/GOSUB destination not filled in yet */
          dest = set_linedest(basicvars.current);
        else {	/* Destination line number is given by an expression */
          int32 line = eval_integer();
          if (line<0 || line>MAXLINENO) error(ERR_LINENO);	/* Line number is out of range */
          dest = find_line(line);
          if (get_lineno(dest) != line) error(ERR_LINEMISS, line);
          dest = FIND_EXEC(dest);
        }
        if (basicvars.traces.branches) trace_branch(basicvars.current, dest);
        if (onwhat == BASIC_TOKEN_GOSUB) {	/* Got 'ON ... GUSUB'. Find point to which to return */
          while (*basicvars.current != ':' && *basicvars.current != asc_NUL) basicvars.current = skip_token(basicvars.current);
          if (*basicvars.current == ':') basicvars.current++;
          push_gosub();
        }
        basicvars.current = dest;
      }
    }
    else if (onwhat == BASIC_TOKEN_XFNPROCALL || onwhat == BASIC_TOKEN_FNPROCALL) {	/* Got 'ON ... PROC' */
     fnprocdef *dp = NULL;
      variable *pp = NULL;
      if (index>1) basicvars.current = find_onentry(basicvars.current, index);
      if (*basicvars.current == BASIC_TOKEN_XELSE) {	/* Branch to statement after 'ELSE' */
        basicvars.current+=1+OFFSIZE;		/* Find statement after 'ELSE' */
        if (*basicvars.current == BASIC_TOKEN_XLINENUM) error(ERR_SYNTAX);	/* Line number is not allowed here */
      }
      else {	/* Call one of the procedures */
        if (*basicvars.current == BASIC_TOKEN_XFNPROCALL) {	/* Procedure call not seen before */
          byte *ep, *base = get_srcaddr(basicvars.current);	/* Find the start of the procedure name */
          ep = skip_name(base);
          if (*(ep-1) == '(') ep--;	/* Do not include '(' of parameter list in name */
          pp = find_fnproc(base, ep-base);
          dp = pp->varentry.varfnproc;
          set_address(basicvars.current, pp);
          *basicvars.current = BASIC_TOKEN_FNPROCALL;
          basicvars.current+=1+LOFFSIZE;		/* Skip pointer to procedure */
          if (*basicvars.current != '(') {	/* PROC call has no parameters */
            if (dp->parmlist != NIL) error(ERR_NOTENUFF, pp->varname);	/* But it should have */
          }
          else if (dp->parmlist == NIL) {		/* Got a '(' but PROC/FN has no parameters */
            error(ERR_TOOMANY, pp->varname);
          }
        }
        else if (*basicvars.current == BASIC_TOKEN_FNPROCALL) {	/* Known procedure */
          pp = GET_ADDRESS(basicvars.current, variable *);
          dp = pp->varentry.varfnproc;
          basicvars.current+=1+LOFFSIZE;		/* Skip pointer to procedure */
        }
        else {
          error(ERR_SYNTAX);
        }
        if (*basicvars.current == '(') push_parameters(dp, pp->varname);	/* Deal with parameters */
        if (basicvars.traces.enabled) {
          if (basicvars.traces.procs) trace_proc(pp->varname, TRUE);
          if (basicvars.traces.branches) trace_branch(basicvars.current, dp->fnprocaddr);
        }
        while (*basicvars.current != ':' && *basicvars.current != asc_NUL) basicvars.current = skip_token(basicvars.current);	/* Find return address */
        if (*basicvars.current == ':') basicvars.current++;
        push_proc(pp->varname, dp->parmcount);
        basicvars.current = dp->fnprocaddr;
      }
    }
    else {
      error(ERR_SYNTAX);
    }
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_onbranch\n");
#endif /* DEBUG */
}

/*
** 'exec_on' deals with the various types of 'ON' statement
*/
void exec_on(void) {
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_on\n");
#endif /* DEBUG */
  basicvars.current++;	/* Skip ON token */
  if (*basicvars.current == BASIC_TOKEN_ERROR)	/* Dealing with 'ON ERROR' */
    exec_onerror();
  else if (ateol[*basicvars.current])	/* Got just 'ON' */
    emulate_on();
  else {
    exec_onbranch();
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_on\n");
#endif /* DEBUG */
}

/*
** 'exec_oscli' issues an OS command.
** The interpreter supports and extended 'OSCLI ... TO' version of
** the statement which allows command responses to be read
*/
void exec_oscli(void) {
  stackitem stringtype;
  basicstring descriptor;
  lvalue response, linecount;
  boolean tofile;
  char *oscli_string;
  char respname[FNAMESIZE];
  int count, n;
  FILE *respfile, *respfh;
  basicarray *ap;

#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_oscli\n");
#endif /* DEBUG */
  basicvars.current++;	/* Hop over the OSCLI token */
  expression();
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) error(ERR_TYPESTR);
  oscli_string=malloc(MAXSTRING);
  tofile = *basicvars.current == BASIC_TOKEN_TO;
  if (tofile) {	/* Have got 'OSCLI <command> TO' */
    basicvars.current++;
    get_lvalue(&response);
    if (response.typeinfo != VAR_STRARRAY) error(ERR_STRARRAY);
    if (*basicvars.current == ',') {	/* Variable in which to store count of lines read */
      basicvars.current++;
      get_lvalue(&linecount);
    }
    else {
      linecount.typeinfo = 0;
    }
  }
  check_ateol();
  descriptor = pop_string();
  memmove(oscli_string, descriptor.stringaddr, descriptor.stringlen);	/* Copy string */
  oscli_string[descriptor.stringlen] = asc_NUL;		/* Append a NUL keep OS_CLI happy */
  if (stringtype == STACK_STRTEMP) free_string(descriptor);
/* Issue command */
  if (!tofile) {	/* Response not wanted - Run command and go home */
    mos_oscli(oscli_string, NIL, NULL);
    free(oscli_string);
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "Exited function mainstate.c:exec_oscli at line %d\n", 2 + __LINE__);
#endif /* DEBUG */
    return;
  }
/*
** Issue the command and then read the command response
*/
  respfh=secure_tmpnam(respname);
  if (!respfh) {
    free(oscli_string);
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "Exited function mainstate.c:exec_oscli at line %d\n", 2 + __LINE__);
#endif /* DEBUG */
    error (ERR_OSCLIFAIL, strerror (errno));
    return;
  }
  mos_oscli(oscli_string, respname, respfh);
  free(oscli_string);
  respfile = fopen(respname, "rb");
  if (respfile == 0) return;
  ap = *response.address.arrayaddr;
/* Start by discarding the current contents of the array */
  descriptor.stringlen = 0;
  descriptor.stringaddr = nullstring;	/* Found in variables.c */
  for (n=0; n<ap->arrsize; n++) {
    free_string(ap->arraystart.stringbase[n]);
    ap->arraystart.stringbase[n] = descriptor;
  }
  count = 0;	/* Number of lines read */
  while (!feof(respfile) && count+1<ap->arrsize) {	/* Read the command output */
    int length;
    char *p = fgets(basicvars.stringwork, MAXSTRING, respfile);
    if (p == NIL) {	/* Either an error or EOF reached and no data read */
      if (!ferror(respfile)) break;		/* End of file and no data read */
      fclose(respfile);
      remove(respname);
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "Exited function mainstate.c:exec_oscli at line %d\n", 2 + __LINE__);
#endif /* DEBUG */
      error(ERR_BROKEN, __LINE__, "mainstate");
    }
/* Remove any CRs or LFs or trailing blanks in the line and copy it to the string array */
    if (p == NIL) break;
    p = basicvars.stringwork;
    if (p[0] == '\r') p++;	/* Remove possible CR at the start of the line */
    length = strlen(p);
    while (length>0 && (p[length-1] == '\n' || p[length-1] == '\r' || p[length-1] == ' ')) length--;
    if (length>0 || !feof(respfile)) {	/* Don't want an empty line at the end of the array */
      descriptor.stringlen = length;
      descriptor.stringaddr = alloc_string(length);
      if (length>0) memmove(descriptor.stringaddr, p, length);
      count++;
      ap->arraystart.stringbase[count] = descriptor;
    }
  }
  fclose(respfile);
  remove(respname);
/* Save the number of lines stored in the array */
  if (linecount.typeinfo != 0) store_value(linecount, count, NOSTRING);
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "Exited function mainstate.c:exec_oscli at line %d\n", 2 + __LINE__);
#endif /* DEBUG */
}

/*
** 'exec_overlay' deals with the unsupported 'OVERLAY' statement
*/
void exec_overlay(void) {
  error(ERR_UNSUPSTATE);
}

/*
** 'exec_proc' calls a procedure
*/
void exec_proc(void) {
  fnprocdef *dp;
  variable *vp;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_proc\n");
#endif /* DEBUG */
  if (kbd_escpoll()) error(ERR_ESCAPE);
  vp = GET_ADDRESS(basicvars.current, variable *);
  if (strlen(vp->varname) > (MAXNAMELEN-1)) error(ERR_BADVARPROCNAME);
  dp = vp->varentry.varfnproc;
  basicvars.current+=1+LOFFSIZE;		/* Skip pointer to procedure */
  if (*basicvars.current == '(') {
    push_parameters(dp, vp->varname);	/* Deal with parameters */
    if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
  }
  push_proc(vp->varname, dp->parmcount);
  if (basicvars.traces.enabled) {
    if (basicvars.traces.procs) trace_proc(vp->varname, TRUE);
    if (basicvars.traces.branches) trace_branch(basicvars.current, dp->fnprocaddr);
  }
  basicvars.local_restart = &basicvars.error_restart;
  basicvars.current = dp->fnprocaddr;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_proc\n");
#endif /* DEBUG */
}

/*
** 'exec_xproc' is invoked the first time a reference to a procedure is
** seen to locate the procedure and fill in its address
*/
void exec_xproc(void) {
  byte *tp, *base;
  variable *vp;
  fnprocdef *dp;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_xproc\n");
#endif /* DEBUG */
  tp = basicvars.current;
  base = get_srcaddr(tp);	/* Point at name of procedure */
  if (*base != BASIC_TOKEN_PROC) error(ERR_NOTAPROC);	/* Ensure a procedure is being called */
  tp = skip_name(base);		/* Skip name */
  if (*(tp-1) == '(') tp--;	/* Do not include '(' of parameter list in name */
  vp = find_fnproc(base, tp-base);
  dp = vp->varentry.varfnproc;
  *basicvars.current = BASIC_TOKEN_FNPROCALL;
  set_address(basicvars.current, vp);
  tp = basicvars.current+LOFFSIZE+1;
  if (*tp != '(') {	/* PROC call has no parameters */
    if (dp->parmlist != NIL) error(ERR_NOTENUFF, vp->varname+1);	/* But it should have */
    if (!ateol[*tp]) error(ERR_SYNTAX);		/* No parameters - Can check for end of statement here */
  }
  else if (dp->parmlist == NIL) {		/* Got a '(' but PROC/FN has no parameters */
    error(ERR_TOOMANY, vp->varname);
  }
  exec_proc();		/* Call the procedure */
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_xproc\n");
#endif /* DEBUG */
}

/*
** 'exec_quit' finishes the run of the interpreter itself. It
** can be followed a return code. This value is passed back to
** the operating system. It defaults to 0 (EXIT_SUCCESS)
*/
void exec_quit(void) {
  uint8 retcode;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_quit\n");
#endif /* DEBUG */
  basicvars.current++;
  if (isateol(basicvars.current))	/* QUIT is not followed by anything */
    retcode = EXIT_SUCCESS;
  else {	/* QUIT is followed by a return code */
    retcode = eval_integer();
    check_ateol();
  }
  exit_interpreter(retcode);
}

/*
** 'find_data' is called to find the start of the next data field in a
** DATA statement.
** On entry, datacur, the data pointer, will either be NIL or pointing
** at a ',', the DATA token at the start of a line or the NUL at the end
** of the line.
*/
static void find_data(void) {
  byte *dp;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:find_data\n");
#endif /* DEBUG */
  dp = basicvars.datacur;
  if (dp != NIL && (*dp == ',' || *dp == BASIC_TOKEN_DATA)) {	/* Skip to next field */
    basicvars.datacur++;
    return;
  }
  if (dp == NIL)		/* First READ of all */
    dp = basicvars.start;
  else {	/* At end of line - Skip to next one */
/* The 'end of the line' here is actually the end of the source */
/* part of the line. This is followed by the DATA token and the */
/* offset back to the start of the data itself. The code has to */
/* skip these items to move to the start of the next line */
    dp = skip_token(dp+1)+1;
  }
/* Look for the next 'DATA' statement */
  while (!AT_PROGEND(dp) && *FIND_EXEC(dp) != BASIC_TOKEN_DATA) dp+=GET_LINELEN(dp);
  if (AT_PROGEND(dp)) error(ERR_DATA);	/* Have not found a DATA statement */
/*
** The DATA token is followed by the offset to the start of the data
** in the source part of the line. Find address of data
*/
  basicvars.datacur = get_srcaddr(FIND_EXEC(dp));
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:find_data\n");
#endif /* DEBUG */
}

/*
** 'read_numeric' deals with numeric variables found in 'READ' statements.
** Here, the value in the 'DATA' statement is interpreted as an expression
** and not just a string. The data pointer is left pointing at the ','
** or NUL at the end of the line after the field
*/
static void read_numeric(lvalue destination) {
  byte *dp;
  int32 n;
  char text[MAXSTATELEN];
  byte readexpr[MAXSTATELEN];
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:read_numeric\n");
#endif /* DEBUG */
  n = 0;
  dp = skip(basicvars.datacur);
  while (*dp != asc_NUL && *dp != ',') {	/* Copy value to be read */
    text[n] = *dp;
    dp++;
    n++;
  }
  text[n] = asc_NUL;
  if (n == 0) error(ERR_BADEXPR);	/* Number string is empty */
  basicvars.datacur = dp;
  tokenize(text, readexpr, NOLINE, FALSE);	/* Tokenise the expression */
//  tokenize(text, readexpr, NOLINE);	/* Tokenise the expression */
  save_current();	/* Preserve our place in the program */
  basicvars.current = FIND_EXEC(&readexpr[0]);
  expression();
  restore_current();
  switch (destination.typeinfo) {	/* Now save the value just read */
  case VAR_INTWORD:	/* 32-bit integer variable */
    *destination.address.intaddr = pop_anynum32();
    break;
  case VAR_UINT8:	/* Unsigned 8-bit integer variable */
    *destination.address.uint8addr = pop_anynum32();
    break;
  case VAR_INTLONG:	/* 64-bit integer variable */
    *destination.address.int64addr = pop_anynum64();
    break;
  case VAR_FLOAT:	/* Floating point variable */
    *destination.address.floataddr = pop_anynumfp();
    break;
  case VAR_INTBYTEPTR:	/* Pointer to byte-sized integer */
    basicvars.memory[destination.address.offset] = pop_anynum32();
    break;
  case VAR_INTWORDPTR:	/* Pointer to word-sized integer */
    store_integer(destination.address.offset, pop_anynum32());
    break;
  case VAR_FLOATPTR:	/* Pointer to floating point variable */
    store_float(destination.address.offset, pop_anynumfp());
    break;
  default:
    error(ERR_VARNUMSTR);
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:read_numeric\n");
#endif /* DEBUG */
}

/*
** 'read_string' is called when a string variable is found on a
** 'READ' statement. The data pointer is left pointing at the ','
** or NUL at the end of the line after the field
*/
static void read_string(lvalue destination) {
  int32 length, shorten=0;
  byte *start, *cp;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:read_string\n");
#endif /* DEBUG */
  start = cp = skip(basicvars.datacur);
  if (*cp == '\"') {	/* String is in quotes */
    start++;
    do {
      cp++;
      if (*cp == '\"' && *(cp+1) == '\"') cp+=2;
    } while (*cp != asc_NUL && *cp != '\"');
    if (*cp != '\"') error(ERR_QUOTEMISS);	/* " missing */
    length = cp-start;
    do	/* Skip '"' and find next field */
      cp++;
    while (*cp != asc_NUL && *cp != ',');
  }
  else {
    while (*cp != asc_NUL && *cp != ',') cp++;	/* Find end of string */
    length = cp-start;
  }
  basicvars.datacur = cp;
  switch (destination.typeinfo) {	/* Now save the value just read */
  case VAR_STRINGDOL:	/* String variable */
    if (destination.address.straddr->stringlen != length) {
      free_string(*destination.address.straddr);	/* Dispose of old string */
      destination.address.straddr->stringlen = length;
      destination.address.straddr->stringaddr = alloc_string(length);
    }
    if (length != 0) shorten=memcpydedupe(destination.address.straddr->stringaddr, start, length, '"');
    break;
  case VAR_DOLSTRPTR:	/* Pointer to '$<string>' */
    if (length != 0) shorten=memcpydedupe((char *)&basicvars.memory[destination.address.offset], start, length, '"');
    basicvars.memory[destination.address.offset+length-shorten] = asc_CR;
    break;
  default:
    error(ERR_VARNUMSTR);
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:read_string\n");
#endif /* DEBUG */
}

/*
** 'exec_read' deals with the Basic 'READ' statement.
*/
void exec_read(void) {
  lvalue destination;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_read\n");
#endif /* DEBUG */
  basicvars.current++;		/* Skip READ */
  if (ateol[*basicvars.current]) return;	/* Return if there is nothing to do */
  if (basicvars.runflags.outofdata) error(ERR_DATA);	     /* Have run out of data statements */
  while (TRUE) {
    get_lvalue(&destination);
    find_data();
    if ((destination.typeinfo & TYPEMASK)<=VAR_FLOAT || (destination.typeinfo & TYPEMASK)==VAR_UINT8)	/* Numeric value */
      read_numeric(destination);
    else {	/* Character string */
      read_string(destination);
    }
    if (*basicvars.current != ',') break;	/* Escape from loop if there is nothing left to do */
    basicvars.current++;
  }
  check_ateol();
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_read\n");
#endif /* DEBUG */
}

/*
** 'exec_repeat' handles the start of a 'REPEAT' loop
*/
void exec_repeat(void) {
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_repeat\n");
#endif /* DEBUG */
  basicvars.current++;		/* Skip REPEAT token */
  if (*basicvars.current == ':') basicvars.current++;	/* Found a ':' - Move past it */
  if (*basicvars.current == asc_NUL) {	/* Nothing on line after REPEAT - Try next line */
    basicvars.current++;	/* Move to start of next line */
    if (basicvars.traces.lines) trace_line(get_lineno(basicvars.current));
    basicvars.current = FIND_EXEC(basicvars.current);
  }
  push_repeat();
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_repeat\n");
#endif /* DEBUG */
}

/*
** 'exec_report' deals with the 'REPORT' statement which prints a copy
** of the last error message generated.
*/
void exec_report(void) {
  char *p;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_report\n");
#endif /* DEBUG */
  basicvars.current++;
  check_ateol();
  p = get_lasterror();
  emulate_printf("\r\n");
  emulate_vdustr(p, strlen(p));
  basicvars.printcount+=strlen(p);
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_report\n");
#endif /* DEBUG */
}

/*
** 'restore_dataptr' deals with the 'RESTORE <line>' statement
*/
static void restore_dataptr(void) {
  byte *dest, *p;
  int32 line;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:restore_dataptr\n");
#endif /* DEBUG */
  basicvars.runflags.outofdata = FALSE;
  switch (*basicvars.current) {
  case BASIC_TOKEN_XLINENUM:	/* RESTORE <line number> */
/*
** Note: set_linedest' returns a pointer to the first executable token on
** the line but we need a pointer to start of the line
*/
    dest = find_linestart(set_linedest(basicvars.current));
    basicvars.current = skip_token(basicvars.current);	/* Skip 'line number' token */
    check_ateol();
    break;
  case BASIC_TOKEN_LINENUM:	/* RESTORE <line number> */
    dest = GET_ADDRESS(basicvars.current, byte *);
    dest = find_linestart(dest);
    basicvars.current = skip_token(basicvars.current);	/* Skip 'line number' token */
    check_ateol();
    break;
  case '+':		/* Destination is given as an offset from the next line */
    basicvars.current++;
    line = eval_integer();
    check_ateol();
    p = basicvars.current;
    while (*p != asc_NUL) p = skip_token(p);	/* Find the start of the next line */
    p++;		/* Point at start of next line */
/*
** The line count is decrement by one as 'RESTORE +1' moves
** the data pointer to this line and we have just advanced to
** the line
*/
    line--;
    while (!AT_PROGEND(p) && line>0) {
      p+=GET_LINELEN(p);
      line--;
    }
    if (AT_PROGEND(p)) {	/* Reached end of program and no DATA statements were found */
      basicvars.runflags.outofdata = TRUE;
      return;		/* Return as there is nothing more to do */
    }
    dest = p;
    break;
  default:
    if (ateol[*basicvars.current])	/* RESTORE on its own */
      dest = basicvars.start;	/* Start at beginning of program */
    else {	/* RESTORE followed by an expression */
      line = eval_integer();		/* Find number of line */
      check_ateol();
      dest = find_line(line);
      if (get_lineno(dest) != line) error(ERR_LINEMISS, line);
    }
  }
/*
** 'dest' points at the start of the line that is the target of the
** 'RESTORE' statement. Look for a DATA statement at this point or
** after it
*/
  while (!AT_PROGEND(dest) && *FIND_EXEC(dest) != BASIC_TOKEN_DATA) {
    dest+=GET_LINELEN(dest);
  }
  if (AT_PROGEND(dest))		/* No DATA statement was found */
    basicvars.runflags.outofdata = TRUE;
  else {
/*
** Point at DATA token before first item of data. The code needs to
** point here to allow the case 'DATA, ...' (where there is a comma
** immediately after the DATA token) to work. The comma would
** otherwise be skipped by the 'find data' function, that is, the
** first field would be missed.
*/
    basicvars.datacur = get_srcaddr(FIND_EXEC(dest))-1;
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:restore_dataptr\n");
#endif /* DEBUG */
}

/*
** 'exec_restore' handles the Basic 'RESTORE' statement.
*/
void exec_restore(void) {
  stackitem item;

#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_restore\n");
#endif /* DEBUG */
  basicvars.current++;		/* Skip RESTORE token */
  switch (*basicvars.current) {
  case BASIC_TOKEN_ERROR:	/* RESTORE ERROR */
    basicvars.current = skip_token(basicvars.current);
    check_ateol();
    if (GET_TOPITEM != STACK_ERROR) error(ERR_ERRNOTOP);	/* Saved error block not on top of stack */
    basicvars.error_handler = pop_error();
    break;
  case BASIC_TOKEN_LOCAL:	/* RESTORE LOCAL */
    basicvars.current = skip_token(basicvars.current);
    check_ateol();
    if (basicvars.procstack == NIL) error(ERR_LOCAL);		/* LOCAL found outside a PROC/FN */
    item = stack_unwindlocal();
    if (item == STACK_ERROR) {
      basicvars.error_handler = pop_error();
    }
    if (GET_TOPITEM != STACK_PROC) empty_stack(STACK_PROC);
    break;
  case BASIC_TOKEN_DATA:	/* RESTORE DATA */
    basicvars.current = skip_token(basicvars.current);
    check_ateol();
    if (GET_TOPITEM != STACK_DATA) error(ERR_DATANOTOP);	/* Saved DATA pointer not on top of stack */
    basicvars.datacur = pop_data();
 /* Note: this does not restore the 'out of data' flag */
    break;
  default:	/* Move 'DATA' pointer */
    restore_dataptr();
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_restore\n");
#endif /* DEBUG */
}

/*
** 'exec_return' handles returns from GOSUB-type subroutines
*/
void exec_return(void) {
  gosubinfo returnblock;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_return\n");
#endif /* DEBUG */
  basicvars.current++;		/* Skip RETURN token */
  check_ateol();
  if (basicvars.gosubstack == NIL) error(ERR_RETURN);
  if (GET_TOPITEM != STACK_GOSUB) empty_stack(STACK_GOSUB);	/* Throw away unwanted entries on Basic stack */
  returnblock = pop_gosub();
  if (basicvars.traces.branches) trace_branch(basicvars.current, returnblock.retaddr);
  basicvars.current = returnblock.retaddr;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_return\n");
#endif /* DEBUG */
}

/*
** 'exec_run' deals with the 'RUN' command.
** This interpreter supports an extended version of 'RUN' where the
** line number from which to start program execution can be given. It
** is also possible to specift a file name after the command, so that
** it in fact works just like 'CHAIN'.
*/
void exec_run(void) {
  basicstring string;
  byte *bp;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_run\n");
#endif /* DEBUG */
  basicvars.current++;		/* Skip RUN token */
  bp = NIL;
  if (!ateol[*basicvars.current]) {	/* RUN <filename> or RUN <linenumber> found */
    stackitem topitem;
    expression();
    topitem = GET_TOPITEM;
    switch (topitem) {
      char *filename;
      int32 line;
      case STACK_INT: case STACK_UINT8: case STACK_FLOAT: case STACK_INT64:
        line = pop_anynum32();
        if (line<0 || line>MAXLINENO) error(ERR_LINENO);
        bp = find_line(line);
        if (get_lineno(bp) != line) error(ERR_LINEMISS, line);
        break;
      case STACK_STRING: case STACK_STRTEMP:
        string = pop_string();
        filename = tocstring(string.stringaddr, string.stringlen);
        if (topitem == STACK_STRTEMP) free_string(string);
        check_ateol();
        clear_error();
        clear_varlists();
        clear_strings();
        clear_heap();
        read_basic(filename);
        break;
      default:
        error(ERR_BADOPER);
    }
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exiting function mainstate.c:exec_run via run_program()\n");
#endif /* DEBUG */
  run_program(bp);
}

/*
** 'exec_stop' deals with the 'STOP' statement
*/
void exec_stop(void) {
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_stop\n");
#endif /* DEBUG */
  basicvars.current++;
  check_ateol();
  error(ERR_STOP);
}

/*
** 'exec_swap' deals with the 'SWAP' statement that swaps the values of
** two variables or arrays
*/
void exec_swap(void) {
  lvalue first, second;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_swap\n");
#endif /* DEBUG */
  basicvars.current++;		/* Skip SWAP token */
  get_lvalue(&first);
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;		/* Skip ',' token */
  get_lvalue(&second);
  check_ateol();
  if ((first.typeinfo <= VAR_FLOAT || (first.typeinfo >= VAR_INTBYTEPTR && first.typeinfo <= VAR_FLOATPTR)) &&
     (second.typeinfo <= VAR_FLOAT || (second.typeinfo >= VAR_INTBYTEPTR && second.typeinfo <= VAR_FLOATPTR))) {
/* Switching numeric values */
    int64 ival1 = 0, ival2 = 0;
    static float64 fval1, fval2;
    boolean isint = 0;
    switch (first.typeinfo) {		/* Fetch first operand */
    case VAR_INTWORD:
      ival1 = *first.address.intaddr;
      isint = TRUE;
      break;
    case VAR_UINT8:
      ival1 = *first.address.uint8addr;
      isint = TRUE;
      break;
    case VAR_INTLONG:
      ival1 = *first.address.int64addr;
      isint = TRUE;
      break;
    case VAR_FLOAT:
      fval1 = *first.address.floataddr;
      isint = FALSE;
      break;
    case VAR_INTBYTEPTR:
      ival1 = basicvars.memory[first.address.offset];
      isint = TRUE;
      break;
    case VAR_INTWORDPTR:
      ival1 = get_integer(first.address.offset);
      isint = TRUE;
      break;
    case VAR_FLOATPTR:
      fval1 = get_float(first.address.offset);
      isint = FALSE;
      break;
    default:
      error(ERR_BROKEN, __LINE__, "mainstate");
    }

/* Fetch the second operand and store the first in its place */

    switch (second.typeinfo) {
    case VAR_INTWORD:
      ival2 = *second.address.intaddr;
      *second.address.intaddr = isint ? ival1 : TOINT(fval1);
      isint = TRUE;	/* Now change type flag to type of second value */
      break;
    case VAR_UINT8:
      ival2 = *second.address.uint8addr;
      *second.address.uint8addr = isint ? ival1 : TOINT(fval1);
      isint = TRUE;	/* Now change type flag to type of second value */
      break;
    case VAR_INTLONG:
      ival2 = *second.address.int64addr;
      *second.address.int64addr = isint ? ival1 : TOINT64(fval1);
      isint = TRUE;	/* Now change type flag to type of second value */
      break;
    case VAR_FLOAT:
      fval2 = *second.address.floataddr;
      *second.address.floataddr = isint ? TOFLOAT(ival1) : fval1;
      isint = FALSE;
      break;
    case VAR_INTBYTEPTR:
      ival2 = basicvars.memory[second.address.offset];
      basicvars.memory[second.address.offset] = isint ? ival1 : TOINT(fval1);
      isint = TRUE;
      break;
    case VAR_INTWORDPTR:
      ival2 = get_integer(second.address.offset);
      store_integer(second.address.offset, isint ? ival1 : TOINT(fval1));
      isint = TRUE;
      break;
    case VAR_FLOATPTR:
      fval2 = get_float(second.address.offset);
      store_float(second.address.offset, isint ? TOFLOAT(ival1) : fval1);
      isint = FALSE;
      break;
    default:
      error(ERR_BROKEN, __LINE__, "mainstate");
    }

/* Finally store the second operand in place of the first */

    switch (first.typeinfo) {
    case VAR_INTWORD:
      *first.address.intaddr = isint ? ival2 : TOINT(fval2);
      break;
    case VAR_UINT8:
      *first.address.uint8addr = isint ? ival2 : TOINT(fval2);
      break;
    case VAR_INTLONG:
      *first.address.int64addr = isint ? ival2 : TOINT(fval2);
      break;
    case VAR_FLOAT:
      *first.address.floataddr = isint ? TOFLOAT(ival2) : fval2;
      break;
    case VAR_INTBYTEPTR:
      basicvars.memory[first.address.offset] = isint ? ival2 : TOINT(fval2);
      break;
    case VAR_INTWORDPTR:
      store_integer(first.address.offset, isint ? ival2 : TOINT(fval2));
      break;
    case VAR_FLOATPTR:
      store_float(first.address.offset, isint ? TOFLOAT(ival2) : fval2);
      break;
    default:
      error(ERR_BROKEN, __LINE__, "mainstate");
    }
  }
  else if (first.typeinfo == VAR_STRINGDOL || first.typeinfo == VAR_DOLSTRPTR) {
    basicstring stringtemp;
    if (second.typeinfo != VAR_STRINGDOL && second.typeinfo != VAR_DOLSTRPTR) error(ERR_NOSWAP);
    if (first.typeinfo == VAR_STRINGDOL && second.typeinfo == VAR_STRINGDOL) {	/* Swap aaa$ and bbb$ */
      stringtemp = *first.address.straddr;
      *first.address.straddr = *second.address.straddr;
      *second.address.straddr = stringtemp;
    }
    else if (first.typeinfo == VAR_DOLSTRPTR && second.typeinfo == VAR_DOLSTRPTR) {	/* Swap $aaa and $bbb */
      int32 len1, len2;
      len1 = get_stringlen(first.address.offset)+1;	/* +1 for CR at end of string */
      len2 = get_stringlen(second.address.offset)+1;
      memmove(basicvars.stringwork, &basicvars.memory[first.address.offset], len1);
      memmove(&basicvars.memory[first.address.offset], &basicvars.memory[second.address.offset], len2);
      memmove(&basicvars.memory[second.address.offset], basicvars.stringwork, len1);
    }
    else {	/* Swap aaa$ and $bbb or $aaa and bbb$ */
      int len;
      if (first.typeinfo == VAR_DOLSTRPTR) {	/* Turn it into aaa$ and $bbb */
        lvalue temp = first;
        first = second;
        second = temp;
      }
/* Move '$bbb' string to a  proper string */
      stringtemp.stringlen = len = get_stringlen(second.address.offset);
      stringtemp.stringaddr = alloc_string(len);
      if (len>0) memmove(stringtemp.stringaddr, &basicvars.memory[second.address.offset], len);
/* Copy 'aaa$' string to address of other string and turn it into a '$xxx' type string */
      len = first.address.straddr->stringlen;	/* Get length of first string */
      if (len>0) memmove(&basicvars.memory[second.address.offset], first.address.straddr->stringaddr, len);
      basicvars.memory[second.address.offset+len] = asc_CR;
      free_string(*first.address.straddr);
      *first.address.straddr = stringtemp;
    }
  }
  else if ((first.typeinfo & VAR_ARRAY) != 0) {
    /* Arrays have become more complicated due to offheap arrays.
    ** We need to ensure the variable structure itself is corrected */
    basicarray *arraytemp1, *arraytemp2, *arrayswap;
    variable *var1, *var2, *vartmp;
    if (second.typeinfo != first.typeinfo) error(ERR_NOSWAP);
    arraytemp1 = *first.address.arrayaddr;
    arraytemp2 = *second.address.arrayaddr;
    var1 = arraytemp1->parent;
    var2 = arraytemp2->parent;
    /* Firstly, swap the basicarray pointers */
    arrayswap = var1->varentry.vararray;
    var1->varentry.vararray = var2->varentry.vararray;
    var2->varentry.vararray = arrayswap;
    /* But we need to swap back the parent references */
    vartmp=var1->varentry.vararray->parent;
    var1->varentry.vararray->parent=var2->varentry.vararray->parent;
    var2->varentry.vararray->parent=vartmp;
  }
  else {	/* Cannot swap these operands */
    error(ERR_NOSWAP);
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_swap\n");
#endif /* DEBUG */
}

/*
** 'exec_sys' handles the Basic 'SYS' statement, which is used
** to make operating system calls. These are often refered to
** as 'SWIs'.
*/
void exec_sys(void) {
  int32 n, parmcount, ip, swino = 0;
#ifndef TARGET_RISCOS
  int32 fp;
#endif
  size_t flags, outregs[MAXSYSPARMS];
  sysparm inregs[MAXSYSPARMS * 2];
  stackitem parmtype;
  basicstring descriptor, tempdesc[MAXSYSPARMS];
  lvalue destination;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_sys\n");
#endif /* DEBUG */
  basicvars.current++;
  expression();		/* Fetch the SWI name or number */
  parmtype = GET_TOPITEM;
  switch (parmtype) {	/* Untangle the SWI number */
  case STACK_INT: case STACK_UINT8: case STACK_INT64: case STACK_FLOAT:
    swino = pop_anynum32();
    break;
  case STACK_STRING: case STACK_STRTEMP:
    descriptor = pop_string();
    swino = mos_getswinum(descriptor.stringaddr, descriptor.stringlen, 0);
    if (parmtype == STACK_STRTEMP) free_string(descriptor);
    break;
  default:
    error(ERR_TYPENUM);
  }
/* Set up default values for all possible parameters */
  for (n=0; n<MAXSYSPARMS; n++) {
    outregs[n] = inregs[n].i = 0;
    inregs[MAXSYSPARMS+n].f = 0.0;
    tempdesc[n].stringaddr = NIL;
  }
  parmcount = 0; ip = 0;
#ifndef TARGET_RISCOS
  fp = MAXSYSPARMS+1;
#endif
  if (*basicvars.current == ',') basicvars.current++;
/* Now gather the parameters for the SWI call */
  while (!ateol[*basicvars.current] && *basicvars.current != BASIC_TOKEN_TO) {
    if (*basicvars.current != ',') {	/* Parameter position is not empty */
      expression();
      parmtype = GET_TOPITEM;
      switch (parmtype) {
      case STACK_INT: case STACK_UINT8: case STACK_INT64:
#ifdef TARGET_RISCOS
      case STACK_FLOAT:
#endif
        inregs[ip].i = pop_anynum64();
        ip++;
        break;
#ifndef TARGET_RISCOS
      case STACK_FLOAT:
        inregs[fp].f = pop_float();
        fp++;
        break;
#endif
      case STACK_STRING: case STACK_STRTEMP: {
        int32 length;
        char *cp;
        descriptor = pop_string();
/* Copy the string to a temporary location and append a NULL */
        length = descriptor.stringlen;
        tempdesc[parmcount].stringlen = length+1;
        tempdesc[parmcount].stringaddr = cp = alloc_string(length+1);
        if (length>0) memmove(cp, descriptor.stringaddr, length);
        cp[length] = asc_NUL;
        if (parmtype == STACK_STRTEMP) free_string(descriptor);
        inregs[ip].i = (size_t)cp;
        ip++;
        break;
      }
      default:
        error(ERR_VARNUMSTR);	/* Parameter must be an integer or string value */
      }
    }
    parmcount+=1;
    if (parmcount>=MAXSYSPARMS) error(ERR_SYSCOUNT);
    if (*basicvars.current == ',')
      basicvars.current++;	/* Point at start of next parameter */
    else if (!ateol[*basicvars.current] && *basicvars.current != BASIC_TOKEN_TO) {
      error(ERR_SYNTAX);
    }
  }
/* Make the SWI call */
  mos_sys(swino, inregs, outregs, &flags);
  for (n=0; n<MAXSYSPARMS; n++) {	/* Discard any temporary strings used */
    if (tempdesc[n].stringaddr != NIL) free_string(tempdesc[n]);
  }
  if (ateol[*basicvars.current]) return;	/* Not returning any parameters so just go home */
  basicvars.current++;
  parmcount = 0;
/* Copy SWI values returned to the return parameters */
  while (!ateol[*basicvars.current] && *basicvars.current != ';') {
    if (*basicvars.current != ',') {	/* Want this return value */
      get_lvalue(&destination);
      store_value(destination, outregs[parmcount], STRINGOK);
    }
    parmcount++;
    if (parmcount>=MAXSYSPARMS) error(ERR_SYSCOUNT);
    if (*basicvars.current == ',')	/* There is another parameter to follow - Move to its start */
      basicvars.current++;
    else if (!ateol[*basicvars.current] && *basicvars.current != ';') {
      error(ERR_SYNTAX);
    }
  }
  if (*basicvars.current == ';') {	/* Want flags as well */
    basicvars.current++;	/* Skip ';' token */
    get_lvalue(&destination);
    store_value(destination, flags, NOSTRING);
  }
  check_ateol();
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_sys\n");
#endif /* DEBUG */
}

/*
** 'exec_trace' handles the various flavours of trace command
*/
void exec_trace(void) {
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_trace\n");
#endif /* DEBUG */
  basicvars.current++;			/* Skip TRACE token */
  if (*basicvars.current == BASIC_TOKEN_ON) {		/* Line number trace */
    basicvars.traces.enabled = TRUE;
    basicvars.traces.lines = TRUE;
  }
  else if (*basicvars.current == BASIC_TOKEN_VDU) {
    if (*(basicvars.current + 1) == BASIC_TOKEN_OFF) {
      basicvars.current++;
      basicvars.traces.console = FALSE;
    } else {
      basicvars.traces.console = TRUE;
    }
  }
  else if (*basicvars.current == BASIC_TOKEN_OFF) {	/* Turn off any active traces */
    basicvars.traces.enabled = FALSE;
    basicvars.traces.lines = FALSE;
    basicvars.traces.procs = FALSE;
    basicvars.traces.pause = FALSE;
    basicvars.traces.branches = FALSE;
    basicvars.traces.console = FALSE;
  }
  else if (*basicvars.current == BASIC_TOKEN_TO) {	/* Got 'TRACE TO <file>' */
    stackitem stringtype;
    basicstring descriptor;
    basicvars.current++;
    expression();
    check_ateol();
    stringtype = GET_TOPITEM;
    if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) error(ERR_TYPESTR);
    descriptor = pop_string();
    basicvars.tracehandle = fileio_openout(descriptor.stringaddr, descriptor.stringlen);
    if (stringtype == STACK_STRTEMP) free_string(descriptor);
    return;	/* As this code calls 'check_ateol' */
  }
  else if (*basicvars.current == BASIC_TOKEN_CLOSE) {
    if (basicvars.tracehandle != 0) {
      fileio_close(basicvars.tracehandle);
      basicvars.tracehandle = 0;
    }
  }
  else if (ateol[*basicvars.current])		/* Got 'TRACE' on its own */
    error(ERR_BADTRACE);
  else {	/* TRACE <something> [ON|OFF] */
    boolean yes;
    byte option = *(basicvars.current+1);
    if (!ateol[option] && option != BASIC_TOKEN_ON && option != BASIC_TOKEN_OFF) error(ERR_BADTRACE);
    yes = option != BASIC_TOKEN_OFF;
    switch (*basicvars.current) {
    case BASIC_TOKEN_PROC: case BASIC_TOKEN_FN:	/* PROC call/return trace */
      basicvars.traces.procs = yes;
      break;
    case BASIC_TOKEN_GOTO:	/* Branch trace */
      basicvars.traces.branches = yes;
      break;
    case BASIC_TOKEN_STEP:	/* Execute one statement at a time */
      basicvars.traces.pause = yes;
      break;
    case BASIC_TOKEN_RETURN:	/* Stack backtrace */
      basicvars.traces.backtrace = yes;
      break;
    default:
      error(ERR_BADTRACE);
    }
    basicvars.traces.enabled = basicvars.traces.procs || basicvars.traces.branches;
    if (!ateol[option]) basicvars.current++;
  }
  basicvars.current++;		/* Skip TRACE option token */
  check_ateol();
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_trace\n");
#endif /* DEBUG */
}

/*
** 'exec_until' deals with the business end of a 'REPEAT' loop
*/
void exec_until(void) {
  byte *here;
  stack_repeat *rp;
  int64 result = 0;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_until\n");
#endif /* DEBUG */
  if (GET_TOPITEM == STACK_REPEAT)	/* REPEAT control block is top of stack */
    rp = basicvars.stacktop.repeatsp;
  else {	/* Discard stack entries as far as REPEAT control block */
    rp = get_repeat();
  }
  if (rp == NIL) error(ERR_NOTREPEAT);	/* Not in a REPEAT loop */
  if (kbd_escpoll()) error(ERR_ESCAPE);
  here = basicvars.current;	/* Note position of UNTIL for trace purposes */
  basicvars.current++;
  expression();
  result = pop_anynum64();
  if (result == BASFALSE) {	/* Condition still false - Continue with loop */
    if (basicvars.traces.branches) trace_branch(here, rp->repeataddr);
    basicvars.current = rp->repeataddr;
  }
  else {	/* Escape from loop - Remove REPEAT control block from stack */
    pop_repeat();
    if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_until\n");
#endif /* DEBUG */
}

/*
** 'exec_wait' deals with the basic statement 'WAIT'
*/
void exec_wait(void) {
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_wait\n");
#endif /* DEBUG */
  basicvars.current++;
  if (ateol[*basicvars.current])	/* Normal 'WAIT' statement */
    emulate_wait();
  else {	/* WAIT <time to wait> */
    int32 delay = eval_integer();
    check_ateol();
    mos_waitdelay(delay);
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_wait\n");
#endif /* DEBUG */
}

/*
** 'exec_xwhen' deals with the first reference to a 'WHEN' or an
** 'OTHERWISE' statement. In the context of the interpreter they are
** used to mark the end of the statement sequence of the preceding 'WHEN'
** clause. The function fills in the offset from the WHEN to the code
** following the CASE statement's ENDCASE.
*/
void exec_xwhen(void) {
  byte *lp, *lp2;
  int32 depth;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_xwhen\n");
#endif /* DEBUG */
  lp = basicvars.current+1+OFFSIZE;	/* Skip token and offset */
  while (*lp != asc_NUL) lp = skip_token(lp);
  lp++;		/* Point at the start of the line after the 'WHEN' or 'OTHERWISE' */
  depth = 1;
  do {
    if (AT_PROGEND(lp)) error(ERR_ENDCASE);	/* No ENDCASE found for this CASE */
    lp2 = FIND_EXEC(lp);
    if (*lp2 == BASIC_TOKEN_ENDCASE) {	/* Have reached the end of a CASE statement */
      depth--;
      if (depth == 0) break;
    }
    else {	/* Check for a nested CASE statement */
      while (*lp2 != asc_NUL && *lp2 != BASIC_TOKEN_XCASE && *lp2 != BASIC_TOKEN_CASE) lp2 = skip_token(lp2);
      if (*lp2 != asc_NUL) depth++;	/* Have found one of the CASE tokens - Got a nested 'CASE' */
    }
    lp+=GET_LINELEN(lp);
  } while (TRUE);
  lp2++;	/* Skip 'ENDCASE' token */
  if (*lp2 == ':') lp2++;
  if (*lp2 == asc_NUL) {	/* 'ENDCASE' is at end of line */
    lp2++;	/* Move to start of next line */
    lp2 = FIND_EXEC(lp2);
  }
  set_dest(basicvars.current+1, lp2);
  exec_elsewhen();	/* Now go and branch to the ENDCASE */
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_xwhen\n");
#endif /* DEBUG */
}

/*
** 'exec_while' is called when a 'WHILE' statement is found. It checks
** that the statement is valid and evaluates it for the first time.
** A 'while' data structure is created on the stack and the loop entered.
** Most of the work is done by the 'ENDWHILE': this evaluates the
** expression again and either branches back to the start of the loop
** if it is still 'TRUE' or drops through to the next statement.
** Note: if the WHILE loop is to be skipped because the expression
** evaluates to 'FALSE' the first time it is seen, the code will look
** for the first ENDWHILE at the appropriate depth of nesting and call
** that the terminating ENDWHILE for the loop. In most cases this will
** be fine, but it is possible to write legitimate code such as
** 'IF A% THEN ENDWHILE' where this function will pick the ENDWHILE
** here as the one it wants. Obviously this could lead to unexpected
** results. The behaviour of the code in fact matches the Acorn Basic
** interpreter in this respect.
**
** The offset to the first statement of the loop could be stored after
** the WHILE token. The code will then look like a IF statement with
** two embedded pointers
*/
void exec_while(void) {
  byte *expr, *here;
  int64 result = 0;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function mainstate.c:exec_while\n");
#endif /* DEBUG */
  here = basicvars.current;	/* Keep a pointer to the 'WHILE' token */
  basicvars.current+=OFFSIZE+1;	/* Skip 'WHILE' and 'ENDWHILE' branch offset */
  expr = basicvars.current;
  expression();
  result = pop_anynum64();
  if (result != BASFALSE) {	/* If result is not false, enter the loop */
    if (*basicvars.current == ':') basicvars.current++;	/* Loop body found on same line as WHILE statement */
    if (*basicvars.current == asc_NUL) {	/* Loop body starts on next line */
      basicvars.current++;
      if (basicvars.traces.lines) trace_line(get_lineno(basicvars.current));
      basicvars.current = FIND_EXEC(basicvars.current);	/* Move to first token on next line */
    }
    push_while(expr);
  }
  else {	/* Initial 'WHILE' expression value is 'FALSE', so skip loop altogether */
    if (*here == BASIC_TOKEN_WHILE) {	/* Branch destination has been filled in */
      here++;
      basicvars.current = GET_DEST(here);
      if (basicvars.traces.branches) trace_branch(here, basicvars.current);
    }
    else {	/* Have to look for the 'ENDWHILE'. 'basicvars.current' points at token after expression here */
      int32 depth = 1;
      while (depth>0) {
        if (*basicvars.current == asc_NUL) {	/* At the end of a line */
          basicvars.current++;
          if (AT_PROGEND(basicvars.current)) error(ERR_ENDWHILE);	/* No 'ENDWHILE' found */
          basicvars.current = FIND_EXEC(basicvars.current);
        }
        if (*basicvars.current == BASIC_TOKEN_ENDWHILE)
          depth--;
        else if (*basicvars.current == BASIC_TOKEN_WHILE || *basicvars.current == BASIC_TOKEN_XWHILE) {	/* Found a nested loop */
          depth++;
        }
        if (depth>0) basicvars.current=skip_token(basicvars.current);
      }
      basicvars.current++;	/* Skip the ENDWHILE token */
      if (*basicvars.current == ':') basicvars.current++;	/* Skip a ':' after the ENDWHILE */
      if (*basicvars.current == asc_NUL) {	/* There is nothing else on the line - Skip to next line */
        basicvars.current++;
	if (basicvars.traces.lines) trace_line(get_lineno(basicvars.current));
        basicvars.current = FIND_EXEC(basicvars.current);
      }
      set_dest(here+1, basicvars.current);	/* Save the address for latet */
      *here = BASIC_TOKEN_WHILE;
      if (basicvars.traces.branches) trace_branch(here, basicvars.current);
    }
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function mainstate.c:exec_while\n");
#endif /* DEBUG */
}

