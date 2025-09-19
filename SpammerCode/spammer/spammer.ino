
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#define PUYA_SUPPORT 1
#include <FS.h>   //Include File System Headers

// Spammer code

char emptySSID[32];

extern "C" {
#include "user_interface.h"
  typedef void (*freedom_outside_cb_t)(uint8 status);
  int wifi_register_send_pkt_freedom_cb(freedom_outside_cb_t cb);
  void wifi_unregister_send_pkt_freedom_cb(void);
  int wifi_send_pkt_freedom(uint8 *buf, int len, bool sys_seq);
}
// ==================== //

// ================= Runtime / State =================
uint8_t channelIndex = 0;
uint8_t wifi_channel = 1;
uint32_t currentTime = 0;
uint32_t packetCounter = 0;
uint16_t packetSize = 0;        // size of beacon frame currently used
uint32_t packetRateTime = 0;
uint32_t channelSwitchTime = 0;
uint32_t lastBeaconTime = 0;      // per-beacon pacing
uint32_t lastPerApBeaconTime = 0;  // pacing inside round robin

// Configuration (can be tuned)
const uint16_t BEACON_SPACING_MS = 12;      // time between beacons (per AP round robin) ~8-15ms typical
const uint16_t PER_AP_BURST = 1;            // how many beacons per AP per round robin iteration
const uint32_t CHANNEL_DWELL_MS = 5000;     // dwell on a channel before hopping
const uint8_t  MAX_FAKE_APS = 10;           // hard limit

// Stable network table
struct FakeAP {
  char ssid[33];      // null terminated
  uint8_t length;     // actual length
  uint8_t bssid[6];   // fixed BSSID (locally administered)
  bool active;        // slot in use
};

FakeAP apTable[MAX_FAKE_APS];
uint8_t apCount = 0;        // number of active APs
uint8_t rrIndex = 0;        // round‑robin index
bool payloadReady = false;  // indicates apTable populated

// Diagnostics flags
bool DEBUG_BEACONS = false;
bool DEBUG_PARSE = true;
bool DEBUG_CHANNEL = true;

// beacon frame definition
uint8_t beaconPacket[109] = {
  /*  0 - 3  */ 0x80, 0x00, 0x00, 0x00, // Type/Subtype: managment beacon frame
  /*  4 - 9  */ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination: broadcast
  /* 10 - 15 */ 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, // Source
  /* 16 - 21 */ 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, // Source

  // Fixed parameters
  /* 22 - 23 */ 0x00, 0x00, // Fragment & sequence number (will be done by the SDK)
  /* 24 - 31 */ 0x83, 0x51, 0xf7, 0x8f, 0x0f, 0x00, 0x00, 0x00, // Timestamp
  /* 32 - 33 */ 0x14, 0x00, // Interval: 0x64, 0x00 => every 100ms - 0xe8, 0x03 => every 1s
  /* 34 - 35 */ 0x31, 0x00, // capabilities Tnformation

  // Tagged parameters

  // SSID parameters
  /* 36 - 37 */ 0x00, 0x20, // Tag: Set SSID length, Tag length: 32
  /* 38 - 69 */ 0x20, 0x20, 0x20, 0x20,
  0x20, 0x20, 0x20, 0x20,
  0x20, 0x20, 0x20, 0x20,
  0x20, 0x20, 0x20, 0x20,
  0x20, 0x20, 0x20, 0x20,
  0x20, 0x20, 0x20, 0x20,
  0x20, 0x20, 0x20, 0x20,
  0x20, 0x20, 0x20, 0x20, // SSID

  // Supported Rates
  /* 70 - 71 */ 0x01, 0x08, // Tag: Supported Rates, Tag length: 8
  /* 72 */ 0x82, // 1(B)
  /* 73 */ 0x84, // 2(B)
  /* 74 */ 0x8b, // 5.5(B)
  /* 75 */ 0x96, // 11(B)
  /* 76 */ 0x24, // 18
  /* 77 */ 0x30, // 24
  /* 78 */ 0x48, // 36
  /* 79 */ 0x6c, // 54

  // Current Channel
  /* 80 - 81 */ 0x03, 0x01, // Channel set, length
  /* 82 */      0x01,       // Current Channel

  // RSN information
  /*  83 -  84 */ 0x30, 0x18,
  /*  85 -  86 */ 0x01, 0x00,
  /*  87 -  90 */ 0x00, 0x0f, 0xac, 0x02,
  /*  91 -  92 */ 0x02, 0x00,
  /*  93 - 100 */ 0x00, 0x0f, 0xac, 0x04, 0x00, 0x0f, 0xac, 0x04, /*Fix: changed 0x02(TKIP) to 0x04(CCMP) is default. WPA2 with TKIP not supported by many devices*/
  /* 101 - 102 */ 0x01, 0x00,
  /* 103 - 106 */ 0x00, 0x0f, 0xac, 0x02,
  /* 107 - 108 */ 0x00, 0x00
};

