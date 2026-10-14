/**
 * @file flipso_cmd11.h
 * @brief Transport for ITSO customer media definition 11, Programmable
 * Intelligent Media (TS 1000-10 clause 12).
 *
 * CMD11 is an applet on a JavaCard-style platform: SELECT answers with the
 * shell, the directory and an OID list in its FCI, and the products and the
 * log come back whole from commands of its own - ReadIPE, ReadVRG and ReadLog -
 * which answer before any authentication. Only reads are ever sent: never
 * Authenticate, an Update or EndSession.
 *
 * It answers the same SELECT as CMD2, so it is reached from the CMD2 transport
 * once the shell in the FCI says FVC 11, and reads on with that transport's
 * buffers.
 */
#pragma once

#include "flipso_reader.h"
#include "flipso_cmd2.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Read the rest of a CMD11 card whose SELECT has just answered.
 *
 * @param cmd2    the CMD2 context, still holding the FCI in its response.
 * @param card    with the shell from that FCI decoded into it; decoded again,
 *                from @p capture, before this returns.
 * @param capture holding that shell; the rest of the card's blocks are added.
 */
FlipsoReaderStatus flipso_cmd11_read(
    FlipsoCmd2* cmd2,
    Iso14443_4aPoller* poller,
    ItsoCard* card,
    FlipsoCapture* capture);

#ifdef __cplusplus
}
#endif
