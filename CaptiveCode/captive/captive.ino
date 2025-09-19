#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#define PUYA_SUPPORT 1

#include <LittleFS.h>   // Use LittleFS instead of SPIFFS

// Performance optimizations
extern "C" {
#include "user_interface.h"
}

// Captive portal configuration
const char* NETWORK_NAME = "CrystalTags";  // SSID broadcast
const char* nameOfTheFile = "/messages.txt"; // Stored messages

// Channel selection settings
// If FORCE_CHANNEL > 0 use that channel (1..13). If 0 and RANDOMIZE_CHANNEL true, pick from CHANNEL_OPTIONS.
// Otherwise fallback to DEFAULT_CHANNEL.
const int FORCE_CHANNEL = 0;              // Set 1..13 to enforce a channel, or 0 for logic below
const bool RANDOMIZE_CHANNEL = true;      // Randomize channel at boot (stays fixed afterward)
const uint8_t DEFAULT_CHANNEL = 1;        // Fallback channel
const uint8_t CHANNEL_OPTIONS[] = {1, 6, 11};
const uint8_t CHANNEL_OPTIONS_COUNT = sizeof(CHANNEL_OPTIONS)/sizeof(CHANNEL_OPTIONS[0]);
uint8_t chosenChannel = DEFAULT_CHANNEL;  // Resolved during setup()


// Global objects
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);

DNSServer dnsServer;
AsyncWebServer server(80);

String globalStringNetworks = "";            // Newline separated messages


/** Redirect to captive portal if we got a request for another domain. Return true in that case so the page handler do not try to handle the request again. */
boolean captivePortal(AsyncWebServerRequest *request) {
  String hostHeader = request->host();
  if (!isIp(hostHeader)) {
    Serial.println("Redirecting using captivePortal()");
    AsyncWebServerResponse *response = request->beginResponse(302);
    response->addHeader("Location", String("http://") + toStringIp(WiFi.softAPIP()));
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "-1");
    request->send(response);
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
void sendHtml(AsyncWebServerRequest *request, const String& html) {
  if (captivePortal(request)) { // If captive portal redirect instead of displaying the page.
    return;
  }
  AsyncWebServerResponse *response = request->beginResponse(200, "text/html", html);
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "-1");
  request->send(response);
}

void handleRoot(AsyncWebServerRequest *request) { 
  sendHtml(request, readHTMLFile("/index.html")); 
}

void handleAbout(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response = request->beginResponse(200, "text/html", readHTMLFile("/about.html"));
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "-1");
  request->send(response);
}

/** Wifi config page handler */
void handleWifi(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response = request->beginResponse(200, "text/html", readHTMLFile("/index.html"));
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "-1");
  request->send(response);
}


/** Handle the WLAN save form and redirect to WLAN config page again */
void handleWifiSave(AsyncWebServerRequest *request) {
  // server.arg("n").toCharArray(ssid, sizeof(ssid) - 1);
  // server.arg("p").toCharArray(password, sizeof(password) - 1);
  AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "");
  response->addHeader("Location", "wifi");
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "-1");
  request->send(response);
}

void handleNotFound(AsyncWebServerRequest *request) {
  if (captivePortal(request)) { // If captive portal redirect instead of displaying the error page.
    return;
  }
  AsyncWebServerResponse *response = request->beginResponse(200, "text/html", readHTMLFile("/index.html"));
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "-1");
  request->send(response);
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
void handleForm(AsyncWebServerRequest *request) {
  String message = "";
  if (request->hasParam("message", true)) {
    message = request->getParam("message", true)->value();
  }
  
  if (message.length() == 0) {
    handleRoot(request);
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

  AsyncWebServerResponse *response = request->beginResponse(200, "text/html", readHTMLFile("/success.html"));
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "-1");
  request->send(response);
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
  WiFi.persistent(false);

  WiFi.softAPConfig(apIP, apIP, netMsk);
  
  // Try to start AP with retry logic
  bool apOk = false;
  for (int attempt = 0; attempt < 3; attempt++) {
    apOk = WiFi.softAP(NETWORK_NAME, "", chosenChannel, false, 15);
    if (apOk) {
      Serial.printf("[AP] Started SSID='%s' ch=%u IP=%s (attempt %d)\n", NETWORK_NAME, chosenChannel, apIP.toString().c_str(), attempt + 1);
      break;
    } else {
      Serial.printf("[AP][ERROR] softAP start failed (attempt %d)\n", attempt + 1);
      delay(1000);
    }
  }
  
  if (!apOk) {
    Serial.println("[AP][CRITICAL] Failed to start AP after 3 attempts!");
    return;
  }
  
  // Wait for AP to stabilize
  delay(2000);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
  
  // Set WiFi power and sleep settings for stability
  WiFi.setOutputPower(20.5);
  wifi_set_sleep_type(NONE_SLEEP_T);  // Disable WiFi sleep for better stability
  

  // if DNSServer is started with "*" for domain name, it will reply with
  // provided IP to all DNS request
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.setTTL(300);  // Set DNS TTL to 5 minutes
  dnsServer.start(DNS_PORT, "*", apIP);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/about", HTTP_GET, handleAbout);
  server.on("/wifi", HTTP_GET, handleWifi);
  server.on("/wifisave", HTTP_GET, handleWifiSave);
  server.on("/generate_204", HTTP_GET, handleRoot);  //Android captive portal. Maybe not needed. Might be handled by notFound handler.
  server.on("/fwlink", HTTP_GET, handleRoot);  //Microsoft captive portal. Maybe not needed. Might be handled by notFound handler.
  
  // iPhone/iOS captive portal handlers
  server.on("/hotspot-detect.html", HTTP_GET, handleRoot);  // iOS captive portal detection
  server.on("/library/test/success.html", HTTP_GET, handleRoot);  // iOS captive portal success page
  server.on("/captive", HTTP_GET, handleRoot);  // Generic captive portal
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
  delay(10);  // Small delay to prevent watchdog issues
}