// Channels to use
const uint8_t channels[] = {1, 6, 11}; // standard non-overlapping set
const bool wpa2 = true;                // WPA2 IE included
const bool padTo32 = false;            // variable-length SSID tag to avoid trailing space artifacts
String globalStringNetworks = "";     // raw incoming payload (newline separated)

// Count messages in newline-separated string
int countMessages(const String& messages) {
  if (messages.length() == 0) return 0;
  
  int count = 1;
  for (int i = 0; i < messages.length(); i++) {
    if (messages.charAt(i) == '\n') {
      count++;
    }
  }
  return count;
}

// ================= Helpers =================
void clearApTable() {
  for (uint8_t i = 0; i < MAX_FAKE_APS; i++) {
    apTable[i].ssid[0] = '\0';
    apTable[i].length = 0;
    apTable[i].active = false;
  }
  apCount = 0;
  rrIndex = 0;
  payloadReady = false;
}

// Deterministic locally administered BSSID generator from index
void makeBssid(uint8_t idx, uint8_t out[6]) {
  // Locally administered unicast MAC: set bit1 (local), clear bit0 (unicast)
  out[0] = 0x02;           // 00000010 -> local admin
  out[1] = 0x11;
  out[2] = 0x22;
  out[3] = 0x33;
  out[4] = 0x44;
  out[5] = 0x50 + (idx & 0x3F); // up to 64 variations
}

void parseNetworks(const String &payload) {
  clearApTable();
  if (payload.length() == 0) return;
  uint16_t start = 0;
  uint16_t len = payload.length();
  uint8_t slot = 0;
  while (start < len && slot < MAX_FAKE_APS) {
    int nl = payload.indexOf('\n', start);
    int end = (nl == -1) ? len : nl;
    int rawLen = end - start;
    // trim CR or spaces
    while (rawLen > 0 && (payload.charAt(start + rawLen - 1) == '\r' || payload.charAt(start + rawLen - 1) == ' ')) rawLen--;
    while (rawLen > 0 && payload.charAt(start) == ' ') { start++; rawLen--; }
    if (rawLen <= 0) { start = end + 1; continue; }
    if (rawLen > 32) rawLen = 32; // cap
    // Strip matching surrounding quotes '...' or "..."
    if (rawLen >= 2) {
      char first = payload.charAt(start);
      char last  = payload.charAt(start + rawLen - 1);
      if ((first == '\'' && last == '\'') || (first == '"' && last == '"')) {
        start++; rawLen -= 2; // remove surrounding quotes
      }
    }
    // copy into table
    for (int i = 0; i < rawLen; i++) {
      apTable[slot].ssid[i] = payload.charAt(start + i);
    }
    apTable[slot].ssid[rawLen] = '\0';
    apTable[slot].length = rawLen;
    makeBssid(slot, apTable[slot].bssid);
    apTable[slot].active = true;
    slot++;
    start = end + 1;
  }
  apCount = slot;
  payloadReady = apCount > 0;
  if (DEBUG_PARSE) {
    Serial.printf("[PARSE] Loaded %u AP entries\n", apCount);
    for (uint8_t i = 0; i < apCount; i++) {
      Serial.printf("  #%u SSID='%s' len=%u BSSID=%02X:%02X:%02X:%02X:%02X:%02X\n", i,
                    apTable[i].ssid, apTable[i].length,
                    apTable[i].bssid[0], apTable[i].bssid[1], apTable[i].bssid[2],
                    apTable[i].bssid[3], apTable[i].bssid[4], apTable[i].bssid[5]);
    }
  }
}

// ================= Beacon Template =================
// We'll reuse original beaconPacket as a template but will not mutate SSID length tag if padTo32.
// For variable-length (if padTo32=false) we'd reconstruct portions; here keep constant length for speed.

void setupSpammer () {
  for (int i = 0; i < 32; i++) emptySSID[i] = ' ';
  randomSeed(os_random());
  // Determine packet size once; if not wpa2 shorten by RSN IE size (26 bytes removed earlier logic)
  packetSize = sizeof(beaconPacket);
  if (!wpa2) {
    beaconPacket[34] = 0x21;
    packetSize -= 26;
  } else {
    beaconPacket[34] = 0x31;
  }
  wifi_set_channel(channels[0]);
  channelIndex = 0;
  currentTime = millis();
  lastBeaconTime = currentTime;
  channelSwitchTime = currentTime;
}

