#pragma once
#include "btlink_proto.h"

// Internal interfaces of the Bluetooth helper's main component.
namespace helper {
void startConsole();          // USB serial test console (helper_console.cpp)
void markStatusDirty();
bool phoneLinked();
}  // namespace helper

// Keeps the last scan results for the console's "list" command.
void helperNoteScanResult(const btl::DeviceMsg& d);
