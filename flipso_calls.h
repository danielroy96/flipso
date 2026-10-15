/**
 * @file flipso_calls.h
 * @brief Every firmware and C library header the app uses, then short calls
 *        for the app's own functions.
 *
 * The SDK compiles apps with -mlong-calls, because the firmware's functions sit
 * in flash and the app is loaded into RAM, far beyond the reach of a Thumb BL.
 * With -ffunction-sections that makes every call a long one, even to a static
 * function in the same file: a literal pool word holding the address, a load
 * and a BLX. Calls inside the app never leave the app's own .text, so a BL
 * reaches, and the loader relocates one (R_ARM_THM_CALL) as readily as a word.
 *
 * GCC's no_long_calls pragma gives every function declared after it the
 * short_call attribute. So the firmware's headers all come first, while long
 * calls are still the rule, and the pragma last: anything declared after it is
 * Flipso's. A firmware header first included below the pragma would make its
 * functions short calls, which the loader cannot place - add it here instead.
 * tools/flipper/check_calls.py fails the build's .fap when that has happened.
 */
#pragma once

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <furi.h>
#include <furi_hal_rtc.h>

#include <datetime/datetime.h>
#include <dialogs/dialogs.h>
#include <gui/canvas.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <gui/icon.h>
#include <gui/modules/text_input.h>
#include <gui/modules/validators.h>
#include <gui/modules/widget.h>
#include <gui/scene_manager.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <lib/toolbox/simple_array.h>
#include <locale/locale.h>
#include <nfc/nfc.h>
#include <nfc/nfc_poller.h>
#include <nfc/nfc_scanner.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a_poller.h>
#include <nfc/protocols/iso14443_4a/iso14443_4a_poller.h>
#include <nfc/protocols/mf_desfire/mf_desfire.h>
#include <nfc/protocols/mf_desfire/mf_desfire_poller.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <toolbox/bit_buffer.h>
#include <toolbox/path.h>
#include <toolbox/stream/file_stream.h>

#pragma no_long_calls
