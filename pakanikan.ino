/*
  =============================================================================
  SISTEM PAKAN IKAN AUTOMATIS + MONITORING MULTI-PLATFORM (V3.0 FINAL)
  -----------------------------------------------------------------------------
  Hardware: ESP32 Dev Module
  Board Settings: 
    - Board: ESP32 Dev Module
    - Partition Scheme: Default 4MB with spiffs / LittleFS (1.2MB APP / 1.5MB SPIFFS)
  
  Library Required (Install via Library Manager):
    1. Blynk (by Volodymyr Shymanskyy)
    2. OneWire (by Paul Stoffregen)
    3. DallasTemperature (by Miles Burton)
    4. DHT sensor library (by Adafruit) + Adafruit Unified Sensor
  =============================================================================
*/ 
// ======================= 1. BLYNK CREDENTIALS =======================
// PASTE 3 BARIS KODE DARI BLYNK CONSOLE DI SINI:
#define BLYNK_TEMPLATE_ID "TMPL6VWizhkwD"
#define BLYNK_TEMPLATE_NAME "Pakan Ikan Otomatis"
#define BLYNK_AUTH_TOKEN "Jk87uEeW5zqq9hM6m1dydlgPf2ZayxGh"

// ======================= 2. INCLUDES & INITS =======================
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>
#include <LittleFS.h>
#include <time.h>

#define PAKSA_WIFI_DARI_KODE 1 // Set 0 jika ingin ubah WiFi via Web Admin

WebServer server(80);
Preferences prefs;
BlynkTimer blynkTimer;

// STRUCT DATALOGGER BINER
struct LogRec { 
  uint32_t epoch; 
  float hum; 
  float air; 
  float udara; 
  float ph; 
  float doVal; 
};

typedef void (*XOut)(const uint8_t*, size_t);
struct XFile { const char* name; const char* data; uint32_t size; uint32_t crc; uint32_t off; };

// ======================= 3. KONFIGURASI SISTEM =======================
struct Config {
  String namaWifi   = "WATASHIWA MAHO SAMA";
  String passWifi   = "labib818";
  String adminPass  = "admin";

  float ukuranIkan = 0;
  float tebarPadat = 100; // Ekor/Kg

  // Threshold Parameter
  float aktMin = 25.0, aktMax = 30.0;
  float efkMin = 25.0, efkMax = 30.0;
  float airMin = 25.0, airMax = 30.0;
  float phMin  = 6.5,  phMax  = 8.5;
  float doMin  = 4.0,  doMax  = 10.0;

  float histerisis = 0.5;
  float gramPerBlok = 172.0;            // 172 gram
  float detikPerBlok = 5.0;             // per 5 detik
  float faktorPorsi = 0.043;            // Total Pakan = Tebar Padat x 0.043
  float turunBawah = 20.0;              // % Pengurangan porsi jika air dingin
  int   maxManualDetik = 120;

  String waktuPagi  = "08:00";
  String waktuMalam = "20:00";
  int    zonaWaktuJam = 7;              // WIB (UTC+7)

  // Hardware Pinout
  int pinRelayPakan = 26;
  int pinRelay2     = 27;               // Blower / Pompa
  int pinSuhuAir    = 4;                // DS18B20
  int pinDHT        = 15;               // DHT22
  int pinPH         = 34;               // Analog ADC1
  int pinDO         = 35;               // Analog ADC1
  
  int relayActiveLow = 1;               // 1 = Active LOW Relay
  int logAktif = 1;                     // Datalogger per 10 menit
} cfg;

void loadConfig() {
  prefs.begin("pakan", true);
  cfg.namaWifi = prefs.getString("namaWifi", cfg.namaWifi);
  cfg.passWifi = prefs.getString("passWifi", cfg.passWifi);
#if PAKSA_WIFI_DARI_KODE
  cfg.namaWifi = "WATASHIWA MAHO SAMA";
  cfg.passWifi = "labib818";
#endif
  cfg.adminPass = prefs.getString("adminPass", cfg.adminPass);
  cfg.ukuranIkan = prefs.getFloat("ukuranIkan", cfg.ukuranIkan);
  cfg.tebarPadat = prefs.getFloat("tebarPadat", cfg.tebarPadat);
  cfg.aktMin = prefs.getFloat("aktMin", cfg.aktMin);  cfg.aktMax = prefs.getFloat("aktMax", cfg.aktMax);
  cfg.airMin = prefs.getFloat("airMin", cfg.airMin);  cfg.airMax = prefs.getFloat("airMax", cfg.airMax);
  cfg.phMin  = prefs.getFloat("phMin",  cfg.phMin);   cfg.phMax  = prefs.getFloat("phMax",  cfg.phMax);
  cfg.doMin  = prefs.getFloat("doMin",  cfg.doMin);   cfg.doMax  = prefs.getFloat("doMax",  cfg.doMax);
  cfg.histerisis = prefs.getFloat("hist", cfg.histerisis);
  cfg.gramPerBlok = prefs.getFloat("gramPerBlok", cfg.gramPerBlok);
  cfg.detikPerBlok = prefs.getFloat("detikPerBlok", cfg.detikPerBlok);
  cfg.faktorPorsi = prefs.getFloat("faktorPorsi", cfg.faktorPorsi);
  cfg.turunBawah = prefs.getFloat("turunBawah", cfg.turunBawah);
  cfg.maxManualDetik = prefs.getInt("maxManual", cfg.maxManualDetik);
  cfg.waktuPagi = prefs.getString("waktuPagi", cfg.waktuPagi);
  cfg.waktuMalam = prefs.getString("waktuMalam", cfg.waktuMalam);
  cfg.zonaWaktuJam = prefs.getInt("zona", cfg.zonaWaktuJam);
  cfg.pinRelayPakan = prefs.getInt("pinRelayPakan", cfg.pinRelayPakan);
  cfg.pinRelay2 = prefs.getInt("pinRelay2", cfg.pinRelay2);
  cfg.pinSuhuAir = prefs.getInt("pinSuhuAir", cfg.pinSuhuAir);
  cfg.pinDHT = prefs.getInt("pinDHT", cfg.pinDHT);
  cfg.pinPH = prefs.getInt("pinPH", cfg.pinPH);
  cfg.pinDO = prefs.getInt("pinDO", cfg.pinDO);
  cfg.relayActiveLow = prefs.getInt("activeLow", cfg.relayActiveLow);
  cfg.logAktif = prefs.getInt("logAktif", cfg.logAktif);
  prefs.end();
}

