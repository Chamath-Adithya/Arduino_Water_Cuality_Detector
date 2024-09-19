#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <DHT.h>

// Define the pins used
#define DHT_PIN 27
#define TDS_PIN 35
#define BUILTIN_LED 12
#define DHTTYPE DHT11

// WiFi credentials and static IP configuration
const char* ssid = "WaterQualityDetector";
const char* password = "";
IPAddress local_IP(192, 168, 1, 22);
IPAddress gateway(192, 168, 1, 5);
IPAddress subnet(255, 255, 255, 0);

// Initialize WebServer, DNS server, and DHT sensor
WebServer server(80);
DNSServer dnsServer;
DHT dht(DHT_PIN, DHTTYPE);

// Define the captive portal DNS server IP address
const byte DNS_PORT = 53;

// TDS sensor variables
#define VREF 5.0              // analog reference voltage(Volt) of the ADC
#define SCOUNT  30            // sum of sample point

int analogBuffer[SCOUNT];     // store the analog value in the array, read from ADC
int analogBufferTemp[SCOUNT];
int analogBufferIndex = 0;
int copyIndex = 0;

float averageVoltage = 0;
float tdsValue = 0;
float temperature = 16;       // current temperature for compensation

void setup() {
  Serial.begin(115200);
  dht.begin();

  // Configure output pin
  pinMode(BUILTIN_LED, OUTPUT);

  // Set up Access Point with static IP configuration
  Serial.print("Setting up Access Point ... ");
  bool apConfigSuccess = WiFi.softAPConfig(local_IP, gateway, subnet);
  Serial.println(apConfigSuccess ? "Ready" : "Failed!");

  // Start the Access Point
  Serial.print("Starting Access Point ... ");
  bool apStartSuccess = WiFi.softAP(ssid, password);
  Serial.println(apStartSuccess ? "Ready" : "Failed!");

  // Print the IP address of the Access Point
  Serial.print("Access Point IP address: ");
  Serial.println(WiFi.softAPIP());

  // Start DNS server and redirect all requests to the ESP32
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  // Define routes for web server
  server.on("/", HTTP_GET, handleRoot);
  server.on("/toggle-led", HTTP_GET, handleToggleLED);
  server.on("/sensor-data", HTTP_GET, handleSensorData);
  server.on("/wifi-credentials", HTTP_GET, handleWiFiCredentials);
  server.on("/save-credentials", HTTP_POST, handleSaveCredentials);
  server.onNotFound(handleNotFound);

  // Start the web server
  server.begin();
}

void loop() {
  // Handle client requests
  server.handleClient();
  dnsServer.processNextRequest();
  readTDS();
}

void handleRoot() {
  // Generate HTML for the control panel with "nature and tech" theme
  String html = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"><title>ESP32 Control</title>";
  html += "<style>";
  html += "body { background-color: #212529; color: #FFF; font-family: Arial, sans-serif; }";  // Dark background, light text
  html += "h1 { color: #00C853; text-align: center; margin-top: 20px; }";  // Green for headings
  html += "button { background-color: #00C853; color: black; border: none; padding: 10px 20px; text-align: center; text-decoration: none; display: inline-block; font-size: 16px; margin: 4px 2px; cursor: pointer; border-radius: 12px; }";  // Green buttons
  html += ".container { display: flex; justify-content: space-around; margin-top: 20px; }";
  html += ".section { padding: 20px; border: 1px solid #00C853; border-radius: 12px; width: 45%; background-color: #30363B; color: #FFF; box-shadow: 0 4px 8px rgba(0,0,0,0.2); }";  // Darker section background
  html += ".section h2 { color: #00C853; }";
  html += "</style>";
  html += "<script>";
  html += "function goToWiFiCredentials() {";
  html += "  window.location.href = '/wifi-credentials';";
  html += "}";
  html += "function fetchData() {";
  html += "  fetch('/sensor-data').then(response => response.json()).then(data => {";
  html += "    document.getElementById('temperature').innerText = data.temperature + ' °C';";
  html += "    document.getElementById('humidity').innerText = data.humidity + ' %';";
  html += "    document.getElementById('tds').innerText = data.tds + ' ppm';";
  html += "  }).catch(error => console.error('Error fetching sensor data:', error));";
  html += "}";
  html += "setInterval(fetchData, 1000);"; // Fetch data every second
  html += "</script></head><body onload=\"fetchData()\"><h1>Water Quality Detector</h1>";
  html += "<div class=\"container\">";
 // html += "<button onclick=\"goToWiFiCredentials()\">WiFi Credentials</button>"; // Button to navigate to WiFi credentials page
  html += "<div class=\"section\">";
  html += "<h2>Controls</h2>";
  html += "<p>Click the button below to control the LED:</p>";
  html += "<form action=\"/toggle-led\" method=\"get\"><button type=\"submit\">Toggle LED</button></form>";
  html += "</div>";
  html += "<div class=\"section\">";
  html += "<h2>Sensor Data</h2>";
  html += "<p>Temperature: <span id=\"temperature\">-- °C</span></p>";
  html += "<p>Humidity: <span id=\"humidity\">-- %</span></p>";
  html += "<p>TDS Value: <span id=\"tds\">-- ppm</span></p>";
  html += "</div>";
  html += "</div>";
  html += "</body></html>";

  // Send the HTML to the client
  server.send(200, "text/html", html);
}

