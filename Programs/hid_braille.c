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
#include "hid_tables.h"
#include "hid_braille.h"

const char *
hidBrailleGetUsageName (HidUnsignedValue usage) {
  const HidBrailleUsageEntry *entry = hidBrailleUsageEntry(usage);
  return entry? entry->header.name: NULL;
}

int
hidBrailleTestItems (const HidItemsDescriptor *items) {
  const unsigned char *nextByte = items->bytes;
  size_t bytesLeft = items->count;

  HidUnsignedValue usagePage = 0;
  int isBrailleDisplay = 0;

  while (bytesLeft) {
    HidItem item;
    if (!hidNextItem(&item, &nextByte, &bytesLeft)) break;

    switch (item.tag) {
      case HID_ITM_UsagePage:
        usagePage = item.value.u;
        continue;

      case HID_ITM_Usage: {
        HidUnsignedValue page = (item.valueSize == 4)? (item.value.u >> 0X10): usagePage;
        HidUnsignedValue usage = item.value.u & UINT16_MAX;

        isBrailleDisplay = (page == HID_UPG_Braille)
                        && (usage == HID_USG_BRL_BrailleDisplay);

        continue;
      }

      case HID_ITM_Collection:
        if (isBrailleDisplay && (item.value.u == HID_COL_Application)) return 1;
        break;

      case HID_ITM_EndCollection:
      case HID_ITM_Input:
      case HID_ITM_Output:
      case HID_ITM_Feature:
        break;

      default:
        continue;
    }

    // Forget the usage. A main item discards the local items.
    isBrailleDisplay = 0;
  }

  return 0;
}

typedef struct {
  HidBrailleDescription *description;
  unsigned int buttonLimit;
  uint32_t inputBits[UINT8_MAX + 1];
  uint32_t outputBits[UINT8_MAX + 1];

  unsigned int ignoredCells;
  unsigned int ignoredRouterKeys;
} HidBrailleCollector;

static int
hidBrailleIsButton (const HidReportElement *element) {
  if (element->reportType != HID_ITM_Input) return 0;
  if (element->usagePage != HID_UPG_Braille) return 0;
  if (element->usage < HID_BRL_BUTTON_FIRST) return 0;
  if (element->usage > HID_BRL_BUTTON_LAST) return 0;
  return 1;
}

static int
hidBrailleCountButtons (const HidReportElement *element, void *data) {
  unsigned int *count = data;
  if (hidBrailleIsButton(element)) *count += 1;
  return 1;
}

/* Add one cell per element, whether the report descriptor declares the whole
 * row as one main item or as one main item per cell.
 */
static void
hidBrailleAddCell (
  HidBrailleCollector *collector,
  const HidReportElement *element,
  unsigned char dots
) {
  HidBrailleCells *cells = &collector->description->cells;

  if (cells->cellCount) {
    if ((dots == cells->dots)
     && (element->reportIdentifier == cells->identifier)
     && (element->bitSize == cells->bitSize)
     && (element->bitOffset == (cells->bitOffset + (cells->cellCount * cells->bitSize)))) {
      cells->cellCount += 1;
      return;
    }

    /* Ignore another row. It isn't rendered. Prefer eight dots when the same
     * row is declared in another dot format.
     */
    if (dots <= cells->dots) {
      collector->ignoredCells += 1;
      return;
    }
  }

  cells->identifier = element->reportIdentifier;
  cells->bitOffset = element->bitOffset;
  cells->bitSize = element->bitSize;
  cells->cellCount = 1;
  cells->dots = dots;
}

static void
hidBrailleAddRouterKey (
  HidBrailleCollector *collector,
  const HidReportElement *element
) {
  HidBrailleDescription *description = collector->description;
  HidUnsignedValue setUsage = HID_USG_BRL_RouterSet1;

  for (unsigned int depth=element->collectionDepth; depth>0; depth-=1) {
    const HidCollection *collection = &element->collections[depth - 1];
    if (collection->usagePage != HID_UPG_Braille) continue;
    if (collection->usage < HID_USG_BRL_RouterSet1) continue;
    if (collection->usage > HID_USG_BRL_RouterSet3) continue;

    setUsage = collection->usage;
    break;
  }

  HidBrailleRouterSet *set = NULL;

  for (unsigned int index=0; index<description->routerSetCount; index+=1) {
    HidBrailleRouterSet *candidate = &description->routerSets[index];

    if (candidate->usage == setUsage) {
      set = candidate;
      break;
    }
  }

  if (!set) {
    if (description->routerSetCount == HID_BRL_ROUTER_SET_COUNT) {
      collector->ignoredRouterKeys += 1;
      return;
    }

    set = &description->routerSets[description->routerSetCount++];
    set->usage = setUsage;
    set->identifier = element->reportIdentifier;
    set->bitOffset = element->bitOffset;
    set->bitSize = element->bitSize;
    set->keyCount = 1;
    return;
  }

  // Require the keys of a set to be consecutive - see hid_braille.h.
  if ((element->reportIdentifier != set->identifier)
   || (element->bitSize != set->bitSize)
   || (element->bitOffset != (set->bitOffset + (set->keyCount * set->bitSize)))) {
    collector->ignoredRouterKeys += 1;
    return;
  }

  set->keyCount += 1;
}

