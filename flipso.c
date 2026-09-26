/**
 * @file flipso.c
 * @brief Application lifecycle and the formatting helpers the scenes share.
 */
#include "flipso.h"

#include "flipso_icons.h"

#include <gui/modules/validators.h>
#include <furi_hal_rtc.h>
#include <datetime/datetime.h>
#include <locale/locale.h>
#include <string.h>

/* Ticks drive the scan animation; 100 ms is smooth enough and cheap. */
#define FLIPSO_TICK_PERIOD_MS 100

/* G5 into C6, run together rather than separated by a rest, then out. The
 * firmware's own note messages are the whole vocabulary the speaker has here:
 * a message sets a frequency and it sounds until the next one changes it. */
const NotificationSequence flipso_sequence_saved = {
    &message_display_backlight_on,
    &message_blue_255,
    &message_note_g5,
    &message_delay_50,
    &message_note_c6,
    &message_delay_100,
    &message_sound_off,
    NULL,
};

/* The same two notes the other way up. Red and blue together make the magenta
 * that separates this from the blue of a card kept. */
const NotificationSequence flipso_sequence_deleted = {
    &message_display_backlight_on,
    &message_red_255,
    &message_blue_255,
    &message_note_c6,
    &message_delay_50,
    &message_note_g5,
    &message_delay_100,
    &message_sound_off,
    NULL,
};

/* FAT will not take these, and the Flipper's keyboard offers some of them. */
#define FLIPSO_NAME_FORBIDDEN "\\/:*?\"<>|"

bool flipso_name_validator(const char* text, FuriString* error, void* context) {
    for(const char* c = text; *c; c++) {
        if(strchr(FLIPSO_NAME_FORBIDDEN, *c)) {
            furi_string_printf(error, "Name cannot\ncontain %c", *c);
            return false;
        }
    }
    return validator_is_file_callback(text, error, context);
}

uint32_t flipso_now(void) {
    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    return datetime_datetime_to_timestamp(&now);
}

/** Split a Unix timestamp and append it in the user's configured date format. */
static void flipso_cat_timestamp(FuriString* out, uint32_t timestamp, bool with_time) {
    DateTime dt;
    datetime_timestamp_to_datetime(timestamp, &dt);

    FuriString* formatted = furi_string_alloc();
    locale_format_date(formatted, &dt, locale_get_date_format(), "/");
    furi_string_cat(out, formatted);

    if(with_time) {
        furi_string_reset(formatted);
        locale_format_time(formatted, &dt, locale_get_time_format(), false);
        furi_string_cat_printf(out, " %s", furi_string_get_cstr(formatted));
    }

    furi_string_free(formatted);
}

void flipso_cat_date(FuriString* out, uint16_t date) {
    flipso_cat_timestamp(out, itso_date_to_unix(date), false);
}

void flipso_cat_expiry(
    FuriString* out,
    const char* label,
    const char* past_label,
    uint16_t date,
    uint32_t now) {
    /* EN1545 defines a DateStamp of zero as the maximum date (10/11/2041), which
     * in practice is how schemes encode "no expiry". */
    if(date == 0) {
        furi_string_cat_printf(out, "%s: No expiry\n", label);
        return;
    }

    furi_string_cat_printf(out, "%s: ", itso_date_expired(date, now) ? past_label : label);
    flipso_cat_date(out, date);
    furi_string_push_back(out, '\n');
}

void flipso_cat_datetime(FuriString* out, uint32_t dts) {
    flipso_cat_timestamp(out, itso_dts_to_unix(dts), true);
}

void flipso_cat_time(FuriString* out, uint32_t timestamp) {
    flipso_cat_timestamp(out, timestamp, true);
}

void flipso_cat_operator(FuriString* out, const Flipso* app, const char* label, uint16_t oid) {
    const char* name = flipso_operators_name(app->operators, oid);
    if(name) {
        furi_string_cat_printf(out, "%s: %s\n", label, name);
    } else {
        /* ITSO's operator register is not public, so an unknown OID is expected
         * rather than exceptional. Show the number so it can still be looked up. */
        furi_string_cat_printf(out, "%s: %04u\n", label, oid);
    }
}

