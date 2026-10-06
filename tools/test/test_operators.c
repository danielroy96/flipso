/*
 * Host-side test for operator names and card branding.
 *
 * Covers both layers: the built-in table in itso/, whose lookup is a binary
 * search and so depends on the entries staying sorted, and the user's
 * operators.txt, whose optional third column names the card a shell owner
 * issues. Built under ASan/UBSan, so a line that runs off the end of a field
 * buffer is a test failure rather than a subtle one on the device.
 */
#include "test.h"
#include "lookup/flipso_operators.h"
#include "itso/itso_operators.h"

#include <furi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- stream stubs ------------------------------------------------------- */

struct Stream {
    FILE* handle;
};

void* furi_record_open(const char* name) {
    (void)name;
    return NULL;
}

void furi_record_close(const char* name) {
    (void)name;
}

typedef struct Storage Storage;

struct Stream* file_stream_alloc(Storage* storage) {
    (void)storage;
    return calloc(1, sizeof(struct Stream));
}

bool file_stream_open(struct Stream* stream, const char* path, int access, int mode) {
    (void)access;
    (void)mode;
    stream->handle = fopen(path, "rb");
    return stream->handle != NULL;
}

void file_stream_close(struct Stream* stream) {
    if(stream->handle) fclose(stream->handle);
    stream->handle = NULL;
}

void stream_free(struct Stream* stream) {
    free(stream);
}

/* The firmware hands the caller the line terminator along with the line, which
 * is why the parser has to trim one. Reproduced here so it gets exercised. */
bool stream_read_line(struct Stream* stream, FuriString* line) {
    char buffer[256];
    if(!fgets(buffer, sizeof(buffer), stream->handle)) return false;
    furi_string_set_str(line, buffer);
    return true;
}

/* ---- test --------------------------------------------------------------- */

#define OPERATORS_PATH "stub_data_operators.txt"

static void same(const char* what, const char* got, const char* want) {
    int ok = want ? (got && strcmp(got, want) == 0) : (got == NULL);
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if(!ok) {
        printf("      got %s, wanted %s\n", got ? got : "(none)", want ? want : "(none)");
        failures++;
    }
}

static void write_operators(const char* text) {
    FILE* out = fopen(OPERATORS_PATH, "wb");
    if(!out) {
        perror(OPERATORS_PATH);
        exit(1);
    }
    fwrite(text, 1, strlen(text), out);
    fclose(out);
}

