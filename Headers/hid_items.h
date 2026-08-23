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

#ifndef BRLTTY_INCLUDED_HID_ITEMS
#define BRLTTY_INCLUDED_HID_ITEMS

#include "hid_types.h"
#include "hid_defs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef union {
  HidUnsignedValue u;
  HidSignedValue s;
} HidItemValue;

typedef struct {
  HidItemValue value;
  uint8_t tag;
  uint8_t valueSize;
} HidItem;

extern int hidNextItem (
  HidItem *item,
  const unsigned char **bytes,
  size_t *count
);

extern unsigned char hidItemValueSize (unsigned char item);

extern int hidReportSize (
  const HidItemsDescriptor *items,
  HidReportIdentifier identifier,
  HidReportSize *size
);

typedef struct {
  HidCollectionType type;
  HidUnsignedValue usagePage;
  HidUnsignedValue usage;
} HidCollection;

/* Describe one data element of one report, i.e. one slot of one main item.
 * hidWalkReportElements() reports these. A driver can thus locate a field by
 * its usage rather than by a hardcoded report layout.
 */
typedef struct {
  HidReportIdentifier reportIdentifier;
  HidItemTag reportType; // HID_ITM_Input, HID_ITM_Output, or HID_ITM_Feature

  HidUnsignedValue usagePage;
  HidUnsignedValue usage; // 0 if the main item declared no usage (padding)

  /* Use this range for an array item (see HID_USG_FLG_VARIABLE within flags).
   * An array item holds a selector index rather than a state.
   */
  HidUnsignedValue usageMinimum;
  HidUnsignedValue usageMaximum;

  uint32_t bitOffset; // within the report's data, excluding the identifier byte
  uint32_t bitSize;

  HidSignedValue logicalMinimum;
  HidSignedValue logicalMaximum;
  HidUnsignedValue flags; // HID_USG_FLG_*

  unsigned int elementIndex; // within its main item
  unsigned int elementCount; // the report count of the main item

  /* List the enclosing collections, outermost first. Don't use them beyond
   * the duration of the call.
   */
  unsigned int collectionDepth;
  const HidCollection *collections;
} HidReportElement;

typedef int HidReportElementHandler (const HidReportElement *element, void *data);

extern int hidWalkReportElements (const HidItemsDescriptor *items, HidReportElementHandler *handleElement, void *data);

/* Get or set a field of a report. HID packs the fields of a report
 * consecutively, least significant bit first. Reject a size of more than 32
 * bits. Such a value can't be represented.
 */
extern HidUnsignedValue hidGetReportBits (const unsigned char *data, uint32_t offset, uint32_t size);
extern void hidSetReportBits (unsigned char *data, uint32_t offset, uint32_t size, HidUnsignedValue value);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* BRLTTY_INCLUDED_HID_ITEMS */
