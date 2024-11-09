#define SS_PIN D4   //D4
#define RST_PIN D3  //D3

#define GSM_TX 12  // New pin for GSM TX
#define GSM_RX 13  // New pin for GSM RX


#include <SPI.h>
#include <MFRC522.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESP8266HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <ArduinoJson.h>

LiquidCrystal_I2C lcd(0x3F, 16, 2);
SoftwareSerial gsm(GSM_RX, GSM_TX);
MFRC522 mfrc522(SS_PIN, RST_PIN);  // Create MFRC522 instance.
MFRC522::MIFARE_Key key;

String firstCard = "";
String secondCard = "";
bool firstCardScanned = false;
bool scanmode = false;
String humanScanUID;
String armsScanUID;

AsyncWebServer server(80);
const char *ssid = "vivo T2x 5G";
const char *password = "DORAEMON";

char currentDateTime[21];
bool writehumanMode = false;
bool writearmsMode = false;

// AsyncWebServerRequest *currentRequest = nullptr;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000);

void setup() {
  Serial.begin(115200);  // Initiate a serial communication
  // gsm.begin(9600);
  SPI.begin();         // Initiate SPI bus
  mfrc522.PCD_Init();  // Initiate MFRC522
  Wire.begin();
  lcd.init();
  lcd.backlight();
  // Initialize the key with default 0xFF key (common default)
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }
  server.on("/", HTTP_GET, handleRoot);
  server.on("/turnOnScanMode", HTTP_POST, handleTurnOnScanMode);
  server.on("/turnOffScanMode", HTTP_POST, handleTurnOffScanMode);

  server.on("/turnOnScanMode", HTTP_OPTIONS, handleCORS);
  server.on("/turnOffScanMode", HTTP_OPTIONS, handleCORS);

  server.on("/writeHumanCard", HTTP_POST, writeHumanCards);
  server.on("/writeArmsCard", HTTP_POST, writeArmsCards);

  server.on("/writeHumanCard", HTTP_OPTIONS, handleCORS);
  server.on("/writeArmsCard", HTTP_OPTIONS, handleCORS);

  // pinMode(GSM_TX, OUTPUT);
  // pinMode(GSM_RX, INPUT);

  WiFi.begin(ssid, password);
  lcd.setCursor(0, 1);
  lcd.print("Connecting WiFi");
  Serial.print(F("Connecting to WiFi"));

  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    Serial.print(F("."));
  }

  // lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connected");
  Serial.println(F("\nWiFi Connected"));
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());
  Serial.println("IP Address: " + WiFi.localIP().toString());

  server.begin();

  timeClient.update();
  setTime(timeClient.getEpochTime());
  timeClient.begin();
}

