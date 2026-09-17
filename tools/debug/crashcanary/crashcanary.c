/**
 * @file crashcanary.c
 * @brief Fails a furi_check on purpose, to verify the crash tooling works.
 *
 * Useful when changing anything in `tools/flipper/flipctl.py` around crash
 * detection: run this, and `flipctl crash` should report a halted device.
 *
 * The countdown is deliberately long: `ufbt launch` holds the serial port for
 * the first few seconds, so `flipctl crash --watch` cannot attach until it lets
 * go. A five-second countdown only left one line visible. In a real session you
 * arm the watch first and then reproduce, which does not have this problem.
 *
 * Recovering afterwards needs a hardware reset: hold LEFT + BACK for about five
 * seconds. Nothing over USB can restart a halted Flipper.
 */
#include <furi.h>

#define TAG "CrashCanary"

int32_t crashcanary_app(void* p) {
    UNUSED(p);

    /* These lines are the point of the exercise: they are the last thing
     * `flipctl crash --watch` sees before the device goes silent, which is how
     * a real fault gets localised. */
    for(int i = 15; i > 0; i--) {
        FURI_LOG_I(TAG, "crashing in %d", i);
        furi_delay_ms(1000);
    }

    furi_check(false, "flipctl crash canary");
    return 0;
}
