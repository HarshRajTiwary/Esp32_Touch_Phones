#pragma once
// The A7672S serial port: UART driver + optional CMUX multiplexer.
//
//   Raw mode : the UART carries one AT stream (start-up, and the fallback if
//              the module refuses AT+CMUX).
//   Mux mode : CMUX basic option. Channel 1 = AT commands + URCs,
//              channel 2 = data (AT for dialing, then PPP).
//
// A receive task owns the UART input and hands the bytes of each channel to
// the registered callback. Writes may come from any task.
#include <stddef.h>
#include <stdint.h>

namespace mport {

enum class Mode : uint8_t { Raw, Mux };
constexpr int kChanRaw = 0;   // raw mode (no multiplexer)
constexpr int kChanAt = 1;
constexpr int kChanData = 2;

// Called from the receive task with bytes of one channel.
typedef void (*RxFn)(int chan, const uint8_t* data, size_t len);

struct Config {
  int uart;
  int tx, rx, rts, cts;       // -1 = not used
  uint32_t baud;
};

bool begin(const Config& cfg, RxFn rx);
bool setBaud(uint32_t baud);
uint32_t baud();
Mode mode();

// Writes to a channel. In raw mode only kChanRaw/kChanAt are valid (same stream).
bool write(int chan, const void* data, size_t len);
bool writeStr(int chan, const char* s);

// Call after "AT+CMUX=0" returned OK: opens DLCI 0, 1 and 2 and switches to
// mux mode. Returns false (and stays in raw mode) if the module does not answer.
bool muxStart(uint32_t perChannelTimeoutMs);
// Closes the multiplexer (CLD) and returns to raw mode.
void muxStop();

// True once the module printed its plain-text boot banner while in mux mode,
// i.e. it restarted by itself. Cleared by muxStart()/clearRestartSeen().
bool restartSeen();
void clearRestartSeen();

// Discards buffered input (used before re-initialising).
void flushInput();

struct Stats {
  uint32_t rxBytes, txBytes;
  uint32_t frames, fcsErrors, garbage;
  bool peerFlowOff;            // module sent FCoff (stop sending data)
};
Stats stats();

}  // namespace mport
