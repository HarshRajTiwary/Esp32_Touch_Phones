// Touch calibration, battery estimate, network input rules and the storage of
// contacts / call history / messages (through the PC storage backend).
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#include <vector>

#include "battery.h"
#include "call_log.h"
#include "contacts.h"
#include "messages.h"
#include "net_rules.h"
#include "plog.h"
#include "storage.h"
#include "test.h"
#include "touch.h"

bool hostFileExists(const char* path);
void hostRemoveFile(const char* path);

// ---------------------------------------------------------------------------
// The panel's raw touch axes are often swapped / mirrored relative to the screen;
// the 4-point affine calibration must recover any such mapping.
static void testTouchCalibration() {
  const int16_t scr[5][2] = {{24, 24}, {216, 24}, {216, 296}, {24, 296}, {120, 160}};
  struct Case {
    const char* name;
    float a, b, c, d, e, f;  // screen -> raw: rx = a*x + b*y + c, ry = d*x + e*y + f
  };
  const Case cases[] = {
      {"straight", 15.5f, 0, 220, 0, 11.4f, 250},
      {"mirrored x", -15.5f, 0, 3900, 0, 11.4f, 250},
      {"axes swapped", 0, 11.8f, 190, -15.9f, 0, 3880},
      {"slightly rotated", 15.2f, 0.9f, 230, -0.7f, 11.3f, 300},
  };
  for (const Case& k : cases) {
    int16_t raw[5][2];
    for (int i = 0; i < 5; ++i) {
      raw[i][0] = (int16_t)lrintf(k.a * scr[i][0] + k.b * scr[i][1] + k.c);
      raw[i][1] = (int16_t)lrintf(k.d * scr[i][0] + k.e * scr[i][1] + k.f);
    }
    float cal[6];
    CHECK(touch::computeCalibration(raw, scr, 4, cal));
    int16_t x, y;
    touch::mapRaw(cal, raw[4][0], raw[4][1], &x, &y);  // the 5th point was not used for the fit
    if (abs(x - scr[4][0]) > 1 || abs(y - scr[4][1]) > 1) printf("  touch case '%s': got %d,%d\n", k.name, x, y);
    CHECK(abs(x - scr[4][0]) <= 1 && abs(y - scr[4][1]) <= 1);
  }
  // Collinear points cannot define a calibration
  const int16_t line[4][2] = {{100, 100}, {200, 200}, {300, 300}, {400, 400}};
  const int16_t lineScr[4][2] = {{10, 10}, {20, 20}, {30, 30}, {40, 40}};
  float cal[6];
  CHECK(!touch::computeCalibration(line, lineScr, 4, cal));
  // Results are clamped to the screen
  const float id[6] = {1, 0, 0, 0, 1, 0};
  int16_t x, y;
  touch::mapRaw(id, -50, 5000, &x, &y);
  CHECK_EQ(x, 0);
  CHECK_EQ(y, 319);
}

// ---------------------------------------------------------------------------
static void testBattery() {
  CHECK_EQ(battery::percentFromMv(0), -1);
  CHECK_EQ(battery::percentFromMv(3200), 0);
  CHECK_EQ(battery::percentFromMv(3300), 0);
  CHECK_EQ(battery::percentFromMv(3820), 50);
  CHECK_EQ(battery::percentFromMv(4180), 100);
  CHECK_EQ(battery::percentFromMv(4300), 100);
  int prev = -1;
  bool monotonic = true;
  for (int mv = 3300; mv <= 4200; mv += 5) {
    int p = battery::percentFromMv(mv);
    if (p < prev) monotonic = false;
    prev = p;
  }
  CHECK(monotonic);
  battery::Estimator est;
  CHECK_EQ(est.update(0), -1);
  CHECK_EQ(est.update(3820), 50);
  int p = est.update(3800);  // small dip (transmit burst): shown value does not jitter
  CHECK_EQ(p, 50);
  for (int i = 0; i < 20; ++i) p = est.update(3600);
  CHECK(p <= 15);
  CHECK(!est.likelyCharging());
  for (int i = 0; i < 12; ++i) est.update(3700 + i * 40);
  CHECK(est.likelyCharging());
}

// ---------------------------------------------------------------------------
static void testNetRules() {
  const char* why = nullptr;
  CHECK(netrules::atSafe("airtelgprs.com"));
  CHECK(netrules::atSafe(""));
  CHECK(!netrules::atSafe("a\"b"));
  CHECK(!netrules::atSafe("a\\b"));
  CHECK(!netrules::atSafe("line\rAT+CPOF"));
  CHECK(!netrules::atSafe(nullptr));
  CHECK(netrules::validApn("airtelgprs.com", &why));
  CHECK(netrules::validApn("jionet", &why));
  CHECK(!netrules::validApn("", &why));
  CHECK(!netrules::validApn("bad apn", &why));
  CHECK(!netrules::validApn("x\",\"y", &why));
  CHECK(netrules::validSsid("Phone2-1A2B", &why));
  CHECK(!netrules::validSsid("", &why));
  CHECK(!netrules::validSsid("123456789012345678901234567890123", &why));  // 33 chars
  CHECK(!netrules::validWpaPass("1234567", &why));
  CHECK(netrules::validWpaPass("12345678", &why));
  CHECK(!netrules::validWpaPass(std::string(64, 'a').c_str(), &why));
  CHECK_EQ(netrules::nextBackoff(0, 5000, 120000), 5000);
  CHECK_EQ(netrules::nextBackoff(5000, 5000, 120000), 10000);
  CHECK_EQ(netrules::nextBackoff(80000, 5000, 120000), 120000);
  CHECK_EQ(netrules::nextBackoff(120000, 5000, 120000), 120000);
}

