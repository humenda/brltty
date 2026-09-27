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
#include <errno.h>

#include "log.h"
#include "parse.h"
#include "hid_braille.h"

typedef enum {
  PARM_CELLCOUNT,
  PARM_SCREENREADER
} DriverParameter;
#define BRLPARMS "cellcount", "screenreader"

#include "brl_driver.h"
#include "brldefs-hb.h"

#define MAXIMUM_REPORT_SIZE 0X100

BEGIN_KEY_NAME_TABLE(braille)
  KEY_NAME_ENTRY(HB_KEY_Dot1, "Dot1"),
  KEY_NAME_ENTRY(HB_KEY_Dot2, "Dot2"),
  KEY_NAME_ENTRY(HB_KEY_Dot3, "Dot3"),
  KEY_NAME_ENTRY(HB_KEY_Dot4, "Dot4"),
  KEY_NAME_ENTRY(HB_KEY_Dot5, "Dot5"),
  KEY_NAME_ENTRY(HB_KEY_Dot6, "Dot6"),
  KEY_NAME_ENTRY(HB_KEY_Dot7, "Dot7"),
  KEY_NAME_ENTRY(HB_KEY_Dot8, "Dot8"),
  KEY_NAME_ENTRY(HB_KEY_Space, "Space"),
  KEY_NAME_ENTRY(HB_KEY_LeftSpace, "LeftSpace"),
  KEY_NAME_ENTRY(HB_KEY_RightSpace, "RightSpace"),
END_KEY_NAME_TABLE

BEGIN_KEY_NAME_TABLE(navigation)
  KEY_NAME_ENTRY(HB_KEY_PanLeft, "PanLeft"),
  KEY_NAME_ENTRY(HB_KEY_PanRight, "PanRight"),
  KEY_NAME_ENTRY(HB_KEY_RockerUp, "RockerUp"),
  KEY_NAME_ENTRY(HB_KEY_RockerDown, "RockerDown"),
  KEY_NAME_ENTRY(HB_KEY_RockerPress, "RockerPress"),

  KEY_NAME_ENTRY(HB_KEY_JoystickCenter, "JoystickCenter"),
  KEY_NAME_ENTRY(HB_KEY_JoystickUp, "JoystickUp"),
  KEY_NAME_ENTRY(HB_KEY_JoystickDown, "JoystickDown"),
  KEY_NAME_ENTRY(HB_KEY_JoystickLeft, "JoystickLeft"),
  KEY_NAME_ENTRY(HB_KEY_JoystickRight, "JoystickRight"),

  KEY_NAME_ENTRY(HB_KEY_DPadCenter, "DPadCenter"),
  KEY_NAME_ENTRY(HB_KEY_DPadUp, "DPadUp"),
  KEY_NAME_ENTRY(HB_KEY_DPadDown, "DPadDown"),
  KEY_NAME_ENTRY(HB_KEY_DPadLeft, "DPadLeft"),
  KEY_NAME_ENTRY(HB_KEY_DPadRight, "DPadRight"),
END_KEY_NAME_TABLE

BEGIN_KEY_NAME_TABLE(routing)
  KEY_GROUP_ENTRY(HB_GRP_RoutingKeys, "RoutingKey"),
END_KEY_NAME_TABLE

BEGIN_KEY_NAME_TABLE(control)
  KEY_GROUP_ENTRY(HB_GRP_ScreenReaderControls, "ScreenReaderControl"),
END_KEY_NAME_TABLE

BEGIN_KEY_NAME_TABLES(all)
  KEY_NAME_TABLE(braille),
  KEY_NAME_TABLE(navigation),
  KEY_NAME_TABLE(routing),
  KEY_NAME_TABLE(control),
END_KEY_NAME_TABLES

DEFINE_KEY_TABLE(all)

BEGIN_KEY_TABLE_LIST
  &KEY_TABLE_DEFINITION(all),
END_KEY_TABLE_LIST

struct BrailleDataStruct {
  HidBrailleDescription *description;
  HidBrailleInput input;

  struct {
    KeyNumberSet navigation;
    unsigned char routing[HID_BRL_ROUTER_BYTES];
  } pressed;

  struct {
    unsigned char rewrite;
    unsigned char cells[HID_BRL_CELL_LIMIT];
  } text;