void nextChannel() {
  if (sizeof(channels) > 1) {
    channelIndex++;
    if (channelIndex >= sizeof(channels)) channelIndex = 0;
    uint8_t ch = channels[channelIndex];
    if (ch != wifi_channel && ch >= 1 && ch <= 14) {
      wifi_channel = ch;
      wifi_set_channel(wifi_channel);
      if (DEBUG_CHANNEL) Serial.printf("[CHAN] -> %d\n", wifi_channel);
    }
  }
}

void setup() {
  Serial.begin(9600); // Match original baud rate to avoid corruption
  delay(1000);
  
  Serial.println("=== SPAMMER DEVICE STARTING ===");
  Serial.println("Waiting for message data from captive portal...");

  // start WiFi
  WiFi.mode(WIFI_OFF);
  wifi_set_opmode(STATION_MODE);
  WiFi.setOutputPower(20.5); // Set to maximum legal power (20.5 dBm)
  setupSpammer();
  
  // Initialize timing variables
  currentTime = millis();
  packetRateTime = currentTime;
  channelSwitchTime = currentTime;
  lastPerApBeaconTime = currentTime;
  
  Serial.println("WiFi spammer initialized");
  Serial.println("Configuration:");
  Serial.printf("- Beacon spacing: %dms\n", BEACON_SPACING_MS);
  Serial.printf("- Channel dwell: %lums\n", (unsigned long)CHANNEL_DWELL_MS);
  Serial.printf("- Max fake APs: %u\n", MAX_FAKE_APS);
  Serial.println("Ready to receive and broadcast messages");
}

String receivedBuffer = ""; // Buffer to accumulate incoming payload between markers
bool inFrame = false;        // Have we seen <START> marker
const size_t MAX_BUFFER_SIZE = 2048; // Safety guard

