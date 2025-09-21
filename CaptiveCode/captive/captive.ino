#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#define PUYA_SUPPORT 1

#include <LittleFS.h>   // Use LittleFS instead of SPIFFS

// Performance optimizations
extern "C" {
#include "user_interface.h"
}

// Captive portal configuration
const char *myHostname = "waves";            // Host header accepted as local
const char* NETWORK_NAME = "Digital_Traces";  // SSID broadcast
const char* nameOfTheFile = "/messages.txt"; // Stored messages

// Channel selection settings
// If FORCE_CHANNEL > 0 use that channel (1..13). If 0 and RANDOMIZE_CHANNEL true, pick from CHANNEL_OPTIONS.
// Otherwise fallback to DEFAULT_CHANNEL.
const int FORCE_CHANNEL = 0;              // Set 1..13 to enforce a channel, or 0 for logic below
const bool RANDOMIZE_CHANNEL = true;      // Randomize channel at boot (stays fixed afterward)
const uint8_t DEFAULT_CHANNEL = 1;        // Fallback channel
const uint8_t CHANNEL_OPTIONS[] = {3, 5, 10};
const uint8_t CHANNEL_OPTIONS_COUNT = sizeof(CHANNEL_OPTIONS)/sizeof(CHANNEL_OPTIONS[0]);
uint8_t chosenChannel = DEFAULT_CHANNEL;  // Resolved during setup()


// Global objects
const byte DNS_PORT = 53;
IPAddress apIP(8,8,8,8); // this fixes android 
IPAddress netMsk(255, 255, 255, 0);

DNSServer dnsServer;
ESP8266WebServer server(80);

String globalStringNetworks = "";            // Newline separated messages



/** Redirect to captive portal if we got a request for another domain. Return true in that case so the page handler do not try to handle the request again. */
boolean captivePortal() {
  if (!isIp(server.hostHeader()) && server.hostHeader() != (String(myHostname) + ".local")) {
    Serial.println("Redirecting using captivePortal()");
    server.sendHeader("Location", String("http://") + toStringIp(server.client().localIP()), true);
    server.send(200, "text/html", readHTMLFile("/index.html"));
    server.client().stop(); // Stop is needed because we sent no content length
    return true;
  }
  return false;
}

boolean isIp(String str) {
  for (size_t i = 0; i < str.length(); i++) {
    int c = str.charAt(i);
    if (c != '.' && (c < '0' || c > '9')) {
      return false;
    }
  }
  return true;
}

/** IP to String? */
String toStringIp(IPAddress ip) {
  String res = "";
  for (int i = 0; i < 3; i++) {
    res += String((ip >> (8 * i)) & 0xFF) + ".";
  }
  res += String(((ip >> 8 * 3)) & 0xFF);
  return res;
}

// HTML file reading 
String readHTMLFile(const char* filename) {
  File file = LittleFS.open(filename, "r");
  if (!file) {
    return F("<html><body><h1>Error: File not found</h1></body></html>");
  }
  
  String content = file.readString();
  file.close();
  return content;
}

// Send HTML with no-cache headers and anti-caching mechanisms
void sendHtml(const String& html) {
  if (captivePortal()) { // If captive portal redirect instead of displaying the page.
    return;
  }
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");

  server.send(200, "text/html", html);
}

void handleRoot() { sendHtml(readHTMLFile("/index.html")); }

void handleAbout() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");

  server.send(200, "text/html", readHTMLFile("/about.html"));

}

/** Wifi config page handler */
void handleWifi() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");

  
  server.send(200, "text/html", readHTMLFile("/index.html"));
  server.client().stop(); // Stop is needed because we sent no content length
}


/** Handle the WLAN save form and redirect to WLAN config page again */
void handleWifiSave() {
  // server.arg("n").toCharArray(ssid, sizeof(ssid) - 1);
  // server.arg("p").toCharArray(password, sizeof(password) - 1);
  server.sendHeader("Location", "wifi", true);
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send(302, "text/plain", "");    // Empty content inhibits Content-length header so we have to close the socket ourselves.
  server.client().stop(); // Stop is needed because we sent no content length
}



void handleNotFound() {
  if (captivePortal()) { // If caprive portal redirect instead of displaying the error page.
    return;
  }
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send(200, "text/html", readHTMLFile("/index.html"));
}

// File operations
String readMessagesFile() {
  File f = LittleFS.open(nameOfTheFile, "r");
  if (!f) return "";
  
  String data = f.readString();
  f.close();
  return data;
}

void writeMessagesFile(const String& content) {
  File f = LittleFS.open(nameOfTheFile, "w");
  if (f) {
    f.print(content);
    f.close();
  }
}