void loop() {
  static String humanUID, humanData, armsUID, armsData;
  
  if (scanmode) {
    lcd.setCursor(0, 0);
    lcd.print("Scan human Card !");
    // Look for new cards
    if (!mfrc522.PICC_IsNewCardPresent()) {
      return;
    }

    // Select one of the cards
    if (!mfrc522.PICC_ReadCardSerial()) {
      return;
    }
    
    Serial.print("Scan card !");
    // Show UID on serial monitor
    String content = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      content.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
      content.concat(String(mfrc522.uid.uidByte[i], HEX));
    }
    content.toUpperCase();
    Serial.println("UID tag: " + content);

    if (!firstCardScanned) {
      firstCard = scanRFID();
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("First Card Scanned");
      // Send firstCard to the server and get the response
      if (sendCardToServer(firstCard)) {
        firstCardScanned = true;
        Serial.println("First card scanned. Please scan the second card.");
        lcd.setCursor(0, 1);
        lcd.print("Scan 2nd Card");
      } else {
        Serial.println("Failed to get response from the server.");
        lcd.setCursor(0, 1);
        lcd.print("Server Error");
      }

    } else if (firstCardScanned) {
      secondCard = scanRFID();
      Serial.println("Second card scanned.");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Second Card Scanned");

      if (firstCard == String(humanScanUID) && secondCard == armsScanUID) {
        Serial.println("Access Granted");
        lcd.setCursor(0, 1);
        lcd.print("Access Granted");
        Serial.println("Welcome ");
        delay(1000);
        Serial.println("Have FUN");
        scandatalog(firstCard, secondCard);
      } else {
        Serial.println("Access Denied");
        lcd.setCursor(0, 1);
        lcd.print("Access Denied");
        sendSMS(String("Authentication failed"), "6203269737");
      }

      // Reset for the next pair of cards
      firstCardScanned = false;
      firstCard = "";
      secondCard = "";
      humanScanUID = "";
      armsScanUID = "";
    }
    delay(8000);
  }  // Add delay to allow time between scans
  if (writehumanMode) {
    if (!mfrc522.PICC_IsNewCardPresent()) {
      return;
    }

    // Select one of the cards
    if (!mfrc522.PICC_ReadCardSerial()) {
      return;
    }
    // Get the current date and time from NTPClient
    snprintf(currentDateTime, sizeof(currentDateTime), "%02d%02d%04d/%02d%02d%02d", day(), month(), year(), hour(), minute(), second());

    // Get the UID of the card
    String uidString = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      uidString += String(mfrc522.uid.uidByte[i], HEX);
    }
    Serial.println(uidString);

    // Generate random alphanumeric data
    String randomData = String(currentDateTime);  // Change the length as needed
    Serial.println(randomData);
    Serial.print(F("Put your human card for Write !"));
    lcd.setCursor(0, 0);
    lcd.print("Writing Human");

    byte dataBlock[16];
    randomData.getBytes(dataBlock, sizeof(dataBlock));

    // Authenticate with the card
    MFRC522::StatusCode status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, 1, &key, &(mfrc522.uid));
    delay(400);
    if (status != MFRC522::STATUS_OK) {
      Serial.print("Authentication failed: ");
      Serial.println(mfrc522.GetStatusCodeName(status));
      lcd.setCursor(0, 1);
      lcd.print("Auth Failed");
      mfrc522.PCD_Init();
      return;
    }
    // Write data to the card

    status = mfrc522.MIFARE_Write(1, dataBlock, 16);
    if (status != MFRC522::STATUS_OK) {
      Serial.print("Write failed: ");
      Serial.println(mfrc522.GetStatusCodeName(status));
      lcd.setCursor(0, 1);
      lcd.print("Write Failed");
      return;
    }
    delay(400);

    // Read data from the card
    byte buffer[18];
    byte bufferSize = sizeof(buffer);
    status = mfrc522.MIFARE_Read(1, buffer, &bufferSize);
    if (status != MFRC522::STATUS_OK) {
      Serial.print("Read failed: ");
      Serial.println(mfrc522.GetStatusCodeName(status));
      lcd.setCursor(0, 1);
      lcd.print("Read Failed");
      return;
    }

    // Convert read buffer to String
    String readData = "";
    for (byte i = 0; i < bufferSize - 2; i++) {
      readData += (char)buffer[i];
    }
    Serial.print("UID: ");
    Serial.println(uidString);
    Serial.print("Random Data Written: ");
    Serial.println(randomData);
    Serial.print("Data Read from Card: " + readData);

    Serial.println(" ");

    memset(currentDateTime, 0, sizeof(currentDateTime));
    Serial.println("Cleared currentDateTime");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Human Written");

    // Halt PICC
    mfrc522.PICC_HaltA();
    // Stop encryption on PCD
    mfrc522.PCD_StopCrypto1();
    // Store human data
    humanUID = uidString;
    humanData = randomData;
    // Turn off write mode
    writehumanMode = false;
  }
  if (writearmsMode) {
    // Look for new cards
    if (!mfrc522.PICC_IsNewCardPresent()) {
      return;
    }

    // Select one of the cards
    if (!mfrc522.PICC_ReadCardSerial()) {
      return;
    }
    snprintf(currentDateTime, sizeof(currentDateTime), "%02d%02d%04d/%02d%02d%02d", day(), month(), year(), hour(), minute(), second());

    Serial.print("Place the arms card !: ");
    lcd.setCursor(0, 0);
    lcd.print("Writing Arms");
    Serial.println(currentDateTime);
    delay(400);

    // Get the UID of the card
    String uidString = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      uidString += String(mfrc522.uid.uidByte[i], HEX);
    }
    Serial.println(uidString);

    // Generate random alphanumeric data
    String randomData = String(currentDateTime);  // Change the length as needed
    Serial.println(randomData);

    byte dataBlock[16];
    randomData.getBytes(dataBlock, sizeof(dataBlock));

    // Authenticate with the card
    MFRC522::StatusCode status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, 1, &key, &(mfrc522.uid));
    if (status != MFRC522::STATUS_OK) {
      Serial.print("Authentication failed: ");
      Serial.println(mfrc522.GetStatusCodeName(status));
      lcd.setCursor(0, 1);
      lcd.print("Auth Failed");
      // mfrc522.PCD_Init();
      return;
    }

    // Write data to the card

    status = mfrc522.MIFARE_Write(1, dataBlock, 16);
    if (status != MFRC522::STATUS_OK) {
      Serial.print("Write failed: ");
      Serial.println(mfrc522.GetStatusCodeName(status));
      lcd.setCursor(0, 1);
      lcd.print("Write Failed");
      mfrc522.PCD_Init();
      return;
    }
    delay(400);
    // Read data from the card
    byte buffer[18];
    byte bufferSize = sizeof(buffer);
    status = mfrc522.MIFARE_Read(1, buffer, &bufferSize);
    if (status != MFRC522::STATUS_OK) {
      Serial.print("Read failed: ");
      Serial.println(mfrc522.GetStatusCodeName(status));
      lcd.setCursor(0, 1);
      lcd.print("Read Failed");
      return;
    }

    // Convert read buffer to String
    String readData = "";
    for (byte i = 0; i < bufferSize - 2; i++) {
      readData += (char)buffer[i];
    }

    Serial.print("UID: ");
    Serial.println(uidString);
    Serial.print("Random Data Written: ");
    Serial.println(randomData);
    Serial.print("Data Read from Card: " + readData);


    Serial.println(" ");

    // Halt PICC
    mfrc522.PICC_HaltA();
    // Stop encryption on PCD
    mfrc522.PCD_StopCrypto1();
    // Store arms data
    armsUID = uidString;
    armsData = randomData;

    memset(currentDateTime, 0, sizeof(currentDateTime));
    Serial.println("Cleared currentDateTime");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Arms Written");

    // Turn off write mode
    writearmsMode = false;
  }
  delay(2000);
  // Once both modes are processed, send the data to the database
  if (!writehumanMode && !writearmsMode && !humanUID.isEmpty() && !armsUID.isEmpty()) {
    sendToDatabase(humanUID, humanData, armsUID, armsData);
    // Clear the UID and data after sending to database
    humanUID = "";
    humanData = "";
    armsUID = "";
    armsData = "";
  }
}