void loop() {
  while (Serial.available()) {
    char raw = (char)Serial.read();
    char c = raw;
    // Filter out obvious noise (non-printable except markers characters '<','>')
    if ((c < 32 || c > 126) && c != '<' && c != '>' && c != '\n' && c != '\r') {
      // Skip silently; could also count skipped bytes
      continue;
    }
    // Normalize CR to nothing (we treat only LF implicitly when markers appear)
    if (c == '\r') continue;

    receivedBuffer += c; // Temporarily store to search for markers

    // Limit size early to avoid runaway growth if markers missing
    if (receivedBuffer.length() > MAX_BUFFER_SIZE) {
      Serial.println(">> WARNING: Buffer overflow without END marker. Resetting state.");
      receivedBuffer = "";
      inFrame = false;
      continue;
    }

    if (!inFrame) {
      // Look for <START>
      int startPos = receivedBuffer.indexOf("<START>");
      if (startPos >= 0) {
        // Discard everything before <START>
        if (startPos > 0) {
          receivedBuffer.remove(0, startPos);
        }
        // Remove the <START> marker itself
        receivedBuffer.remove(0, 7); // length of <START>
        inFrame = true;
        // Prepare a clean payload buffer (reuse receivedBuffer)
        Serial.println(">> <START> detected. Beginning to capture payload...");
      } else {
        // Haven't found start marker yet; keep buffer from growing unbounded
        if (receivedBuffer.length() > 20) {
          // Keep last 20 chars for marker search
            receivedBuffer = receivedBuffer.substring(receivedBuffer.length() - 20);
        }
      }
    } else {
      // We are inside a frame, look for <END>
      int endPos = receivedBuffer.indexOf("<END>");
      if (endPos >= 0) {
        globalStringNetworks = receivedBuffer.substring(0, endPos);
        Serial.println(">> <END> detected. Parsing payload...");
        parseNetworks(globalStringNetworks);
        receivedBuffer = "";
        inFrame = false;
      }
    }
  }

  currentTime = millis();

  // Channel dwell / hop
  if (currentTime - channelSwitchTime > CHANNEL_DWELL_MS) {
    channelSwitchTime = currentTime;
    nextChannel();
  }

  // Beacon scheduling (round robin across stable AP table)
  if (payloadReady && apCount > 0) {
    if (currentTime - lastPerApBeaconTime >= BEACON_SPACING_MS) {
      lastPerApBeaconTime = currentTime;
      // select AP
      if (rrIndex >= apCount) rrIndex = 0;
      FakeAP &ap = apTable[rrIndex];
      rrIndex++;
      if (!ap.active) return; // skip if somehow inactive

      // Build frame in-place
      // BSSID + Source MAC (we mirror) at offsets 10..15 & 16..21
      memcpy(&beaconPacket[10], ap.bssid, 6);
      memcpy(&beaconPacket[16], ap.bssid, 6);
      // SSID field: tag length at [37], content at [38]
      if (padTo32) {
        beaconPacket[37] = 32; // keep constant
        memcpy(&beaconPacket[38], emptySSID, 32);
        memcpy(&beaconPacket[38], ap.ssid, ap.length);
        // channel & timestamp handled below, then direct send of beaconPacket
      } else {
        // Variable-length rebuild: base template pieces
        const uint8_t *tpl = beaconPacket; // use current mutated (BSSID already written)
        const uint8_t SSID_TAG_OFFSET = 36; // tag number at 36, length at 37
        const uint8_t SSID_DATA_OFFSET = 38; // start of SSID data
        const uint8_t TAIL_TEMPLATE_OFFSET = 70; // where supported rates tag started in original template
        uint8_t tailLen = packetSize - TAIL_TEMPLATE_OFFSET; // remaining IEs length in template
        uint8_t ssidLen = ap.length;
        if (ssidLen > 32) ssidLen = 32;
        // new dynamic size: header (first 38 bytes including tag+len+start of data) + ssidLen + tailLen
        // But first 38 bytes includes 2 bytes tag+len + start of SSID data position.
        uint16_t dynSize = SSID_DATA_OFFSET + ssidLen + tailLen;
        if (dynSize > sizeof(beaconPacket)) dynSize = sizeof(beaconPacket); // safety cap
        uint8_t frame[109];
        // Copy bytes up to SSID tag data start
        memcpy(frame, tpl, SSID_DATA_OFFSET); // includes tag + placeholder len + start position
        // Set SSID length tag
        frame[37] = ssidLen;
        // Copy SSID bytes
        memcpy(&frame[SSID_DATA_OFFSET], ap.ssid, ssidLen);
        // Copy tail (rates + channel + RSN etc.) right after SSID
        memcpy(&frame[SSID_DATA_OFFSET + ssidLen], &tpl[TAIL_TEMPLATE_OFFSET], tailLen);
        // Fix channel byte location: originally at offset 82 in padded frame; new offset shifts by (32 - ssidLen)
        uint8_t padReduction = 32 - ssidLen;
        uint16_t newChannelOffset = 82 - padReduction; // adjust position
        if (newChannelOffset < dynSize) frame[newChannelOffset] = wifi_channel;
        // Timestamp update (low 4 bytes) stays at 24..27 (unchanged by shift)
        uint32_t tsLow2 = micros();
        memcpy(&frame[24], &tsLow2, 4);
        // Transmit dynamic frame
        for (uint8_t n = 0; n < PER_AP_BURST; n++) {
          int r = wifi_send_pkt_freedom(frame, dynSize, 1);
          if (r == 0) packetCounter++; else if (DEBUG_BEACONS) Serial.printf("[ERR] vlen send=%d\n", r);
          delay(1);
        }
        if (DEBUG_BEACONS) Serial.printf("[BEACON-VAR] SSID='%s' len=%u ch=%d size=%u\n", ap.ssid, ssidLen, wifi_channel, dynSize);
        return; // avoid padded path send below
      }
      // channel
      beaconPacket[82] = wifi_channel;
      // (Optional) update timestamp bytes 24..31 for more realism
      uint32_t tsLow = micros();
      memcpy(&beaconPacket[24], &tsLow, 4); // coarse (not full 8-byte TSF fidelity)

      // Padded (fixed-length) path transmit
      for (uint8_t n = 0; n < PER_AP_BURST; n++) {
        int r = wifi_send_pkt_freedom((uint8_t*)beaconPacket, packetSize, 1);
        if (r == 0) packetCounter++; else if (DEBUG_BEACONS) Serial.printf("[ERR] send=%d\n", r);
        delay(1);
      }
      if (DEBUG_BEACONS) Serial.printf("[BEACON-FIX] SSID='%s' ch=%d\n", ap.ssid, wifi_channel);
    }
  }

  // show packet-rate each second
  if (currentTime - packetRateTime > 1000) {
    packetRateTime = currentTime;
    Serial.print("Packets/s: ");
    Serial.println(packetCounter);
    packetCounter = 0;
  }
}
