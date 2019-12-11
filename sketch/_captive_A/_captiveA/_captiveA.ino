
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#define PUYA_SUPPORT 1
#include <FS.h>   //Include File System Headers

// #define SPAMMER 1

#define CAPTIVE 1

char emptySSID[32];

#ifdef SPAMMER
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

// beacon frame definition
uint8_t beaconPacket[109] = {
  /*  0 - 3  */ 0x80, 0x00, 0x00, 0x00, // Type/Subtype: managment beacon frame
  /*  4 - 9  */ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination: broadcast
  /* 10 - 15 */ 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, // Source
  /* 16 - 21 */ 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, // Source

  // Fixed parameters
  /* 22 - 23 */ 0x00, 0x00, // Fragment & sequence number (will be done by the SDK)
  /* 24 - 31 */ 0x83, 0x51, 0xf7, 0x8f, 0x0f, 0x00, 0x00, 0x00, // Timestamp
  /* 32 - 33 */ 0xe8, 0x03, // Interval: 0x64, 0x00 => every 100ms - 0xe8, 0x03 => every 1s
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
const uint8_t channels[] = {1, 6, 11}; // used Wi-Fi channels (available: 1-14)
const bool wpa2 = true; // WPA2 networks
const bool appendSpaces = true; // makes all SSIDs 32 characters long to improve performance
#endif

#ifdef CAPTIVE
// saver
const char *myHostname = "waves";

const char* filename = "/messages.txt";
const byte DNS_PORT = 53;
IPAddress apIP(8, 8, 8, 8);
IPAddress netMsk(255, 255, 255, 0);

DNSServer dnsServer;
ESP8266WebServer server(80);
#endif


String NETWORK_NAME = "˜˜LEAVE A WAVE NOTE˜˜";
uint32_t INTERVAL = 50;
String globalStringNetworks = "";

#ifdef CAPTIVE

 String responseHTML = "<!DOCTYPE html>\n<html>\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<head>\n    <link href=\"https://fonts.googleapis.com/css?family=Sulphur+Point&display=swap\" rel=\"stylesheet\"><title>Leave your message</title>\n</head>\n<body>\n  <div class=\"content\">\n    <h1>leave a note</h1>\n    <form class=\"form\" action=\"/message\" id=\"form1\">\n      <input maxlength=\"31\" type=\"text\" name=\"message\">\n      </input>\n    </form>\n    <button type=\"submit\" form=\"form1\" value=\"Submit\">\n      SEND\n    </button>\n  </div>\n  <div class=\"readmore\">\n    <a class=\"link\" href=\"/readmore\">read more</a>\n  </div>\n</body>\n<style>\n  * {\n    font-family: \"Sulphur Point\";\n  }\n  body {\n    background-color: #ffd300;\n    height: 100vh;\n    overflow-y: hidden;\n  }\n  .content {\n    background-color: #ffd300;\n    height: 100%;\n    overflow-y: hidden;\n    display: flex;\n    flex-direction: column;\n    justify-content: space-around;\n  }\n  h1 {\n    color: black;\n    text-align: center;\n    font-weight: 500;\n    margin: 30px;\n  }\n  .form {\n    height: 20px;\n    width: 80%;\n    margin: 0 auto;\n    margin-bottom: 50px;\n    display: flex\n  }\n  input {\n    height: 20px;\n    width: 100%;\n    font-size: 15px;\n    margin: 0 auto;\n    border-top: 0px;\n    border-left: 0px;\n    border-right: 0px;\n    outline: 0;\n    background-color: #ffd300;\n    border-bottom: 1px solid black;\n    padding:2px 2px;\n  }\n  button {\n    height: 35px;\n    width: 200px;\n    border: 0px solid black;\n    margin: 0 auto;\n    color: #ffd300;\n    font-weight: 1000;\n    background-color: black;\n    border-color: black;\n  }\n  button:hover {\n    background-color: red;\n    color: white\n  }\n\n  .readmore {\n    position: absolute;\n    bottom: 0;\n    height: 20px;\n    width: 100%;\n    color: black;\n    margin-bottom: 5px;\n  }\n  link {\n    color: black !important;\n  }\n  image {\n    position: static;\n    top: 0;\n  }\n</style>\n</html>";
#endif
void setup() {
  Serial.begin(9600);
  delay(1000);

#ifdef CAPTIVE

  //Initialize File System
  SPIFFS.begin();

  //Format File System
  // SPIFFS.format();
 
 
  // WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(NETWORK_NAME, "", 3, false, 8);
  delay(2000); // Without delay I've seen the IP address blank

  // if DNSServer is started with "*" for domain name, it will reply with
  // provided IP to all DNS request
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);

  server.on("/", handleRoot);
  server.on("/wifi", handleWifi);
  server.on("/wifisave", handleWifiSave);
  server.on("/generate_204", handleRoot);  //Android captive portal. Maybe not needed. Might be handled by notFound handler.
  server.on("/fwlink", handleRoot);  //Microsoft captive portal. Maybe not needed. Might be handled by notFound handler.
  
  
  // replay to all requests with same HTML
  // server.onNotFound([]() {
  //  server.send(200, "text/html", responseHTML);
  // });
  server.onNotFound(handleNotFound);


  server.on("/message", handleForm); //form action is handled here

  server.begin();
  delay(1000);

  globalStringNetworks = readFile();
  delay(5000);
  if (globalStringNetworks != "") {
    Serial.println(globalStringNetworks);    
  }
#endif

#ifdef SPAMMER

    // start WiFi
  WiFi.mode(WIFI_OFF);
  wifi_set_opmode(STATION_MODE);

  setupSpammer();
  #endif
}