int main(void) {
    printf("Operator names and branding\n");

    /* --- The built-in table on its own. --- */
    remove(OPERATORS_PATH);

    /*
     * Lookup is a binary search, so an entry added out of order becomes
     * unreachable rather than wrong. Every OID the table carries is named here:
     * a new entry needs a line, and the search finds it only while the table
     * stays sorted.
     */
    static const uint16_t known[] = {78,  96,  109, 116, 125,  130,  143,  152,  162,
                                     163, 165, 196, 226, 246,  247,  262,  285,  287,
                                     288, 289, 303, 313, 1136, 8000, 8288, 8323, 24585};
    unsigned unreachable = 0;
    for(size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        if(!itso_operator_name(known[i])) unreachable++;
    }
    check("every built-in entry is reachable (the table is sorted)", unreachable == 0);

    same("a known operator is named", itso_operator_name(78), "Transport for London");
    /* The same organisation in its service operator role: ISAM 004E30F3. */
    same(
        "a service operator's number is named", itso_operator_name(24585), "Transport for London");
    same(
        "a council that issues concessionary passes is named",
        itso_operator_name(165),
        "Reading Borough Council");
    /* The pass carries the national scheme's artwork, but the menu is titled
     * with the council that issued it rather than a generic "ITSO Card". */
    same("and titles the pass it issues", itso_operator_brand(165), "Reading Borough Council");
    same("an unknown operator is not", itso_operator_name(4242), NULL);
    /* Shared central-product OIDs are named but brand nothing: they own
     * products on other issuers' cards and issue none of their own. */
    same(
        "a shared central-product OID is named", itso_operator_name(246), "SEFT Central Products");
    same("but it brands no card", itso_operator_brand(246), NULL);

    same("a shell owner that brands a card reports it", itso_operator_brand(226), "Freedom Pass");
    /* Neighbouring OIDs in the same town: the bus company brands its card, the
     * council's concessionary pass is covered above. */
    same("a bus operator is named", itso_operator_name(163), "Reading Buses");
    same("and brands its own card", itso_operator_brand(163), "Reading Buses");
    /* The brand is the app's title bar, so an operator we have only seen as a
     * product owner must not lend its name to somebody else's card. */
    same("an operator that brands no card reports none", itso_operator_brand(78), NULL);
    same("an unknown operator has no brand", itso_operator_brand(4242), NULL);
    /* STNR names the technology, not the card: the brand is what the operator
     * sells its card as. */
    same("a National Rail operator is named", itso_operator_name(262), "Chiltern Railways");
    same("and titles its card as it sells it", itso_operator_brand(262), "Chiltern Smartcard");
    /* Read from the card itself, whose season tickets belong to OID 246 above:
     * the shell names the operator, the products do not. */
    same(
        "a train operator seen as a shell owner",
        itso_operator_name(287),
        "Great Western Railway");
    same("titles its card as printed", itso_operator_brand(287), "GWR Touch");

    FlipsoOperators* operators = flipso_operators_alloc();
    same(
        "with no file, the built-in name is used",
        flipso_operators_name(operators, 78),
        "Transport for London");
    same(
        "with no file, the built-in brand is used",
        flipso_operators_brand(operators, 226),
        "Freedom Pass");
    flipso_operators_free(operators);

    /* --- The user's file layered over it. --- */
    write_operators("# a comment, and the blank line below it\n"
                    "\n"
                    "246,South Western Railway,South West Trains Smart\n"
                    "226,Greater London\n"
                    "999,Local Bus Co\n"
                    "  1000 ,  Padded Name  ,  Padded Brand  \n"
                    "1001,Name,This brand is far too long to fit in the buffer\n"
                    "1002,,No name at all\n"
                    "not a number,Ignored\n"
                    "1003 no comma at all\n");

    operators = flipso_operators_alloc();

    same(
        "the file names an operator the built-in table does not",
        flipso_operators_name(operators, 999),
        "Local Bus Co");
    same(
        "the file brands a card the built-in table does not",
        flipso_operators_brand(operators, 246),
        "South West Trains Smart");
    same(
        "an operator named without a brand still has no brand",
        flipso_operators_brand(operators, 999),
        NULL);

    /* A user correcting the name column should not silently lose the brand. */
    same(
        "a name-only line overrides the name",
        flipso_operators_name(operators, 226),
        "Greater London");
    same(
        "a name-only line keeps the built-in brand",
        flipso_operators_brand(operators, 226),
        "Freedom Pass");

    same(
        "padding around a field is trimmed",
        flipso_operators_name(operators, 1000),
        "Padded Name");
    same(
        "padding around a brand is trimmed too",
        flipso_operators_brand(operators, 1000),
        "Padded Brand");

    /* Truncated rather than refused: the user gets most of what they typed. */
    same(
        "an over-long brand is truncated to fit",
        flipso_operators_brand(operators, 1001),
        "This brand is far too long");

    same("a line with no name is rejected", flipso_operators_name(operators, 1002), NULL);
    same("a line with no comma is rejected", flipso_operators_name(operators, 1003), NULL);
    same(
        "an operator the file does not mention falls through",
        flipso_operators_name(operators, 78),
        "Transport for London");

    flipso_operators_free(operators);
    remove(OPERATORS_PATH);

    printf("%s\n", failures ? "FAILED" : "All operator tests passed");
    return failures ? 1 : 0;
}
