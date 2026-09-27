/*
 * BRLTTY - A background process providing access to the console screen (when in
 *          text mode) for a blind person using a refreshable braille display.
 *
 * Copyright (C) 1995-2026 by The BRLTTY Developers.
 *
 * BRLTTY comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU Lesser General Public License, as published by the Free Software
 * Foundation; either version 2.1 of the License, or (at your option) any
 * later version. Please see the file LICENSE-LGPL for details.
 *
 * Web Page: http://brltty.app/
 *
 * This software is maintained by Dave Mielke <dave@mielke.cc>.
 */

#ifndef BRLTTY_INCLUDED_HB_BRLDEFS
#define BRLTTY_INCLUDED_HB_BRLDEFS

/* Number a key by the offset of its usage from the first button usage of the
 * braille display page. The number then doesn't depend on the declaration
 * order in the report descriptor. One key table therefore serves every
 * conforming display.
 */
typedef enum {
  HB_KEY_Dot1           =  0, // 0X201
  HB_KEY_Dot2           =  1, // 0X202
  HB_KEY_Dot3           =  2, // 0X203
  HB_KEY_Dot4           =  3, // 0X204
  HB_KEY_Dot5           =  4, // 0X205
  HB_KEY_Dot6           =  5, // 0X206
  HB_KEY_Dot7           =  6, // 0X207
  HB_KEY_Dot8           =  7, // 0X208
  HB_KEY_Space          =  8, // 0X209
  HB_KEY_LeftSpace      =  9, // 0X20A
  HB_KEY_RightSpace     = 10, // 0X20B

  // Skip 0X20C through 0X20F, which name control groups, not controls.

  HB_KEY_JoystickCenter = 15, // 0X210
  HB_KEY_JoystickUp     = 16, // 0X211
  HB_KEY_JoystickDown   = 17, // 0X212
  HB_KEY_JoystickLeft   = 18, // 0X213
  HB_KEY_JoystickRight  = 19, // 0X214
  HB_KEY_DPadCenter     = 20, // 0X215
  HB_KEY_DPadUp         = 21, // 0X216
  HB_KEY_DPadDown       = 22, // 0X217
  HB_KEY_DPadLeft       = 23, // 0X218
  HB_KEY_DPadRight      = 24, // 0X219
  HB_KEY_PanLeft        = 25, // 0X21A
  HB_KEY_PanRight       = 26, // 0X21B
  HB_KEY_RockerUp       = 27, // 0X21C
  HB_KEY_RockerDown     = 28, // 0X21D
  HB_KEY_RockerPress    = 29, // 0X21E
} HB_NavigationKey;

typedef enum {
  HB_GRP_NavigationKeys = 0,
  HB_GRP_RoutingKeys,

  /* Offer a value of the screen reader control usage (0X06) as a key. Give it
   * no meaning here. The specification doesn't define the values of this named
   * array.
   */
  HB_GRP_ScreenReaderControls
} HB_KeyGroup;

#endif /* BRLTTY_INCLUDED_HB_BRLDEFS */
