// flash 40mhz, cpu 80mhz, 512k (no spiffs)

#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>

#define DEFAULTssid "***"
#define DEFAULTpsw "***"
#define MQTTid "ArseniDoorbell"
#define MQTTip "***"
#define MQTTport ***
#define MQTTuser "***"
#define MQTTpsw "***"
#define MQTTpubQos 2
#define MQTTsubQos 1

#define DEFAULTssidAddr 0
#define DEFAULTpswAddr 64
#define nextAddr 128
#define NewAddr 250
#define EPversion 1
#define WIFIwait 25000

#define PULL_PIN 2
#define LED_PIN 1
#define TRIG_PIN 0

void EPSW(int p_start_posn, const char p_string[]);
String EPSR(int p_start_posn);
void mqttDataCb(char* topic, byte* payload, unsigned int length);
void mqttConnectedCb();
void mqttDisconnectedCb();
void checkComm();
void wifiSetup();
void processNet();
void interrupt();

boolean pendingDisconnect = true;

WiFiClient wclient;
PubSubClient client(MQTTip, MQTTport, mqttDataCb, wclient);

// EEPROM

void EPLW(int address, long value) {
  byte four = (value & 0xFF);
  byte three = ((value >> 8) & 0xFF);
  byte two = ((value >> 16) & 0xFF);
  byte one = ((value >> 24) & 0xFF);
  
  EEPROM.write(address, four);
  EEPROM.write(address + 1, three);
  EEPROM.write(address + 2, two);
  EEPROM.write(address + 3, one);
}

long EPLR(int address) {
  long four = EEPROM.read(address);
  long three = EEPROM.read(address + 1);
  long two = EEPROM.read(address + 2);
  long one = EEPROM.read(address + 3);

  return ((four << 0) & 0xFF) + ((three << 8) & 0xFFFF) + ((two << 16) & 0xFFFFFF) + ((one << 24) & 0xFFFFFFFF);
}

void EPSW(int p_start_posn, const char p_string[] ) {

  int str_len = strlen(p_string);
  for (int l_posn = 0; l_posn < str_len; l_posn ++) {
    byte l_byte = (byte) p_string[l_posn];
    byte l_read = EEPROM.read(p_start_posn + l_posn);
    if (l_read != l_byte)
      EEPROM.write(p_start_posn + l_posn, l_byte);
  }
  //write the NULL termination
  if (EEPROM.read(p_start_posn + str_len) != 0)
  EEPROM.write(p_start_posn + str_len, 0);
}

String EPSR(int p_start_posn) { //EEPROMStringRead
  //Read a NULL terminated string from EEPROM
  //Only strings up to 128 bytes are supported
  byte l_byte;

  //Count first, reserve exact string length and then extract
  int l_posn = 0;
  while (true) {
    l_byte = EEPROM.read(p_start_posn + l_posn);
    if (l_byte == 0)
      break;
    l_posn ++;
  }

  //Now extract the string
  String l_string = "";
  l_string.reserve(l_posn + 1);
  l_posn = 0;
  while (true) {
    l_byte = EEPROM.read(p_start_posn + l_posn);
    if (l_byte == 0)
      break;
    l_string += (char) l_byte;
    l_posn ++;
    if (l_posn == 128)
      break;
  }
  
  return l_string;
}

// #################### esp8266 ####################

void mqttDataCb(char* topic, byte* payload, unsigned int length) {
  char* message = (char *) payload;
  message[length] = 0;
  if (!strcmp(topic, MQTTid "/ssid")) {
    EPSW(DEFAULTssidAddr, message);
    EEPROM.commit();
    client.publish(MQTTid "/ssid/confirm", "1", 2, false);
  } else if (!strcmp(topic, MQTTid "/psw")) {
    EPSW(DEFAULTpswAddr, message);
    EEPROM.commit();
    client.publish(MQTTid "/psw/confirm", "1", 2, false);
  }

}

void mqttConnectedCb() {
  client.subscribe(MQTTid "/ssid", 1);
  client.subscribe(MQTTid "/psw", 1);
  client.publish(MQTTid "/status", "1", 2, true);  
}

void mqttDisconnectedCb() {  

}

// #################### setup e loop ####################

void setup() {

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  pinMode(PULL_PIN, OUTPUT);
  digitalWrite(PULL_PIN, LOW);
  attachInterrupt(TRIG_PIN, interrupt, RISING);
  digitalWrite(TRIG_PIN, LOW);
  
  ArduinoOTA.setHostname(MQTTid);
  EEPROM.begin(256);

  wifiSetup();
}

void wifiSetup() {
  if (EPLR(NewAddr) != EPversion) {
    EPSW(DEFAULTssidAddr, DEFAULTssid);
    EPSW(DEFAULTpswAddr, DEFAULTpsw);
    EPLW(NewAddr, EPversion);
    EEPROM.commit();
  }

  // tenta per WIFIwait la connessione all'ssid salvato; se fallisce, tenta per WIFIwait la connessione all'ssid di default; se fallisce, tenta per l'eterntià la connessione all'ssid salvato
  WiFi.mode(WIFI_STA);
  WiFi.begin(EPSR(DEFAULTssidAddr).c_str(), EPSR(DEFAULTpswAddr).c_str());
  unsigned long wifistart = millis();
  while (millis() - wifistart < WIFIwait) {
    delay(10);
    if (WiFi.status() == WL_CONNECTED)
      break;
  }
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(DEFAULTssid, DEFAULTpsw);
    wifistart = millis();
    while (millis() - wifistart < WIFIwait) {
      delay(10);
      if (WiFi.status() == WL_CONNECTED)
        break;
    }
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.begin(EPSR(DEFAULTssidAddr).c_str(), EPSR(DEFAULTpswAddr).c_str());
    }
  }
}

void processNet() {
  if (WiFi.status() == WL_CONNECTED) {
    //ArduinoOTA.begin();
    //ArduinoOTA.begin();
    //ArduinoOTA.handle();
    if (client.connected()) {
      client.loop();
    } else {
      if (client.connect(MQTTid, MQTTuser, MQTTpsw, MQTTid "/status", 2, true, "0")) {
          pendingDisconnect = false;
          mqttConnectedCb();
      }
    }
  } else {
    if (client.connected())
      client.disconnect();
  }
  if (!client.connected() && !pendingDisconnect) {
    pendingDisconnect = true;
    mqttDisconnectedCb();
  }
}

volatile unsigned long lastEvent = 0;
volatile byte status = 0;

#define CHECKWAIT 500
#define PRESSWAIT 1000
#define LEDOFFWAIT 20000

/* status
 * 0: riposo
 * 1: aspetto long press
 * 2: aspetto il segnale
 * 3: rilevato, periodo di grazia
 */
 
void loop() {

  unsigned long now = millis();
  if (status == 1 && now - lastEvent > CHECKWAIT) {
    lastEvent = now;
    status = 2;
  }
  if (status == 2) {
    if (now - lastEvent > PRESSWAIT) {
      lastEvent = now;
      status = 0;
    } else {
      if (digitalRead(TRIG_PIN)) {
        lastEvent = now;
        status = 3;
        digitalWrite(LED_PIN, LOW);
        client.publish(MQTTid "/Scattato", "1", MQTTpubQos, false);
      }
    }
  }
  if (status == 3 && now - lastEvent > LEDOFFWAIT) {
    lastEvent = now;
    status = 0;
    digitalWrite(LED_PIN, HIGH);
  }

  if (status != 1 && status != 2)
    processNet();

}

void ICACHE_RAM_ATTR interrupt() {
  if (status == 0) {
    status = 1;
    lastEvent = millis();
  }
}