static void
hidBrailleSetValue (HidBrailleValue *value, const HidReportElement *element) {
  if (value->found) return;

  value->found = 1;
  value->identifier = element->reportIdentifier;
  value->reportType = element->reportType;
  value->bitOffset = element->bitOffset;
  value->bitSize = element->bitSize;
}

static int
hidBrailleCollectElement (const HidReportElement *element, void *data) {
  HidBrailleCollector *collector = data;
  HidBrailleDescription *description = collector->description;

  {
    uint32_t *bits = NULL;

    switch (element->reportType) {
      case HID_ITM_Input:
        bits = &collector->inputBits[element->reportIdentifier];
        break;

      case HID_ITM_Output:
        bits = &collector->outputBits[element->reportIdentifier];
        break;

      default:
        break;
    }

    if (bits) {
      uint32_t end = element->bitOffset + element->bitSize;
      if (end > *bits) *bits = end;
    }
  }

  if (element->usagePage != HID_UPG_Braille) return 1;

  if (hidBrailleIsButton(element)) {
    if (description->buttonCount < collector->buttonLimit) {
      HidBrailleButton *button = &description->buttons[description->buttonCount++];

      button->usage = element->usage;
      button->identifier = element->reportIdentifier;
      button->bitOffset = element->bitOffset;
    }

    return 1;
  }

  switch (element->usage) {
    case HID_USG_BRL_8DotCell:
    case HID_USG_BRL_6DotCell:
      if (element->reportType == HID_ITM_Output) {
        hidBrailleAddCell(
          collector, element,
          ((element->usage == HID_USG_BRL_8DotCell)? 8: 6)
        );
      }

      break;

    case HID_USG_BRL_RouterKey:
    case HID_USG_BRL_RowRouterKey:
      if (element->reportType == HID_ITM_Input) {
        hidBrailleAddRouterKey(collector, element);
      }

      break;

    case HID_USG_BRL_CellCount:
      if (element->reportType != HID_ITM_Output) {
        hidBrailleSetValue(&description->cellCount, element);
      }

      break;

    case HID_USG_BRL_ScreenReaderControl:
      if (element->reportType != HID_ITM_Output) {
        hidBrailleSetValue(&description->screenReaderControl, element);
      }

      break;

    case HID_USG_BRL_ScreenReaderIdentifier:
      if (element->reportType != HID_ITM_Input) {
        hidBrailleSetValue(&description->screenReaderIdentifier, element);
      }

      break;

    default:
      break;
  }

  return 1;
}

static int
hidBrailleTestIdentifiers (const HidItemsDescriptor *items) {
  const unsigned char *nextByte = items->bytes;
  size_t bytesLeft = items->count;

  while (bytesLeft) {
    HidItem item;
    if (!hidNextItem(&item, &nextByte, &bytesLeft)) break;
    if (item.tag == HID_ITM_ReportID) return 1;
  }

  return 0;
}

static void
hidBrailleSetInputSizes (HidBrailleCollector *collector) {
  HidBrailleDescription *description = collector->description;

  for (unsigned int identifier=0; identifier<=UINT8_MAX; identifier+=1) {
    uint32_t bits = collector->inputBits[identifier];
    if (!bits) continue;

    size_t bytes = (bits + 7) / 8;
    if (description->identifiersUsed) bytes += 1;

    if (bytes > UINT16_MAX) {
      logMessage(LOG_WARNING,
        "HID input report too large: %02X: %"PRIsize, identifier, bytes
      );

      continue;
    }

    description->inputSizes[identifier] = bytes;
  }
}

