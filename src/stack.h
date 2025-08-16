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
**      This file contains the definitions of functions concerned with
**      manipulating the Basic stack
*/

#ifndef __stack_h
#define __stack_h

#include "common.h"
#include "basicdefs.h"

extern void *alloc_stackmem(size_t);
extern void *alloc_stackstrmem(int32);
extern void free_stackmem(void);
extern void push_lvalue(int32, pointers);
extern void push_int(int32);
extern void push_int64(int64);
extern void push_uint8(uint8);
extern void push_float(float64);
extern void push_string(basicstring);
extern void push_strtemp(int32, char *);
extern void push_dolstring(int32, char *);
extern void push_array(basicarray *, int32);
extern void push_arraytemp(basicarray *, int32);
extern void push_pointer(void *);
extern fnprocinfo *push_proc(char *, int32);
extern void push_fn(char *, int32);
extern void push_gosub(void);
extern void push_while(byte *);
extern void push_repeat(void);
extern void push_intfor(lvalue, byte *, int32, int32, boolean);
extern void push_int64for(lvalue, byte *, int64, int64, boolean);
extern void push_floatfor(lvalue, byte *, float64, float64, boolean);
extern void push_data(byte *);
extern void push_error(errorblock);
extern void push_varyint(int64);
extern size_t *make_opstack(void);
extern sigjmp_buf *make_restart(void);
extern boolean safestack(void);
extern lvalue pop_lvalue(void);
extern int64 pop_anyint(void);
extern uint8 pop_uint8(void);
extern int32 pop_int(void);
extern int64 pop_int64(void);
extern int32 pop_anynum32(void);
extern int64 pop_anynum64(void);
extern float64 pop_anynumfp(void);
extern float80 pop_anynumld(void);
extern float64 pop_float(void);
extern basicstring pop_string(void);
extern basicarray *pop_array(void);
extern basicarray pop_arraytemp(void);
extern fnprocinfo pop_proc(void);
extern fnprocinfo pop_fn(void);
extern gosubinfo pop_gosub(void);
extern stack_while *get_while(void);
extern stack_repeat *get_repeat(void);
extern stack_for *get_for(void);
extern void pop_while(void);
extern void pop_repeat(void);
extern void pop_for(void);
extern byte *pop_data(void);
extern errorblock pop_error(void);
extern void save_int(lvalue, int32);
extern void save_uint8(lvalue, uint8);
extern void save_int64(lvalue, int64);
extern void save_float(lvalue, float64);
extern void save_string(lvalue, basicstring);
extern void save_array(lvalue);
extern void save_retint(lvalue, lvalue, int32);
extern void save_retuint8(lvalue, lvalue, uint8);
extern void save_retint64(lvalue, lvalue, int64);
extern void save_retfloat(lvalue, lvalue, float64);
extern void save_retstring(lvalue, lvalue, basicstring);
extern void restore_parameters(int32);
extern void empty_stack(stackitem);
extern void empty_stack_to_fn_or_proc(void);
extern stackitem stack_unwindlocal(void);
extern void reset_stack(byte *);
extern void init_stack(void);
extern void clear_stack(void);
extern void *alloc_local(int32);
#ifdef DEBUG
extern void debug_show_stackitemtype(int32);
#endif

/* LARGEST_ENTRY is the size of the largest string or numeric entry on the stack */

#define LARGEST_ENTRY sizeof(basicstring)
#define ALIGNSIZE(type) (ALIGN(sizeof(type)))

/*
** 'IS_SAFESTACK' returns TRUE if it is safe to move the Basic stack.
** At the moment this is only allowed if the stack is empty, that is,
** the only thing on it is the operator stack and the program is not
** in a procedure or function
*/
#define IS_SAFESTACK (basicvars.procstack==NIL && basicvars.stacktop.intsp->itemtype==STACK_OPSTACK)

/* The following macros are used to speed up operations on the BASIC stack */

#define is8or32int(x) ((x == STACK_INT) || (x == STACK_UINT8))