#ifdef CAPTIVE

void handleRoot2 () {
  server.send(200, "text/html", responseHTML);
}
#endif

void loop() {
 #ifdef SPAMMER
  while(Serial.available()) {
    globalStringNetworks = Serial.readString();// read the incoming data as string
    Serial.println("received networks");
    Serial.println(globalStringNetworks);
  }

  #endif

  // spammer
  #ifdef SPAMMER

  currentTime = millis();

  // send out SSIDs
  if (currentTime - attackTime > INTERVAL) {

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
      // read out next SSID
      j = 0;
      do {
        tmp = pgm_read_byte(ssids + i + j);
        j++;
      } while (tmp != '_' && j <= 32 && i + j < ssidsLen);

      uint8_t ssidLen = j - 1;
      
      // set MAC address
      macAddr[5] = ssidNum;
      ssidNum++;

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
        for(int k=0;k<3;k++){
          packetCounter += wifi_send_pkt_freedom(beaconPacket, packetSize, 0) == 0;
          delay(2);
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
  #endif

  // handle people connecting
  #ifdef CAPTIVE
  dnsServer.processNextRequest();
  server.handleClient();
  #endif
}

#ifdef CAPTIVE

String readFile () {
    String data;

    int i;
    
    //Read File data
    File f = SPIFFS.open(filename, "r");
    
    if (f)
    {
        while(f.available()) {
          //Lets read line by line from the file
          String line = f.readString();
          data += line;
        }
        f.close();  //Close file
        return data;
    }
}
#endif

#ifdef SPAMMER

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
#endif
#ifdef CAPTIVE
void handleForm() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
 String message = server.arg("message"); 

 String oldNetworks = readFile();

 String finalToWrite = "";
 server.send(200, "text/html", responseHTML); //Send web page

  //Create New File And Write Data to It
  //w=Write Open file for writing
  File f = SPIFFS.open(filename, "w");
  
  if (f)
  {
      //Write data to file
      if (oldNetworks.length() > 1) {
        finalToWrite = message + "_" + oldNetworks;
      } else {
        finalToWrite = message;
      }
      
      f.print(finalToWrite);

      delay(10);

      globalStringNetworks = readFile();
      Serial.println(globalStringNetworks);

      f.close();  //Close file
  }


}
#endif

// funcitons for spamming networks
// generates random MAC
#ifdef SPAMMER

void randomMac() {
  for (int i = 0; i < 6; i++)
    macAddr[i] = random(256);
}
#endif
#ifdef SPAMMER

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
#endif
