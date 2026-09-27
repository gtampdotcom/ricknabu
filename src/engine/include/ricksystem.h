/*
 * engine/include/ricksystem.h -- ported from xrick/include/ricksystem.h
 *
 * Genuinely portable: just base typedefs, TRUE/FALSE, and the sys_* function
 * declarations. No TI-specific content found here. Left almost unchanged;
 * removed the MSVC #pragma warning block (meaningless outside MSVC, was
 * already conditional on _MSC_VER so harmless to drop rather than carry).
 *
 * sys_init's original TI signature is (int, char**) mirroring a
 * classic argc/argv-style entry, even though the TI obviously has no argv
 * either -- NABU homebrew has no command line at all, so sys_init_nabu()
 * (see hal/) takes no arguments; this declaration is kept for source
 * compatibility with any engine file that still calls sys_init(argc, argv)
 * verbatim, which can just pass sys_init(0, NULL).
 */

#ifndef _SYSTEM_H
#define _SYSTEM_H

#include "config.h"

#include <stddef.h> /* NULL */

#ifdef __GNUC__
#define UNUSED(x) x __attribute((unused))
#else
#define UNUSED(x) x
#endif

/* there are true at least on x86 platforms, not sure how well 32 bits works on the TI port */
typedef unsigned char U8;         /*  8 bits unsigned */
typedef unsigned short int U16;   /* 16 bits unsigned */
typedef unsigned long U32;        /* 32 bits unsigned */
typedef signed char S8;           /*  8 bits signed   */
typedef signed short int S16;     /* 16 bits signed   */
typedef signed long S32;          /* 32 bits signed   */

#define TRUE 1
#define FALSE 0

extern void sys_init(int, char **);
extern void sys_shutdown(void);
extern void sys_panic(char *, ...);
extern void sys_printf(char *, ...);
extern void sys_sleep(U16);

void sys_resettime(void);
extern U32 sys_gettime(void);

#endif

/* eof */