void saveConfig() {
  prefs.begin("pakan", false);
  prefs.putString("namaWifi", cfg.namaWifi);
  prefs.putString("passWifi", cfg.passWifi);
  prefs.putString("adminPass", cfg.adminPass);
  prefs.putFloat("ukuranIkan", cfg.ukuranIkan);
  prefs.putFloat("tebarPadat", cfg.tebarPadat);
  prefs.putFloat("aktMin", cfg.aktMin);  prefs.putFloat("aktMax", cfg.aktMax);
  prefs.putFloat("airMin", cfg.airMin);  prefs.putFloat("airMax", cfg.airMax);
  prefs.putFloat("phMin", cfg.phMin);    prefs.putFloat("phMax", cfg.phMax);
  prefs.putFloat("doMin", cfg.doMin);    prefs.putFloat("doMax", cfg.doMax);
  prefs.putFloat("hist", cfg.histerisis);
  prefs.putFloat("gramPerBlok", cfg.gramPerBlok);
  prefs.putFloat("detikPerBlok", cfg.detikPerBlok);
  prefs.putFloat("faktorPorsi", cfg.faktorPorsi);
  prefs.putFloat("turunBawah", cfg.turunBawah);
  prefs.putInt("maxManual", cfg.maxManualDetik);
  prefs.putString("waktuPagi", cfg.waktuPagi);
  prefs.putString("waktuMalam", cfg.waktuMalam);
  prefs.putInt("zona", cfg.zonaWaktuJam);
  prefs.putInt("pinRelayPakan", cfg.pinRelayPakan);
  prefs.putInt("pinRelay2", cfg.pinRelay2);
  prefs.putInt("pinSuhuAir", cfg.pinSuhuAir);
  prefs.putInt("pinDHT", cfg.pinDHT);
  prefs.putInt("pinPH", cfg.pinPH);
  prefs.putInt("pinDO", cfg.pinDO);
  prefs.putInt("activeLow", cfg.relayActiveLow);
  prefs.putInt("logAktif", cfg.logAktif);
  prefs.end();
}

// ======================= 4. SENSOR OBJECTS & VARS =======================
OneWire owAir(4);
DallasTemperature dtAir(&owAir);
DHT dhtObj(15, DHT22);

float paparanUdara = NAN, dalamAir = NAN;
float suhuDHT = NAN, kelembaban = NAN;
float valPH = NAN, valDO = NAN;

unsigned long lastDHT = 0;
unsigned long lastSensorReq = 0;
bool sensorWaiting = false;

// Function Prototypes
void feederSet(bool on, float detik, String mode);
float outPakanTurun();
float durasiDetik(float gram);

// Filter Oversampling Analog ADC
float readAnalogVoltage(int pin) {
  long sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(pin);
    delayMicroseconds(100);
  }
  return ((sum / 10.0) / 4095.0) * 3.3;
}

void sensorLoop() {
  unsigned long now = millis();

  // DS18B20 Water Temperature
  if (!sensorWaiting && now - lastSensorReq > 3000) {
    dtAir.requestTemperatures();
    lastSensorReq = now;
    sensorWaiting = true;
  } else if (sensorWaiting && now - lastSensorReq > 1000) {
    float a = dtAir.getTempCByIndex(0);
    if (a > -100 && a < 125) dalamAir = a;
    sensorWaiting = false;
  }

  // DHT22 Air & Humidity + Analog Sensors
  if (now - lastDHT > 3000) {
    lastDHT = now;
    float t = dhtObj.readTemperature();
    float h = dhtObj.readHumidity();
    if (!isnan(t) && !isnan(h)) { suhuDHT = t; paparanUdara = t; kelembaban = h; }

    // Formula Kalibrasi Analog (Sesuaikan dengan larutan kalibrasi)
    float voltPH = readAnalogVoltage(cfg.pinPH);
    float voltDO = readAnalogVoltage(cfg.pinDO);
    valPH = 3.5 * voltPH + 0.1;
    valDO = voltDO * 4.0;
  }
}

float suhuAktualLive() {
  if (isnan(paparanUdara) || isnan(dalamAir)) return isnan(dalamAir) ? paparanUdara : dalamAir;
  return (paparanUdara + dalamAir) / 2.0;
}
float suhuEfektifLive() { return dalamAir; }

String rng(float a, float b) { return String(a, 1) + " - " + String(b, 1); }
String statusRange(float v, float mn, float mx) {
  if (isnan(v)) return "-";
  if (v < mn) return "di bawah";
  if (v > mx) return "di atas";
  return "normal";
}

// ======================= 5. BLYNK CLOUD V2 INTEGRATION =======================
BLYNK_CONNECTED() {
  Blynk.syncVirtual(V5); // Sync Tebar Padat dari Cloud saat Online
}

BLYNK_WRITE(V5) { // Parameter Input Tebar Padat
  cfg.tebarPadat = param.asFloat();
  saveConfig();
}

BLYNK_WRITE(V6) { // Switch Feed Manual Blynk
  int val = param.asInt();
  if (val == 1) {
    float g = outPakanTurun();
    float d = durasiDetik(g);
    feederSet(true, d > 0 ? d : cfg.maxManualDetik, "blynk_manual");
  } else {
    feederSet(false, 0, "");
  }
}

BLYNK_WRITE(V7) { // Switch Blower / Pompa Blynk
  int val = param.asInt();
  digitalWrite(cfg.pinRelay2, (val == 1) ? (cfg.relayActiveLow ? LOW : HIGH) : (cfg.relayActiveLow ? HIGH : LOW));
}

void sendDataToBlynk() {
  if (Blynk.connected()) {
    Blynk.virtualWrite(V0, isnan(dalamAir) ? 0 : dalamAir);
    Blynk.virtualWrite(V1, isnan(valPH) ? 0 : valPH);
    Blynk.virtualWrite(V2, isnan(valDO) ? 0 : valDO);
    Blynk.virtualWrite(V3, isnan(suhuDHT) ? 0 : suhuDHT);
    Blynk.virtualWrite(V4, isnan(kelembaban) ? 0 : kelembaban);
    
    float totalPakan = cfg.tebarPadat * cfg.faktorPorsi;
    float durasi = (totalPakan / 2.0 / cfg.gramPerBlok) * cfg.detikPerBlok;
    Blynk.virtualWrite(V8, durasi);
  }
}

// ======================= 6. LOGIKA PAKAN & RELAY =======================
float porsiPakan() { return cfg.tebarPadat * cfg.faktorPorsi; }

float faktorSuhu() {
  float s = suhuAktualLive();
  if (!isnan(s) && s < cfg.aktMin) return 1.0 - (cfg.turunBawah / 100.0);
  return 1.0;
}
float outPakanDefault() { return porsiPakan() / 2.0; }
float outPakanTurun()   { return (porsiPakan() * faktorSuhu()) / 2.0; } 
float durasiDetik(float gram) { return (gram / cfg.gramPerBlok) * cfg.detikPerBlok; }

bool feederOn = false;
unsigned long feederOffAt = 0;
String feederMode = "-";
bool relay2On = false;

void relayWrite(int pin, bool on) {
  digitalWrite(pin, (on != (bool)cfg.relayActiveLow) ? HIGH : LOW);
}

void feederSet(bool on, float detik, String mode) {
  feederOn = on;
  relayWrite(cfg.pinRelayPakan, on);
  feederMode = on ? mode : "-";
  feederOffAt = (on && detik > 0) ? millis() + (unsigned long)(detik * 1000.0) : 0;
  if (!on) Blynk.virtualWrite(V6, 0); // Reset Tombol Manual Blynk
}

