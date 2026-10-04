/**
 * @file itso.h
 * @brief Data model and decoder for the ITSO Shell held on a UK transport smartcard.
 *
 * Structures and field offsets follow ITSO TS 1000 (version 2.1.5, 2025-03):
 *   Part 1  - data types (DATE, DTS, VALC/VALS) and location definitions
 *   Part 2  - Shell Environment, Directory, IPE, Value Record and Log Directory Entry
 *   Part 5  - per-IPE-type datasets and the Transient Ticket Record
 *   Part 10 - the customer media definitions (CMD2, CMD4, CMD7, CMD12)
 *
 * Everything here is pure computation over byte buffers: no NFC, no GUI. That keeps
 * the decoder testable off-device and keeps the transport layer free to stream
 * sectors in whatever order is cheapest.
 *
 * This header is the decoder's whole public interface, and code outside itso/
 * includes it rather than the parts. The parts follow the layers of the spec:
 *
 *   itso_types.h     limits, codes and value types shared by everything below
 *   itso_location.h  a decoded location
 *   itso_util.h      bit fields, dates, money and day masks
 *   media/           the card as storage: shell, directory and sector chains,
 *                    and the Type 2 tag layouts (CMD4, CMD9, CMD10)
 *   ipe/             what a product holds: its IPE dataset, one file per TYP,
 *                    its value records, and the extensions decoded on demand
 *   itso_log.h       the taps in the cyclic log
 *   itso_card.h      the card those all decode into
 *   itso_names.h     names for coded values
 */
#pragma once

#include "itso_types.h"
#include "itso_location.h"
#include "itso_util.h"
#include "media/itso_shell.h"
#include "media/itso_directory.h"
#include "media/itso_type2.h"
#include "ipe/itso_product.h"
#include "ipe/itso_space_saving.h"
#include "ipe/itso_capping.h"
#include "ipe/itso_reservation.h"
#include "itso_log.h"
#include "itso_card.h"
#include "itso_names.h"