void sendSMS(String message, String phoneNumber) {
  gsm.print("AT+CMGF=1\r");  // Set the GSM module to text mode
  delay(100);
  gsm.print("AT+CMGS=\"");
  gsm.print(phoneNumber);
  gsm.print("\"\r");
  delay(100);
  gsm.print(message);
  delay(100);
  gsm.write(26);  // ASCII code for CTRL+Z to send the SMS
  delay(5000);    // Wait for the SMS to be sent
}
String scanRFID() {
  String rfidId = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    rfidId += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
    rfidId += String(mfrc522.uid.uidByte[i], HEX);
  }
  rfidId.toUpperCase();
  Serial.println(rfidId);
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  return rfidId;
}

void handleRoot(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "Welcome to RFID Reader");
  addCORSHeaders(response);
  request->send(response);
}

void handleTurnOnScanMode(AsyncWebServerRequest *request) {
  scanmode = true;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Scan Mode ON"));
  AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "Scan Mode Enabled");
  addCORSHeaders(response);
  request->send(response);
}

void handleTurnOffScanMode(AsyncWebServerRequest *request) {
  scanmode = false;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Scan Mode OFF"));
  AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "Scan Mode Disabled");
  addCORSHeaders(response);
  request->send(response);
}
void writeHumanCards(AsyncWebServerRequest *request) {
  writehumanMode = true;
  // currentRequest = request;
  AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "Write Human Mode Enable");
  addCORSHeaders(response);
  request->send(response);
}

