#pragma once
// =============================================================================
//  Phone2 (ESP32-S3 phone) - timings and tunables. Pins live in board_config.h.
// =============================================================================

// ---- Modem start-up state machine (every state has a timeout + retry count) ----
#define MODEM_PWRKEY_ON_MS          1000    // "press" time to switch on (off needs >= 2.5 s)
#define MODEM_PWRKEY_OFF_MS         3000    // "press" time to switch off
#define MODEM_OFF_SETTLE_MS         3000    // SIMCom: >= 2 s between power-off and power-on
#define MODEM_PROBE_MS              3000    // is it already on? (before pressing PWRKEY)
#define MODEM_BOOT_TIMEOUT_MS       25000   // first OK after PWRKEY (datasheet: ~8 s typical)
#define MODEM_SIM_TIMEOUT_MS        15000   // SIM reports NOT READY for a few s after boot
#define MODEM_SEARCH_WARN_MS        120000  // log "still searching" after this
#define MODEM_MAX_START_ATTEMPTS    3       // then back off (MODEM_RETRY_BACKOFF_MS)
#define MODEM_RETRY_BACKOFF_MS      30000
#define MODEM_AT_TIMEOUTS_RESET     4       // consecutive command timeouts -> recovery
#define MODEM_CMUX_N1               1500    // max CMUX frame payload (the A7672S default)

// ---- Modem polling (URCs do the real work; polls only refresh slow values) ----
#define MODEM_POLL_IDLE_MS          20000   // CSQ / registration when registered
#define MODEM_POLL_SEARCH_MS        3000    // ... while searching
#define MODEM_POLL_SLOW_MS          60000   // battery, operator, CPSI, temperature
#define MODEM_SMS_SWEEP_MS          300000  // re-read SIM storage in case a +CMTI was missed
#define MODEM_CLOCK_POLL_MS         1800000
#define MODEM_CALL_POLL_MS          3000    // AT+CLCC safety poll during calls
#define SMS_CONCAT_TIMEOUT_MS       600000  // give up waiting for missing parts of a long SMS

// ---- Cellular data (PPP) ----
#define DATA_DIAL_TIMEOUT_MS        30000
#define DATA_PPP_TIMEOUT_MS         40000   // LCP + IPCP must finish in this time
#define DATA_BACKOFF_MIN_MS         5000
#define DATA_BACKOFF_MAX_MS         120000
#define DEFAULT_APN                 "airtelgprs.com"   // only a default - editable in Settings > Mobile data

// ---- Hotspot ----
#define HOTSPOT_MAX_CLIENTS_DEFAULT 4
#define HOTSPOT_CHANNEL             6

// ---- UI / storage ----
#define STORAGE_SAVE_DELAY_MS       1500
#define UI_TOUCH_POLL_MS            15
#define UI_IDLE_FRAME_MS            1000
#define TOAST_MS                    3500
#define FS_PARTITION_LABEL          "storage"
#define FS_BASE_PATH                "/lfs"
