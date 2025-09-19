#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#define PUYA_SUPPORT 1
#include <FS.h>   //Include File System Headers

// captive portal code
String NETWORK_NAME = "˜˜DIGITAL TRACES˜˜";
String globalStringNetworks = "";

DNSServer dnsServer;
ESP8266WebServer server(80);

const char* nameOfTheFile = "/messages.txt";
const byte DNS_PORT = 53;
IPAddress apIP(8, 8, 8, 8);
IPAddress netMsk(255, 255, 255, 0);

boolean captivePortal() {
  if (!isIp(server.hostHeader()) && server.hostHeader() != (String(NETWORK_NAME) + ".local")) {
    Serial.println("Request redirected to captive portal");
    server.sendHeader("Location", String("http://") + toStringIp(server.client().localIP()), true);
    server.send(302, "text/plain", "");   // Empty content inhibits Content-length header so we have to close the socket ourselves.
    server.client().stop(); // Stop is needed because we sent no content length
    return true;
  }
  return false;
}

/** Is this an IP? */
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

// Function to read HTML file from SPIFFS
String readHTMLFile(const char* filename) {
  String content = "";
  File file = SPIFFS.open(filename, "r");
  if (file) {
    while(file.available()) {
      content += file.readString();
    }
    file.close();
    return content;
  } else {
    Serial.print("Failed to open file: ");
    Serial.println(filename);
    return "<html><body><h1>Error: File not found</h1></body></html>";
  }
}

void handleRoot() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  
  String html = readHTMLFile("/index.html");
  server.send(200, "text/html", html);
  server.client().stop();
}


void handleWifi() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  
  String html = readHTMLFile("/index.html");
  server.send(200, "text/html", html);
  server.client().stop();
}

void handleWifiSave() {
  server.sendHeader("Location", "wifi", true);
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send(302, "text/plain", "");
  server.client().stop();
}

void handleNotFound() {
  if (captivePortal()) {
    return;
  }
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  
  String html = readHTMLFile("/index.html");
  server.send(200, "text/html", html);
}

void handleRoot2() {
  String html = readHTMLFile("/index.html");
  server.send(200, "text/html", html);
}

void handleAbout() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");

  String html = readHTMLFile("/about.html");
  server.send(200, "text/html", html);
}

String readFile() {
  String data;
  File f = SPIFFS.open(nameOfTheFile, "r");
  
  if (f) {
    while(f.available()) {
      String line = f.readString();
      data += line;
    }
    f.close();
    return data;
  }
  return "";
}

void handleForm() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  
  String message = server.arg("message"); 
  String oldNetworks = readFile();
  String finalToWrite = "";
  
  // Send success page from file
  String html = readHTMLFile("/success.html");
  server.send(200, "text/html", html);

  // Create New File And Write Data to It
  File f = SPIFFS.open(nameOfTheFile, "w");
  
  if (f) {
    if (oldNetworks.length() > 1) {
      finalToWrite = message + "_" + oldNetworks;
    } else {
      finalToWrite = message;
    }
    
    f.print(finalToWrite);
    delay(10);

    globalStringNetworks = readFile();
    Serial.println(globalStringNetworks);
    f.close();
  }
}

void setup() {
  Serial.begin(9600);
  delay(1000);
  Serial.println("begin");    

  // Initialize File System
  if (!SPIFFS.begin()) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  
  // List files for debugging
  Serial.println("SPIFFS files:");
  Dir dir = SPIFFS.openDir("/");
  while (dir.next()) {
    Serial.print("  ");
    Serial.println(dir.fileName());
  }

  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(NETWORK_NAME, "", 1, false, 8);
  WiFi.setOutputPower(999);
  delay(2000);

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);

  server.on("/", handleRoot);
  server.on("/about", handleAbout);
  server.on("/wifi", handleWifi);
  server.on("/wifisave", handleWifiSave);
  server.on("/generate_204", handleRoot);  // Android captive portal
  server.on("/fwlink", handleRoot);  // Microsoft captive portal
  server.onNotFound(handleNotFound);
  server.on("/message", handleForm);

  server.begin();
  delay(1000);

  globalStringNetworks = readFile();
  delay(5000);
  if (globalStringNetworks != "") {
    Serial.println(globalStringNetworks);    
  }
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
}