
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#define PUYA_SUPPORT 1
#include <FS.h>   //Include File System Headers

// ready battery voltage code
int sensorPin = A0;    // select the input pin for the potentiometer
float batteryVoltage = 0;  // variable to store the value coming from the sensor
float divider = 194;


// read battery interval
uint32_t INTERVAL = 5000; //102.4 milliseconds
uint32_t currentTime = 0;
uint32_t readTime = 0;


// captive portal code

String responseHTML = "<!DOCTYPE html>\n<html>\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<head>\n    <link href=\"https://fonts.googleapis.com/css?family=Sulphur+Point&display=swap\" rel=\"stylesheet\"><title>Leave your message</title>\n</head>\n<body>\n  <div class=\"content\">\n    <h1> Leave a Note </h1>\n    <form class=\"form\" action=\"/message\" id=\"form1\">\n      <input maxlength=\"31\" type=\"text\" name=\"message\">\n      </input>\n    </form>\n    <button type=\"submit\" form=\"form1\" value=\"Submit\">\n      SEND\n    </button>\n  </div>\n  <div class=\"readmore\">\n    <a class=\"link\" href=\"/readmore\">read more</a>\n  </div>\n</body>\n<style>\n  * {\n    font-family: \"Sulphur Point\";\n  }\n  body {\n    background-color: #ffd300;\n    height: 100vh;\n    overflow-y: hidden;\n  }\n  .content {\n    background-color: #ffd300;\n    height: 100%;\n    overflow-y: hidden;\n    display: flex;\n    flex-direction: column;\n    justify-content: space-around;\n  }\n  h1 {\n    color: black;\n    text-align: center;\n    font-weight: 500;\n    margin: 30px;\n  }\n  .form {\n    height: 20px;\n    width: 80%;\n    margin: 0 auto;\n    margin-bottom: 50px;\n    display: flex\n  }\n  input {\n    height: 20px;\n    width: 100%;\n    font-size: 15px;\n    margin: 0 auto;\n    border-top: 0px;\n    border-left: 0px;\n    border-right: 0px;\n    outline: 0;\n    background-color: #ffd300;\n    border-bottom: 1px solid black;\n    padding:2px 2px;\n  }\n  button {\n    height: 35px;\n    width: 200px;\n    border: 0px solid black;\n    margin: 0 auto;\n    color: #ffd300;\n    font-weight: 1000;\n    background-color: black;\n    border-color: black;\n  }\n  button:hover {\n    background-color: red;\n    color: white\n  }\n\n  .readmore {\n    position: absolute;\n    bottom: 0;\n    height: 20px;\n    width: 100%;\n    color: black;\n    margin-bottom: 5px;\n  }\n  link {\n    color: black !important;\n  }\n  image {\n    position: static;\n    top: 0;\n  }\n</style>\n</html>";

String responseSettings1 = "<!DOCTYPE html>\n<html>\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<head>\n    <link href=\"https://fonts.googleapis.com/css?family=Sulphur+Point&display=swap\" rel=\"stylesheet\"><title>Settings</title>\n</head>\n<body>\n  <div class=\"content\">\n    <h1>battery voltage:";
String responseSettings2 = "0";
String responseSettings3 = "</h1>\n</body>\n<style>\n  * {\n    font-family: \"Sulphur Point\";\n  }\n  body {\n    background-color: #ffd300;\n    height: 100vh;\n    overflow-y: hidden;\n  }\n  .content {\n    background-color: #ffd300;\n    height: 100%;\n    overflow-y: hidden;\n    display: flex;\n    flex-direction: column;\n    justify-content: space-around;\n  }\n  h1 {\n    color: black;\n    text-align: center;\n    font-weight: 500;\n    margin: 30px;\n  }\n  .form {\n    height: 20px;\n    width: 80%;\n    margin: 0 auto;\n    margin-bottom: 50px;\n    display: flex\n  }\n  input {\n    height: 20px;\n    width: 100%;\n    font-size: 15px;\n    margin: 0 auto;\n    border-top: 0px;\n    border-left: 0px;\n    border-right: 0px;\n    outline: 0;\n    background-color: #ffd300;\n    border-bottom: 1px solid black;\n    padding:2px 2px;\n  }\n  button {\n    height: 35px;\n    width: 200px;\n    border: 0px solid black;\n    margin: 0 auto;\n    color: #ffd300;\n    font-weight: 1000;\n    background-color: black;\n    border-color: black;\n  }\n  button:hover {\n    background-color: red;\n    color: white\n  }\n\n  .readmore {\n    position: absolute;\n    bottom: 0;\n    height: 20px;\n    width: 100%;\n    color: black;\n    margin-bottom: 5px;\n  }\n  link {\n    color: black !important;\n  }\n  image {\n    position: static;\n    top: 0;\n  }\n</style>\n</html>";

String responseSettings = responseSettings1 + responseSettings2 + responseSettings3;

// saver
const char *myHostname = "waves";

String NETWORK_NAME = "˜˜WAVE TAGS˜˜";
String globalStringNetworks = "";


const char* filename = "/messages.txt";
const byte DNS_PORT = 53;
IPAddress apIP(8, 8, 8, 8);
IPAddress netMsk(255, 255, 255, 0);

DNSServer dnsServer;
ESP8266WebServer server(80);

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

/** Redirect to captive portal if we got a request for another domain. Return true in that case so the page handler do not try to handle the request again. */
boolean captivePortal() {
  if (!isIp(server.hostHeader()) && server.hostHeader() != (String(myHostname) + ".local")) {
    server.sendHeader("Location", String("http://") + toStringIp(server.client().localIP()), true);
    server.send(200, "text/html", responseHTML);
    server.client().stop(); // Stop is needed because we sent no content length
    return true;
  }
  return false;
}

void handleSettings() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send(200, "text/html", responseSettings);
}

void handleRoot() {
  if (captivePortal()) { // If caprive portal redirect instead of displaying the page.
    return;
  }
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");

  server.send(200, "text/html", responseHTML);
}



/** Wifi config page handler */
void handleWifi() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");

  
  server.send(200, "text/html", responseHTML);
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
  server.send(200, "text/html", responseHTML);
}

void handleRoot2 () {
  server.send(200, "text/html", responseHTML);
}


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

void setup() {
  Serial.begin(9600);
  delay(1000);
    Serial.println("begin");    

   //Initialize File System
  SPIFFS.begin();

  //Format File System
  // SPIFFS.format();
 
 
  // WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(NETWORK_NAME, "", 3, false, 8);

  WiFi.setOutputPower(500);
  delay(2000); // Without delay I've seen the IP address blank

  // if DNSServer is started with "*" for domain name, it will reply with
  // provided IP to all DNS request
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);

  server.on("/", handleRoot);
  server.on("/settings", handleSettings);
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
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  currentTime = millis();

  if (currentTime - readTime > INTERVAL) {
    readTime = currentTime;

    batteryVoltage = (analogRead(sensorPin) / divider);
    
    responseSettings2 = (String)batteryVoltage; 
    responseSettings = responseSettings1 + responseSettings2 + responseSettings3;
  }
}