void handleWiFiCredentials() {
  // HTML page for entering WiFi credentials
  String html = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"><title>WiFi Credentials</title>";
  html += "<style>";
  html += "body { background-color: #212529; color: #FFF; font-family: Arial, sans-serif; }";  // Dark background, light text
  html += "h1 { color: #00C853; text-align: center; margin-top: 20px; }";  // Green for headings
  html += "input[type='text'], input[type='password'] { padding: 10px; margin: 5px; width: 100%; }"; // Input fields
  html += "input[type='submit'] { background-color: #00C853; color: black; border: none; padding: 10px 20px; text-align: center; text-decoration: none; display: inline-block; font-size: 16px; margin: 4px 2px; cursor: pointer; border-radius: 12px; }"; // Submit button
  html += "button.back-button { position: absolute; top: 10px; left: 10px; background-color: #00C853; color: black; border: none; padding: 10px 20px; text-align: center; text-decoration: none; display: inline-block; font-size: 16px; cursor: pointer; border-radius: 12px; }"; // Back button
  html += "</style></head><body>";
  html += "<a href=\"/\">"; // Back button
  html += "<button class=\"back-button\">Back</button>";
  html += "</a>";
  html += "<p> <br><br> </p>";
  html += "<h1>WiFi Credentials</h1>";
  html += "<div style=\"max-width: 300px; margin: 0 auto;\">"; // Centered container
  html += "<form action=\"/save-credentials\" method=\"post\">";
  html += "SSID: <input type=\"text\" name=\"ssid\"><br>";
  html += "Password: <input type=\"password\" name=\"password\"><br>";
  html += "<input type=\"submit\" value=\"Submit\">";
  html += "</form>";
  html += "</div></body></html>";

  // Send the HTML to the client
  server.send(200, "text/html", html);
}

void handleSaveCredentials() {
  // Get the SSID and password from the POST request
  String ssid = server.arg("ssid");
  String password = server.arg("password");

  // Print the entered credentials to the serial monitor
  Serial.println("Entered WiFi Credentials:");
  Serial.println("SSID: " + ssid);
  Serial.println("Password: " + password);

  // Redirect back to the root page
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleToggleLED() {
  // Toggle the state of the built-in LED
  digitalWrite(BUILTIN_LED, !digitalRead(BUILTIN_LED));
  // Redirect back to the root page
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSensorData() {
  // Read sensor data
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  float tdsValue = readTDS();

  // Generate JSON response
  String json = "{";
  json += "\"temperature\":" + String(temperature) + ",";
  json += "\"humidity\":" + String(humidity) + ",";
  json += "\"tds\":" + String(tdsValue);
  json += "}";

  // Send the JSON response to the client
  server.send(200, "application/json", json);
}

void handleNotFound() {
  // Redirect all requests to the root page
  server.sendHeader("Location", "/");
  server.send(303);
}

float readTDS() {
  static unsigned long analogSampleTimepoint = millis();
  if(millis() - analogSampleTimepoint > 40U) {     //every 40 milliseconds,read the analog value from the ADC
    analogSampleTimepoint = millis();
    analogBuffer[analogBufferIndex] = analogRead(TDS_PIN);    //read the analog value and store into the buffer
    analogBufferIndex++;
    if(analogBufferIndex == SCOUNT) 
      analogBufferIndex = 0;
  }
  
  static unsigned long printTimepoint = millis();
  if(millis() - printTimepoint > 800U) {
    printTimepoint = millis();
    for(copyIndex = 0; copyIndex < SCOUNT; copyIndex++)
      analogBufferTemp[copyIndex] = analogBuffer[copyIndex];
    averageVoltage = getMedianNum(analogBufferTemp, SCOUNT) * (float)VREF / 4096.0;
    float compensationCoefficient = 1.0 + 0.02 * (temperature - 25.0);
    float compensationVolatge = averageVoltage / compensationCoefficient;
    tdsValue = (133.42 * compensationVolatge * compensationVolatge * compensationVolatge - 255.86 * compensationVolatge * compensationVolatge + 857.39 * compensationVolatge) * 0.5;
  }
  return tdsValue;
}

int getMedianNum(int bArray[], int iFilterLen) {
  int bTab[iFilterLen];
  for (byte i = 0; i < iFilterLen; i++)
    bTab[i] = bArray[i];
  int i, j, bTemp;
  for (j = 0; j < iFilterLen - 1; j++) {
    for (i = 0; i < iFilterLen - j - 1; i++) {
      if (bTab[i] > bTab[i + 1]) {
        bTemp = bTab[i];
        bTab[i] = bTab[i + 1];
        bTab[i + 1] = bTemp;
      }
    }
  }
  if ((iFilterLen & 1) > 0)
    bTemp = bTab[(iFilterLen - 1) / 2];
  else
    bTemp = (bTab[iFilterLen / 2] + bTab[iFilterLen / 2 - 1]) / 2;
  return bTemp;
}
