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

#ifndef BRLTTY_INCLUDED_HID_BRAILLE
#define BRLTTY_INCLUDED_HID_BRAILLE

#include "hid_items.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Support braille displays which conform to the HID braille display usage
 * page (0X41). Operate only on a report descriptor and on report buffers.
 * Perform no input or output.
 */

/* Identify a button by the offset of its usage from the first button usage.
 * The button usages are contiguous. Key numbers, and thus key tables, then
 * don't depend on the declaration order in the report descriptor.
 */
#define HID_BRL_BUTTON_FIRST HID_USG_BRL_KeyboardDot1
#define HID_BRL_BUTTON_LAST HID_USG_BRL_RockerPress
#define HID_BRL_BUTTON_COUNT (HID_BRL_BUTTON_LAST - HID_BRL_BUTTON_FIRST + 1)

#define HID_BRL_ROUTER_SET_COUNT 3
#define HID_BRL_CELL_LIMIT 0XFF
#define HID_BRL_ROUTER_BYTES ((HID_BRL_CELL_LIMIT + 7) / 8)

static inline unsigned int
hidBrailleButtonNumber (HidUnsignedValue usage) {
  return usage - HID_BRL_BUTTON_FIRST;
}

static inline HidUnsignedValue
hidBrailleButtonUsage (unsigned int number) {
  return number + HID_BRL_BUTTON_FIRST;
}

typedef struct {
  HidReportIdentifier identifier;

  /* Count the leading identifier byte even when the report descriptor doesn't
   * use report identifiers. gioWriteHidReport() expects it. Contrast this with
   * inputSizes below.
   */
  size_t reportSize;

  uint32_t bitOffset;
  uint32_t bitSize; // of one cell
  unsigned int cellCount;
  unsigned char dots; // 8 or 6
} HidBrailleCells;

/* Require the keys of a router set to be declared contiguously within one
 * report. They can then be located by an offset and a count. No known device
 * does otherwise.
 */
typedef struct {
  HidUnsignedValue usage; // HID_USG_BRL_RouterSet1 through RouterSet3
  HidReportIdentifier identifier;
  uint32_t bitOffset;
  uint32_t bitSize; // of one key
  unsigned int keyCount;
} HidBrailleRouterSet;

typedef struct {
  HidUnsignedValue usage;
  HidReportIdentifier identifier;
  uint32_t bitOffset;
} HidBrailleButton;

typedef struct {
  unsigned char found;
  HidReportIdentifier identifier;
  HidItemTag reportType;
  uint32_t bitOffset;
  uint32_t bitSize;
} HidBrailleValue;

typedef struct {
  HidBrailleCells cells;

  HidBrailleRouterSet routerSets[HID_BRL_ROUTER_SET_COUNT];
  unsigned int routerSetCount;

  HidBrailleValue cellCount; // HID_USG_BRL_CellCount
  HidBrailleValue screenReaderControl; // HID_USG_BRL_ScreenReaderControl
  HidBrailleValue screenReaderIdentifier; // HID_USG_BRL_ScreenReaderIdentifier

  /* Hold the number of bytes which the device sends, indexed by report
   * identifier. Include a leading identifier byte only if identifiersUsed is
   * set.
   */
  uint16_t inputSizes[UINT8_MAX + 1];
  unsigned char identifiersUsed:1;

  unsigned int buttonCount;
  HidBrailleButton buttons[];
} HidBrailleDescription;

/* Test whether this report descriptor declares a braille display application
 * collection. Use it to claim a device without knowing its vendor or product.
 */
extern int hidBrailleTestItems (const HidItemsDescriptor *items);

/* Return a description as a single allocation owned by the caller. Return
 * NULL if the descriptor doesn't describe a usable braille display. Log the
 * reason in that case.
 */
extern HidBrailleDescription *hidBrailleNewDescription (const HidItemsDescriptor *items);
extern void hidBrailleDestroyDescription (HidBrailleDescription *description);

typedef int HidBrailleDescriptionLister (const char *line, void *data);

extern int hidBrailleListDescription (
  const HidBrailleDescription *description,
  HidBrailleDescriptionLister *listLine, void *data
);

extern void hidBrailleLogDescription (const HidBrailleDescription *description);
extern const char *hidBrailleGetUsageName (HidUnsignedValue usage);

/* Pass the cells as dot patterns in ISO 11548-1 order. Pass fewer than the
 * cell count of the display to leave the remaining cells blank. Pass the size
 * of the whole report. The report is zeroed first, and its identifier byte is
 * set.
 */
extern int hidBrailleMakeCellsReport (
  const HidBrailleDescription *description,
  const unsigned char *cells, unsigned int count,
  unsigned char *report, size_t size
);

typedef struct {
  uint32_t buttons; // a bit per button number

  // A key per bit, least significant first, as enqueueUpdatedKeyGroup() wants.
  unsigned char routerKeys[HID_BRL_ROUTER_BYTES];

  unsigned int cellCount;
  HidUnsignedValue screenReaderControl;

  unsigned char haveCellCount:1;
  unsigned char haveScreenReaderControl:1;
} HidBrailleInput;

/* Decode those fields of one input report which this report descriptor places
 * within it. Leave the other fields alone. A sequence of reports can thus
 * update the same input.
 */
extern int hidBrailleDecodeInput (
  const HidBrailleDescription *description,
  const unsigned char *report, size_t size,
  HidBrailleInput *input
);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* BRLTTY_INCLUDED_HID_BRAILLE */
