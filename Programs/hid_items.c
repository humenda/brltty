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

#include "prologue.h"

#include <string.h>

#include "log.h"
#include "strfmt.h"
#include "hid_items.h"
#include "hid_defs.h"
#include "hid_tables.h"

unsigned char
hidItemValueSize (unsigned char item) {
  static const unsigned char sizes[4] = {0, 1, 2, 4};
  return sizes[HID_ITEM_SIZE(item)];
}

int
hidNextItem (
  HidItem *item,
  const unsigned char **bytes,
  size_t *count
) {
  if (!*count) return 0;

  const unsigned char *byte = *bytes;
  const unsigned char *endBytes = byte + *count;

  unsigned char itemTag = HID_ITEM_TAG(*byte);
  unsigned char valueSize = hidItemValueSize(*byte);

  const unsigned char *endValue = ++byte + valueSize;
  if (endValue > endBytes) return 0;

  item->tag = itemTag;
  item->valueSize = valueSize;
  item->value.u = 0;

  {
    unsigned char shift = 0;

    while (byte < endValue) {
      item->value.u |= *byte++ << shift;
      shift += 8;
    }

    if (hidHasSignedValue(item->tag)) {
      shift = 0X20 - shift;
      item->value.u <<= shift;
      item->value.s >>= shift;
    }
  }

  *bytes = byte;
  *count = endBytes - byte;
  return 1;
}

int
hidReportSize (
  const HidItemsDescriptor *items,
  HidReportIdentifier identifier,
  HidReportSize *size
) {
  const unsigned char *nextByte = items->bytes;
  size_t bytesLeft = items->count;

  int noIdentifier = !identifier;
  int reportFound = noIdentifier;

  size_t inputSize = 0;
  size_t outputSize = 0;
  size_t featureSize = 0;

  uint64_t itemTagsEncountered = 0;
  HidUnsignedValue reportIdentifier = 0;
  HidUnsignedValue reportSize = 0;
  HidUnsignedValue reportCount = 0;

  while (bytesLeft) {
    size_t offset = nextByte - items->bytes;
    HidItem item;

    if (!hidNextItem(&item, &nextByte, &bytesLeft)) {
      if (bytesLeft) return 0;
      break;
    }

    if (item.tag == HID_ITM_ReportID) {
      if (noIdentifier) {
        reportFound = 0;
        break;
      }

      reportIdentifier = item.value.u;
      if (reportIdentifier == identifier) reportFound = 1;
    } else {
      switch (item.tag) {
      {
        size_t *size;

        case HID_ITM_Input:
          size = &inputSize;
          goto doSize;

        case HID_ITM_Output:
          size = &outputSize;
          goto doSize;

        case HID_ITM_Feature:
          size = &featureSize;
          goto doSize;

        doSize:
          if (reportIdentifier == identifier) *size += reportSize * reportCount;
          break;
      }

        case HID_ITM_ReportCount:
          reportCount = item.value.u;
          break;

        case HID_ITM_ReportSize:
          reportSize = item.value.u;
          break;

        case HID_ITM_Collection:
        case HID_ITM_EndCollection:
        case HID_ITM_UsagePage:
        case HID_ITM_UsageMinimum:
        case HID_ITM_UsageMaximum:
        case HID_ITM_Usage:
        case HID_ITM_LogicalMinimum:
        case HID_ITM_LogicalMaximum:
        case HID_ITM_PhysicalMinimum:
        case HID_ITM_PhysicalMaximum:
          break;

        default: {
          if (!(itemTagsEncountered & HID_ITEM_TAG_BIT(item.tag))) {
            logMessage(LOG_CATEGORY(HID_IO),
              "unhandled item tag at offset %"PRIsize ": 0X%02X",
              offset, item.tag
            );
          }

          break;
        }
      }
    }

    itemTagsEncountered |= HID_ITEM_TAG_BIT(item.tag);
  }

  if (reportFound) {
    char log[0X100];
    STR_BEGIN(log, sizeof(log));
    STR_PRINTF("report size: %02X", identifier);

    {
      typedef struct {
        const char *label;
        size_t *bytes;
        size_t bits;
      } SizeEntry;

      SizeEntry sizeTable[] = {
        { .label = "In",
          .bits = inputSize,
          .bytes = &size->input
        },

        { .label = "Out",
          .bits = outputSize,
          .bytes = &size->output
        },

        { .label = "Ftr",
          .bits = featureSize,
          .bytes = &size->feature
        },
      };

      SizeEntry *entry = sizeTable;
      const SizeEntry *end = entry + ARRAY_COUNT(sizeTable);

      while (entry < end) {
        size_t bytes = (entry->bits + 7) / 8;
        if (bytes && !noIdentifier) bytes += 1;
        *entry->bytes = bytes;

        STR_PRINTF(" %s:%" PRIsize, entry->label, bytes);
        entry += 1;
      }
    }

    STR_END;
    logMessage(LOG_CATEGORY(HID_IO), "%s", log);
  }

  return reportFound;
}

