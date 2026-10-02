/**
 * @file itso_names.c
 * @brief Human-readable names for the coded values carried in an ITSO Shell.
 *
 * Code lists are reproduced from ITSO TS 1000-5 Annex A, which in turn quotes
 * EN 1545-1. Strings are deliberately short: the Flipper screen is 128px wide.
 */
#include "itso.h"

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

const char* itso_entitlement_name(uint8_t code) {
    switch(code) {
    case 0:
        return "None";
    case 1:
        return "Warrant";
    case 2:
        return "Limited free ride";
    case 3:
        return "Proportional fare";
    case 4:
        return "Flat fare discount";
    case 5:
        return "Flat fare";
    case 6:
        return "Charge to account";
    case 7:
        return "Subscription";
    case 8:
        return "Frequent traveller";
    case 9:
        return "Senator";
    case 10:
        return "Premium";
    case 11:
        return "Gold status";
    case 12:
        return "Silver status";
    case 13:
        return "Capped fare";
    case 14:
        return "Free travel";
    case 15:
        return "Half fare";
    default:
        return "Scheme specific";
    }
}

const char* itso_profile_name(uint8_t code) {
    switch(code) {
    case 0:
        return "Unspecified";
    case 1:
        return "Adult";
    case 2:
        return "Child";
    case 3:
        return "Student";
    case 4:
        return "Pensioner";
    case 5:
        return "Disabled";
    case 6:
        return "Disabled (sight)";
    case 7:
        return "Disabled (hearing)";
    case 8:
        return "Unemployed";
    case 9:
        return "Staff";
    case 10:
        return "Military";
    case 11:
        return "Resident";
    case 12:
        return "Owned haulage";
    case 13:
        return "Bus company";
    case 14:
        return "Long distance";
    case 15:
        return "Local transport";
    case 16:
        return "Commuter";
    case 17:
        return "Animal";
    case 18:
        return "Object";
    case 19:
        return "Scholar";
    case 20:
        return "Trainee";
    case 21:
        return "Police";
    case 22:
        return "Motorbike";
    case 23:
        return "Pushbike";
    case 24:
        return "Pram";
    case 25:
        return "Senior";
    default:
        return "Other";
    }
}