#define GET_TOPITEM (basicvars.stacktop.intsp->itemtype)
#define TOPITEMISINT ((basicvars.stacktop.intsp->itemtype == STACK_INT) || (basicvars.stacktop.intsp->itemtype == STACK_UINT8) || (basicvars.stacktop.intsp->itemtype == STACK_INT64))

#define IS_NUMERIC(x) ((x == STACK_INT) || (x == STACK_UINT8) || (x == STACK_INT64) || (x == STACK_FLOAT))

#define TOPITEMISNUM ((basicvars.stacktop.intsp->itemtype == STACK_INT) || (basicvars.stacktop.intsp->itemtype == STACK_UINT8) || (basicvars.stacktop.intsp->itemtype == STACK_INT64) || (basicvars.stacktop.intsp->itemtype == STACK_FLOAT))

#define TOPITEMISNUMARRAY ((basicvars.stacktop.intsp->itemtype == STACK_INTARRAY) || (basicvars.stacktop.intsp->itemtype == STACK_UINT8ARRAY) || (basicvars.stacktop.intsp->itemtype == STACK_INT64ARRAY) || (basicvars.stacktop.intsp->itemtype == STACK_FLOATARRAY))

#define TOPITEMISNUMARRTEMP ((basicvars.stacktop.intsp->itemtype == STACK_IATEMP) || (basicvars.stacktop.intsp->itemtype == STACK_U8ATEMP) || (basicvars.stacktop.intsp->itemtype == STACK_I64ATEMP) || (basicvars.stacktop.intsp->itemtype == STACK_FATEMP))

#define TOPITEMISFOR ((basicvars.stacktop.intsp->itemtype == STACK_INTFOR) || (basicvars.stacktop.intsp->itemtype == STACK_INT64FOR) || (basicvars.stacktop.intsp->itemtype == STACK_FLOATFOR))

#define INCR_INT(x) basicvars.stacktop.intsp->intvalue+=(x)
#define INCR_FLOAT(x) basicvars.stacktop.floatsp->floatvalue+=(x)
#define DECR_INT(x) basicvars.stacktop.intsp->intvalue-=(x)
#define DECR_FLOAT(x) basicvars.stacktop.floatsp->floatvalue-=(x)
#define INTDIV_INT(x) basicvars.stacktop.intsp->intvalue/=(x)
#define INTDIV_UINT8(x) basicvars.stacktop.uint8sp->uint8value/=(x)
#define INTDIV_INT64(x) basicvars.stacktop.int64sp->int64value/=(x)
#define DIV_FLOAT(x) basicvars.stacktop.floatsp->floatvalue/=(x)
#define INTMOD_INT(x) basicvars.stacktop.intsp->intvalue%=(x)
#define INTMOD_UINT8(x) basicvars.stacktop.uint8sp->uint8value%=(x)
#define INTMOD_INT64(x) basicvars.stacktop.int64sp->int64value%=(x)
#define AND_INT(x) basicvars.stacktop.intsp->intvalue&=(x)
#define AND_UINT8(x) basicvars.stacktop.uint8sp->uint8value&=(x)
#define AND_INT64(x) basicvars.stacktop.int64sp->int64value&=(x)
#define OR_INT(x) basicvars.stacktop.intsp->intvalue|=(x)
#define OR_UINT8(x) basicvars.stacktop.uint8sp->uint8value|=(x)
#define OR_INT64(x) basicvars.stacktop.int64sp->int64value|=(x)
#define EOR_INT(x) basicvars.stacktop.intsp->intvalue^=(x)
#define EOR_UINT8(x) basicvars.stacktop.uint8sp->uint8value^=(x)
#define EOR_INT64(x) basicvars.stacktop.int64sp->int64value^=(x)
#define NEGATE_INT basicvars.stacktop.intsp->intvalue = -basicvars.stacktop.intsp->intvalue
#define NEGATE_INT64 basicvars.stacktop.int64sp->int64value = -basicvars.stacktop.int64sp->int64value
#define NEGATE_FLOAT basicvars.stacktop.floatsp->floatvalue = -basicvars.stacktop.floatsp->floatvalue
#define NOT_INT basicvars.stacktop.intsp->intvalue = ~basicvars.stacktop.intsp->intvalue
#define NOT_UINT8 basicvars.stacktop.uint8sp->uint8value = ~basicvars.stacktop.uint8sp->uint8value
#define NOT_INT64 basicvars.stacktop.int64sp->int64value = ~basicvars.stacktop.int64sp->int64value
#define ABS_INT basicvars.stacktop.intsp->intvalue = abs(basicvars.stacktop.intsp->intvalue)
#define ABS_INT64 basicvars.stacktop.int64sp->int64value = llabs(basicvars.stacktop.int64sp->int64value)
#define ABS_FLOAT basicvars.stacktop.floatsp->floatvalue = fabs(basicvars.stacktop.floatsp->floatvalue)
#define CPEQ_INT(x) basicvars.stacktop.intsp->intvalue = \
                   (basicvars.stacktop.intsp->intvalue==(x) ? BASTRUE : BASFALSE)