void flipso_cat_location(
    FuriString* out,
    Flipso* app,
    const char* label,
    const ItsoLocation* location) {
    if(!location->valid) return;

    const char* place = NULL;
    switch(itso_location_code_kind(location)) {
    case ItsoLocCodeNlc:
        place = flipso_stations_name(app->stations, location->code);
        break;
    case ItsoLocCodeNaptan:
        place = flipso_naptan_stop(app->naptan, location->code);
        break;
    case ItsoLocCodeAtco:
        place = flipso_naptan_atco(app->naptan, location->code);
        break;
    default:
        break;
    }

    if(place) {
        /* LocDefType 216 is a service number and a stop together (TS 1000-1
         * table 42c), and only the stop half has just been named. Keep the
         * service, which itso_render_location left in front of the '@'. */
        const char* stop = location->def_type == 216 ? strchr(location->text, '@') : NULL;
        if(stop) {
            furi_string_cat_printf(
                out, "%s: %.*s @ %s\n", label, (int)(stop - location->text), location->text,
                place);
        } else {
            furi_string_cat_printf(out, "%s: %s\n", label, place);
        }
        return;
    }

    /* A location with no code of its own, or one no table on the card covers. */
    furi_string_cat_printf(out, "%s: %s\n", label, location->text);
}

const ItsoProduct* flipso_find_product(const Flipso* app, uint8_t typ) {
    for(uint8_t i = 0; i < app->card.product_count; i++) {
        /* On the card only. The screens this drives - the purse, the ID - are
         * about the card as it is, and a purse the card threw away last year
         * shown under "Pay as you go" would read as the balance it holds now.
         * The product list is where those are reachable, labelled as what they
         * are. */
        if(!app->card.products[i].on_card) continue;
        if(app->card.products[i].typ == typ) return &app->card.products[i];
    }
    return NULL;
}

void flipso_product_title(const ItsoProduct* product, char* out, size_t len) {
    /* PTYP is a scheme-private subtype; showing the bare number next to the name
     * reads as part of the name ("ITSO ID 29"), so it lives on the detail screen. */
    snprintf(out, len, "%s", itso_typ_name(product->typ));
}

const Icon* flipso_product_icon(const ItsoProduct* product) {
    switch(product->typ) {
    case ItsoTypStoredTravelRights:
    case ItsoTypChargeToAccount1:
    case ItsoTypChargeToAccount2:
        /* Anything that holds or spends money. */
        return &I_purse_10px;

    case ItsoTypId:
    case ItsoTypEntitlement:
        return &I_id_10px;

    case ItsoTypPeriodTicket:
    case ItsoTypPeriodCompact:
        /* Bounded by dates rather than by rides, so a calendar rather than a
         * ticket: that is the distinction a holder cares about. */
        return &I_pass_10px;

    case ItsoTypJourneyTicket:
    case ItsoTypReservationTicket:
    case ItsoTypCarnet:
    case ItsoTypMultiUse:
    case ItsoTypVoucher:
        return &I_ticket_10px;

    case ItsoTypLoyalty1:
    case ItsoTypLoyalty2:
        return &I_star_10px;

    default:
        return &I_tag_10px;
    }
}

void flipso_cat_card_number(FuriString* out, const char* isrn) {
    /* The ISRN prints as issuer, operator, then serial: 633597 1234 0012 3458. */
    static const uint8_t groups[] = {6, 4, 4, 4};
    size_t pos = 0;
    for(size_t g = 0; g < COUNT_OF(groups); g++) {
        if(g) furi_string_push_back(out, ' ');
        for(uint8_t i = 0; i < groups[g]; i++) {
            furi_string_push_back(out, isrn[pos++]);
        }
    }
}

void flipso_cat_money(FuriString* out, const char* label, const ItsoMoney* money) {
    if(!money->valid) return;
    char text[24];
    itso_format_money(money, text, sizeof(text));
    furi_string_cat_printf(out, "%s: %s\n", label, text);
}