HidBrailleDescription *
hidBrailleNewDescription (const HidItemsDescriptor *items) {
  unsigned int buttonCount = 0;
  if (!hidWalkReportElements(items, hidBrailleCountButtons, &buttonCount)) return NULL;

  HidBrailleDescription *description;
  size_t size = sizeof(*description);
  size += ARRAY_SIZE(description->buttons, buttonCount);

  if (!(description = malloc(size))) {
    logMallocError();
    return NULL;
  }

  memset(description, 0, size);
  description->identifiersUsed = hidBrailleTestIdentifiers(items);

  HidBrailleCollector collector = {
    .description = description,
    .buttonLimit = buttonCount
  };

  if (!hidWalkReportElements(items, hidBrailleCollectElement, &collector)) {
    free(description);
    return NULL;
  }

  hidBrailleSetInputSizes(&collector);

  if (collector.ignoredCells) {
    logMessage(LOG_WARNING,
      "HID braille cells ignored: %u", collector.ignoredCells
    );
  }

  if (collector.ignoredRouterKeys) {
    logMessage(LOG_WARNING,
      "HID braille router keys ignored: %u", collector.ignoredRouterKeys
    );
  }

  HidBrailleCells *cells = &description->cells;

  if (!cells->cellCount) {
    logMessage(LOG_WARNING, "no HID braille cells");
    free(description);
    return NULL;
  }

  if (cells->cellCount > HID_BRL_CELL_LIMIT) {
    logMessage(LOG_WARNING,
      "too many HID braille cells: %u", cells->cellCount
    );

    cells->cellCount = HID_BRL_CELL_LIMIT;
  }

  /* Size the report up to its furthest field. The cells may not be its only
   * field. Always count the identifier byte (see hid_braille.h).
   */
  cells->reportSize = ((collector.outputBits[cells->identifier] + 7) / 8) + 1;

  return description;
}

void
hidBrailleDestroyDescription (HidBrailleDescription *description) {
  if (description) free(description);
}

static const char *
hidBrailleGetReportTypeName (HidItemTag reportType) {
  switch (reportType) {
    case HID_ITM_Input:   return "In";
    case HID_ITM_Output:  return "Out";
    case HID_ITM_Feature: return "Ftr";
    default:              return "?";
  }
}

int
hidBrailleListDescription (
  const HidBrailleDescription *description,
  HidBrailleDescriptionLister *listLine, void *data
) {
  char line[0X80];

  {
    const HidBrailleCells *cells = &description->cells;

    STR_BEGIN(line, sizeof(line));
    STR_PRINTF(
      "Braille Cells: Rpt:%02X Off:%"PRIu32 " Siz:%"PRIu32 " Cnt:%u Dots:%u Len:%"PRIsize,
      cells->identifier, cells->bitOffset, cells->bitSize,
      cells->cellCount, cells->dots, cells->reportSize
    );
    STR_END;

    if (!listLine(line, data)) return 0;
  }

  for (unsigned int index=0; index<description->routerSetCount; index+=1) {
    const HidBrailleRouterSet *set = &description->routerSets[index];

    STR_BEGIN(line, sizeof(line));
    STR_PRINTF(
      "Braille Router Set: %s: Rpt:%02X Off:%"PRIu32 " Siz:%"PRIu32 " Cnt:%u",
      hidBrailleGetUsageName(set->usage),
      set->identifier, set->bitOffset, set->bitSize, set->keyCount
    );
    STR_END;

    if (!listLine(line, data)) return 0;
  }

  for (unsigned int index=0; index<description->buttonCount; index+=1) {
    const HidBrailleButton *button = &description->buttons[index];
    const char *name = hidBrailleGetUsageName(button->usage);

    STR_BEGIN(line, sizeof(line));
    STR_PRINTF(
      "Braille Button: %s: Rpt:%02X Off:%"PRIu32 " Key:%u",
      (name? name: "?"), button->identifier, button->bitOffset,
      hidBrailleButtonNumber(button->usage)
    );
    STR_END;

    if (!listLine(line, data)) return 0;
  }

  {
    typedef struct {
      const char *label;
      const HidBrailleValue *value;
    } ValueEntry;

    const ValueEntry valueTable[] = {
      { .label = "Cell Count",
        .value = &description->cellCount
      },

      { .label = "Screen Reader Control",
        .value = &description->screenReaderControl
      },

      { .label = "Screen Reader Identifier",
        .value = &description->screenReaderIdentifier
      },
    };

    const ValueEntry *entry = valueTable;
    const ValueEntry *end = entry + ARRAY_COUNT(valueTable);

    while (entry < end) {
      const HidBrailleValue *value = entry->value;

      if (value->found) {
        STR_BEGIN(line, sizeof(line));
        STR_PRINTF(
          "Braille %s: Rpt:%02X %s Off:%"PRIu32 " Siz:%"PRIu32,
          entry->label, value->identifier,
          hidBrailleGetReportTypeName(value->reportType),
          value->bitOffset, value->bitSize
        );
        STR_END;

        if (!listLine(line, data)) return 0;
      }

      entry += 1;
    }
  }

  for (unsigned int identifier=0; identifier<=UINT8_MAX; identifier+=1) {
    size_t size = description->inputSizes[identifier];
    if (!size) continue;

    STR_BEGIN(line, sizeof(line));
    STR_PRINTF(
      "Braille Input Report: Rpt:%02X Len:%"PRIsize, identifier, size
    );
    STR_END;

    if (!listLine(line, data)) return 0;
  }

  return 1;
}

