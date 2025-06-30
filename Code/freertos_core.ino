#include <DHT.h>
#include <DHT_U.h>
#include <Adafruit_Sensor.h>
#include <ThingSpeak.h>
#include <WiFi.h>
#include <ESP_Mail_Client.h>

// WiFi credentials
const char* ssid = "Galaxy A52s 5G8952";
const char* password = "ctug0251";

// DHT Sensor
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// Sensor Pins
#define SOIL_MOISTURE_PIN 34
#define FLAME_SENSOR_PIN 35
#define GAS_SENSOR_PIN 15
#define BUZZER_PIN 27
#define LED_PIN 26

// Thresholds
#define TEMP_THRESHOLD 45.0
#define SOIL_THRESHOLD 20
#define FIRE_DETECTED LOW
#define GAS_DETECTED 2000

// ThingSpeak
WiFiClient client;
unsigned long myChannelNumber = 2855304;
const char *myWriteAPIKey = "SEN7047L84LRC6VK";

// SMTP Config
#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465
#define AUTHOR_EMAIL "sujai242004@gmail.com"
#define AUTHOR_PASSWORD "mljq eial emav ydlm"
#define RECIPIENT_EMAIL "sujai242004@gmail.com"

SMTPSession smtp;
SMTP_Message message;
Session_Config config;

// Sensor Data
float temp = 0, humid = 0, soilMoisture = 0;
int fire = HIGH, gasa = 0;

// Email Flags
bool soilTempAlertSent = false;
bool fireGasAlertSent = false;
bool triggerSoilTempEmail = false;
bool triggerFireGasEmail = false;

// Email Function
void sendAlertEmail(String subject, String msg) {
  smtp.debug(1);
  config.server.host_name = SMTP_HOST;
  config.server.port = SMTP_PORT;
  config.login.email = AUTHOR_EMAIL;
  config.login.password = AUTHOR_PASSWORD;
  config.login.user_domain = "";

  message.sender.name = "ESP32 Monitor";
  message.sender.email = AUTHOR_EMAIL;
  message.subject = subject;
  message.addRecipient("User", RECIPIENT_EMAIL);
  message.text.content = msg.c_str();
  message.text.charSet = "utf-8";
  message.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;

  if (!smtp.connect(&config)) {
    Serial.println("SMTP connection failed");
    return;
  }

  if (!MailClient.sendMail(&smtp, &message)) {
    Serial.println("Email failed: " + smtp.errorReason());
  }

  smtp.closeSession();
}

// ========== TASKS ==========

void TaskSensors(void *parameter) {
  while (1) {
    temp = dht.readTemperature();
    humid = dht.readHumidity();

    int soilRaw = analogRead(SOIL_MOISTURE_PIN);
    soilMoisture = ((soilRaw / 4095.0) * 100);

    gasa = analogRead(GAS_SENSOR_PIN);
    fire = digitalRead(FLAME_SENSOR_PIN);

    Serial.printf("[SENSORS] Temp: %.2f°C, Humidity: %.2f%%, Soil: %.2f%%, Gas: %d, Flame: %d\n",
                  temp, humid, soilMoisture, gasa, fire);

    vTaskDelay(pdMS_TO_TICKS(2000));  
  }
}

void TaskAlertLogic(void *parameter) {
  while (1) {
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, HIGH);

    // --- Soil/Temp Alert ---
    if (soilMoisture < SOIL_THRESHOLD || temp > TEMP_THRESHOLD) {
      digitalWrite(LED_PIN, HIGH);
      if (!soilTempAlertSent) {
        triggerSoilTempEmail = true;
        soilTempAlertSent = true;
      }
    } else {
      soilTempAlertSent = false;
    }

    // --- Fire/Gas Alert ---
    if (fire == FIRE_DETECTED || gasa > GAS_DETECTED) {
      digitalWrite(BUZZER_PIN, LOW);
      if (!fireGasAlertSent) {
        triggerFireGasEmail = true;
        fireGasAlertSent = true;
      }
    } else {
      fireGasAlertSent = false;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));  
  }
}

void TaskEmail(void *parameter) {
  while (1) {
    if (triggerSoilTempEmail) {
      triggerSoilTempEmail = false;
      String msg = " Soil Moisture or Temperature Issue:\n";
      msg += " Temp: " + String(temp) + " °C\n";
      msg += " Soil Moisture: " + String(soilMoisture) + "%\n";
      sendAlertEmail("ALERT: Soil/Temperature", msg);
    }

    if (triggerFireGasEmail) {
      triggerFireGasEmail = false;
      String msg = " FIRE or GAS Detected!\n";
      if (fire == FIRE_DETECTED)
        msg += " Flame Detected!\n";
      if (gasa > GAS_DETECTED)
        msg += " Gas Level: " + String(gasa) + "\n";
      sendAlertEmail("ALERT: Fire/Gas", msg);
    }

    vTaskDelay(pdMS_TO_TICKS(5000));  
  }
}

void TaskThingSpeak(void *parameter) {
  while (1) {
    ThingSpeak.setField(1, temp);
    ThingSpeak.setField(2, humid);
    ThingSpeak.setField(3, soilMoisture);

    int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
    if (x == 200) {
      Serial.println("[ThingSpeak] Update successful.");
    } else {
      Serial.println("[ThingSpeak] Failed, code: " + String(x));
    }

    vTaskDelay(pdMS_TO_TICKS(3000)); 
  }
}

// ========== SETUP ==========

void setup() {
  Serial.begin(115200);
  dht.begin();

  pinMode(SOIL_MOISTURE_PIN, INPUT);
  pinMode(FLAME_SENSOR_PIN, INPUT);
  pinMode(GAS_SENSOR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("\nWiFi Connected!");

  ThingSpeak.begin(client);

  // ==== Task Creation with Core Assignment ====
  xTaskCreatePinnedToCore(TaskSensors,     "Sensors",     3000, NULL, 1, NULL, 1); // Core 1
  xTaskCreatePinnedToCore(TaskAlertLogic,  "AlertLogic",  3000, NULL, 1, NULL, 1); // Core 1
  xTaskCreatePinnedToCore(TaskEmail,       "EmailTask",   5000, NULL, 1, NULL, 0); // Core 0
  xTaskCreatePinnedToCore(TaskThingSpeak,  "ThingSpeak",  4000, NULL, 1, NULL, 0); // Core 0
}

void loop() {
 
}
