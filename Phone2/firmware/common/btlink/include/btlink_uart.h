#pragma once
// UART transport for the BT link. Used unchanged on both MCUs:
// a receive task decodes frames and hands them to the callback; send() may be
// called from any task (it is serialised by a mutex).
#include <stddef.h>
#include <stdint.h>

#include "btlink_codec.h"

namespace btl {

typedef void (*FrameHandler)(const Frame& f);

struct UartConfig {
  int port;         // UART_NUM_x
  int txPin, rxPin;
  uint32_t baud;
};

bool start(const UartConfig& cfg, FrameHandler handler);
bool send(Msg type, const void* payload = nullptr, size_t len = 0);
template <class T>
bool sendMsg(Msg type, const T& msg) { return send(type, &msg, sizeof msg); }

uint32_t rxFrames();
uint32_t txFrames();
uint32_t rxErrors();         // CRC + framing errors
uint32_t lastRxMs();         // plogMillis() of the last good frame, 0 = never

}  // namespace btl
