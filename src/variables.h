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
*/

#ifndef __variables_h
#define __variables_h

#include "common.h"
#include "basicdefs.h"

extern void clear_varlists(void);
extern void list_variables(char);
extern void list_libraries();
extern void detail_library(library *);
extern variable *find_variable(byte *, int);
extern variable *find_fnproc(byte *, int);
extern variable *create_variable(byte *, int32, library *);
extern void define_array(variable *, boolean, boolean);
extern void init_staticvars(void);
extern void clear_offheaparrays(void);
extern void exec_clear_himem(void);

extern char *nullstring;

#endif