// Send message list to spammer device via Serial
void sendMessagesToSpammer() {
  if (globalStringNetworks.length() > 0) {
    // Frame format: <START>{payload}<END>
    // This allows receiver to ignore any boot garbage until <START>
    Serial.print("<START>");
    Serial.print(globalStringNetworks);
    Serial.println("<END>");
  }
}

// Count messages in a newline-separated string
int countMessages(const String& messages) {
  if (messages.length() == 0) return 0;
  
  int count = 1; // Start with 1 if there's any content
  for (int i = 0; i < messages.length(); i++) {
    if (messages.charAt(i) == '\n') {
      count++;
    }
  }
  return count;
}

// Trim messages to keep only the most recent 'maxMessages'
String trimToMaxMessages(const String& messages, int maxMessages) {
  if (messages.length() == 0) return "";
  
  int messageCount = countMessages(messages);
  if (messageCount <= maxMessages) {
    return messages;
  }
  
  // Find the position to start keeping messages
  int messagesToSkip = messageCount - maxMessages;
  int newlinesSeen = 0;
  int startPos = 0;
  
  for (int i = 0; i < messages.length(); i++) {
    if (messages.charAt(i) == '\n') {
      newlinesSeen++;
      if (newlinesSeen == messagesToSkip) {
        startPos = i + 1;
        break;
      }
    }
  }
  
  return messages.substring(startPos);
}

// Form handler with 20-message limit
void handleForm() {
  String message = server.arg("message");
  if (message.length() == 0) {
    
    handleRoot();
    return;
  }

  
  // Process file operations after response with message limit
  String oldMessages = readMessagesFile();
  String newMessages = message; // Start with new message
  
  if (oldMessages.length() > 0) {
    newMessages += "\n" + oldMessages; // Use newline separator
  }
  
  // Trim to maximum 20 messages
  newMessages = trimToMaxMessages(newMessages, 20);
  
  // Write to file
  writeMessagesFile(newMessages);
  globalStringNetworks = newMessages;
  
  // Send updated message list to spammer device
  sendMessagesToSpammer();

  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");

  server.send(200, "text/html", readHTMLFile("/success.html"));
}

void setup() {
  Serial.begin(9600);
  if(!LittleFS.begin()) {
    Serial.println("error starting littlfs");
    return;
  };




  // Decide on channel
  if (FORCE_CHANNEL > 0 && FORCE_CHANNEL <= 13) {
    chosenChannel = (uint8_t)FORCE_CHANNEL;
  } else if (RANDOMIZE_CHANNEL) {
    randomSeed(os_random() ^ micros());
    chosenChannel = CHANNEL_OPTIONS[random(CHANNEL_OPTIONS_COUNT)];
  } else {
    chosenChannel = DEFAULT_CHANNEL;
  }
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  delay(2000);  // Without delay I've seen the IP address blank

  bool apOk = WiFi.softAP(NETWORK_NAME, "", chosenChannel, false, 12);
  if (apOk) {
    Serial.printf("[AP] Started SSID='%s' ch=%u IP=%s\n", NETWORK_NAME, chosenChannel, apIP.toString().c_str());
  } else {
    Serial.println("[AP][ERROR] softAP start failed");
  }
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
  WiFi.setOutputPower(20.5);
  wifi_set_sleep_type(NONE_SLEEP_T);  // Disable WiFi sleep for better stability

  delay(2000); // Without delay I've seen the IP address blank

  // if DNSServer is started with "*" for domain name, it will reply with
  // provided IP to all DNS request
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.setTTL(300);  // Set DNS TTL to 5 minutes
  dnsServer.start(DNS_PORT, "*", apIP);

  server.on("/", handleRoot);
  server.on("/about", handleAbout);
  server.on("/wifi", handleWifi);
  server.on("/wifisave", handleWifiSave);
  server.on("/generate_204", handleRoot);  //Android captive portal. Maybe not needed. Might be handled by notFound handler.
  server.on("/fwlink", handleRoot);  //Microsoft captive portal. Maybe not needed. Might be handled by notFound handler.
  
  // iPhone/iOS captive portal handlers
  //server.on("/hotspot-detect.html", handleRoot);  // iOS captive portal detection
  //server.on("/library/test/success.html", handleRoot);  // iOS captive portal success page
  //server.on("/captive", handleRoot);  // Generic captive portal
    /*

  // Additional common captive portal endpoints
  server.on("/ncsi.txt", handleRoot);  // Windows Network Connectivity Status Indicator
  server.on("/connecttest.txt", handleRoot);  // Windows 10 captive portal
  server.on("/redirect", handleRoot);  // Generic redirect endpoint
  server.on("/success.txt", handleRoot);  // Generic success check
*/
  

  server.onNotFound(handleNotFound);


  server.on("/message", HTTP_POST, handleForm); // Ensure form route exists

  server.begin();

  
  // Load stored messages for spammer broadcast
  globalStringNetworks = readMessagesFile();
  sendMessagesToSpammer();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  delay(50);
}