void relay2Loop() {
  static unsigned long last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  bool want = relay2On;
  if (isnan(dalamAir)) want = false;
  else if (!relay2On && dalamAir > cfg.airMax) want = true;
  else if (relay2On && dalamAir <= (cfg.airMax - cfg.histerisis)) want = false;
  
  if (want != relay2On) { 
    relay2On = want; 
    relayWrite(cfg.pinRelay2, relay2On); 
    Blynk.virtualWrite(V7, relay2On ? 1 : 0);
  }
}

String lastFeedInfo = "-";
int lastDayPagi = -1, lastDayMalam = -1;

void scheduleLoop() {
  if (feederOn && feederOffAt && (long)(millis() - feederOffAt) >= 0) {
    feederSet(false, 0, "");
  }

  static unsigned long last = 0;
  if (millis() - last < 1000) return;
  last = millis();

  struct tm t;
  if (!getLocalTime(&t, 10)) return;
  char hm[6]; snprintf(hm, sizeof(hm), "%02d:%02d", t.tm_hour, t.tm_min);

  // Pagi 08:00
  if (cfg.waktuPagi == hm && lastDayPagi != t.tm_yday) {
    lastDayPagi = t.tm_yday;
    float g = outPakanTurun(), d = durasiDetik(g);
    if (d > 0) { feederSet(true, d, "pagi"); lastFeedInfo = String("Pagi ") + hm + " | " + String(g, 1) + " g | " + String(d, 2) + " dtk"; }
  }
  // Malam 20:00
  if (cfg.waktuMalam == hm && lastDayMalam != t.tm_yday) {
    lastDayMalam = t.tm_yday;
    float g = outPakanTurun(), d = durasiDetik(g);
    if (d > 0) { feederSet(true, d, "malam"); lastFeedInfo = String("Malam ") + hm + " | " + String(g, 1) + " g | " + String(d, 2) + " dtk"; }
  }
}

// ======================= 7. DATALOGGER & STREAM EXCEL ENGINE =======================
#define LOG_FILE      "/datalogs.bin"
#define LOG_TMP       "/datalogs.tmp"
#define LOG_MAX       8000     // ~55 hari @10 menit
#define LOG_INTERVAL  600      // 10 menit

uint32_t logN = 0;
String lastLogInfo = "-";

void logInit() {
  logN = 0;
  if (!LittleFS.exists(LOG_FILE)) return;
  File f = LittleFS.open(LOG_FILE, FILE_READ);
  if (f) { logN = f.size() / sizeof(LogRec); f.close(); }
}

void logTrim() {
  uint32_t keep = LOG_MAX / 2;
  if (logN <= keep) return;
  File in = LittleFS.open(LOG_FILE, FILE_READ);
  if (!in) return;
  File out = LittleFS.open(LOG_TMP, FILE_WRITE);
  if (!out) { in.close(); return; }
  in.seek((logN - keep) * sizeof(LogRec));
  uint8_t buf[256];
  int n;
  while ((n = in.read(buf, sizeof(buf))) > 0) out.write(buf, n);
  in.close(); out.close();
  LittleFS.remove(LOG_FILE);
  LittleFS.rename(LOG_TMP, LOG_FILE);
  logInit();
}

void logAppend(const LogRec& r) {
  File f = LittleFS.open(LOG_FILE, FILE_APPEND);
  if (!f) return;
  f.write((const uint8_t*)&r, sizeof(r));
  f.close();
  logN++;
  if (logN > LOG_MAX) logTrim();
}

void logClear() {
  LittleFS.remove(LOG_FILE);
  logN = 0; lastLogInfo = "-";
}

time_t nextLog = 0;

String nextLogStr() {
  if (!cfg.logAktif) return "-";
  if (nextLog == 0) return "segera";
  time_t lt = nextLog + cfg.zonaWaktuJam * 3600;
  struct tm t; gmtime_r(<, &t);
  char buf[8]; strftime(buf, sizeof(buf), "%H:%M", &t);
  return String(buf);
}

void logSetSwitch(bool on) {
  cfg.logAktif = on ? 1 : 0;
  nextLog = 0;
  prefs.begin("pakan", false);
  prefs.putInt("logAktif", cfg.logAktif);
  prefs.end();
}

static void logWriteNow(time_t now) {
  float udara = isnan(suhuDHT) ? paparanUdara : suhuDHT;
  LogRec r = { (uint32_t)now, kelembaban, dalamAir, udara, valPH, valDO };
  logAppend(r);
  time_t lt = now + cfg.zonaWaktuJam * 3600;
  struct tm t; gmtime_r(<, &t);
  char buf[24]; strftime(buf, sizeof(buf), "%d/%m %H:%M", &t);
  lastLogInfo = String(buf);
}

void logLoop() {
  static unsigned long last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  if (!cfg.logAktif) { nextLog = 0; return; }
  if (millis() < 8000) return;
  struct tm t;
  if (!getLocalTime(&t, 10)) return;
  time_t now = time(nullptr);
  if (nextLog == 0 || now >= nextLog) {
    static unsigned long waitSince = 0;
    if (isnan(dalamAir) || isnan(valPH)) {
      if (waitSince == 0) waitSince = millis();
      if (millis() - waitSince < 60000) return;
    }
    waitSince = 0;
    logWriteNow(now);
    nextLog = (nextLog == 0 || now >= nextLog + LOG_INTERVAL) ? now + LOG_INTERVAL : nextLog + LOG_INTERVAL;
  }
}

static File gRec;
static bool recOpen() {
  if (!LittleFS.exists(LOG_FILE)) return false;
  gRec = LittleFS.open(LOG_FILE, FILE_READ);
  return (bool)gRec;
}
static bool recNext(LogRec& r) {
  if (!gRec) return false;
  return gRec.read((uint8_t*)&r, sizeof(LogRec)) == sizeof(LogRec);
}
static void recClose() { if (gRec) gRec.close(); }

// STREAM EXCEL STREAMER
static XOut xOut = nullptr;
static uint32_t xCrcTab[256];
static bool xCrcOk = false;
static uint32_t xCrc, xLen;
static uint8_t xBuf[1400];
static size_t xBufN = 0;
static int xTzSec = 0;

static void xCrcBuild() {
  if (xCrcOk) return;
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    xCrcTab[i] = c;
  }
  xCrcOk = true;
}
static uint32_t xCrcUpd(uint32_t c, const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; i++) c = xCrcTab[(c ^ p[i]) & 0xFF] ^ (c >> 8);
  return c;
}
static void xFlush() { if (xOut && xBufN) { xOut(xBuf, xBufN); xBufN = 0; } }
static void xput(const void* p, size_t n) {
  const uint8_t* b = (const uint8_t*)p;
  if (!xOut) { xCrc = xCrcUpd(xCrc, b, n); xLen += (uint32_t)n; return; }
  while (n) {
    size_t c = sizeof(xBuf) - xBufN; if (c > n) c = n;
    memcpy(xBuf + xBufN, b, c); xBufN += c; b += c; n -= c;
    if (xBufN == sizeof(xBuf)) xFlush();
  }
}
static void xs(const char* s) { xput(s, strlen(s)); }
static void p16(uint8_t*& p, uint16_t v) { *p++ = v & 0xFF; *p++ = v >> 8; }
static void p32(uint8_t*& p, uint32_t v) { p16(p, v & 0xFFFF); p16(p, v >> 16); }

