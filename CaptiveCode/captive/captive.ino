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
const char* NETWORK_NAME = "Digital_Traces";  // No special chars, shorter name
const char* nameOfTheFile = "/messages.txt";
const byte DNS_PORT = 53;

// Use more standard captive portal IP
IPAddress apIP(192, 168, 4, 1);  // Standard AP IP
IPAddress netMsk(255, 255, 255, 0);

// Global objects
DNSServer dnsServer;
ESP8266WebServer server(80);
String globalStringNetworks = "";

// Performance counters
unsigned long lastHeapCheck = 0;
const unsigned long HEAP_CHECK_INTERVAL = 30000; // Check every 30 seconds

// Optimized captive portal detection
boolean captivePortal() {
  String hostHeader = server.hostHeader();
  
  // Fast check for common patterns
  if (hostHeader.length() == 0) return false;
  
  // Check if it's already our IP or local domain
  if (isIp(hostHeader) || 
      hostHeader.endsWith(".local") || 
      hostHeader == String(NETWORK_NAME)) {
    return false;
  }
  
  // Redirect to captive portal
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(302, "text/plain", "");
  server.client().stop();
  return true;
}

// Optimized IP check
boolean isIp(const String& str) {
  if (str.length() < 7 || str.length() > 15) return false; // Quick length check
  
  int dotCount = 0;
  for (size_t i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (c == '.') {
      dotCount++;
      if (dotCount > 3) return false;
    } else if (c < '0' || c > '9') {
      return false;
    }
  }
  return dotCount == 3;
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

// Send HTML with no-cache headers
void sendNoCacheHTML(const String& html) {
  if (html.length() == 0) {
    server.send(500, "text/plain", "Internal Server Error - HTML not loaded");
    return;
  }
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send(200, "text/html", html);
}

void handleRoot() {
  String html = readHTMLFile("/index.html");
  sendNoCacheHTML(html);
}

void handleAbout() {
  String html = readHTMLFile("/about.html");
  sendNoCacheHTML(html);
}

// Handlers for various captive portal endpoints
void handleWifi() {
  String html = readHTMLFile("/index.html");
  sendNoCacheHTML(html);
}

void handleConnecttest() {
  String html = readHTMLFile("/index.html");
  sendNoCacheHTML(html);
}

void handleWifiSave() {
  server.sendHeader("Location", "/", true);
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(302, "text/plain", "");
  server.client().stop();
}

void handleNotFound() {
  if (captivePortal()) {
    return;
  }
  String html = readHTMLFile("/index.html");
  sendNoCacheHTML(html);
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
  
  // Read success page from file
  String successHTML = readHTMLFile("/success.html");
  if (successHTML.indexOf("Error: File not found") >= 0) {
    server.send(500, "text/plain", "Success page not found");
    return;
  }
  
  // Send response immediately for better UX
  sendNoCacheHTML(successHTML);
  
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
}

void setup() {
  Serial.begin(115200);
  
  // System optimizations
  system_update_cpu_freq(160);
  
  // Initialize File System
  if (!LittleFS.begin()) {
    return;
  }
  
  // Configure WiFi
  delay(100);
  WiFi.setOutputPower(999);
  
  if (!WiFi.softAPConfig(apIP, apIP, netMsk)) {
    return;
  }
  
  bool apStarted = WiFi.softAP(NETWORK_NAME, "", 1, false, 6);
  if (!apStarted) {
    return;
  }
  
  delay(1000);
  
  if (WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) {
    return;
  }
  
  // DNS server for captive portal
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  if (!dnsServer.start(DNS_PORT, "*", apIP)) {
    return;
  }
  
  // Setup web server routes
  setupRoutes();
  server.onNotFound(handleNotFound);
  server.begin();
  
  delay(500);
  
  // Load existing messages and display them for spammer device
  globalStringNetworks = readMessagesFile();
  if (globalStringNetworks.length() > 0) {
    // Split and display each message for spammer to pick up
    String remainingMessages = globalStringNetworks;
    int messageNum = 1;
    
    while (remainingMessages.length() > 0) {
      int newlinePos = remainingMessages.indexOf('\n');
      String currentMessage;
      
      if (newlinePos == -1) {
        currentMessage = remainingMessages;
        remainingMessages = "";
      } else {
        currentMessage = remainingMessages.substring(0, newlinePos);
        remainingMessages = remainingMessages.substring(newlinePos + 1);
      }
      
      if (currentMessage.length() > 0) {
        Serial.printf("  %d: %s\n", messageNum, currentMessage.c_str());
        messageNum++;
      }
    }
  }
}

void setupRoutes() {
  // Main routes
  server.on("/", handleRoot);
  server.on("/index.html", handleRoot);
  server.on("/about", handleAbout);
  server.on("/message", HTTP_POST, handleForm);
  
  // Captive portal compatibility routes
  server.on("/generate_204", handleRoot);
  server.on("/gen_204", handleRoot);
  server.on("/hotspot-detect.html", handleRoot);
  server.on("/library/test/success.html", handleRoot);
  server.on("/fwlink", handleRoot);
  server.on("/ncsi.txt", handleConnecttest);
  server.on("/connecttest.txt", handleConnecttest);
  server.on("/connectivity-check.html", handleRoot);
  server.on("/check_network_status.txt", handleConnecttest);
  server.on("/wifi", handleWifi);
  server.on("/wifisave", handleWifiSave);
  server.onNotFound(handleNotFound);
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  
  // Memory management and AP monitoring check (every 30 seconds)
  unsigned long currentTime = millis();
  if (currentTime - lastHeapCheck > HEAP_CHECK_INTERVAL) {
    lastHeapCheck = currentTime;
    
    uint32_t freeHeap = ESP.getFreeHeap();
    
    // Check if AP is still running
    if (WiFi.getMode() != WIFI_AP && WiFi.getMode() != WIFI_AP_STA) {
      ESP.restart(); // Restart if AP mode is lost
    }
  }
  
  yield();
}