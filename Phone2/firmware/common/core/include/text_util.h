#pragma once
// String, UTF-8, phone-number and time helpers. Pure C++ (host-testable).
#include <stddef.h>
#include <stdint.h>

namespace txt {

// ---- generic strings -------------------------------------------------------
void copy(char* dst, const char* src, size_t cap);          // always NUL-terminates
bool startsWith(const char* s, const char* prefix);
bool ieq(const char* a, const char* b);                      // ASCII case-insensitive
int  icmp(const char* a, const char* b);
bool icontains(const char* hay, const char* needle);
int  hexVal(char c);                                         // -1 if not hex
void trim(char* s);
uint32_t hash(const char* s);

// ---- UTF-8 -----------------------------------------------------------------
// Decodes one code point and advances p. Returns -1 at end of string.
// Invalid bytes decode as U+FFFD.
int32_t utf8Next(const char*& p);
int  utf8Encode(uint32_t cp, char* out);                      // returns 1..4 bytes
size_t utf8Append(char* dst, size_t used, size_t cap, uint32_t cp);  // returns new length
size_t utf8Truncate(char* s, size_t maxBytes);                // cut on a char boundary
size_t utf8Length(const char* s);                             // code points
void utf8PopBack(char* s);                                    // delete last code point

// Converts UTF-8 text to the ASCII subset our fonts can draw.
// Common symbols are transliterated (₹ -> Rs., quotes, dashes, accents).
// Returns true if anything had to be replaced by '?'.
bool toDisplay(const char* utf8, char* out, size_t cap);

// ---- phone numbers ---------------------------------------------------------
void digitsOnly(const char* in, char* out, size_t cap);
bool numbersMatch(const char* a, const char* b);             // compares last 10 digits
bool looksLikeNumber(const char* s);                          // only +, digits, spaces, -
void sanitizeDial(const char* in, char* out, size_t cap);     // keep 0-9 + * #
bool isUssdCode(const char* s);                               // *123# style
void maskNumber(const char* in, char* out, size_t cap);       // +91******3210
void initials(const char* name, char* out3);                  // "Ravi Kumar" -> "RK"

// ---- time (local epoch seconds, treated as UTC arithmetic) ------------------
uint32_t makeEpoch(int year, int mon, int day, int hour, int min, int sec);
void splitEpoch(uint32_t t, int* year, int* mon, int* day, int* hour, int* min, int* sec, int* wday);
void fmtClock(uint32_t t, char* out);                         // "14:05"      (>= 6 bytes)
void fmtDayDate(uint32_t t, char* out);                       // "Sat, 26 Sep" (>= 16 bytes)
void fmtWhen(uint32_t t, uint32_t now, char* out, size_t cap);// "14:05" / "Yesterday" / "Mon" / "26 Sep"
void fmtDuration(uint32_t secs, char* out);                   // "0:42", "1:02:03" (>= 16 bytes)
extern const char* const kMonthShort[12];
extern const char* const kDayShort[7];

}  // namespace txt
