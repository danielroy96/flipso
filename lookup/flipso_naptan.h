/**
 * @file flipso_naptan.h
 * @brief Resolves NaPTAN bus stop codes to stop names.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest name the table may hold. Must match NAME_MAX in build_naptan.py. */
#define FLIPSO_NAPTAN_NAME_MAX 40

/** Longest AtcoCode, ITSO TS 1000-1 table 40. */
#define FLIPSO_NAPTAN_ATCO_MAX 12

typedef struct FlipsoNaptan FlipsoNaptan;

/**
 * Open the stop table.
 *
 * Read from /ext/apps_data/flipso/naptan.dat, which the project ships ready
 * built in data/ for copying onto the card - see data/README.md. Unlike the
 * station table it is not packaged inside the .fap: at 21 MB it would be
 * re-uploaded over USB on every install, which takes upwards of ten minutes and
 * leaves an unloadable .fap if it is interrupted.
 *
 * The asset path is still checked second, so a build small enough to package
 * works if anyone wants one. Either way the table stays on the card and is
 * searched in place, costing no heap - see tools/naptan/FORMAT.md.
 */
FlipsoNaptan* flipso_naptan_alloc(void);
void flipso_naptan_free(FlipsoNaptan* instance);

/** True when a usable stop table was found. */
bool flipso_naptan_available(const FlipsoNaptan* instance);

/** Stops the table names by NaptanCode; 0 when there is no table. */
uint32_t flipso_naptan_count(const FlipsoNaptan* instance);

/**
 * Look up a NaptanCode, as the digits an ITSO card stores.
 *
 * The card holds the code folded onto a telephone keypad and packed into four
 * bytes of BCD (TS 1000-1 table 28), which is lossy, so the table is keyed on
 * the same folded digits rather than on the code itself. @p digits is up to
 * eight decimal characters; leading zeros are insignificant.
 *
 * @return the stop name, or NULL if there is no table or no such code.
 */
const char* flipso_naptan_stop(FlipsoNaptan* instance, const char* digits);

/**
 * Look up an AtcoCode of up to twelve characters. Matching ignores case, since
 * the register publishes them upper case but a card need not.
 *
 * @return the stop name, or NULL if there is no table or no such code.
 */
const char* flipso_naptan_atco(FlipsoNaptan* instance, const char* atco);

#ifdef __cplusplus
}
#endif
