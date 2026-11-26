/**
 * @file  proto.h
 * @brief Text protocol between the stand and the PC (USB CDC, 115200 8N1
 *        nominal; lines end with '\n'). See docs/<lang>/protocol.md.
 */
#ifndef PROTO_H
#define PROTO_H
#include <stdint.h>

void proto_init(void);
void proto_rx(const uint8_t *buf, uint32_t len);   /* ISR-safe ring buffer   */
void proto_poll(void);                             /* parse complete lines   */
void proto_telemetry(void);                        /* send one #TEL line     */
void proto_event(const char *fmt, ...);            /* send "#EVT ..."        */
void proto_bus(const char *bus, const char *dir, const uint8_t *d, uint16_t n);
void proto_send(const char *s);                    /* raw line (adds \n)     */

#endif