static const char X_CT[] =
  "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
  "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
  "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
  "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
  "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
  "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
  "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
  "</Types>";
static const char X_RELS[] =
  "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
  "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
  "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
  "</Relationships>";
static const char X_WB[] =
  "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
  "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
  "<sheets><sheet name=\"datalogs\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>";
static const char X_WBRELS[] =
  "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
  "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
  "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
  "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
  "</Relationships>";
static const char X_STYLES[] =
  "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
  "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
  "<numFmts count=\"2\"><numFmt numFmtId=\"164\" formatCode=\"yyyy\\-mm\\-dd\\ hh:mm:ss\"/><numFmt numFmtId=\"165\" formatCode=\"0.0\"/></numFmts>"
  "<fonts count=\"2\"><font><sz val=\"11\"/><name val=\"Calibri\"/></font><font><b/><sz val=\"11\"/><name val=\"Calibri\"/></font></fonts>"
  "<fills count=\"2\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill></fills>"
  "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>"
  "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
  "<cellXfs count=\"4\">"
  "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
  "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/>"
  "<xf numFmtId=\"164\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
  "<xf numFmtId=\"165\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
  "</cellXfs>"
  "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles></styleSheet>";

static const char X_SHEET_HEAD[] =
  "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
  "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
  "<cols><col min=\"1\" max=\"1\" width=\"7\" customWidth=\"1\"/><col min=\"2\" max=\"2\" width=\"21\" customWidth=\"1\"/>"
  "<col min=\"3\" max=\"3\" width=\"18\" customWidth=\"1\"/><col min=\"4\" max=\"4\" width=\"12\" customWidth=\"1\"/>"
  "<col min=\"5\" max=\"5\" width=\"13\" customWidth=\"1\"/><col min=\"6\" max=\"6\" width=\"10\" customWidth=\"1\"/>"
  "<col min=\"7\" max=\"7\" width=\"12\" customWidth=\"1\"/></cols><sheetData>"
  "<row r=\"1\">"
  "<c r=\"A1\" s=\"1\" t=\"inlineStr\"><is><t>No</t></is></c>"
  "<c r=\"B1\" s=\"1\" t=\"inlineStr\"><is><t>Waktu</t></is></c>"
  "<c r=\"C1\" s=\"1\" t=\"inlineStr\"><is><t>Kelembaban Udara (%)</t></is></c>"
  "<c r=\"D1\" s=\"1\" t=\"inlineStr\"><is><t>Suhu Air (°C)</t></is></c>"
  "<c r=\"E1\" s=\"1\" t=\"inlineStr\"><is><t>Suhu Udara (°C)</t></is></c>"
  "<c r=\"F1\" s=\"1\" t=\"inlineStr\"><is><t>pH Air</t></is></c>"
  "<c r=\"G1\" s=\"1\" t=\"inlineStr\"><is><t>DO (mg/L)</t></is></c>"
  "</row>";

static void sheetGen(uint32_t n) {
  xs(X_SHEET_HEAD);
  recOpen();
  LogRec r; uint32_t i = 0; char b[380];
  while (i < n && recNext(r)) {
    i++; unsigned row = (unsigned)i + 1;
    double ser = ((double)r.epoch + xTzSec) / 86400.0 + 25569.0;
    int l = snprintf(b, sizeof(b), "<row r=\"%u\"><c r=\"A%u\"><v>%u</v></c><c r=\"B%u\" s=\"2\"><v>%.8f</v></c>",
                     row, row, (unsigned)i, row, ser);
    auto cell = [&](char col, float v) {
      if (isnan(v)) return;
      l += snprintf(b + l, sizeof(b) - l, "<c r=\"%c%u\" s=\"3\"><v>%.2f</v></c>", col, row, (double)v);
    };
    cell('C', r.hum); cell('D', r.air); cell('E', r.udara); cell('F', r.ph); cell('G', r.doVal);
    l += snprintf(b + l, sizeof(b) - l, "</row>");
    xput(b, l);
  }
  recClose();
  xs("</sheetData></worksheet>");
}

static uint32_t xlsxExport(XOut out, uint32_t nRec, int tzSec) {
  xCrcBuild(); xTzSec = tzSec;
  XFile f[6] = {
    { "[Content_Types].xml",        X_CT, 0, 0, 0 },
    { "_rels/.rels",                X_RELS, 0, 0, 0 },
    { "xl/workbook.xml",            X_WB, 0, 0, 0 },
    { "xl/_rels/workbook.xml.rels", X_WBRELS, 0, 0, 0 },
    { "xl/styles.xml",              X_STYLES, 0, 0, 0 },
    { "xl/worksheets/sheet1.xml",   nullptr, 0, 0, 0 },
  };
  for (int i = 0; i < 5; i++) {
    f[i].size = strlen(f[i].data);
    f[i].crc = ~xCrcUpd(0xFFFFFFFFu, (const uint8_t*)f[i].data, f[i].size);
  }
  xOut = nullptr; xCrc = 0xFFFFFFFFu; xLen = 0;
  sheetGen(nRec);
  f[5].size = xLen; f[5].crc = ~xCrc;

  uint32_t off = 0, cd = 0;
  for (int i = 0; i < 6; i++) { f[i].off = off; off += 30 + (uint32_t)strlen(f[i].name) + f[i].size; cd += 46 + (uint32_t)strlen(f[i].name); }
  uint32_t total = off + cd + 22;
  if (!out) return total;

  xOut = out; xBufN = 0;
  uint8_t h[46]; uint8_t* p;
  for (int i = 0; i < 6; i++) {
    p = h;
    p32(p, 0x04034b50); p16(p, 20); p16(p, 0); p16(p, 0); p16(p, 0); p16(p, 0x21);
    p32(p, f[i].crc); p32(p, f[i].size); p32(p, f[i].size); p16(p, (uint16_t)strlen(f[i].name)); p16(p, 0);
    xput(h, p - h); xs(f[i].name);
    if (f[i].data) xput(f[i].data, f[i].size); else sheetGen(nRec);
  }
  for (int i = 0; i < 6; i++) {
    p = h;
    p32(p, 0x02014b50); p16(p, 20); p16(p, 20); p16(p, 0); p16(p, 0); p16(p, 0); p16(p, 0x21);
    p32(p, f[i].crc); p32(p, f[i].size); p32(p, f[i].size);
    p16(p, (uint16_t)strlen(f[i].name)); p16(p, 0); p16(p, 0); p16(p, 0); p16(p, 0); p32(p, 0); p32(p, f[i].off);
    xput(h, p - h); xs(f[i].name);
  }
  p = h;
  p32(p, 0x06054b50); p16(p, 0); p16(p, 0); p16(p, 6); p16(p, 6); p32(p, cd); p32(p, off); p16(p, 0);
  xput(h, p - h);
  xFlush(); xOut = nullptr;
  return total;
}

