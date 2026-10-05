/**
 * @file itso_names.c
 * @brief Human-readable names for the coded values carried in an ITSO Shell.
 *
 * Code lists are reproduced from ITSO TS 1000-5 Annex A, which in turn quotes
 * EN 1545-1. Strings are deliberately short: the Flipper screen is 128px wide.
 *
 * Only the short lists live here. The long ones are in names/, which the
 * device reads from an asset rather than carrying in RAM - see
 * names/itso_name_tables.c. A name a scene keeps a pointer to has to stay
 * here: the device's lookups return buffers that are reused.
 */
#include "itso.h"
#include "itso_operators.h"

#include <string.h>

const char* itso_typ_name(uint8_t typ) {
    switch(typ) {
    case 0:
        return "Private product";
    case ItsoTypStoredTravelRights:
        return "Pay as you go";
    case ItsoTypLoyalty1:
        return "Loyalty";
    case ItsoTypChargeToAccount1:
    case ItsoTypChargeToAccount2:
        return "Charge to account";
    case ItsoTypEntitlement:
        return "Entitlement";
    case ItsoTypId:
        return "ITSO ID";
    case ItsoTypLoyalty2:
        return "Loyalty";
    case ItsoTypPeriodTicket:
        return "Period ticket";
    case ItsoTypJourneyTicket:
        return "Journey ticket";
    case ItsoTypReservationTicket:
        return "Reserved journey";
    case ItsoTypVoucher:
        return "Voucher";
    case ItsoTypTolling:
        return "Toll pass";
    case ItsoTypPeriodCompact:
        return "Paper period ticket";
    case ItsoTypCarnet:
        return "Book of tickets";
    case ItsoTypMultiUse:
        return "Multi-use ticket";
    default:
        return "Product";
    }
}

const char* itso_status_name(ItsoProductStatus status) {
    switch(status) {
    case ItsoProductStatusUnused:
        return "Unused";
    case ItsoProductStatusActive:
        return "Active";
    case ItsoProductStatusBlocked:
        return "Blocked";
    default:
        return "Unknown";
    }
}

const char* itso_shell_reject_name(ItsoShellVerdict verdict) {
    switch(verdict) {
    case ItsoShellRejectShort:
        return "Too short";
    case ItsoShellRejectIin:
        return "Not ITSO's issuer number";
    case ItsoShellRejectCompact:
        return "Compact layout";
    case ItsoShellRejectGeometry:
        return "Impossible layout";
    case ItsoShellRejectNumber:
        return "Card number not decimal";
    case ItsoShellAccepted:
        return "Accepted";
    default:
        return "Not read";
    }
}

const char* itso_iin_name(uint32_t iin) {
    /* ITSO holds a single registered six-digit issuer number, used by every
     * ITSO shell (TS 1000-2 clause 4.1.4.1). */
    return (iin == 633597) ? "ITSO" : NULL;
}

bool itso_railcard_key(const uint8_t* code, size_t len, uint32_t* key) {
    while(len > 0 && (code[len - 1] == ' ' || code[len - 1] == 0)) {
        len--;
    }
    if(len != 3) return false;
    *key = 0;
    memcpy(key, code, 3);
    return true;
}

bool itso_seat_attribute_key(const char* code, uint32_t* key) {
    if(strlen(code) != 4) return false;
    memcpy(key, code, 4);
    return true;
}

bool itso_discount_from_card(const uint8_t* code, size_t len) {
    return len == 5 && memcmp(code, "XXXXX", 5) == 0;
}

const char* itso_count_name(ItsoCountKind kind) {
    switch(kind) {
    case ItsoCountRides:
        return "Rides left";
    case ItsoCountPasses:
        return "Passes left";
    case ItsoCountTransactions:
        return "Uses this period";
    case ItsoCountPoints:
        return "Points";
    case ItsoCountCoupons:
        return "Coupons left";
    case ItsoCountJourneys:
        return "Journeys left";
    case ItsoCountUses:
        return "Uses left";
    case ItsoCountCrossings:
        return "Crossings left";
    default:
        return NULL;
    }
}

const char* itso_gender_name(uint8_t id_flags) {
    /* IDFlags bits 1 and 2 (TS 1000-5 table 24). Both clear means not known and
     * both set means deliberately not specified; neither is worth a screen row,
     * so both report as nothing to show. */
    switch((id_flags >> 1) & 0x03) {
    case 1:
        return "Male";
    case 2:
        return "Female";
    default:
        return NULL;
    }
}

/* EN1545 AccommodationClassCode, reproduced in TS 1000-5 annex A.1. The ticket
 * stores it in three bits, so only the first eight codes can appear. */
const char* itso_class_name(uint8_t code) {
    switch(code & 0x07) {
    case 1:
        return "First";
    case 2:
        return "Standard";
    case 3:
        return "Small";
    case 4:
        return "Large";
    case 5:
        return "Business";
    case 6:
        return "Economy";
    case 7:
        return "Club";
    default:
        return NULL;
    }
}

/* TS 1000-5 annex A.24: ITSO language code n is the (n-1)th pair here, as ISO
 * 639-1. Transcribed from the 2025-03 edition, where code 70 is printed twice:
 * Ido and Igbo. Igbo is taken as 71, the only number the sequence leaves free.
 * Two letters a language rather than a name keeps the table to 374 bytes of
 * .rodata, which the Flipper loads into RAM. */
static const char itso_languages[] = "abomaaafaksqamarhyasavaeayazbmbaeubebndzbhbibsbrbgmykmescach"
                                     "cenyzhzacucvkwcocrhrcsdanieneoeteefofjfifrfyffgdgilgkadekikl"
                                     "gnguhahehzhihohuisigigidiegaitjajvknkrkskkkirwrnswkvkgkokukj"
                                     "kylolalvlnltiaiuiklulglbmkmgmsmldvmtgvmimrmhelmomnnanvngnend"
                                     "senonbnnocojorospipsfaplptpaqurmrorurwsmsgsascsrshsttnsnsdsi"
                                     "skslsonressuswsssvtltytgtatttethbotitotstrtktwugukuruzvevivo"
                                     "wacywoxhyiyozu";

bool itso_language_code(uint8_t code, char out[3]) {
    size_t count = (sizeof(itso_languages) - 1) / 2;
    if(code == 0 || code > count) return false;
    out[0] = itso_languages[(code - 1) * 2];
    out[1] = itso_languages[(code - 1) * 2 + 1];
    out[2] = '\0';
    return true;
}

const char* itso_language_name(uint8_t code) {
    /* Names only for the languages a UK or Irish card is likely to carry. */
    switch(code) {
    case 44:
        return "English";
    case 182:
        return "Welsh";
    case 54:
        return "Scottish Gaelic";
    case 74:
        return "Irish";
    case 37:
        return "Cornish";
    case 109:
        return "Manx";
    default:
        return NULL;
    }
}