void flipso_cat_product(FuriString* out, Flipso* app, const ItsoProduct* product, uint32_t now) {
    flipso_cat_operator(out, app, "Operator", product->oid);

    /* The retailer is only worth a row when it differs from the owner; on most
     * products the operator sells its own product and the two are the same. */
    if(product->has_retailer && product->retailer != product->oid) {
        flipso_cat_operator(out, app, "Sold by", product->retailer);
    }

    if(product->status != ItsoProductStatusUnknown) {
        furi_string_cat_printf(out, "Status: %s\n", itso_status_name(product->status));
    }

    flipso_cat_expiry(out, "Expires", "Expired", product->expiry, now);

    /* On a purse the start date gates auto-top-up rather than the product: TS
     * 1000-5 table 2 is explicit that stored travel rights may be spent at any
     * time. Labelling it "Valid from" would claim the purse was unusable. It is
     * shown with the top-up terms instead. */
    if(product->has_start && product->typ != ItsoTypStoredTravelRights) {
        furi_string_cat(out, "Valid from: ");
        flipso_cat_date(out, product->start);
        if(product->ticket.has_start_time && product->ticket.start_time) {
            furi_string_cat_printf(
                out, " %02u:%02u", product->ticket.start_time / 60,
                product->ticket.start_time % 60);
        }
        furi_string_push_back(out, '\n');
    } else if(product->ticket.valid_from_dts) {
        /* Revisions 1 and 2 of a period ticket hold a DTS here, not a DATE. */
        furi_string_cat(out, "Valid from: ");
        flipso_cat_datetime(out, product->ticket.valid_from_dts);
        furi_string_push_back(out, '\n');
    }

    if(product->has_end_date && product->end_date != product->expiry) {
        flipso_cat_expiry(out, "Valid to", "Ended", product->end_date, now);
    }

    /* The pass in use and the stock of unused passes expire separately, so a
     * season ticket can be live while the passes behind it have lapsed. */
    if(product->has_current_expiry) {
        flipso_cat_expiry(out, "Pass until", "Pass ended", product->current_expiry, now);
    }
    if(product->has_stored_expiry && product->stored_expiry != product->expiry) {
        flipso_cat_expiry(out, "Unused until", "Unused expired", product->stored_expiry, now);
    }

    flipso_cat_location(out, app, "From", &product->from);
    flipso_cat_location(out, app, "To", &product->to);

    /* A period ticket may leave both locations out, and then it is good wherever
     * its owner has configured that product type to be accepted - an operator's
     * whole network, typically. The card cannot say more than that, and saying
     * nothing reads as though Flipso had failed to decode them. */
    if(product->typ == ItsoTypPeriodTicket && product->ticket.valid && !product->from.valid &&
       !product->to.valid) {
        furi_string_cat(out, "Area: not on the card;\n  set by the operator\n  for this ticket type\n");
    }

    if(product->balance.valid) {
        flipso_cat_money(
            out, product->balance_is_spend ? "Spent" : "Balance", &product->balance);
    }

    const char* count_label = itso_count_name(product->count_kind);
    if(count_label) {
        furi_string_cat_printf(out, "%s: %lu\n", count_label, (unsigned long)product->count);
    }
    if(product->count_kind == ItsoCountTransactions && product->has_charge_period) {
        furi_string_cat_printf(
            out, "  of %u per %u wks\n", product->max_transactions, product->weeks_per_period);
    }

    if(product->ticket_used) {
        furi_string_cat(out, "Marked used\n");
    }
    if(product->auto_renew) {
        furi_string_cat(out, "Auto-renews\n");
    }
    if(product->priority_override) {
        furi_string_cat(out, "Used before other\nproducts\n");
    }

    /* A journey in progress: legs taken so far and the fare accumulated across
     * them, which is what a capped or multi-leg discount is computed from. */
    if(product->has_journey && (product->journey_legs || product->cumulative_fare.value)) {
        furi_string_cat_printf(out, "Journey legs: %u\n", product->journey_legs);
        flipso_cat_money(out, "Fare so far", &product->cumulative_fare);
    }
    if(product->has_transfers && product->transfers) {
        furi_string_cat_printf(out, "Transfers: %u\n", product->transfers);
    }

    if(product->has_name) {
        furi_string_cat_printf(out, "Name: %s\n", product->name);
    }
    if(product->has_dob) {
        furi_string_cat_printf(
            out, "Born: %02u/%02u/%04u\n", product->dob_day, product->dob_month,
            product->dob_year);
    }
    if(product->has_id_flags) {
        const char* gender = itso_gender_name(product->id_flags);
        if(gender) furi_string_cat_printf(out, "Gender: %s\n", gender);
        if(itso_id_companion(product->id_flags)) {
            furi_string_cat(out, "Companion travels free\n");
        }
        if(itso_id_personalised(product->id_flags)) {
            furi_string_cat(out, "Photo on card\n");
        }
    }

    if(product->has_entitlement) {
        furi_string_cat_printf(
            out, "Entitlement: %s\n", itso_entitlement_name(product->entitlement_code));
        if(product->concession_class) {
            furi_string_cat_printf(
                out, "Class: %s\n", itso_profile_name(product->concession_class));
        }
    }

    if(product->has_sub_expiry && product->sub_expiry != product->expiry) {
        furi_string_cat(out, "Entitlement to: ");
        flipso_cat_date(out, product->sub_expiry);
        furi_string_push_back(out, '\n');
    }

    if(product->has_passback && product->passback) {
        furi_string_cat_printf(out, "Passback: %u min\n", product->passback);
    }
}