void writeArmsCards(AsyncWebServerRequest *request) {
  writearmsMode = true;
  // currentRequest = request;
  AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "Write Arms Mode E");
  addCORSHeaders(response);
  request->send(response);
}


void addCORSHeaders(AsyncWebServerResponse *response) {
  response->addHeader(F("Access-Control-Allow-Origin"), F("*"));
  response->addHeader(F("Access-Control-Allow-Methods"), F("GET, POST, PUT, DELETE, OPTIONS"));
  response->addHeader(F("Access-Control-Allow-Headers"), F("Content-Type, Authorization"));
}

void handleCORS(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response = request->beginResponse(200);
  response->addHeader("Access-Control-Allow-Origin", "*");
  response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE");
  response->addHeader("Access-Control-Allow-Headers", "Content-Type");
  request->send(response);
}

void sendToDatabase(String humanUID, String humanData, String armsUID, String armsData) {
  if (WiFi.status() == WL_CONNECTED) {  // Check WiFi connection status
    HTTPClient http;
    WiFiClient client;
    const char *serverName = "http://192.168.161.65/rfid_data/write_rfid_data.php";
    http.begin(client, serverName);  // Your PHP script URL
    armsUID.toUpperCase();
    http.addHeader("Content-Type", "application/json");
    JsonDocument doc;
    doc["humanUID"] = humanUID;
    doc["humanData"] = humanData;
    doc["armsUID"] = armsUID;
    doc["armsData"] = armsData;
    String postData;
    serializeJson(doc, postData);

    int httpResponseCode = http.POST(postData);

    if (httpResponseCode > 0) {
      String response = http.getString();  // Get the response to the request
      Serial.println(httpResponseCode);    // Print return code
      Serial.println(response);            // Print request answer
    } else {
      Serial.print("Error on sending POST: ");
      Serial.println(httpResponseCode);
    }

    http.end();  // Free resources
  } else {
    Serial.println("Error in WiFi connection");
  }
}
bool sendCardToServer(String cardID) {
  HTTPClient http;
  WiFiClient client;
  String url = "http://192.168.161.65/rfid_data/write_rfid_data.php?human_uid=" + cardID;
  http.begin(client, url);

  int httpCode = http.GET();
  if (httpCode > 0) {
    String payload = http.getString();
    Serial.println("HTTP Response code: " + String(httpCode));
    Serial.println("Response payload :" + payload);

    // StaticJsonDocument<200> doc;
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      http.end();
      return false;
    }

    // Ensure the correct conversion of data types
    if (doc.containsKey("human_uid") && doc.containsKey("arms_uid")) {
      humanScanUID = doc["human_uid"].as<String>();  // Convert to String
      armsScanUID = doc["arms_uid"].as<String>();    // Convert to String
      Serial.println("Human UID: " + String(humanScanUID));
      Serial.println("Arms UID: " + String(armsScanUID));
    } else {
      Serial.println("Invalid JSON format.");
      http.end();
      return false;
    }
  } else {
    Serial.println("Error on HTTP request");
    http.end();
    return false;
  }

  http.end();
  return true;
}
void scandatalog(String human_UID, String arms_UID) {
  if (WiFi.status() == WL_CONNECTED) {  // Check WiFi connection status
    HTTPClient http;
    WiFiClient client;
    const char *serverName = "http://192.168.161.65:5000/log";
    http.begin(client, serverName);
    http.addHeader("Content-Type", "application/json");
    JsonDocument doc;
    doc["arms_uid"] = arms_UID;
    doc["human_uid"] = human_UID;
    String postData;
    serializeJson(doc, postData);
    int httpResponseCode = http.POST(postData);

    if (httpResponseCode > 0) {
      String response = http.getString();  // Get the response to the request
      Serial.println(httpResponseCode);    // Print return code
      Serial.println(response);            // Print request answer
    } else {
      Serial.print("Error on sending POST: ");
      Serial.println(httpResponseCode);
    }

    http.end();  // Free resources
  } else {
    Serial.println("Error in WiFi connection");
  }
}