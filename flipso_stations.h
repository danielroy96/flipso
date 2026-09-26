/**
 * @file flipso_stations.h
 * @brief Resolves rail National Location Codes to station names.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest name the table may hold. Must match NAME_MAX in build_stations.py. */
#define FLIPSO_STATION_NAME_MAX 40

typedef struct FlipsoStations FlipsoStations;

/**
 * Open the station table.
 *
 * The table ships inside the .fap as a file asset, which the firmware unpacks
 * to /ext/apps_assets/flipso/ when the app is installed. A build of your own at
 * /ext/apps_data/flipso/stations.dat is preferred when present.
 *
 * Either way the table stays on the card and is searched in place: several
 * thousand names would cost more RAM than the whole rest of Flipso, and a
 * lookup needs only a dozen short reads. See tools/stations/FORMAT.md.
 */
FlipsoStations* flipso_stations_alloc(void);
void flipso_stations_free(FlipsoStations* instance);

/** True when a usable station table was found. */
bool flipso_stations_available(const FlipsoStations* instance);

/** Stations the table names; 0 when there is no table. */
uint32_t flipso_stations_count(const FlipsoStations* instance);

/**
 * Look up a four-character NLC.
 * @return the station name, or NULL if there is no table or no such code.
 */
const char* flipso_stations_name(FlipsoStations* instance, const char* nlc);

#ifdef __cplusplus
}
#endif
