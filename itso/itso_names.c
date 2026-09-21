/**
 * @file itso_names.c
 * @brief Human-readable names for the coded values carried in an ITSO Shell.
 *
 * Code lists are reproduced from ITSO TS 1000-5 Annex A, which in turn quotes
 * EN 1545-1. Strings are deliberately short: the Flipper screen is 128px wide.
 */
#include "itso.h"

const char* itso_typ_name(uint8_t typ) {
    switch(typ) {
    case 0:
        return "Private app";
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
        return "Loyalty 2";
    case ItsoTypPeriodTicket:
        return "Period ticket";
    case ItsoTypJourneyTicket:
        return "Journey ticket";
    case ItsoTypReservationTicket:
        return "Reserved journey";
    case ItsoTypVoucher:
        return "Voucher";
    case ItsoTypTolling:
        return "Tolling";
    case ItsoTypPeriodCompact:
        return "Period pass";
    case ItsoTypCarnet:
        return "Carnet";
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
    switch(code) {
    case 0:
        return "Event";
    case 1:
        return "Sale";
    case 2:
        return "Validation (out)";
    case 3:
        return "Undo";
    case 4:
        return "Top up";
    case 5:
        return "Auto top up";
    case 6:
        return "Validation (rtn)";
    case 7:
        return "Fare deducted";
    case 8:
        return "Exchange";
    case 9:
        return "Loyalty redeemed";
    case 10:
        return "Refund";
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
        return "Bad IIN";
    case ItsoShellRejectCompact:
        return "Compact shell";
    case ItsoShellRejectGeometry:
        return "Bad geometry";
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
        return "IEP";
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

const char* itso_count_name(ItsoCountKind kind) {
    switch(kind) {
    case ItsoCountRides:
        return "Rides left";
    case ItsoCountPasses:
        return "Passes left";
    case ItsoCountTransactions:
        return "Charges used";
    case ItsoCountPoints:
        return "Points";
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
