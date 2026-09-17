/**
 * @file flipso.c
 * @brief Application lifecycle and the formatting helpers the scenes share.
 */
#include "flipso.h"

#include "flipso_icons.h"

#include <furi_hal_rtc.h>
#include <datetime/datetime.h>
#include <locale/locale.h>

/* Ticks drive the scan animation; 100 ms is smooth enough and cheap. */
#define FLIPSO_TICK_PERIOD_MS 100

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

    if(itso_location_is_nlc(location)) {
        const char* station = flipso_stations_name(app->stations, location->code);
        if(station) {
            furi_string_cat_printf(out, "%s: %s\n", label, station);
            return;
        }
    }

    /* Not a rail code, or one the station table does not cover. */
    furi_string_cat_printf(out, "%s: %s\n", label, location->text);
}

const ItsoProduct* flipso_find_product(const Flipso* app, uint8_t typ) {
    for(uint8_t i = 0; i < app->card.product_count; i++) {
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

    view_dispatcher_add_view(
        app->view_dispatcher, FlipsoViewScan, flipso_scan_view_get_view(app->scan_view));
    view_dispatcher_add_view(
        app->view_dispatcher, FlipsoViewMenu, flipso_menu_view_get_view(app->menu_view));
    view_dispatcher_add_view(
        app->view_dispatcher, FlipsoViewText, flipso_text_view_get_view(app->text_view));
    view_dispatcher_add_view(app->view_dispatcher, FlipsoViewWidget, widget_get_view(app->widget));

    app->reader = flipso_reader_alloc();
    app->operators = flipso_operators_alloc();
    app->stations = flipso_stations_alloc();

    return app;
}

static void flipso_free(Flipso* app) {
    furi_assert(app);

    /* Exiting the app does not unwind the scene stack by itself, so run the
     * current scene's on_exit to stop polling and release the LED and backlight. */
    scene_manager_stop(app->scene_manager);

    flipso_reader_free(app->reader);
    flipso_operators_free(app->operators);
    flipso_stations_free(app->stations);

    view_dispatcher_remove_view(app->view_dispatcher, FlipsoViewScan);
    view_dispatcher_remove_view(app->view_dispatcher, FlipsoViewMenu);
    view_dispatcher_remove_view(app->view_dispatcher, FlipsoViewText);
    view_dispatcher_remove_view(app->view_dispatcher, FlipsoViewWidget);

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
    view_dispatcher_run(app->view_dispatcher);

    flipso_free(app);
    return 0;
}