  unsigned char cellCountForced:1;
};

static int
writeReport (BrailleDisplay *brl, const unsigned char *report, size_t size) {
  logOutputPacket(report, size);
  return gioWriteHidReport(brl->gioEndpoint, report, size) != -1;
}

static int
writeFeature (BrailleDisplay *brl, const unsigned char *report, size_t size) {
  logOutputPacket(report, size);
  return gioWriteHidFeature(brl->gioEndpoint, report, size) != -1;
}

/* Always begin a report buffer with the report identifier. Do so even when the
 * report descriptor doesn't use report identifiers. gioWriteHidReport() and
 * gioWriteHidFeature() both take it from the first byte.
 */
static size_t
getWriteSize (BrailleDisplay *brl, const HidBrailleValue *value) {
  size_t size = (value->reportType == HID_ITM_Feature)?
                gioGetHidFeatureSize(brl->gioEndpoint, value->identifier):
                gioGetHidOutputSize(brl->gioEndpoint, value->identifier);

  if (size && value->identifier) size -= 1;

  {
    size_t minimum = (value->bitOffset + value->bitSize + 7) / 8;
    if (size < minimum) size = minimum;
  }

  return size + 1;
}

static int
writeScreenReaderIdentifier (BrailleDisplay *brl, char **parameters) {
  const HidBrailleValue *value = &brl->data->description->screenReaderIdentifier;
  if (!value->found) return 1;

  int setting = 0;
  const char *parameter = parameters[PARM_SCREENREADER];

  if (parameter && *parameter) {
    static const int minimum = 0;
    static const int maximum = UINT16_MAX;

    if (!validateInteger(&setting, parameter, &minimum, &maximum)) {
      logMessage(LOG_WARNING, "invalid screen reader setting: %s", parameter);
    }
  }

  size_t size = getWriteSize(brl, value);
  if (size > MAXIMUM_REPORT_SIZE) return 1;

  unsigned char report[size];
  memset(report, 0, size);
  report[0] = value->identifier;
  hidSetReportBits(&report[1], value->bitOffset, value->bitSize, setting);

  if (value->reportType == HID_ITM_Feature) return writeFeature(brl, report, size);
  return writeReport(brl, report, size);
}

static void
setCellCount (BrailleDisplay *brl, unsigned int count) {
  if (!count) return;
  if (count > brl->data->description->cells.cellCount) return;
  if (count == brl->textColumns) return;

  brl->textColumns = count;
  brl->resizeRequired = 1;
}

static int
probeDisplay (BrailleDisplay *brl, char **parameters) {
  const HidItemsDescriptor *items = gioGetHidItems(brl->gioEndpoint);
  if (!items) return 0;

  HidBrailleDescription *description = hidBrailleNewDescription(items);
  if (!description) return 0;
  brl->data->description = description;
  hidBrailleLogDescription(description);

  unsigned int cellCount = description->cells.cellCount;

  {
    int setting = 0;
    const char *parameter = parameters[PARM_CELLCOUNT];

    if (parameter && *parameter) {
      static const int minimum = 1;
      const int maximum = cellCount;

      if (validateInteger(&setting, parameter, &minimum, &maximum)) {
        cellCount = setting;
        brl->data->cellCountForced = 1;
      } else {
        logMessage(LOG_WARNING, "invalid cell count setting: %s", parameter);
      }
    }
  }

  /* Honor the number of braille cells usage. It states how many cells of the
   * row the device uses. The device sends it within an input report. Honor it
   * once such a report arrives.
   */
  if (description->cellCount.found
   && (description->cellCount.reportType != HID_ITM_Input)) {
    logMessage(LOG_WARNING, "HID braille cell count not within an input report");
  }

  brl->textColumns = cellCount;
  brl->textRows = 1;

  if (!writeScreenReaderIdentifier(brl, parameters)) return 0;
  return 1;
}

static int
connectResource (BrailleDisplay *brl, const char *identifier) {
  GioDescriptor descriptor;
  gioInitializeDescriptor(&descriptor);

  // Recognize a conforming display by its report descriptor rather than by a
  // model table.
  descriptor.hid.testItems = hidBrailleTestItems;

  return connectBrailleResource(brl, identifier, &descriptor, NULL);
}

