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
    /* The shell's own checksum, which is the only thing on the card Flipso can
     * actually verify: the data groups are sealed with keys it does not have,
     * so everything else on these screens is reported on the card's word. Both
     * outcomes are stated, and a mismatch shows its numbers, because the useful
     * thing to do with one is to report the card. TS 1000-2 clause 4.1.15. */
    if(card->secrc_checked) {
        if(card->secrc_valid) {
            furi_string_cat(text, "Checksum: verified\n");
        } else {
            furi_string_cat_printf(
                text, "Checksum: MISMATCH\n  stored %04X\n  computed %04X\n",
                card->secrc_stored, card->secrc_computed);
        }
    }
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

    /* Where this came from, for a card opened off the SD card. The read time
     * matters more than it looks: a balance is only true as of the tap that
     * wrote it, and a saved card carries no hint of its own age otherwise. */
    if(!furi_string_empty(app->loaded_path)) {
        FuriString* name = furi_string_alloc();
        flipso_saved_name(name, furi_string_get_cstr(app->loaded_path));
        furi_string_cat_printf(text, "\n\e#Saved card\nName: %s\n", furi_string_get_cstr(name));
        furi_string_free(name);

        uint32_t read_at = flipso_capture_time(app->capture);
        if(read_at) {
            furi_string_cat(text, "Read: ");
            flipso_cat_time(text, read_at);
            furi_string_push_back(text, '\n');
        }
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
