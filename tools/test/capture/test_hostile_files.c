/**
 * @file test_hostile_files.c
 * @brief Files that are not what they claim, which must load as nothing rather than crash.
 */
#include "test_capture.h"

void hostile_files(void) {
    FlipsoCapture* capture = flipso_capture_alloc();

    check(
        "a file of another type is refused",
        !flipso_capture_parse_line(capture, "Filetype: Flipper NFC device"));
    check(
        "our own filetype is accepted",
        flipso_capture_parse_line(capture, "Filetype: Flipso card"));
    /* Lines come off the stream with their newline still attached. */
    check(
        "and is still accepted with the line ending on it",
        flipso_capture_parse_line(capture, "Filetype: Flipso card\r\n"));
    check(
        "a filetype that merely starts the same is refused",
        !flipso_capture_parse_line(capture, "Filetype: Flipso cardboard"));
    check(
        "a version from the future is refused",
        !flipso_capture_parse_line(capture, "Version: 99"));
    check(
        "a version that is not a number is refused",
        !flipso_capture_parse_line(capture, "Version: banana"));
    check("our own version is accepted", flipso_capture_parse_line(capture, "Version: 1"));

    /* Everything below is junk that must be survivable rather than fatal: these
     * files sit in a directory the user can edit. */
    static const char* junk[] = {
        "",
        "\n",
        ":",
        ": 00 11",
        "Shell:",
        "Shell: ",
        "Shell: 0",
        "Shell: 0011223",
        "Shell: ZZ",
        "Shell: 00 11 ZZ 22",
        "Shell 00 11",
        "AVeryLongKeyIndeedThatNobodyWrote: 00",
        "Product: 00 11",
        "Product x: 00 11",
        "Product 0: 00 11",
        "Product 99: 00 11",
        "Product 256: 00 11",
        "Product 4294967296: 00",
        "Read at:",
        "Read at: not a number",
        "Read at: 99999999999999999999",
        "   Shell   :   00 11 22   ",
    };
    bool fatal = false;
    for(size_t i = 0; i < sizeof(junk) / sizeof(junk[0]); i++) {
        if(!flipso_capture_parse_line(capture, junk[i])) fatal = true;
    }
    check("no junk line is treated as fatal", !fatal);

    /* Only the last of those carried a usable Shell, padded though it is. */
    check("a padded key still lands", flipso_capture_valid(capture));

    flipso_capture_free(capture);

    /* A deterministic sweep of random bytes through the line parser. Under
     * ASan this is what catches a read past the end of a key or a hex pair. */
    uint32_t seed = 0x9E3779B9u;
    capture = flipso_capture_alloc();
    for(int round = 0; round < 20000; round++) {
        char line[64];
        size_t len = 0;
        seed = seed * 1103515245u + 12345u;
        size_t want = (seed >> 16) % (sizeof(line) - 1);
        while(len < want) {
            seed = seed * 1103515245u + 12345u;
            char c = (char)(" :ShellDirectoryProductLogVersionFiletypeReadat0123456789ABCDEFxz\t\r"
                                [(seed >> 16) % 66]);
            line[len++] = c;
        }
        line[len] = '\0';
        flipso_capture_parse_line(capture, line);
        /* Keep it from filling up and refusing everything after the first few
         * hundred rounds, which would stop the sweep exercising anything. */
        if((round % 64) == 0) flipso_capture_reset(capture);
    }
    check("random lines survive the parser", 1);
    flipso_capture_free(capture);
}