// ======================= 8. LOCAL WEB DASHBOARD SERVER =======================
String fnum(float v) { return isnan(v) ? "null" : String(v, 2); }

String statusJson() {
  struct tm t; char jam[9] = "--:--:--";
  if (getLocalTime(&t, 10)) strftime(jam, sizeof(jam), "%H:%M:%S", &t);
  float sa = suhuAktualLive();
  String j = "{";
  j.reserve(600);
  j += "\"suhuAktualLive\":" + fnum(sa);
  j += ",\"suhuEfektifLive\":" + fnum(suhuEfektifLive());
  j += ",\"paparanUdara\":" + fnum(paparanUdara);
  j += ",\"dalamAir\":" + fnum(dalamAir);
  j += ",\"suhuDHT\":" + fnum(suhuDHT);
  j += ",\"valPH\":" + fnum(valPH);
  j += ",\"valDO\":" + fnum(valDO);
  j += ",\"logCount\":" + String(logN);
  j += ",\"lastLog\":\"" + lastLogInfo + "\"";
  j += String(",\"logOn\":") + (cfg.logAktif ? "true" : "false");
  j += ",\"nextLog\":\"" + nextLogStr() + "\"";
  j += ",\"kelembaban\":" + fnum(kelembaban);
  j += ",\"stAktual\":\"" + statusRange(sa, cfg.aktMin, cfg.aktMax) + "\"";
  j += ",\"stEfektif\":\"" + statusRange(dalamAir, cfg.efkMin, cfg.efkMax) + "\"";
  j += ",\"stAir\":\"" + statusRange(dalamAir, cfg.airMin, cfg.airMax) + "\"";
  j += ",\"stPH\":\"" + statusRange(valPH, cfg.phMin, cfg.phMax) + "\"";
  j += ",\"stDO\":\"" + statusRange(valDO, cfg.doMin, cfg.doMax) + "\"";
  j += ",\"rAktual\":\"" + rng(cfg.aktMin, cfg.aktMax) + "\"";
  j += ",\"rEfektif\":\"" + rng(cfg.efkMin, cfg.efkMax) + "\"";
  j += ",\"rAir\":\"" + rng(cfg.airMin, cfg.airMax) + "\"";
  j += ",\"rPH\":\"" + rng(cfg.phMin, cfg.phMax) + "\"";
  j += ",\"rDO\":\"" + rng(cfg.doMin, cfg.doMax) + "\"";
  j += ",\"ukuranIkan\":" + fnum(cfg.ukuranIkan);
  j += ",\"tebarPadat\":" + fnum(cfg.tebarPadat);
  j += ",\"porsiPakan\":" + fnum(porsiPakan());
  j += ",\"outPakanDefault\":" + fnum(outPakanDefault());
  j += ",\"outPakanTurun\":" + fnum(outPakanTurun());
  j += ",\"feeder\":" + String(feederOn ? "true" : "false");
  j += ",\"relay2\":" + String(relay2On ? "true" : "false");
  j += ",\"mode\":\"" + feederMode + "\"";
  j += ",\"lastFeed\":\"" + lastFeedInfo + "\"";
  j += ",\"jam\":\"" + String(jam) + "\"";
  j += ",\"waktuPagi\":\"" + cfg.waktuPagi + "\",\"waktuMalam\":\"" + cfg.waktuMalam + "\"}";
  return j;
}