/** One transaction, as three short lines. */
static void flipso_cat_value_record(
    FuriString* out,
    const ItsoProduct* product,
    const ItsoValueRecord* record) {
    /* Three short lines rather than one wide one: a date and time is sixteen
     * characters, which leaves nothing for what happened or for what the
     * balance became. */
    furi_string_cat_printf(out, "%s\n", itso_transaction_name(record->txn));
    furi_string_cat(out, "  ");
    flipso_cat_datetime(out, record->dts);
    furi_string_push_back(out, '\n');

    if(record->amount.valid) {
        char money[24];
        itso_format_money(&record->amount, money, sizeof(money));
        furi_string_cat_printf(out, "  %s\n", money);
    } else if(record->has_count) {
        /* The counter means whatever the product's type says it means, and it
         * means the same thing in every record. */
        const char* label = itso_count_name(product->count_kind);
        if(label) {
            furi_string_cat_printf(out, "  %s: %lu\n", label, (unsigned long)record->count);
        }
    }
}

void flipso_cat_value_history(FuriString* out, const ItsoProduct* product) {
    /* Index 0 is the live record, which the screen has already shown as the
     * balance or the counter, so a product whose group holds one written record
     * has no history to show rather than a section with one line in it.
     *
     * Unless there is no live record: a saved card whose product group did not
     * read still has whatever records the file kept, and every one of those is
     * earlier by definition. Skipping the first regardless would hide one. */
    uint8_t first = product->value_parsed ? 1 : 0;
    if(product->value_history_count <= first) return;

    /* Two sections rather than one. A card keeps two value records and writes
     * each new one over the oldest, so anything before them survives only
     * because a file remembered it - and running the two together would present
     * what the file knows as what the card says.
     *
     * A product the card has dropped has already said so for the whole screen,
     * and every record it has is from a file by definition, so it keeps one
     * list rather than being told the same thing twice. */
    const bool split = product->on_card;

    for(uint8_t section = 0; section < (split ? 2 : 1); section++) {
        const bool on_card = (section == 0);
        bool headed = false;

        for(uint8_t i = first; i < product->value_history_count; i++) {
            const ItsoValueRecord* record = &product->value_history[i];
            if(split && record->on_card != on_card) continue;

            if(!headed) {
                furi_string_cat(
                    out, on_card ? "\n\e#Earlier on card\n" : "\n\e#From past reads\n");
                headed = true;
            }
            flipso_cat_value_record(out, product, record);
        }
    }
}