#define HID_VALUE_BITS_LIMIT 0X20

HidUnsignedValue
hidGetReportBits (const unsigned char *data, uint32_t offset, uint32_t size) {
  if (size > HID_VALUE_BITS_LIMIT) {
    logMessage(LOG_WARNING, "HID field too wide: %"PRIu32" bits", size);
    return 0;
  }

  HidUnsignedValue value = 0;

  for (uint32_t index=0; index<size; index+=1) {
    uint32_t bit = offset + index;
    if (data[bit / 8] & (1 << (bit % 8))) value |= UINT32_C(1) << index;
  }

  return value;
}

void
hidSetReportBits (unsigned char *data, uint32_t offset, uint32_t size, HidUnsignedValue value) {
  if (size > HID_VALUE_BITS_LIMIT) {
    logMessage(LOG_WARNING, "HID field too wide: %"PRIu32" bits", size);
    return;
  }

  for (uint32_t index=0; index<size; index+=1) {
    uint32_t bit = offset + index;
    unsigned char mask = 1 << (bit % 8);

    if (value & (UINT32_C(1) << index)) {
      data[bit / 8] |= mask;
    } else {
      data[bit / 8] &= ~mask;
    }
  }
}

#define HID_USAGE_LIMIT 0X200
#define HID_COLLECTION_LIMIT 0X10
#define HID_PUSH_LIMIT 0X10
#define HID_ELEMENT_LIMIT 0X400
#define HID_REPORT_BITS_LIMIT 0X10000

/* Hold the items which Push saves and Pop restores. */
typedef struct {
  HidUnsignedValue usagePage;
  HidUnsignedValue reportIdentifier;
  HidUnsignedValue reportSize;
  HidUnsignedValue reportCount;
  HidSignedValue logicalMinimum;
  HidSignedValue logicalMaximum;
  HidSignedValue physicalMinimum;
  HidSignedValue physicalMaximum;
  HidSignedValue unitExponent;
  HidUnsignedValue unit;
} HidGlobalItems;

/* Hold the items which a main item consumes and then discards. Store usages as
 * extended usages, i.e. with the usage page in the high sixteen bits.
 */
typedef struct {
  HidUnsignedValue usages[HID_USAGE_LIMIT];
  unsigned int usageCount;

  HidUnsignedValue usageMinimum;
  HidUnsignedValue usageMaximum;
  unsigned char haveUsageMinimum:1;
  unsigned char haveUsageMaximum:1;

  /* Honor only the first usage of a delimited set. Its usages are
   * alternatives for the same control.
   */
  unsigned char withinDelimiter:1;
  unsigned char delimiterHonored:1;
} HidLocalItems;

typedef enum {
  HID_RPT_INPUT,
  HID_RPT_OUTPUT,
  HID_RPT_FEATURE,
  HID_RPT_COUNT
} HidWalkReportType;

typedef struct {
  HidGlobalItems global;
  HidGlobalItems pushed[HID_PUSH_LIMIT];
  unsigned int pushDepth;

  HidLocalItems local;

  HidCollection collections[HID_COLLECTION_LIMIT];
  unsigned int collectionDepth;

  /* Keep a bit cursor per report type and report identifier. A field is
   * positioned relative to the start of its own report.
   */
  uint32_t bitOffsets[HID_RPT_COUNT][UINT8_MAX + 1];

  uint64_t itemTagsEncountered;
  unsigned int usagesDropped;

  HidReportElementHandler *handleElement;
  void *handlerData;
} HidWalk;

static void
hidClearLocalItems (HidWalk *walk) {
  memset(&walk->local, 0, sizeof(walk->local));
}

static HidUnsignedValue
hidExtendedUsage (const HidWalk *walk, const HidItem *item) {
  if (item->valueSize == 4) return item->value.u;
  return (walk->global.usagePage << 0X10) | (item->value.u & UINT16_MAX);
}

static int
hidAddLocalUsage (HidWalk *walk, const HidItem *item) {
  if (walk->local.usageCount == HID_USAGE_LIMIT) {
    walk->usagesDropped += 1;
    return 1;
  }

  walk->local.usages[walk->local.usageCount++] = hidExtendedUsage(walk, item);
  return 1;
}