static int
hidBrailleLogLine (const char *line, void *data) {
  return logMessage(LOG_CATEGORY(HID_IO), "%s", line);
}

void
hidBrailleLogDescription (const HidBrailleDescription *description) {
  hidBrailleListDescription(description, hidBrailleLogLine, NULL);
}

int
hidBrailleMakeCellsReport (
  const HidBrailleDescription *description,
  const unsigned char *cells, unsigned int count,
  unsigned char *report, size_t size
) {
  const HidBrailleCells *field = &description->cells;

  if (size < field->reportSize) {
    logMessage(LOG_WARNING,
      "HID braille cells report buffer too small: %"PRIsize, size
    );

    return 0;
  }

  memset(report, 0, size);
  report[0] = field->identifier;
  unsigned char *data = report + 1;

  if (count > field->cellCount) count = field->cellCount;

  for (unsigned int index=0; index<count; index+=1) {
    unsigned char cell = cells[index];
    if (field->dots == 6) cell &= 0X3F;

    hidSetReportBits(
      data, field->bitOffset + (index * field->bitSize),
      field->bitSize, cell
    );
  }

  return 1;
}

int
hidBrailleDecodeInput (
  const HidBrailleDescription *description,
  const unsigned char *report, size_t size,
  HidBrailleInput *input
) {
  HidReportIdentifier identifier = 0;

  if (description->identifiersUsed) {
    if (!size) return 0;
    identifier = *report++;
    size -= 1;
  }

  size_t bits = size * 8;

  for (unsigned int index=0; index<description->buttonCount; index+=1) {
    const HidBrailleButton *button = &description->buttons[index];
    if (button->identifier != identifier) continue;
    if ((button->bitOffset + 1) > bits) continue;

    if (hidGetReportBits(report, button->bitOffset, 1)) {
      input->buttons |= UINT32_C(1) << hidBrailleButtonNumber(button->usage);
    } else {
      input->buttons &= ~(UINT32_C(1) << hidBrailleButtonNumber(button->usage));
    }
  }

  // Steer the cursor with the first router set only.
  if (description->routerSetCount) {
    const HidBrailleRouterSet *set = &description->routerSets[0];

    if (set->identifier == identifier) {
      unsigned int keyCount = set->keyCount;
      if (keyCount > HID_BRL_CELL_LIMIT) keyCount = HID_BRL_CELL_LIMIT;

      for (unsigned int key=0; key<keyCount; key+=1) {
        uint32_t offset = set->bitOffset + (key * set->bitSize);
        if ((offset + set->bitSize) > bits) break;

        unsigned char *byte = &input->routerKeys[key / 8];
        unsigned char mask = 1 << (key % 8);

        if (hidGetReportBits(report, offset, set->bitSize)) {
          *byte |= mask;
        } else {
          *byte &= ~mask;
        }
      }
    }
  }

  {
    const HidBrailleValue *value = &description->cellCount;

    if (value->found && (value->identifier == identifier)
     && (value->reportType == HID_ITM_Input)
     && ((value->bitOffset + value->bitSize) <= bits)) {
      input->cellCount = hidGetReportBits(report, value->bitOffset, value->bitSize);
      input->haveCellCount = 1;
    }
  }

  {
    const HidBrailleValue *value = &description->screenReaderControl;

    if (value->found && (value->identifier == identifier)
     && (value->reportType == HID_ITM_Input)
     && ((value->bitOffset + value->bitSize) <= bits)) {
      input->screenReaderControl = hidGetReportBits(report, value->bitOffset, value->bitSize);
      input->haveScreenReaderControl = 1;
    }
  }

  return 1;
}