#define CPEQ_UINT8(x) basicvars.stacktop.uint8sp->uint8value = \
                     (basicvars.stacktop.uint8sp->uint8value==(x) ? BASTRUE : BASFALSE)
#define CPEQ_INT64(x) basicvars.stacktop.int64sp->int64value = \
                     (basicvars.stacktop.int64sp->int64value==(x) ? BASTRUE : BASFALSE)
#define CPNE_INT(x) basicvars.stacktop.intsp->intvalue = \
                   (basicvars.stacktop.intsp->intvalue!=(x) ? BASTRUE : BASFALSE)
#define CPNE_UINT8(x) basicvars.stacktop.uint8sp->uint8value = \
                     (basicvars.stacktop.uint8sp->uint8value!=(x) ? BASTRUE : BASFALSE)
#define CPNE_INT64(x) basicvars.stacktop.int64sp->int64value = \
                     (basicvars.stacktop.int64sp->int64value!=(x) ? BASTRUE : BASFALSE)
#define CPGT_INT(x) basicvars.stacktop.intsp->intvalue = \
                   (basicvars.stacktop.intsp->intvalue>(x) ? BASTRUE : BASFALSE)
#define CPGT_UINT8(x) basicvars.stacktop.uint8sp->uint8value = \
                     (basicvars.stacktop.uint8sp->uint8value>(x) ? BASTRUE : BASFALSE)
#define CPGT_INT64(x) basicvars.stacktop.int64sp->int64value = \
                     (basicvars.stacktop.int64sp->int64value>(x) ? BASTRUE : BASFALSE)
#define CPLT_INT(x) basicvars.stacktop.intsp->intvalue = \
                   (basicvars.stacktop.intsp->intvalue<(x) ? BASTRUE : BASFALSE)
#define CPLT_UINT8(x) basicvars.stacktop.uint8sp->uint8value = \
                     (basicvars.stacktop.uint8sp->uint8value<(x) ? BASTRUE : BASFALSE)
#define CPLT_INT64(x) basicvars.stacktop.int64sp->int64value = \
                     (basicvars.stacktop.int64sp->int64value<(x) ? BASTRUE : BASFALSE)
#define CPGE_INT(x) basicvars.stacktop.intsp->intvalue = \
                   (basicvars.stacktop.intsp->intvalue>=(x) ? BASTRUE : BASFALSE)
#define CPGE_UINT8(x) basicvars.stacktop.uint8sp->uint8value = \
                     (basicvars.stacktop.uint8sp->uint8value>=(x) ? BASTRUE : BASFALSE)
#define CPGE_INT64(x) basicvars.stacktop.int64sp->int64value = \
                     (basicvars.stacktop.int64sp->int64value>=(x) ? BASTRUE : BASFALSE)
#define CPLE_INT(x) basicvars.stacktop.intsp->intvalue = \
                   (basicvars.stacktop.intsp->intvalue<=(x) ? BASTRUE : BASFALSE)
#define CPLE_UINT8(x) basicvars.stacktop.uint8sp->uint8value = \
                     (basicvars.stacktop.uint8sp->uint8value<=(x) ? BASTRUE : BASFALSE)
#define CPLE_INT64(x) basicvars.stacktop.int64sp->int64value = \
                     (basicvars.stacktop.int64sp->int64value<=(x) ? BASTRUE : BASFALSE)

#endif