const char STYLE[] PROGMEM = R"(
<meta charset="utf-8">
<meta name=viewport content="width=device-width,initial-scale=1">
<style>
:root{--bg:#f4f6f8;--card:#fff;--tx:#1b2430;--mut:#6b7785;--ac:#0a7ea4;--bd:#e1e6eb}
@media(prefers-color-scheme:dark){:root{--bg:#10151b;--card:#18202a;--tx:#e8edf2;--mut:#93a0ae;--ac:#4cc3e8;--bd:#27323f}}
*{box-sizing:border-box}body{margin:0;font-family:system-ui,sans-serif;background:var(--bg);color:var(--tx);padding:16px;max-width:900px;margin:auto}
h1{font-size:20px}h2{font-size:15px;color:var(--mut);margin:20px 0 8px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}
.card{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:12px}
.card small{color:var(--mut);display:block;font-size:12px}.card b{font-size:22px}
a{color:var(--ac)}label{display:block;font-size:13px;color:var(--mut);margin-top:10px}
input,select{width:100%;padding:9px;border-radius:8px;border:1px solid var(--bd);background:var(--card);color:var(--tx);font-size:14px}
button{margin-top:16px;padding:11px 18px;border:0;border-radius:8px;background:var(--ac);color:#fff;font-size:15px;cursor:pointer}
.sw{display:flex;align-items:center;justify-content:space-between}
.tg{position:relative;width:60px;height:32px;background:#9aa5b1;border-radius:32px;cursor:pointer;transition:.2s}
.tg::after{content:"";position:absolute;top:3px;left:3px;width:26px;height:26px;border-radius:50%;background:#fff;transition:.2s}
.tg.on{background:#22a35a}.tg.on::after{left:31px}
.pill{display:inline-block;padding:2px 8px;border-radius:10px;font-size:12px;background:var(--bd);color:var(--mut)}
table{width:100%;border-collapse:collapse;font-size:13px}th,td{padding:6px 8px;border-bottom:1px solid var(--bd);text-align:left;white-space:nowrap}th{color:var(--mut);position:sticky;top:0;background:var(--card)}
.tw{max-height:560px;overflow:auto;padding:0}
button.sm{margin-top:0;padding:6px 12px;font-size:14px}
button.dng{background:#d64545}
</style>)";

const char HOME_HTML[] PROGMEM = R"(<!doctype html><html><head><title>Pakan Ikan Auto</title>%STYLE%</head><body>
<h1>Monitoring Pakan Ikan Auto</h1>
<p><small><b>Home</b> · <a href=/logs>Logs</a> · <a href=/admin>Admin</a></small></p>
<div class=grid>
<div class=card><div class=sw><div><small id=mode>Mesin pakan (Relay 1)</small><b id=st>OFF</b></div><div class=tg id=tg onclick=tog()></div></div>
<small style="margin-top:8px">Simulasi langsung ON / OFF mesin pakan</small></div>
<div class=card><small>Air / blower (Relay 2) - otomatis</small><b id=r2>OFF</b>
<small style="margin-top:8px">ON jika suhu air di atas batas set</small></div>
</div>
<h2>KUALITAS AIR & SENSOR</h2><div class=grid id=g1></div>
<h2>SETTING RENTANG ACUAN</h2><div class=grid id=g2></div>
<h2>BUDIDAYA & PORSI</h2><div class=grid id=g3></div>
<h2>DATA LOG</h2><div class=grid id=g4></div>
<p><a href=/logs>Buka halaman Data Log →</a></p>
<p><small><span id=jam></span></small></p>
<script>
const f=(v,u)=>v===null?'-':v+(u||'');
const C=' \u00b0C';
const card=(l,v,s)=>`<div class=card><small>${l}</small><b>${v}</b>${s?'<br><span class=pill>'+s+'</span>':''}</div>`;
async function load(){try{const d=await (await fetch('/api/status')).json();
tg.className='tg'+(d.feeder?' on':'');st.textContent=d.feeder?'ON':'OFF';mode.textContent='Mesin pakan (Relay 1)'+(d.feeder?' - '+d.mode:'');
r2.textContent=d.relay2?'ON':'OFF';
g1.innerHTML=card('Suhu Air (DS18B20)',f(d.dalamAir,C),d.stAir)+card('pH Air',f(d.valPH),d.stPH)+card('DO (mg/L)',f(d.valDO),d.stDO)+card('Suhu Udara (DHT22)',f(d.suhuDHT,C))+card('Kelembaban Udara',f(d.kelembaban,' %'));
g2.innerHTML=card('Suhu Normal Air',d.rAir+C)+card('Rentang pH Ideal',d.rPH)+card('Rentang DO Ideal',d.rDO+' mg/L');
g3.innerHTML=card('ukuranIkan',f(d.ukuranIkan))+card('tebarPadat',f(d.tebarPadat))+card('porsiPakan',f(d.porsiPakan,' g'))+card('outPakanDefault / sesi',f(d.outPakanDefault,' g'))+card('outPakanTurun / sesi',f(d.outPakanTurun,' g'))+card('waktuPagi',d.waktuPagi)+card('waktuMalam',d.waktuMalam)+card('Pakan terakhir',d.lastFeed);
g4.innerHTML=card('Pencatatan log',d.logOn?'ON':'OFF')+card('Jumlah data log',d.logCount)+card('Log terakhir',d.lastLog);
jam.textContent='Jam perangkat '+d.jam}catch(e){}}
async function tog(){const on=!tg.classList.contains('on');await fetch('/api/feeder',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'state='+(on?1:0)});load()}
load();setInterval(load,3000);
</script></body></html>)";

const char LOGS_HTML[] PROGMEM = R"(<!doctype html><html><head><title>Data Log</title>%STYLE%</head><body>
<h1>Data Log Kualitas Air & Pakan</h1>
<p><small><a href=/>Home</a> · <b>Logs</b> · <a href=/admin>Admin</a></small></p>
<div class=card><div class=sw><div><small>Switch pencatatan log (tiap 10 menit)</small><b id=lst>OFF</b></div><div class=tg id=lg onclick=togLog()></div></div>
<small style="margin-top:8px" id=lnx></small></div>
<div class=grid id=g4 style="margin-top:10px"></div>
<div class=card style="margin-top:10px"><div class=sw style="gap:8px;flex-wrap:wrap">
<select id=ord onchange=chg() style="width:auto"><option value=asc>Terlama - Terbaru</option><option value=desc>Terbaru - Terlama</option></select>
<span><button class=sm onclick=prev()>«</button> <small id=pinfo style="display:inline"></small> <button class=sm onclick=next()>»</button></span></div></div>
<div class="card tw" style="margin-top:8px"><table><thead><tr><th>No</th><th>Waktu</th><th>Kelembaban (%)</th><th>Suhu Air (°C)</th><th>Suhu Udara (°C)</th><th>pH Air</th><th>DO (mg/L)</th></tr></thead><tbody id=lt></tbody></table></div>
<p><a href=/datalogs.xlsx>⬇ Download datalogs.xlsx</a> <small>(Format Microsoft Excel)</small></p>
<div class=card><small>Hapus semua data log lama untuk mulai data log baru (butuh login admin).</small>
<button class="sm dng" style="margin-top:8px" onclick=clr()>Hapus semua data log</button></div>
<p><small id=jam></small></p>
<script>
let pg=1,pages=1,lc=-1;
const card=(l,v)=>`<div class=card><small>${l}</small><b>${v}</b></div>`;
const v1=v=>v===null?'-':v;
async function load(){try{const d=await (await fetch('/api/status')).json();
lg.className='tg'+(d.logOn?' on':'');lst.textContent=d.logOn?'ON':'OFF';
lnx.textContent=d.logOn?('Mencatat tiap 10 menit. Log berikutnya sekitar jam '+d.nextLog):'OFF: tidak mencatat.';
g4.innerHTML=card('Jumlah data log',d.logCount)+card('Log terakhir',d.lastLog)+card('Interval log','10 menit');
if(d.logCount!==lc){lc=d.logCount;loadLogs()}
jam.textContent='Jam perangkat '+d.jam}catch(e){}}
async function loadLogs(){try{const r=await (await fetch('/api/logs?page='+pg+'&order='+ord.value)).json();
pg=r.page;pages=r.pages;
pinfo.textContent='Halaman '+r.page+' / '+r.pages+' ('+r.total+' data)';
lt.innerHTML=r.rows.map(x=>'<tr><td>'+x[0]+'</td><td>'+x[1]+'</td><td>'+v1(x[2])+'</td><td>'+v1(x[3])+'</td><td>'+v1(x[4])+'</td><td>'+v1(x[5])+'</td><td>'+v1(x[6])+'</td></tr>').join('')||'<tr><td colspan=7>Belum ada data log</td></tr>'}catch(e){}}
function chg(){pg=1;loadLogs()}
function prev(){if(pg>1){pg--;loadLogs()}}
function next(){if(pg<pages){pg++;loadLogs()}}
async function togLog(){const on=!lg.classList.contains('on');await fetch('/api/logswitch',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'state='+(on?1:0)});load()}
async function clr(){if(!confirm('Hapus SEMUA data log?'))return;
const r=await fetch('/api/clearlog',{method:'POST'});if(r.ok){pg=1;lc=-1;load()}else alert('Gagal menghapus')}
load();setInterval(load,5000);
</script></body></html>)";

String field(const char* label, const char* name, String val, const char* type) {
  return String("<label>") + label + "<input type=" + type + " name=" + name + " value=\"" + val + "\" step=any></label>";
}
String field(const char* label, const char* name, String val) { return field(label, name, val, "text"); }
String rangeField(const char* label, const char* base, float mn, float mx) {
  return String("<label>") + label + " (min - max)</label><div style='display:flex;gap:8px;align-items:center'>"
    + "<input type=number step=any name=" + base + "Min value=\"" + String(mn, 1) + "\"><span>-</span>"
    + "<input type=number step=any name=" + base + "Max value=\"" + String(mx, 1) + "\"></div>";
}

String adminPage() {
  String h = "<!doctype html><html><head><title>Admin Pakan</title>" + String(FPSTR(STYLE)) + "</head><body>";
  h += "<h1>Admin Setting</h1><p><small><a href=/>Home</a> · <a href=/logs>Logs</a> · <b>Admin</b></small></p><form id=f>";
  h += "<h2>WiFi & Security</h2><div class=card>";
  h += field("Nama WiFi", "namaWifi", cfg.namaWifi) + field("Password WiFi", "passWifi", cfg.passWifi);
  h += field("Password Admin", "adminPass", cfg.adminPass) + "</div>";
  h += "<h2>Budidaya & Porsi</h2><div class=card>";
  h += field("Ukuran Ikan", "ukuranIkan", String(cfg.ukuranIkan), "number") + field("Tebar Padat (Ekor/Kg)", "tebarPadat", String(cfg.tebarPadat), "number");
  h += field("Faktor Porsi (tebarPadat x faktor)", "faktorPorsi", String(cfg.faktorPorsi, 4), "number");
  h += field("Gram per Blok (172g)", "gramPerBlok", String(cfg.gramPerBlok), "number");
  h += field("Detik per Blok (5s)", "detikPerBlok", String(cfg.detikPerBlok), "number");
  h += field("Pengurangan % Suhu Dingin", "turunBawah", String(cfg.turunBawah), "number");
  h += field("Batas Max ON Manual (dtk)", "maxManualDetik", String(cfg.maxManualDetik), "number") + "</div>";
  h += "<h2>Rentang Acuan Sensor</h2><div class=card>";
  h += rangeField("Suhu Air", "setDalamAir", cfg.airMin, cfg.airMax);
  h += rangeField("pH Air Ideal", "ph", cfg.phMin, cfg.phMax);
  h += rangeField("DO Ideal (mg/L)", "do", cfg.doMin, cfg.doMax);
  h += field("Histerisis Relay 2 (°C)", "histerisis", String(cfg.histerisis), "number") + "</div>";
  h += "<h2>Jadwal & Waktu</h2><div class=card>";
  h += field("Waktu Pagi", "waktuPagi", cfg.waktuPagi, "time") + field("Waktu Malam", "waktuMalam", cfg.waktuMalam, "time");
  h += field("Zona Waktu (UTC Offset Jam, WIB=7)", "zonaWaktuJam", String(cfg.zonaWaktuJam), "number") + "</div>";
  h += "<h2>Pin Hardware ESP32</h2><div class=card>";
  h += field("Pin Relay 1 (Pakan)", "pinRelayPakan", String(cfg.pinRelayPakan), "number");
  h += field("Pin Relay 2 (Air/Blower)", "pinRelay2", String(cfg.pinRelay2), "number");
  h += field("Pin Suhu Air (DS18B20)", "pinSuhuAir", String(cfg.pinSuhuAir), "number");
  h += field("Pin DHT22 Udara", "pinDHT", String(cfg.pinDHT), "number");
  h += field("Pin Analog pH", "pinPH", String(cfg.pinPH), "number");
  h += field("Pin Analog DO", "pinDO", String(cfg.pinDO), "number");
  h += "<label>Tipe Relay<select name=relayActiveLow><option value=1" + String(cfg.relayActiveLow ? " selected" : "") + ">Active LOW</option><option value=0" + String(cfg.relayActiveLow ? "" : " selected") + ">Active HIGH</option></select></label></div>";
  h += "<button type=submit>Simpan & Restart</button> <small id=msg></small></form>";
  h += "<script>f.onsubmit=async e=>{e.preventDefault();const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(f))});msg.textContent=r.ok?'Tersimpan, restart...':'Gagal'}</script>";
  h += String(R"X(<h2>Data Log</h2><div class=card><a href=/logs>Halaman Log</a> · <a href=/datalogs.xlsx>Download Excel</a><br>
<button type=button onclick="if(confirm('Hapus log?'))fetch('/api/clearlog',{method:'POST'}).then(()=>location.reload())">Hapus Log</button></div>)X");
  h += "<p><small>Total Record: " + String(logN) + "</small></p></body></html>";
  return h;
}

bool auth() {
  if (!server.authenticate("admin", cfg.adminPass.c_str())) { server.requestAuthentication(); return false; }
  return true;
}

bool restartPending = false;
unsigned long restartAt = 0;

void fixRange(float& a, float& b) { if (a > b) { float t = a; a = b; b = t; } }

void handleConfig() {
  if (!auth()) return;
  auto cS = [&](const char* n, String& v) { if (server.hasArg(n)) v = server.arg(n); };
  auto cF = [&](const char* n, float& v) { if (server.hasArg(n)) v = server.arg(n).toFloat(); };
  auto cI = [&](const char* n, int& v) { if (server.hasArg(n)) v = server.arg(n).toInt(); };
  cS("namaWifi", cfg.namaWifi); cS("passWifi", cfg.passWifi); cS("adminPass", cfg.adminPass);
  cF("ukuranIkan", cfg.ukuranIkan); cF("tebarPadat", cfg.tebarPadat);
  cF("setDalamAirMin", cfg.airMin); cF("setDalamAirMax", cfg.airMax);
  cF("phMin", cfg.phMin); cF("phMax", cfg.phMax);
  cF("doMin", cfg.doMin); cF("doMax", cfg.doMax);
  fixRange(cfg.airMin, cfg.airMax); fixRange(cfg.phMin, cfg.phMax); fixRange(cfg.doMin, cfg.doMax);
  cF("histerisis", cfg.histerisis);
  cF("faktorPorsi", cfg.faktorPorsi); cF("gramPerBlok", cfg.gramPerBlok); cF("detikPerBlok", cfg.detikPerBlok);
  cF("turunBawah", cfg.turunBawah); cI("maxManualDetik", cfg.maxManualDetik);
  cS("waktuPagi", cfg.waktuPagi); cS("waktuMalam", cfg.waktuMalam); cI("zonaWaktuJam", cfg.zonaWaktuJam);
  cI("pinRelayPakan", cfg.pinRelayPakan); cI("pinRelay2", cfg.pinRelay2);
  cI("pinSuhuAir", cfg.pinSuhuAir); cI("pinDHT", cfg.pinDHT); cI("pinPH", cfg.pinPH); cI("pinDO", cfg.pinDO);
  cI("relayActiveLow", cfg.relayActiveLow);
  saveConfig();
  server.send(200, "text/plain", "OK");
  restartPending = true; restartAt = millis() + 1500;
}

void handleFeeder() {
  if (!auth()) return;
  bool on = server.arg("state") == "1";
  feederSet(on, on ? cfg.maxManualDetik : 0, "manual");
  server.send(200, "application/json", statusJson());
}

void handleLogSwitch() {
  if (!auth()) return;
  logSetSwitch(server.arg("state") == "1");
  server.send(200, "application/json", statusJson());
}

static void webOut(const uint8_t* p, size_t n) {
  server.sendContent((const char*)p, n);
  scheduleLoop(); relay2Loop();
  yield();
}

void handleLogXlsx() {
  uint32_t n = logN;
  uint32_t total = xlsxExport(nullptr, n, cfg.zonaWaktuJam * 3600);
  server.setContentLength(total);
  server.sendHeader("Content-Disposition", "attachment; filename=\"datalogs.xlsx\"");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", "");
  xlsxExport(webOut, n, cfg.zonaWaktuJam * 3600);
}

String f1(float v) { return isnan(v) ? "null" : String(v, 2); }

void handleLogs() {
  const uint32_t PER_HAL = 100;
  uint32_t total = logN;
  uint32_t pages = total ? (total + PER_HAL - 1) / PER_HAL : 1;
  uint32_t page = server.hasArg("page") ? (uint32_t)server.arg("page").toInt() : 1;
  if (page < 1) page = 1;
  if (page > pages) page = pages;
  bool desc = server.arg("order") == "desc";
  uint32_t start = 0, count = 0;
  if (total > 0) {
    if (desc) { uint32_t end = total - (page - 1) * PER_HAL; start = end > PER_HAL ? end - PER_HAL : 0; count = end - start; }
    else { start = (page - 1) * PER_HAL; count = (total - start > PER_HAL) ? PER_HAL : (total - start); }
  }
  static LogRec rows[100];
  uint32_t got = 0;
  if (count > 0 && recOpen()) {
    gRec.seek((size_t)start * sizeof(LogRec));
    while (got < count && recNext(rows[got])) got++;
    recClose();
  }
  String j; j.reserve(got * 65 + 80);
  j = "{\"total\":" + String(total) + ",\"page\":" + String(page) + ",\"pages\":" + String(pages) + ",\"rows\":[";
  for (uint32_t i = 0; i < got; i++) {
    uint32_t k = desc ? (got - 1 - i) : i;
    const LogRec& r = rows[k];
    time_t tt = (time_t)r.epoch + cfg.zonaWaktuJam * 3600;
    struct tm tm; gmtime_r(&tt, &tm);
    char ts[20]; strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", &tm);
    if (i) j += ",";
    j += "[" + String(start + k + 1) + ",\"" + ts + "\"," + f1(r.hum) + "," + f1(r.air) + "," + f1(r.udara) + "," + f1(r.ph) + "," + f1(r.doVal) + "]";
  }
  j += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

void handleClearLog() {
  if (!auth()) return;
  logClear();
  nextLog = 0;
  server.send(200, "text/plain", "OK");
}

// ======================= 9. SETUP & LOOP MAIN =======================
bool staMode = false;
bool apOn = false;
DNSServer dnsServer;
const char* AP_SSID = "PakanIkan-Setup";
const char* AP_PASS = "12345678";

void onWifiConnected() {
  Serial.println("WiFi Connected. IP: " + WiFi.localIP().toString());
  configTime(cfg.zonaWaktuJam * 3600, 0, "pool.ntp.org", "time.google.com");
  if (MDNS.begin("pakan")) MDNS.addService("http", "tcp", 80);
}

void startAP() {
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(200);
  dnsServer.start(53, "*", WiFi.softAPIP());
  apOn = true;
}

void wifiLoop() {
  static unsigned long last = 0;
  if (apOn) dnsServer.processNextRequest();
  bool ok = (WiFi.status() == WL_CONNECTED);
  if (ok && !staMode) {
    staMode = true;
    onWifiConnected();
    return;
  }
  if (!ok) {
    staMode = false;
    if (millis() - last < 30000) return;
    last = millis();
    if (apOn && WiFi.softAPgetStationNum() > 0) return;
    WiFi.disconnect();
    WiFi.begin(cfg.namaWifi.c_str(), cfg.passWifi.c_str());
  }
}

void startWifi() {
  WiFi.persistent(false);
  WiFi.setHostname("pakan");
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  startAP();
  WiFi.begin(cfg.namaWifi.c_str(), cfg.passWifi.c_str());
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(250);
  if (WiFi.status() == WL_CONNECTED) {
    staMode = true;
    onWifiConnected();
  }
}

void handleNotFound() {
  if (apOn) {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

void setup() {
  Serial.begin(115200);
  loadConfig();
  
  pinMode(cfg.pinRelayPakan, OUTPUT); 
  pinMode(cfg.pinRelay2, OUTPUT);
  relayWrite(cfg.pinRelayPakan, false); 
  relayWrite(cfg.pinRelay2, false);
  
  dtAir.begin();
  dhtObj.begin();
  analogReadResolution(12);
  
  if (!LittleFS.begin(true)) Serial.println("LittleFS Mount Failed");
  logInit();
  
  startWifi();
  Blynk.config(BLYNK_AUTH_TOKEN);

  // Routes Local Web Server
  server.on("/", []() { String h = FPSTR(HOME_HTML); h.replace("%STYLE%", FPSTR(STYLE)); server.send(200, "text/html; charset=utf-8", h); });
  server.on("/logs", []() { String h = FPSTR(LOGS_HTML); h.replace("%STYLE%", FPSTR(STYLE)); server.send(200, "text/html; charset=utf-8", h); });
  server.on("/api/status", []() { server.send(200, "application/json", statusJson()); });
  server.on("/admin", []() { if (auth()) server.send(200, "text/html; charset=utf-8", adminPage()); });
  server.on("/api/config", HTTP_POST, handleConfig);
  server.on("/api/feeder", HTTP_POST, handleFeeder);
  server.on("/datalogs.xlsx", HTTP_GET, handleLogXlsx);
  server.on("/api/logs", HTTP_GET, handleLogs);
  server.on("/api/logswitch", HTTP_POST, handleLogSwitch);
  server.on("/api/clearlog", HTTP_POST, handleClearLog);
  server.onNotFound(handleNotFound);
  server.begin();

  // Blynk Timer kirim data per 2 detik
  blynkTimer.setInterval(2000L, sendDataToBlynk);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.run();
  }
  blynkTimer.run();
  server.handleClient();
  sensorLoop();
  relay2Loop();
  scheduleLoop();
  logLoop();
  wifiLoop();

  if (restartPending && millis() > restartAt) ESP.restart();
}
// Tambahkan Virtual Pin V9 untuk menyimpankan URL download
void sendDataToBlynk() {
  if (Blynk.connected()) {
    Blynk.virtualWrite(V0, isnan(dalamAir) ? 0 : dalamAir);
    Blynk.virtualWrite(V1, isnan(valPH) ? 0 : valPH);
    Blynk.virtualWrite(V2, isnan(valDO) ? 0 : valDO);
    Blynk.virtualWrite(V3, isnan(suhuDHT) ? 0 : suhuDHT);
    Blynk.virtualWrite(V4, isnan(kelembaban) ? 0 : kelembaban);
    
    float totalPakan = cfg.tebarPadat * cfg.faktorPorsi;
    float durasi = (totalPakan / 2.0 / cfg.gramPerBlok) * cfg.detikPerBlok;
    Blynk.virtualWrite(V8, durasi);

    // Kirim Link URL Download Excel Local Server ke Blynk V9
    String downloadUrl = "http://" + WiFi.localIP().toString() + "/datalogs.xlsx";
    Blynk.virtualWrite(V9, downloadUrl);
  }
}