static int
brl_construct (BrailleDisplay *brl, char **parameters, const char *device) {
  if ((brl->data = malloc(sizeof(*brl->data)))) {
    memset(brl->data, 0, sizeof(*brl->data));

    if (connectResource(brl, device)) {
      if (probeDisplay(brl, parameters)) {
        setBrailleKeyTable(brl, &KEY_TABLE_DEFINITION(all));
        makeOutputTable(dotsTable_ISO11548_1);

        brl->data->text.rewrite = 1;
        return 1;
      }

      hidBrailleDestroyDescription(brl->data->description);
      disconnectBrailleResource(brl, NULL);
    }

    free(brl->data);
    brl->data = NULL;
  } else {
    logMallocError();
  }

  return 0;
}

static void
brl_destruct (BrailleDisplay *brl) {
  disconnectBrailleResource(brl, NULL);

  if (brl->data) {
    hidBrailleDestroyDescription(brl->data->description);
    free(brl->data);
    brl->data = NULL;
  }
}

static int
brl_writeWindow (BrailleDisplay *brl, const wchar_t *text) {
  const HidBrailleDescription *description = brl->data->description;
  size_t count = brl->textColumns;

  if (cellsHaveChanged(brl->data->text.cells, brl->buffer, count,
                       NULL, NULL, &brl->data->text.rewrite)) {
    unsigned char cells[count];
    translateOutputCells(cells, brl->data->text.cells, count);

    size_t size = description->cells.reportSize;
    unsigned char report[size];

    if (!hidBrailleMakeCellsReport(description, cells, count, report, size)) return 0;
    if (!writeReport(brl, report, size)) return 0;
  }

  return 1;
}

static BraillePacketVerifierResult
verifyPacket (
  BrailleDisplay *brl,
  unsigned char *bytes, size_t size,
  size_t *length, void *data
) {
  const HidBrailleDescription *description = brl->data->description;

  if (size == 1) {
    // Take the first byte as the report identifier only if the descriptor uses
    // them.
    HidReportIdentifier identifier = description->identifiersUsed? bytes[0]: 0;

    size_t reportSize = description->inputSizes[identifier];
    if (!reportSize) return BRL_PVR_INVALID;

    *length = reportSize;
  }

  return BRL_PVR_INCLUDE;
}

static size_t
readPacket (BrailleDisplay *brl, void *packet, size_t size) {
  return readBraillePacket(brl, NULL, packet, size, verifyPacket, NULL);
}

static int
handleInputReport (BrailleDisplay *brl, const unsigned char *report, size_t size) {
  const HidBrailleDescription *description = brl->data->description;
  HidBrailleInput *input = &brl->data->input;

  input->haveCellCount = 0;
  input->haveScreenReaderControl = 0;
  if (!hidBrailleDecodeInput(description, report, size, input)) return 1;

  if (!enqueueUpdatedKeys(brl, input->buttons, &brl->data->pressed.navigation,
                          HB_GRP_NavigationKeys, 0)) {
    return 0;
  }

  if (description->routerSetCount) {
    if (!enqueueUpdatedKeyGroup(brl, description->routerSets[0].keyCount,
                                input->routerKeys, brl->data->pressed.routing,
                                HB_GRP_RoutingKeys)) {
      return 0;
    }
  }

  if (input->haveCellCount && !brl->data->cellCountForced) {
    setCellCount(brl, input->cellCount);
  }

  if (input->haveScreenReaderControl) {
    HidUnsignedValue control = input->screenReaderControl;

    if (control <= KTB_KEY_MAX) {
      if (!enqueueKey(brl, HB_GRP_ScreenReaderControls, control)) return 0;
    } else {
      logMessage(LOG_WARNING,
        "unsupported HID braille screen reader control: %"PRIu32, control
      );
    }
  }

  return 1;
}

static int
brl_readCommand (BrailleDisplay *brl, KeyTableCommandContext context) {
  unsigned char packet[MAXIMUM_REPORT_SIZE];
  size_t size;

  while ((size = readPacket(brl, packet, sizeof(packet)))) {
    if (!handleInputReport(brl, packet, size)) return BRL_CMD_RESTARTBRL;
  }

  return (errno == EAGAIN)? EOF: BRL_CMD_RESTARTBRL;
}
