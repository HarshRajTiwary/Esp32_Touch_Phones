#pragma once
// USB serial debug console (115200 baud). Type "help" for the command list.
namespace console {
void begin();
void poll();  // call from the main loop
}