const char* itso_transaction_name(uint8_t code) {
    /* EventTypeCode, TS 1000-5 annex A.20. Four bits on the card, so only the
     * first sixteen of its codes can appear. Code 0 does three jobs there - a
     * mid-journey check, a product created empty, a change with no code of its
     * own - so it is named for none of them in particular. */
    switch(code) {
    case 0:
        return "Other";
    case 1:
        return "Sale";
    case 2:
        return "Outward journey";
    case 3:
        return "Cancelled";
    case 4:
        return "Top-up";
    case 5:
        return "Auto top-up";
    case 6:
        return "Return journey";
    case 7:
        return "Fare paid";
    case 8:
        return "Exchange";
    case 9:
        return "Points redeemed";
    case 10:
        return "Refunded";
    case 11:
        return "Tap in";
    case 12:
        return "Tap out";
    case 13:
        return "Ticket activated";
    case 14:
        return "Multi-leg journey";
    case 15:
        return "Account payment";
    default:
        return "Transaction";
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

const char* itso_shell_reject_name(ItsoShellReject reject) {
    switch(reject) {
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

/* EN1545 PaymentMeansCode, reproduced in TS 1000-5 annex A.12. ITSO stores the
 * code in four bits, so only the sixteen codes below can appear. */
const char* itso_payment_name(uint8_t code) {
    switch(code & 0x0F) {
    case 0:
        return "Unspecified";
    case 1:
        return "Cash";
    case 2:
        return "Cheque";
    case 3:
        return "Card";
    case 4:
        return "E-purse";
    case 5:
        return "Charge to account";
    case 6:
        return "Direct debit";
    case 7:
        return "Invoiced";
    case 8:
        return "Pay as you go";
    case 9:
        return "Loyalty points";
    case 10:
        return "Token";
    case 11:
        return "Membership";
    case 12:
        return "Auto-renew";
    case 13:
        return "Warrant";
    case 14:
        return "Voucher";
    default:
        return "Other";
    }
}

/*
 * National Rail railcard codes, the three characters a TYP 24's DiscountCode
 * holds (RSPS3002 section 3.8.3) and the fares data's railcard file keys on.
 * Not from the ITSO specification: compiled on 2026-10-02 from SAP Concur's
 * published rail discount codes (github.com/SAP-docs/preview.developer.concur.com,
 * travel-profile v2 reference), a list of fares-data railcards
 * (gist.github.com/maier-stefan/9a5782bf03086e376e7fe5029ca32b27), and the
 * RailUK Fares & Ticketing Guide, section 6, for DIC. A code none of them
 * names is shown as the code.
 */
static const struct {
    char code[4];
    const char* name;
    bool card; /**< A card to carry, rather than a group or online offer. */
} itso_railcards[] = {
    {"2TR", "Two Together Railcard", true},
    {"C50", "Club 50", true},
    {"CRC", "Cambrian Railcard", true},
    {"CTD", "Cotswold Line Railcard", true},
    {"DCG", "Devon & Cornwall Gold Card", true},
    {"DCR", "Devon & Cornwall Railcard", true},
    {"DIC", "Disabled Child Railcard", true},
    {"DIS", "Disabled Persons Railcard", true},
    {"DRD", "Dales Railcard", true},
    {"EVC", "Esk Valley Railcard", true},
    {"FAM", "Family & Friends Railcard", true},
    {"GS3", "GroupSave", false},
    {"HMF", "HM Forces Railcard", true},
    {"HOW", "Heart of Wales Railcard", true},
    {"HRC", "Highland Railcard", true},
    {"JCP", "Jobcentre Plus Travel Discount Card", true},
    {"NDC", "New Deal Photocard", true},
    {"NDJ", "New Deal Photocard", true},
    {"NEW", "Network Railcard", true},
    {"NGC", "Annual Gold Card", true},
    {"OC5", "Online Club 50", false},
    {"PBR", "Pembrokeshire Railcard", true},
    {"SRN", "Senior Railcard", true},
    {"SRY", "Young Scot Railcard", true},
    {"TST", "26-30 Railcard", true},
    {"TSU", "16-17 Saver", true},
    {"VLC", "Valleys Student Railcard", true},
    {"VLS", "Valleys Senior Railcard", true},
    {"YNG", "16-25 Railcard", true},
};

const char* itso_railcard_name(const uint8_t* code, size_t len, bool* card) {
    while(len > 0 && (code[len - 1] == ' ' || code[len - 1] == 0)) {
        len--;
    }
    if(len != 3) return NULL;
    for(size_t i = 0; i < sizeof(itso_railcards) / sizeof(itso_railcards[0]); i++) {
        if(memcmp(itso_railcards[i].code, code, 3) != 0) continue;
        if(card) *card = itso_railcards[i].card;
        return itso_railcards[i].name;
    }
    return NULL;
}

bool itso_discount_from_card(const uint8_t* code, size_t len) {
    return len == 5 && memcmp(code, "XXXXX", 5) == 0;
}

/*
 * AccommodationAttribute codes. RSPS3002 3.8.6 takes them from the National
 * Reservation System's reference data, which RSPS5048 defines and which is not
 * published. "SEAT" and "HTMS" (a hot meal at the seat) are the two codes RDG's
 * Guide to Rail Retailing quotes from it; the rest are the plain abbreviations
 * a code in that style would use, and are decoded only because they cannot
 * reasonably mean anything else. Any other code is shown as the card has it.
 */
static const struct {
    char code[5];
    const char* name;
} itso_seat_attributes[] = {
    {"AISL", "Aisle"},
    {"AIRL", "Airline style"},
    {"BACK", "Facing backwards"},
    {"BIKE", "Bicycle space"},
    {"FACE", "Facing forwards"},
    {"HTMS", "Hot meal at seat"},
    {"LUGG", "Near luggage space"},
    {"POWR", "Power socket"},
    {"PRIO", "Priority seat"},
    {"QUIE", "Quiet coach"},
    {"SEAT", "Seat"},
    {"TABL", "Table"},
    {"TOIL", "Near toilet"},
    {"WCHR", "Wheelchair space"},
    {"WIND", "Window"},
    {"WNDW", "Window"},
};

const char* itso_seat_attribute_name(const char* code) {
    for(size_t i = 0; i < sizeof(itso_seat_attributes) / sizeof(itso_seat_attributes[0]); i++) {
        if(strcmp(itso_seat_attributes[i].code, code) == 0) return itso_seat_attributes[i].name;
    }
    return NULL;
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

uint16_t itso_isam_oid(uint32_t isam) {
    uint16_t top = (uint16_t)(isam >> 19); /* The 13 bits every OID has. */
    if(!(isam & (1UL << 18))) return top;
    if(!(isam & (1UL << 17))) return (uint16_t)(0x2000 | top);
    return (uint16_t)(((isam & (1UL << 16)) ? 0xE000 : 0x6000) | top);
}

/*
 * SPT Glasgow Subway station numbering.
 *
 * Unlike the rail NLCs and NaPTAN codes the other locations use, the Subway
 * numbers its 15 stations 1-15, anticlockwise from Govan (the outer-circle
 * order). The table is complete: the Subway has exactly fifteen stations and
 * every one is here. This is *not* from the ITSO specification - it is the SPT
 * scheme's own encoding, reverse-engineered by Ryan Murphy and published at
 * https://github.com/fork-bombed/spt-decoded (src/spt.py), and cross-checked
 * against the current SPT station list. The station ID is the stage number of the
 * bus fare stage (LocDefType 202) a Subway gate writes to a TYP 29 ticket's
 * UsageRec - the machine number is the gate - as Ryan Murphy's published dump of
 * real tickets shows.
 */
const char* itso_spt_subway_station(uint8_t id) {
    static const char* const stations[] = {
        "Govan",
        "Partick",
        "Kelvinhall",
        "Hillhead",
        "Kelvinbridge",
        "St George's Cross",
        "Cowcaddens",
        "Buchanan Street",
        "St Enoch",
        "Bridge Street",
        "West Street",
        "Shields Road",
        "Kinning Park",
        "Cessnock",
        "Ibrox",
    };
    if(id < 1 || id > sizeof(stations) / sizeof(stations[0])) return NULL;
    return stations[id - 1];
}
