/**
 * @file flipso_scene_id.c
 * @brief Holder identity and entitlements (IPE types 16 and 14).
 */
#include "../flipso.h"

void flipso_scene_id_on_enter(void* context) {
    Flipso* app = context;
    const ItsoCard* card = &app->card;
    uint32_t now = flipso_now();

    FuriString* text = furi_string_alloc();
    uint8_t found = 0;

    for(uint8_t i = 0; i < card->product_count; i++) {
        const ItsoProduct* product = &card->products[i];
        if(product->typ != ItsoTypId && product->typ != ItsoTypEntitlement) continue;

        if(found) furi_string_cat(text, "\n");
        furi_string_cat_printf(text, "\e#%s\n", itso_typ_name(product->typ));
        found++;

        if(product->has_name) {
            furi_string_cat_printf(text, "Name: %s\n", product->name);
        } else if(product->typ == ItsoTypId) {
            /* The name fields are optional and often left off cards that carry a
             * printed photo ID instead. */
            furi_string_cat(text, "Name: not stored\n");
        }

        if(product->has_dob) {
            furi_string_cat_printf(
                text, "Born: %02u/%02u/%04u\n", product->dob_day, product->dob_month,
                product->dob_year);
        }

        if(product->has_id_flags) {
            const char* gender = itso_gender_name(product->id_flags);
            if(gender) furi_string_cat_printf(text, "Gender: %s\n", gender);
        }

        flipso_cat_operator(text, app, "Operator", product->oid);

        flipso_cat_expiry(text, "Expires", "Expired", product->expiry, now);

        if(product->has_entitlement) {
            furi_string_cat_printf(
                text, "Entitlement: %s\n", itso_entitlement_name(product->entitlement_code));
            /* Profile code zero is "unspecified", which is a row that tells the
             * holder nothing. The product screen omits it for the same reason. */
            if(product->concession_class) {
                furi_string_cat_printf(
                    text, "Class: %s\n", itso_profile_name(product->concession_class));
            }
        }

        if(product->has_start) {
            furi_string_cat(text, "Valid from: ");
            flipso_cat_date(text, product->start);
            furi_string_push_back(text, '\n');
        }

        if(product->has_sub_expiry) {
            flipso_cat_expiry(text, "Valid to", "Expired", product->sub_expiry, now);
        }

        flipso_cat_location(text, app, "From", &product->from);
        flipso_cat_location(text, app, "To", &product->to);

        /* The two IDFlags that change what happens at the gate. */
        if(product->has_id_flags) {
            if(itso_id_companion(product->id_flags)) {
                furi_string_cat(text, "Companion travels free\n");
            }
            furi_string_cat_printf(
                text, "Photo on card: %s\n",
                itso_id_personalised(product->id_flags) ? "yes" : "no");
        }

        if(product->has_passback && product->passback) {
            furi_string_cat_printf(text, "Passback: %u min\n", product->passback);
        }

        furi_string_cat_printf(text, "Status: %s\n", itso_status_name(product->status));
    }

    if(!found) {
        furi_string_cat(text, "\e#ID\nNo identity product on\nthis card.\n");
    }

    flipso_text_view_set_text(app->text_view, furi_string_get_cstr(text));
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipsoViewText);

    furi_string_free(text);
}

bool flipso_scene_id_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void flipso_scene_id_on_exit(void* context) {
    Flipso* app = context;
    flipso_text_view_set_text(app->text_view, "");
}