static int
hidReportElements (HidWalk *walk, const HidItem *item, size_t offset) {
  HidWalkReportType type;

  switch (item->tag) {
    case HID_ITM_Input:   type = HID_RPT_INPUT;   break;
    case HID_ITM_Output:  type = HID_RPT_OUTPUT;  break;
    default:              type = HID_RPT_FEATURE; break;
  }

  unsigned int count = walk->global.reportCount;
  uint32_t bitSize = walk->global.reportSize;

  if (count > HID_ELEMENT_LIMIT) {
    logMessage(LOG_WARNING,
      "HID report count too large at offset %"PRIsize ": %u", offset, count
    );

    return 0;
  }

  uint32_t *cursor = &walk->bitOffsets[type][walk->global.reportIdentifier];
  uint64_t bits = (uint64_t)bitSize * count;

  if ((*cursor + bits) > HID_REPORT_BITS_LIMIT) {
    logMessage(LOG_WARNING,
      "HID report too large at offset %"PRIsize ": %"PRIu64" bits",
      offset, (*cursor + bits)
    );

    return 0;
  }

  /* Skip a field wider than a HidUnsignedValue. It can't be handed to a
   * caller. Still advance the cursor past the space it occupies.
   */
  if (bitSize && (bitSize <= HID_VALUE_BITS_LIMIT)) {
    HidReportElement element = {
      .reportIdentifier = walk->global.reportIdentifier,
      .reportType = item->tag,

      .bitSize = bitSize,
      .logicalMinimum = walk->global.logicalMinimum,
      .logicalMaximum = walk->global.logicalMaximum,
      .flags = item->value.u,

      .elementCount = count,
      .collectionDepth = walk->collectionDepth,
      .collections = walk->collections,
    };

    if (walk->local.haveUsageMinimum) {
      element.usageMinimum = walk->local.usageMinimum & UINT16_MAX;

      element.usageMaximum = walk->local.haveUsageMaximum?
                             (walk->local.usageMaximum & UINT16_MAX):
                             element.usageMinimum;
    }

    for (unsigned int index=0; index<count; index+=1) {
      element.elementIndex = index;
      element.bitOffset = *cursor + (index * bitSize);

      if (walk->local.usageCount) {
        /* Apply the last usage to each remaining element when a main item
         * declares fewer usages than elements.
         */
        unsigned int which = index;
        if (which >= walk->local.usageCount) which = walk->local.usageCount - 1;

        HidUnsignedValue usage = walk->local.usages[which];
        element.usagePage = usage >> 0X10;
        element.usage = usage & UINT16_MAX;
      } else if (walk->local.haveUsageMinimum) {
        HidUnsignedValue usage = element.usageMinimum + index;
        if (usage > element.usageMaximum) usage = element.usageMaximum;

        element.usagePage = walk->local.usageMinimum >> 0X10;
        element.usage = usage;
      } else {
        element.usagePage = walk->global.usagePage;
        element.usage = 0;
      }

      if (!walk->handleElement(&element, walk->handlerData)) return 0;
    }
  }

  *cursor += bits;
  return 1;
}