void flipso_cat_purse_terms(FuriString* out, const ItsoProduct* product) {
    if(product->has_limits && product->max_value.valid && product->max_value.value) {
        flipso_cat_money(out, "Maximum", &product->max_value);
    }
    if(product->max_negative.valid && product->max_negative.value) {
        flipso_cat_money(out, "Overdraft", &product->max_negative);
    }

    if(product->has_top_up) {
        flipso_cat_money(out, "Tops up by", &product->top_up_amount);
        flipso_cat_money(out, "  when below", &product->top_up_threshold);
        furi_string_cat_printf(
            out, "  %s\n", product->auto_top_up ? "(enabled)" : "(not enabled)");
        if(product->auto_top_up_internal) furi_string_cat(out, "  from another purse\n");
        if(product->typ == ItsoTypStoredTravelRights && product->has_start) {
            furi_string_cat(out, "  not before ");
            flipso_cat_date(out, product->start);
            furi_string_push_back(out, '\n');
        }
    }

    if(product->has_deposit) {
        flipso_cat_money(out, "Deposit", &product->deposit);
        if(product->deposit_mop) {
            furi_string_cat_printf(
                out, "  paid by %s\n", itso_payment_name(product->deposit_mop));
        }
        if(product->deposit_vat) {
            furi_string_cat_printf(
                out, "  VAT %u.%02u%%\n", product->deposit_vat / 100, product->deposit_vat % 100);
        }
    }
}

void flipso_cat_isam(FuriString* out, const Flipso* app, const char* label, uint32_t isam) {
    /* Zero is what a record holds until a POST first writes it (TS 1000-2
     * clause 7.2.4.4), not an ISAM of operator zero. */
    if(!isam) return;
    flipso_cat_operator(out, app, label, itso_isam_oid(isam));
    furi_string_cat_printf(out, "  ISAM %08lX\n", (unsigned long)isam);
}

/** A deposit line and what the card says about paying it and getting it back. */
static void flipso_cat_deposit(
    FuriString* out,
    const char* label,
    const ItsoMoney* amount,
    uint8_t mop,
    uint16_t vat,
    bool refundable) {
    flipso_cat_money(out, label, amount);
    if(mop) furi_string_cat_printf(out, "  paid by %s\n", itso_payment_name(mop));
    if(vat) furi_string_cat_printf(out, "  VAT %u.%02u%%\n", vat / 100, vat % 100);
    furi_string_cat(out, refundable ? "  refundable\n" : "  refund at owner's say\n");
}

