/*
 * BRLTTY - A background process providing access to the Linux console (when in
 *          text mode) for a blind person using a refreshable braille display.
 *
 * Copyright (C) 1995-2005 by The BRLTTY Team. All rights reserved.
 *
 * BRLTTY comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU General Public License, as published by the Free Software
 * Foundation.  Please see the file COPYING for details.
 *
 * Web Page: http://mielke.cc/brltty/
 *
 * This software is maintained by Dave Mielke <dave@mielke.cc>.
 */

#ifndef BRLTTY_INCLUDED_SCR_FROZEN
#define BRLTTY_INCLUDED_SCR_FROZEN

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "scr_base.h"

typedef struct {
  BaseScreen base;
  int (*open) (BaseScreen *);		/* called every time the screen is frozen */
  void (*close) (void);		/* called to discard frozen screen image */
} FrozenScreen;

extern void initializeFrozenScreen (FrozenScreen *);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* BRLTTY_INCLUDED_SCR_FROZEN */
