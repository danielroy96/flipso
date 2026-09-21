/**
 * @file flipso_scene_product.c
 * @brief Everything Flipso decoded about one product.
 */
#include "../flipso.h"

void flipso_scene_product_on_enter(void* context) {
    Flipso* app = context;

    if(app->selected_product >= app->card.product_count) {
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    const ItsoProduct* product = &app->card.products[app->selected_product];
    FuriString* text = furi_string_alloc();

    char title[32];
    flipso_product_title(product, title, sizeof(title));
    furi_string_cat_printf(text, "\e#%s\n", title);

    flipso_cat_product(text, app, product, flipso_now());

    flipso_cat_last_transaction(text, product);
    flipso_cat_purse_terms(text, product);
    flipso_cat_value_history(text, product);

    if(product->value_group && !product->value_parsed) {
        furi_string_cat(text, "Has a value record that\ncould not be read.\n");
    }
    if(product->foreign_iin) {
        furi_string_cat(text, "Operator is on another\nnetwork.\n");
    }

    furi_string_cat(text, "\n\e#Technical\n");
    furi_string_cat_printf(text, "Type: %u.%u\n", product->typ, product->ptyp);
    furi_string_cat_printf(text, "Operator ID: %u\n", product->oid);
    if(product->oid_extended) {
        furi_string_cat_printf(
            text, "  (extended, raw %u)\n", (unsigned)(product->oid & 0x1FFF));
    }
    furi_string_cat_printf(text, "Directory entry: %u\n", product->dir_index);
    if(product->body_parsed) {
        furi_string_cat_printf(text, "Format revision: %u\n", product->format_rev);
        /* The bitmap says which optional elements the dataset carries, which is
         * the first thing you need when a field is missing unexpectedly. */
        furi_string_cat_printf(text, "Elements: 0x%02X\n", product->bitmap);

        if(product->has_remove_date) {
            /* 255 is the documented "only the product owner may remove this". */
            if(product->remove_date == 255) {
                furi_string_cat(text, "Removable: owner only\n");
            } else {
                furi_string_cat_printf(
                    text, "Removable: %u days\n  after expiry\n", product->remove_date);
            }
        }
        if(product->has_iin) {
            furi_string_cat_printf(text, "Owner network: %06lu\n", (unsigned long)product->iin);
        }

        /* The instance identity. Nothing else in the shell distinguishes one
         * copy of a product from another, so this is what a scheme would quote
         * back when asked about this particular ticket. */
        if(product->instance_valid) {
            furi_string_cat_printf(text, "Created by ISAM:\n  %08lX #%lu\n",
                (unsigned long)product->isam_id, (unsigned long)product->isam_seq);
            if(product->iteration) {
                furi_string_cat_printf(text, "Iteration: %u\n", product->iteration);
            }
            furi_string_cat_printf(text, "Seal key: %u\n", product->key_id);
        }

        if(product->value_parsed) {
            furi_string_cat_printf(text, "Value writes: %u\n", product->value_ts);
            furi_string_cat_printf(
                text, "Last POST ISAM:\n  %08lX\n", (unsigned long)product->value_isam);
            if(product->value_action_seq) {
                furi_string_cat_printf(text, "Action seq: %u\n", product->value_action_seq);
            }
        }
    } else {
        /* Either the sector read failed or this is a type Flipso reports from
         * the directory entry alone. */
        furi_string_cat(text, "Detail not decoded\n");
    }

    flipso_text_view_set_text(app->text_view, furi_string_get_cstr(text));
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipsoViewText);

    furi_string_free(text);
}

bool flipso_scene_product_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void flipso_scene_product_on_exit(void* context) {
    Flipso* app = context;
    flipso_text_view_set_text(app->text_view, "");
}