void flipso_cat_id_details(FuriString* out, const ItsoProduct* product) {
    if(product->typ != ItsoTypId || !product->body_parsed) return;

    /* On an English, Scottish or Welsh concessionary pass this is the pass
     * issuer - the council - by the schemes' own numbering, which is not
     * published; elsewhere it is whatever the owner uses it for. */
    if(product->has_cpicc) {
        furi_string_cat_printf(out, "Issuer code: %u\n  (0x%04X)\n", product->cpicc, product->cpicc);
    }
    if(product->has_holder_id) {
        furi_string_cat_printf(out, "Holder no.: %lu\n", (unsigned long)product->holder_id);
    }
    if(product->has_secondary_holder && product->secondary_holder_id) {
        furi_string_cat_printf(
            out, "2nd holder no.: %lu\n", (unsigned long)product->secondary_holder_id);
    }

    char code[3];
    if(itso_language_code(product->language, code)) {
        const char* name = itso_language_name(product->language);
        furi_string_cat_printf(out, "Language: %s\n", name ? name : code);
    }

    /* HalfDayOfWeek: two network-defined periods per day (annex A.10). A zero
     * mask selects nothing, which on a real card means the element is unused. */
    if(product->has_half_days && product->half_days) {
        char days[40];
        uint8_t mask = itso_half_days_mask(product->half_days);
        itso_format_days(mask, days, sizeof(days));
        furi_string_cat_printf(out, "Valid: %s\n", days);
        static const char* const names[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
        for(int day = 0; day < 7; day++) {
            uint8_t pair = (product->half_days >> (14 - 2 * day)) & 0x03;
            if(pair == 0x02) furi_string_cat_printf(out, "  %s 1st period only\n", names[day]);
            if(pair == 0x01) furi_string_cat_printf(out, "  %s 2nd period only\n", names[day]);
        }
        if(mask & ITSO_DOW_SPECIAL) furi_string_cat(out, "  and special days\n");
    }

    /* How a POST rounds a half or proportional fare for this holder. */
    if(product->rounding & ITSO_ROUNDING_ENABLED) {
        furi_string_cat_printf(
            out, "Fares rounded %s\n  to %s\n",
            (product->rounding & ITSO_ROUNDING_FLAG) ? "up" : "down",
            (product->rounding & ITSO_ROUNDING_VALUE) ? "5p" : "1p");
    }

    /* IDFlags bits 3, 6 and 7 (TS 1000-5 table 24). */
    if(product->has_id_flags && (product->id_flags & 0x08)) {
        furi_string_cat(out, "Details held in a URI\n  application\n");
    }
    if(product->has_deposit) {
        flipso_cat_deposit(
            out, "Deposit", &product->deposit, product->deposit_mop, product->deposit_vat,
            (product->id_flags & 0x40) != 0);
    }
    if(product->has_shell_deposit) {
        flipso_cat_deposit(
            out, "Card deposit", &product->shell_deposit, product->shell_deposit_mop,
            product->shell_deposit_vat, (product->id_flags & 0x80) != 0);
    }
}

void flipso_cat_capping(FuriString* out, Flipso* app, const ItsoProduct* product) {
    if(product->vgx_ref != 1 && product->vgx_ref != 2) return;
    /* Decoded here rather than held in ItsoProduct: four locations make it the
     * largest thing a product could carry, and at most one product has one. */
    size_t len = 0;
    const uint8_t* group = flipso_capture_product_group(app->capture, product->dir_index, &len);
    if(!group || !product->on_card) return;

    ItsoCapping* cap = malloc(sizeof(ItsoCapping));
    uint8_t valc = product->balance.valid ? product->balance.currency : 0;
    if(itso_parse_capping(group, len, app->card.sector_size, valc, cap)) {
        furi_string_cat(out, "\n\e#Fare capping\n");
        bool any = false;
        static const char* const rules[] = {NULL, "daily", "short period", "long period"};
        for(uint8_t a = 0; a < ITSO_CAP_ACCUMULATORS; a++) {
            const ItsoCapAccumulator* acc = &cap->acc[a];
            if(acc->rule == ItsoCapRuleNone) continue;
            any = true;
            furi_string_cat_printf(
                out, "Cap %u: %s\n", a + 1, acc->rule < COUNT_OF(rules) ? rules[acc->rule] : "other");
            if(acc->rule == ItsoCapRuleDay) {
                flipso_cat_money(out, "  today", &acc->day);
            } else {
                flipso_cat_money(out, "  so far", &acc->multiday);
                if(acc->day_count) furi_string_cat_printf(out, "  day %u\n", acc->day_count);
            }
            flipso_cat_money(out, "  uncapped", &acc->uncapped);
            if(acc->last_fare.valid && acc->last_fare.value) {
                flipso_cat_money(out, "  last fare", &acc->last_fare);
            }
            if(acc->cap_dts) {
                furi_string_cat(out, "  capped ");
                flipso_cat_datetime(out, acc->cap_dts);
                furi_string_push_back(out, '\n');
            }
            flipso_cat_location(out, app, "  at", &acc->location);
        }
        /* An unused product holds the structure with every rule at zero. */
        if(!any) furi_string_cat(out, "Not used yet\n");
        if(cap->strategy) furi_string_cat_printf(out, "Strategy: %u\n", cap->strategy);
    }
    free(cap);
}

void flipso_cat_ticket_terms(FuriString* out, const ItsoProduct* product) {
    const ItsoTicketTerms* t = &product->ticket;
    if(!t->valid) return;

    char text[48];
    /* Day filters are a period ticket's; a journey ticket has none to show. */
    if(product->typ == ItsoTypPeriodTicket) {
        uint8_t days = itso_ticket_days(t->valid_days, t->flags);
        itso_format_days(days, text, sizeof(text));
        furi_string_cat_printf(out, "Days: %s\n", text);
        itso_format_part_days(days, t->flags, text, sizeof(text));
        if(text[0]) furi_string_cat_printf(out, "  %s\n", text);
        if(!(days & ITSO_DOW_SPECIAL)) furi_string_cat(out, "  not public holidays\n");
        if(t->flags & ITSO_T22_OFF_PEAK_ONLY) furi_string_cat(out, "Off-peak only\n");
    }

    /* TYP 23's mode group: how rides are counted, and what joins legs into one
     * journey (TS 1000-5 table 35a). */
    if(t->has_mode_group) {
        static const char* const modes[] = {"each ride", "each journey", "simple ticket"};
        furi_string_cat_printf(
            out, "Counts: %s\n", t->mode < COUNT_OF(modes) ? modes[t->mode] : "other");
        if(t->mode == ItsoJourneyModeStoredJourneys) {
            /* TimeLimit counts 30 second steps between the start of one leg and
             * the next. */
            furi_string_cat_printf(
                out, "  up to %u changes,\n  %u min apart\n", t->max_transfers,
                t->time_limit / 2);
        }
        flipso_cat_money(out, "Ride value", &t->ride_value);
    }

    /* Below 1440 the time falls on the expiry date itself; from 1440 it is the
     * next morning, which is how a ticket covers the last buses after midnight.
     * Zero is left out: schemes use it for "the POST decides". */
    if(t->expiry_time) {
        uint16_t minutes = t->expiry_time;
        bool next_day = minutes >= 1440;
        if(next_day) minutes -= 1440;
        furi_string_cat_printf(
            out, "Ends: %02u:%02u %s\n", minutes / 60, minutes % 60,
            next_day ? "the day after expiry" : "on the expiry date");
    }

    if(t->has_pass_duration && t->pass_duration) {
        static const char* const units[] = {"day", "month", "quarter", "year"};
        const char* unit = t->duration_unit < COUNT_OF(units) ? units[t->duration_unit] : "unit";
        furi_string_cat_printf(
            out, "Pass length: %u %s%s\n", t->pass_duration, unit,
            t->pass_duration == 1 ? "" : "s");
    }

    /* AutoRenewQuantity1 counts passes in stored-pass mode and days otherwise
     * (rules 5 and 6 of TS 1000-5 clause 2.9.1.4). */
    if(product->auto_renew && t->renew_quantity) {
        const bool one = t->renew_quantity == 1;
        furi_string_cat_printf(
            out, "Renewal adds: %u %s\n", t->renew_quantity,
            product->stored_passes ? (one ? "pass" : "passes") : (one ? "day" : "days"));
    }
    if(product->auto_renew && t->has_stock_duration && t->stock_duration) {
        furi_string_cat_printf(out, "  and %u days' stock\n", t->stock_duration);
    }

    if(t->adults || t->children || t->concessions) {
        furi_string_cat(out, "Travellers:");
        const char* sep = " ";
        if(t->adults) {
            furi_string_cat_printf(out, "%s%u adult%s", sep, t->adults, t->adults == 1 ? "" : "s");
            sep = ", ";
        }
        if(t->children) {
            furi_string_cat_printf(
                out, "%s%u child%s", sep, t->children, t->children == 1 ? "" : "ren");
            sep = ", ";
        }
        if(t->concessions) {
            furi_string_cat_printf(
                out, "%s%u concession%s", sep, t->concessions, t->concessions == 1 ? "" : "s");
        }
        furi_string_push_back(out, '\n');
    }

    const char* travel_class = itso_class_name(t->travel_class);
    if(travel_class) furi_string_cat_printf(out, "Class: %s\n", travel_class);
    if(product->typ == ItsoTypPeriodTicket && (t->flags & ITSO_T22_TRANSFERABLE)) {
        furi_string_cat(out, "Transferable\n");
    }
    if(t->photocard) furi_string_cat_printf(out, "Photocard: %lu\n", (unsigned long)t->photocard);

    if(t->issue_date) {
        furi_string_cat(out, "Issued: ");
        flipso_cat_date(out, t->issue_date);
        furi_string_push_back(out, '\n');
    }
    if(t->amount_paid.valid) {
        flipso_cat_money(out, "Paid", &t->amount_paid);
        if(t->paid_mop) furi_string_cat_printf(out, "  by %s\n", itso_payment_name(t->paid_mop));
        if(t->vat) furi_string_cat_printf(out, "  VAT %u.%02u%%\n", t->vat / 100, t->vat % 100);
    }
}

void flipso_cat_last_transaction(FuriString* out, const ItsoProduct* product) {
    if(!product->value_parsed) return;

    furi_string_cat_printf(
        out, "Last action: %s\n", itso_transaction_name(product->value_txn));
    if(product->value_dts) {
        furi_string_cat(out, "  at ");
        flipso_cat_datetime(out, product->value_dts);
        furi_string_push_back(out, '\n');
    }
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static bool flipso_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    Flipso* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool flipso_back_event_callback(void* context) {
    furi_assert(context);
    Flipso* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void flipso_tick_event_callback(void* context) {
    furi_assert(context);
    Flipso* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

static Flipso* flipso_alloc(void) {
    Flipso* app = malloc(sizeof(Flipso));
    memset(app, 0, sizeof(Flipso));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&flipso_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, flipso_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, flipso_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, flipso_tick_event_callback, FLIPSO_TICK_PERIOD_MS);

    app->menu_view = flipso_menu_view_alloc();
    app->widget = widget_alloc();
    app->text_view = flipso_text_view_alloc();
    app->scan_view = flipso_scan_view_alloc();
    app->text_input = text_input_alloc();

    view_dispatcher_add_view(
        app->view_dispatcher, FlipsoViewScan, flipso_scan_view_get_view(app->scan_view));
    view_dispatcher_add_view(
        app->view_dispatcher, FlipsoViewMenu, flipso_menu_view_get_view(app->menu_view));
    view_dispatcher_add_view(
        app->view_dispatcher, FlipsoViewText, flipso_text_view_get_view(app->text_view));
    view_dispatcher_add_view(app->view_dispatcher, FlipsoViewWidget, widget_get_view(app->widget));
    view_dispatcher_add_view(
        app->view_dispatcher, FlipsoViewTextInput, text_input_get_view(app->text_input));

    app->reader = flipso_reader_alloc();
    app->capture = flipso_capture_alloc();
    app->loaded_path = furi_string_alloc();
    app->save_path = furi_string_alloc();
    app->operators = flipso_operators_alloc();
    app->stations = flipso_stations_alloc();
    app->naptan = flipso_naptan_alloc();

    return app;
}

static void flipso_free(Flipso* app) {
    furi_assert(app);

    /* Exiting the app does not unwind the scene stack by itself, so run the
     * current scene's on_exit to stop polling and release the LED and backlight. */
    scene_manager_stop(app->scene_manager);

    flipso_reader_free(app->reader);
    flipso_capture_free(app->capture);
    furi_string_free(app->loaded_path);
    furi_string_free(app->save_path);
    flipso_operators_free(app->operators);
    flipso_stations_free(app->stations);
    flipso_naptan_free(app->naptan);

    view_dispatcher_remove_view(app->view_dispatcher, FlipsoViewScan);
    view_dispatcher_remove_view(app->view_dispatcher, FlipsoViewMenu);
    view_dispatcher_remove_view(app->view_dispatcher, FlipsoViewText);
    view_dispatcher_remove_view(app->view_dispatcher, FlipsoViewWidget);
    view_dispatcher_remove_view(app->view_dispatcher, FlipsoViewTextInput);

    text_input_free(app->text_input);
    flipso_scan_view_free(app->scan_view);
    flipso_menu_view_free(app->menu_view);
    flipso_text_view_free(app->text_view);
    widget_free(app->widget);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t flipso_app(void* p) {
    UNUSED(p);

    Flipso* app = flipso_alloc();

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(app->scene_manager, FlipsoSceneScan);
    /* tools/flipper/flipctl waits for this line after a launch. The loader calls
     * an app running as soon as its thread exists, which is also true of one
     * stuck in startup behind the desktop; this line is only reached once the
     * first scene is up and Back and `loader close` will be heard. */
    FURI_LOG_I("Flipso", "UI ready");
    view_dispatcher_run(app->view_dispatcher);

    flipso_free(app);
    return 0;
}