// ---------------------------------------------------------------------------
static void testContactsStore() {
  storage::begin();
  hostRemoveFile("/contacts.bin");
  contacts::load();
  CHECK_EQ(contacts::count(), 0);
  contacts::add("Rahul", "+919876543210");
  contacts::add("anita", "9812345678");
  int iz = contacts::add("Zoya", "+919000000001");
  CHECK_EQ(contacts::count(), 3);
  CHECK_EQ(iz, 2);
  CHECK_STR(contacts::at(0).name, "anita");  // sorted, case-insensitive
  CHECK_STR(contacts::at(1).name, "Rahul");
  // Numbers match on their last 10 digits (+91 / 0 prefixes)
  CHECK_EQ(contacts::findByNumber("09876543210"), 1);
  CHECK_EQ(contacts::findByNumber("+91 98123 45678"), 0);
  CHECK_EQ(contacts::findByNumber("121"), -1);
  char name[40];
  contacts::displayName("9876543210", name, sizeof name);
  CHECK_STR(name, "Rahul");
  contacts::displayName("121", name, sizeof name);
  CHECK_STR(name, "121");
  CHECK_EQ(contacts::add("Empty number", ""), -1);
  int iu = contacts::update(1, "Rahul K", "+919876543210");
  CHECK_STR(contacts::at(iu).name, "Rahul K");
  std::vector<Contact> sim = {{"Rahul SIM", "9876543210"}, {"Mom", "+919111111111"}};
  CHECK_EQ(contacts::importList(sim), 1);  // Rahul already known by number
  CHECK_EQ(contacts::count(), 4);
  contacts::remove(contacts::findByNumber("+919000000001"));
  CHECK_EQ(contacts::count(), 3);
  // Deferred save: nothing written until the store has been quiet for a moment
  contacts::service(plogMillis(), true);
  CHECK(!hostFileExists("/contacts.bin"));
  contacts::service(plogMillis() + 5000, false);  // e.g. during a call: never write
  CHECK(!hostFileExists("/contacts.bin"));
  contacts::service(plogMillis() + 5000, true);
  CHECK(hostFileExists("/contacts.bin"));
  contacts::load();  // round trip
  CHECK_EQ(contacts::count(), 3);
  CHECK_EQ(contacts::findByNumber("+919111111111") >= 0, 1);
}

static void testCallLogStore() {
  hostRemoveFile("/calls.bin");
  calllog::load();
  calllog::clearAll();
  calllog::add("+919876543210", CallType::Outgoing, 1000, 42);
  calllog::add("+919812345678", CallType::Missed, 2000, 0);
  calllog::add("+919812345678", CallType::Missed, 3000, 0);
  CHECK_EQ(calllog::count(), 3);
  CHECK(calllog::at(0).ts == 3000);  // newest first
  CHECK_EQ(calllog::missedUnseen(), 2);
  calllog::markSeen();
  CHECK_EQ(calllog::missedUnseen(), 0);
  CHECK_STR(calllog::lastDialed(), "+919876543210");
  calllog::service(plogMillis() + 5000, true);
  calllog::load();
  CHECK_EQ(calllog::count(), 3);
}

static void testMessagesStore() {
  hostRemoveFile("/sms.bin");
  messages::load();
  messages::clearAll();
  SmsRecord r = {};
  strcpy(r.number, "AX-AIRTEL");
  strcpy(r.text, "Your recharge is successful");
  r.flags = SMS_UNREAD;
  r.ts = 100;
  uint32_t a = messages::add(r);
  strcpy(r.number, "+919876543210");
  strcpy(r.text, "Are you coming?");
  r.ts = 200;
  messages::add(r);
  strcpy(r.text, "Call me");
  r.ts = 300;
  messages::add(r);
  CHECK(a != 0);
  CHECK_EQ(messages::count(), 3);
  CHECK_EQ(messages::unreadCount(), 3);
  messages::Thread th[8];
  int n = messages::buildThreads(th, 8);
  CHECK_EQ(n, 2);
  if (n == 2) {
    CHECK_STR(th[0].number, "+919876543210");  // newest conversation first
    CHECK_EQ(th[0].total, 2);
    CHECK_EQ(th[0].unread, 2);
  }
  messages::markThreadRead("9876543210");
  CHECK_EQ(messages::unreadCount(), 1);
  messages::setFlags(a, SMS_FAILED, SMS_UNREAD);
  CHECK(messages::byId(a) && (messages::byId(a)->flags & SMS_FAILED));
  messages::removeThread("AX-AIRTEL");
  CHECK_EQ(messages::count(), 2);
  messages::service(plogMillis() + 5000, true);
  messages::load();
  CHECK_EQ(messages::count(), 2);
}

void runPhoneTests() {
  testTouchCalibration();
  testBattery();
  testNetRules();
  testContactsStore();
  testCallLogStore();
  testMessagesStore();
}
