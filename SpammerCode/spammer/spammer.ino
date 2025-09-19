
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

// run-time variables
uint8_t channelIndex = 0;
uint8_t macAddr[6];
uint8_t wifi_channel = 1;
uint32_t currentTime = 0;
uint32_t packetSize = 0;
uint32_t packetCounter = 0;
uint32_t attackTime = 0;
uint32_t packetRateTime = 0;


int numberOfNetworksToPropagate = 12;

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

// spammer
const uint8_t channels[] = {6, 9,  11}; // used Wi-Fi channels (available: 1-14)
const bool wpa2 = true; // WPA2 networks
const bool appendSpaces = true; // makes all SSIDs 32 characters long to improve performance


uint32_t INTERVAL = 20; //102.4 milliseconds
String globalStringNetworks = "";

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

// funcitons for spamming networks
// generates random MAC
void randomMac() {
  for (int i = 0; i < 6; i++)
    macAddr[i] = random(256);
}

void setupSpammer () {
  // create empty SSID
  for (int i = 0; i < 32; i++)
    emptySSID[i] = ' ';

  // for random generator
  randomSeed(os_random());

  // set packetSize
  packetSize = sizeof(beaconPacket);
  if (wpa2) {
    beaconPacket[34] = 0x31;
  } else {
    beaconPacket[34] = 0x21;
    packetSize -= 26;
  }

  // generate random mac address
  randomMac();
// get time
  currentTime = millis();

  // set channel
  wifi_set_channel(channels[0]);

}

// goes to next channel
void nextChannel() {
  randomMac();
  if(sizeof(channels) > 1){
    uint8_t ch = channels[channelIndex];
    channelIndex++;
    if (channelIndex > sizeof(channels)) channelIndex = 0;
  
    if (ch != wifi_channel && ch >= 1 && ch <= 14) {
      wifi_channel = ch;
      wifi_set_channel(wifi_channel);
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
  WiFi.setOutputPower(999);
  setupSpammer();
  
  Serial.println("WiFi spammer initialized");
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
        // Payload is data before <END>
        globalStringNetworks = receivedBuffer.substring(0, endPos);
        Serial.println(">> <END> detected. Complete payload received.");
        Serial.println(">> Payload (messages):");
        Serial.println(globalStringNetworks);
        Serial.printf(">> Total messages to broadcast: %d\n", countMessages(globalStringNetworks));
        // Reset for next frame
        receivedBuffer = "";
        inFrame = false;
      }
    }
  }

  
  currentTime = millis();

  // send out SSIDs
  if (currentTime - attackTime > INTERVAL) {
    
    if (globalStringNetworks.length() == 0) {
      // No messages to broadcast, just wait
      attackTime = currentTime;
      return;
    }
    
    Serial.printf(">> Broadcasting cycle started (Channel %d)\n", wifi_channel);

    char ssids[globalStringNetworks.length()+1];
    globalStringNetworks.toCharArray(ssids, globalStringNetworks.length()+1) ;
   
    attackTime = currentTime;

    // temp variables
    int i = 0;
    int j = 0;
    int ssidNum = 1;
    char tmp;
    int ssidsLen = strlen_P(ssids);

    bool sent = false;
    
    // go to next channel
    nextChannel();

    while (i < ssidsLen) {
      // read out next SSID - now using newline separator instead of underscore
      j = 0;
      do {
        tmp = pgm_read_byte(ssids + i + j);
        j++;
      } while (tmp != '\n' && j <= 32 && i + j < ssidsLen);

      uint8_t ssidLen = j - 1;

      // Log the message being broadcast
      char messageBuffer[33];
      memcpy_P(messageBuffer, &ssids[i], ssidLen);
      messageBuffer[ssidLen] = '\0';
      Serial.printf(">> Broadcasting message %d: '%s' (len:%d)\n", ssidNum, messageBuffer, ssidLen);
      
      // set MAC address
      macAddr[5] = ssidNum;
      ssidNum++;
      if (ssidNum > numberOfNetworksToPropagate) {
        break;
      }
      // write MAC address into beacon frame
      memcpy(&beaconPacket[10], macAddr, 6);
      memcpy(&beaconPacket[16], macAddr, 6);

      // reset SSID
      memcpy(&beaconPacket[38], emptySSID, 32);

      // write new SSID into beacon frame
      memcpy_P(&beaconPacket[38], &ssids[i], ssidLen);

      // set channel for beacon frame
      beaconPacket[82] = wifi_channel;

      // send packet
      if(appendSpaces){
        for(int k=0;k<5;k++){
          packetCounter += wifi_send_pkt_freedom(beaconPacket, packetSize, 0) == 0;
          delay(1);
        }
      }
      
      // remove spaces
      else {
        
        uint16_t tmpPacketSize = (packetSize - 32) + ssidLen; // calc size
        uint8_t* tmpPacket = new uint8_t[tmpPacketSize]; // create packet buffer
        memcpy(&tmpPacket[0], &beaconPacket[0], 38 + ssidLen); // copy first half of packet into buffer
        tmpPacket[37] = ssidLen; // update SSID length byte
        memcpy(&tmpPacket[38 + ssidLen], &beaconPacket[70], wpa2 ? 39 : 13); // copy second half of packet into buffer

        // send packet
        for(int k=0;k<3;k++){
          packetCounter += wifi_send_pkt_freedom(tmpPacket, tmpPacketSize, 0) == 0;
          delay(1);
        }

        delete tmpPacket; // free memory of allocated buffer
      }

      i += j;
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
