/**
 * @file flipso_scene_card.c
 * @brief Card number, expiry and issuer, plus the platform details underneath.
 */
#include "../flipso.h"
#include "../itso/itso_operators.h"

void flipso_scene_card_on_enter(void* context) {
    Flipso* app = context;
    const ItsoCard* card = &app->card;
    uint32_t now = flipso_now();

    FuriString* text = furi_string_alloc();

    /* The blocking indicator is a property of the whole shell, so it comes
     * before anything else on the screen: once it is set a POST rejects the
     * card, however valid the products further down still look.
     * TS 1000-2 clause 5.1.2. */
    if(card->shell_blocked) {
        furi_string_cat(
            text,
            "\e#Blocked\n"
            "This card has been stopped by its issuer. Readers will reject it, "
            "even where the products below are still in date.\n\n");
    }

    furi_string_cat(text, "\e#Card number\n");
    flipso_cat_card_number(text, card->isrn);
    furi_string_cat(text, "\n");
    if(!card->isrn_check_ok) {
        furi_string_cat(text, "Check digit does not match\n");
    }

    furi_string_cat(text, "\n\e#Expiry\n");
    flipso_cat_expiry(text, "Expires", "Expired", card->expiry, now);

    furi_string_cat(text, "\n\e#Issuer\n");
    /* Named or not, the shell owner's number is shown here, which the shared
     * helper hides once it can name one. This is the operator that issued the
     * card and so the one that brands it, and the number is what a user needs
     * in order to add their card's branding to their operators file. */
    const char* owner = flipso_operators_name(app->operators, card->oid);
    if(owner) {
        furi_string_cat_printf(text, "Operator: %s (%04u)\n", owner, card->oid);
    } else {
        furi_string_cat_printf(text, "Operator: %04u\n", card->oid);
    }
    /* The IIN names the network the shell belongs to; every ITSO shell uses
     * ITSO's own registered issuer number. */
    const char* network = itso_iin_name(card->iin);
    if(network) {
        furi_string_cat_printf(text, "Network: %s\n", network);
    } else {
        furi_string_cat_printf(text, "Network: %.6s\n", card->isrn);
    }
    /* The good case is stated rather than left to silence, because nothing else
     * on the screen separates a card the issuer is happy with from one whose
     * directory we never managed to read. */
    if(card->dir_valid && !card->shell_blocked) {
        furi_string_cat(text, "Status: Active\n");
    }
    if(card->mcrn_present && card->mcrn[0]) {
        furi_string_cat_printf(text, "Card ref: %s\n", card->mcrn);
    }

    furi_string_cat(text, "\n\e#Platform\n");
    const char* media = (card->fvc == 7) ? "DESFire (CMD7)" :
                        (card->fvc == 12) ? "DESFire (CMD12)" :
                                            "Other";
    furi_string_cat_printf(text, "Media: %s\n", media);
    furi_string_cat_printf(text, "Shell revision: %u\n", card->format_rev);
    furi_string_cat_printf(text, "Keys: KSC %u, KVC %u\n", card->ksc, card->kvc);
    furi_string_cat_printf(
        text, "Layout: %u x %u bytes\n", card->sector_count, card->sector_size);
    furi_string_cat_printf(
        text, "Directory: %u entries\n", card->dir_entries);
    /* The directory sequence number is how a CMD2 card's two directory copies
     * are told apart, and it counts every change made to the shell. */
    if(card->dir_valid) {
        furi_string_cat_printf(text, "Dir sequence: %u\n", card->dir_sequence);
    }

    flipso_text_view_set_text(app->text_view, furi_string_get_cstr(text));
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipsoViewText);

    furi_string_free(text);
}

bool flipso_scene_card_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void flipso_scene_card_on_exit(void* context) {
    Flipso* app = context;
    flipso_text_view_set_text(app->text_view, "");
}