static int
hidHandleItem (HidWalk *walk, const HidItem *item, size_t offset) {
  switch (item->tag) {
    case HID_ITM_UsagePage:
      walk->global.usagePage = item->value.u;
      return 1;

    case HID_ITM_ReportID:
      if (item->value.u > UINT8_MAX) {
        logMessage(LOG_WARNING,
          "HID report identifier too large at offset %"PRIsize ": 0X%"PRIX32,
          offset, item->value.u
        );

        return 0;
      }

      walk->global.reportIdentifier = item->value.u;
      return 1;

    case HID_ITM_ReportSize:
      walk->global.reportSize = item->value.u;
      return 1;

    case HID_ITM_ReportCount:
      walk->global.reportCount = item->value.u;
      return 1;

    case HID_ITM_LogicalMinimum:
      walk->global.logicalMinimum = item->value.s;
      return 1;

    case HID_ITM_LogicalMaximum:
      walk->global.logicalMaximum = item->value.s;
      return 1;

    case HID_ITM_PhysicalMinimum:
      walk->global.physicalMinimum = item->value.s;
      return 1;

    case HID_ITM_PhysicalMaximum:
      walk->global.physicalMaximum = item->value.s;
      return 1;

    case HID_ITM_UnitExponent:
      walk->global.unitExponent = item->value.s;
      return 1;

    case HID_ITM_Unit:
      walk->global.unit = item->value.u;
      return 1;

    case HID_ITM_Push:
      if (walk->pushDepth == HID_PUSH_LIMIT) {
        logMessage(LOG_WARNING,
          "HID items pushed too deeply at offset %"PRIsize, offset
        );

        return 0;
      }

      walk->pushed[walk->pushDepth++] = walk->global;
      return 1;

    case HID_ITM_Pop:
      if (!walk->pushDepth) {
        logMessage(LOG_WARNING,
          "HID items popped but not pushed at offset %"PRIsize, offset
        );

        return 0;
      }

      walk->global = walk->pushed[--walk->pushDepth];
      return 1;

    case HID_ITM_Delimiter:
      if (item->value.u) {
        walk->local.withinDelimiter = 1;
        walk->local.delimiterHonored = 0;
      } else {
        walk->local.withinDelimiter = 0;
      }

      return 1;

    case HID_ITM_Usage:
    case HID_ITM_UsageMinimum:
    case HID_ITM_UsageMaximum:
      if (walk->local.withinDelimiter) {
        if (walk->local.delimiterHonored) return 1;
        walk->local.delimiterHonored = 1;
      }

      switch (item->tag) {
        case HID_ITM_UsageMinimum:
          walk->local.usageMinimum = hidExtendedUsage(walk, item);
          walk->local.haveUsageMinimum = 1;
          return 1;

        case HID_ITM_UsageMaximum:
          walk->local.usageMaximum = hidExtendedUsage(walk, item);
          walk->local.haveUsageMaximum = 1;
          return 1;

        default:
          return hidAddLocalUsage(walk, item);
      }

    case HID_ITM_Collection: {
      if (walk->collectionDepth == HID_COLLECTION_LIMIT) {
        logMessage(LOG_WARNING,
          "HID collections nested too deeply at offset %"PRIsize, offset
        );

        return 0;
      }

      HidCollection *collection = &walk->collections[walk->collectionDepth++];
      collection->type = item->value.u;

      if (walk->local.usageCount) {
        HidUnsignedValue usage = walk->local.usages[0];
        collection->usagePage = usage >> 0X10;
        collection->usage = usage & UINT16_MAX;
      } else {
        collection->usagePage = walk->global.usagePage;
        collection->usage = 0;
      }

      hidClearLocalItems(walk);
      return 1;
    }

    case HID_ITM_EndCollection:
      if (walk->collectionDepth) {
        walk->collectionDepth -= 1;
      } else {
        logMessage(LOG_WARNING,
          "HID collection ended but not begun at offset %"PRIsize, offset
        );
      }

      hidClearLocalItems(walk);
      return 1;

    case HID_ITM_Input:
    case HID_ITM_Output:
    case HID_ITM_Feature: {
      int ok = hidReportElements(walk, item, offset);
      hidClearLocalItems(walk);
      return ok;
    }

    case HID_ITM_DesignatorIndex:
    case HID_ITM_DesignatorMinimum:
    case HID_ITM_DesignatorMaximum:
    case HID_ITM_StringIndex:
    case HID_ITM_StringMinimum:
    case HID_ITM_StringMaximum:
      return 1;

    default:
      if (!(walk->itemTagsEncountered & HID_ITEM_TAG_BIT(item->tag))) {
        logMessage(LOG_CATEGORY(HID_IO),
          "unhandled item tag at offset %"PRIsize ": 0X%02X",
          offset, item->tag
        );
      }

      return 1;
  }
}

int
hidWalkReportElements (
  const HidItemsDescriptor *items,
  HidReportElementHandler *handleElement, void *data
) {
  HidWalk *walk = malloc(sizeof(*walk));

  if (!walk) {
    logMallocError();
    return 0;
  }

  memset(walk, 0, sizeof(*walk));
  walk->handleElement = handleElement;
  walk->handlerData = data;

  const unsigned char *nextByte = items->bytes;
  size_t bytesLeft = items->count;
  int ok = 1;

  while (bytesLeft) {
    size_t offset = nextByte - items->bytes;
    HidItem item;

    if (!hidNextItem(&item, &nextByte, &bytesLeft)) {
      if (bytesLeft) {
        logMessage(LOG_WARNING,
          "incomplete HID item at offset %"PRIsize, offset
        );

        ok = 0;
      }

      break;
    }

    if (!hidHandleItem(walk, &item, offset)) {
      ok = 0;
      break;
    }

    walk->itemTagsEncountered |= HID_ITEM_TAG_BIT(item.tag);
  }

  if (ok) {
    if (walk->collectionDepth) {
      logMessage(LOG_WARNING,
        "HID collections not ended: %u", walk->collectionDepth
      );
    }

    if (walk->usagesDropped) {
      logMessage(LOG_WARNING,
        "HID usages dropped: %u", walk->usagesDropped
      );
    }
  }

  free(walk);
  return ok;
}
