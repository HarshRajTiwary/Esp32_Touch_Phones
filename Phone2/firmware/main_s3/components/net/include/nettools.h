#pragma once
// Small helpers that use the 4G link: network time (SNTP) and a connectivity test.
#include <stdint.h>

namespace nettools {

// Starts SNTP once the PPP link has an IP address (backup for network NITZ time).
void begin();

// Runs an HTTP connectivity check (plain HTTP 204 endpoint) in a background
// task. The result arrives as the text returned by lastTestResult().
void startConnectivityTest();
bool testRunning();
const char* lastTestResult();     // "" until a test finished

}  // namespace nettools
