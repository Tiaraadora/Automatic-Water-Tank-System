#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <FirebaseESP32.h>
#include <ArduinoJson.h>
#include <time.h>

// ===================================
// KONFIGURASI JARINGAN & WAKTU
// ===================================
#define WIFI_SSID "CYBORG"
#define WIFI_PASSWORD "12341234"
// Waktu Indonesia Barat (WIB)
const long WAKTU_OFFSET_JAM = 7 * 3600; 
const char* ntpServer = "pool.ntp.org";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer, WAKTU_OFFSET_JAM, 60000);

// ===================================
// FIREBASE CONFIGURATION
// ===================================
#define FIREBASE_HOST "aqua-monitor-401f7-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "1VlGeJ2fXvhDuOOBVdAOU2z6pV1yTDoBfuYZUtr5"

FirebaseData firebaseData;
FirebaseAuth auth;
FirebaseConfig config;

// ===================================
// PIN & VARIABEL SENSOR/AKTOR
// ===================================
#define LED_BUILTIN 2
#define SENSOR 25       // Sensor Flowmeter (Input)
#define triggerPin 13   // Ultrasonik Trigger
#define echoPin 12      // Ultrasonik Echo

const int relay = 23;      // Relay Valve/Solenoid
const int relay_pump = 15; // Relay Pompa Air

long currentMillis = 0;
long previousFlowMillis = 0;
long previousUltrasonicMillis = 0;
long previousFirebaseMillis = 0;
int flowInterval = 1000;
int ultrasonicInterval = 500; // Ditingkatkan menjadi 500ms untuk stabilitas
int firebaseInterval = 10000; // Check Firebase every 10 seconds
float calibrationFactor = 4.5;
volatile byte pulseCount;
byte pulse1Sec = 0;
float flowRate;
unsigned int flowMilliLitres;
unsigned long totalMilliLitres;

long duration;
float jarak;
float tinggiAir;
float volume;

const float panjang = 8.0;
const float lebar = 8.0;
const float tinggiWadah = 10.0; // Tinggi wadah dalam cm

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ===================================
// VARIABEL MOVING AVERAGE FILTER
// ===================================
#define JML_PEMBACAAN 5 // Jumlah sampel untuk rata-rata
float ultrasonicReadings[JML_PEMBACAAN];
int readingIndex = 0;

// ===================================
// VARIABEL KONTROL
// ===================================
const float AMBANG_BATAS_PENUH = 50.0; // Ubah dari 50% ke 90% (lebih realistis)
const float AMBANG_BATAS_ALIRAN = 0.1;
const unsigned long WAKTU_TUNGGU_NON_ALIRAN = 5UL * 60UL * 1000UL; // 5 menit
const unsigned long WAKTU_TUNGGU_RESTART = 1UL * 60UL * 1000UL;      // 1 menit

unsigned long timerAliran = 0;
unsigned long timerTungguRestart = 0;
bool pompaDihentikanTimeout = false;
bool pompaAktifOtomatis = false;
int lastControlState = -1;

// Time settings from Firebase
String jamMulaiStr = "12:00";
String jamSelesaiStr = "20:00";
int jamMulai = 12;
int menitMulai = 0;
int jamSelesai = 20;
int menitSelesai = 0;

// ===================================
// UTILITY
// ===================================
void IRAM_ATTR pulseCounter() {
    pulseCount++;
}

/**
 * @brief Fungsi untuk mendapatkan nilai jarak yang sudah dirata-ratakan 
 * (Moving Average Filter).
 */
float getAveragedDistance(float newDistance) {
    // Simpan pembacaan baru ke dalam array
    ultrasonicReadings[readingIndex] = newDistance;

    // Pindah ke indeks berikutnya (circular buffer)
    readingIndex = (readingIndex + 1) % JML_PEMBACAAN;

    // Hitung rata-rata
    float total = 0;
    for (int i = 0; i < JML_PEMBACAAN; i++) {
        total += ultrasonicReadings[i];
    }
    // Jika semua pembacaan masih nol (saat startup), kembalikan nilai mentah
    if (total < 0.1) return newDistance;
    
    return total / JML_PEMBACAAN;
}

bool DalamJendelaWaktu() {
    timeClient.update();
    int jamSaatIni = timeClient.getHours();
    int menitSaatIni = timeClient.getMinutes();

    long waktuSaatIniMenit = (long)jamSaatIni * 60 + menitSaatIni;
    long waktuMulaiMenit = (long)jamMulai * 60 + menitMulai;
    long waktuSelesaiMenit = (long)jamSelesai * 60 + menitSelesai;

    // Menangani jendela waktu yang melintasi tengah malam
    if (waktuMulaiMenit > waktuSelesaiMenit) {
        return (waktuSaatIniMenit >= waktuMulaiMenit || waktuSaatIniMenit <= waktuSelesaiMenit);
    } else {
        return (waktuSaatIniMenit >= waktuMulaiMenit && waktuSaatIniMenit <= waktuSelesaiMenit);
    }
}

void parseTimeString(String timeStr, int &jam, int &menit) {
    int colonIndex = timeStr.indexOf(':');
    if (colonIndex > 0) {
        jam = timeStr.substring(0, colonIndex).toInt();
        menit = timeStr.substring(colonIndex + 1).toInt();
    }
}

void fetchTimeSettingsFromFirebase() {
    bool settingsChanged = false;
    
    // Fetch jam_mulai from Firebase
    if (Firebase.getString(firebaseData, "/settings/jam_mulai")) {
        if (firebaseData.dataType() == "string") {
            String newJamMulaiStr = firebaseData.stringData();
            if (newJamMulaiStr != jamMulaiStr) {
                jamMulaiStr = newJamMulaiStr;
                parseTimeString(jamMulaiStr, jamMulai, menitMulai);
                Serial.print("Jam mulai updated from Firebase: ");
                Serial.println(jamMulaiStr);
                settingsChanged = true;
            }
        }
    } else {
        Serial.println("Failed to fetch jam_mulai from Firebase");
        Serial.println(firebaseData.errorReason());
    }

    // Fetch jam_selesai from Firebase
    if (Firebase.getString(firebaseData, "/settings/jam_selesai")) {
        if (firebaseData.dataType() == "string") {
            String newJamSelesaiStr = firebaseData.stringData();
            if (newJamSelesaiStr != jamSelesaiStr) {
                jamSelesaiStr = newJamSelesaiStr;
                parseTimeString(jamSelesaiStr, jamSelesai, menitSelesai);
                Serial.print("Jam selesai updated from Firebase: ");
                Serial.println(jamSelesaiStr);
                settingsChanged = true;
            }
        }
    } else {
        Serial.println("Failed to fetch jam_selesai from Firebase");
        Serial.println(firebaseData.errorReason());
    }
    
    // If settings changed, force update time display
    if (settingsChanged) {
        Serial.println("Time settings changed, updating display...");
        // This will trigger the static time to update on next sendToFirebase call
    }
}

void sendToFirebase(float waterLevelPercentage, float flowRate, float volume) {
    // Static time display - only update when settings change
    static String lastSentTime = "";
    static String lastSentDate = "";
    static bool firstRun = true;
    
    // Get current time (static, not continuously updating)
    timeClient.update();
    String currentTime = String(timeClient.getHours()) + ":" + String(timeClient.getMinutes()) + ":" + String(timeClient.getSeconds());
    
    // Get current date using NTPClient methods
    time_t rawTime = timeClient.getEpochTime();
    struct tm *timeInfo = localtime(&rawTime);
    String currentDate = String(timeInfo->tm_mday) + "/" + String(timeInfo->tm_mon + 1) + "/" + String(timeInfo->tm_year + 1900);
    
    // Send current time to Firebase (static, only changes when time settings change)
    if (firstRun || lastSentTime != currentTime) {
        if (Firebase.setString(firebaseData, "/current_time", currentTime)) {
            Serial.println("Current time sent to Firebase");
            lastSentTime = currentTime;
        } else {
            Serial.println("Failed to send current time");
            Serial.println(firebaseData.errorReason());
        }
    }
    
    // Send current date to Firebase (static, only changes when date changes)
    if (firstRun || lastSentDate != currentDate) {
        if (Firebase.setString(firebaseData, "/current_date", currentDate)) {
            Serial.println("Current date sent to Firebase");
            lastSentDate = currentDate;
        } else {
            Serial.println("Failed to send current date");
            Serial.println(firebaseData.errorReason());
        }
    }
    
    if (firstRun) firstRun = false;

    // Send water level percentage to Firebase
    if (Firebase.setFloat(firebaseData, "/water_level", waterLevelPercentage)) {
        Serial.println("Water level sent to Firebase");
    } else {
        Serial.println("Failed to send water level");
        Serial.println(firebaseData.errorReason());
    }

    // Send water level history (using timestamp as key)
    String historyPath = "/water_level_history/" + String(millis());
    if (Firebase.setFloat(firebaseData, historyPath.c_str(), waterLevelPercentage)) {
        Serial.println("Water level history sent to Firebase");
    } else {
        Serial.println("Failed to send water level history");
    }

    // Send flow rate to Firebase (ensure it's sent every time)
    if (Firebase.setFloat(firebaseData, "/flow_rate", flowRate)) {
        Serial.print("Flow rate sent to Firebase: ");
        Serial.println(flowRate);
    } else {
        Serial.println("Failed to send flow rate");
        Serial.println(firebaseData.errorReason());
    }

    // Send total volume to Firebase
    if (Firebase.setFloat(firebaseData, "/volume_ml", volume)) {
        Serial.println("Volume sent to Firebase");
    } else {
        Serial.println("Failed to send volume");
        Serial.println(firebaseData.errorReason());
    }

    // Send pump status to Firebase
    if (Firebase.setInt(firebaseData, "/control", pompaAktifOtomatis ? 1 : 0)) {
        Serial.println("Pump status sent to Firebase");
    } else {
        Serial.println("Failed to send pump status");
        Serial.println(firebaseData.errorReason());
    }
    
    // Static operational time settings - only send when settings change
    static String lastSentJamMulai = "";
    static String lastSentJamSelesai = "";
    
    // Send operational time settings to Firebase (for verification) - only when changed
    if (lastSentJamMulai != jamMulaiStr) {
        if (Firebase.setString(firebaseData, "/settings/current_jam_mulai", jamMulaiStr)) {
            Serial.println("Current jam_mulai sent to Firebase");
            lastSentJamMulai = jamMulaiStr;
        } else {
            Serial.println("Failed to send current jam_mulai");
            Serial.println(firebaseData.errorReason());
        }
    }
    
    if (lastSentJamSelesai != jamSelesaiStr) {
        if (Firebase.setString(firebaseData, "/settings/current_jam_selesai", jamSelesaiStr)) {
            Serial.println("Current jam_selesai sent to Firebase");
            lastSentJamSelesai = jamSelesaiStr;
        } else {
            Serial.println("Failed to send current jam_selesai");
            Serial.println(firebaseData.errorReason());
        }
    }
    
    // Send operational time settings to main settings paths (for React Native display) - only when changed
    if (lastSentJamMulai != jamMulaiStr) {
        if (Firebase.setString(firebaseData, "/settings/jam_mulai_display", jamMulaiStr)) {
            Serial.println("Jam mulai display sent to Firebase");
        } else {
            Serial.println("Failed to send jam mulai display");
            Serial.println(firebaseData.errorReason());
        }
    }
    
    if (lastSentJamSelesai != jamSelesaiStr) {
        if (Firebase.setString(firebaseData, "/settings/jam_selesai_display", jamSelesaiStr)) {
            Serial.println("Jam selesai display sent to Firebase");
        } else {
            Serial.println("Failed to send jam selesai display");
            Serial.println(firebaseData.errorReason());
        }
    }
}

// ===================================
// SETUP
// ===================================
void setup() {
    Serial.begin(115200);

    // Pin Mode Setup
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(SENSOR, INPUT_PULLUP);
    pinMode(relay, OUTPUT);
    pinMode(relay_pump, OUTPUT);
    pinMode(triggerPin, OUTPUT);
    pinMode(echoPin, INPUT);

    // Inisialisasi Flowmeter
    pulseCount = 0;
    previousFlowMillis = 0;

    // Inisialisasi Moving Average Buffer
    for (int i = 0; i < JML_PEMBACAAN; i++) {
        ultrasonicReadings[i] = 0.0;
    }

    // Inisialisasi LCD
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Connecting...");
    delay(1000);

    // Koneksi WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(300);
    }
    Serial.println();
    Serial.print("Connected with IP: ");
    Serial.println(WiFi.localIP());
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected");

    // Start NTP Client
    timeClient.begin();

    // Configure Firebase
    config.host = FIREBASE_HOST;
    config.signer.tokens.legacy_token = FIREBASE_AUTH;
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    // Pastikan Relay OFF saat startup (Relay HIGH = OFF)
    digitalWrite(relay_pump, HIGH); 
    digitalWrite(relay, HIGH);

    // Attach Interrupt Flowmeter
    attachInterrupt(digitalPinToInterrupt(SENSOR), pulseCounter, FALLING);
    
    // Fetch initial time settings from Firebase
    fetchTimeSettingsFromFirebase();
    
    delay(1000);
    lcd.clear();
}

// ===================================
// LOOP UTAMA
// ===================================
void loop() {
    currentMillis = millis();

    // ----------------------------------------------------
    // 0. FETCH SETTINGS FROM FIREBASE (periodically)
    // ----------------------------------------------------
    if (currentMillis - previousFirebaseMillis >= firebaseInterval) {
        previousFirebaseMillis = currentMillis;
        fetchTimeSettingsFromFirebase();
    }

    // ----------------------------------------------------
    // 1. BACA SENSOR ULTRASONIK (Dengan Moving Average)
    // ----------------------------------------------------
    if (currentMillis - previousUltrasonicMillis >= ultrasonicInterval) {
        // Serial.println("Baca Ultrasonik");
        previousUltrasonicMillis = currentMillis;

        // Prosedur Baca Ultrasonik
        digitalWrite(triggerPin, LOW);
        delayMicroseconds(2);
        digitalWrite(triggerPin, HIGH);
        delayMicroseconds(10);
        digitalWrite(triggerPin, LOW);

        duration = pulseIn(echoPin, HIGH, 30000);
        float rawJarak = duration * 0.034 / 2;

        // Terapkan Moving Average Filter untuk Jarak yang stabil
        jarak = getAveragedDistance(rawJarak); 

        // Kalkulasi Level Air & Volume
        tinggiAir = tinggiWadah - jarak;
        if (tinggiAir < 0) tinggiAir = 0;
        if (tinggiAir > tinggiWadah) tinggiAir = tinggiWadah;

        volume = panjang * lebar * tinggiAir;
        float waterLevelPercentage = (tinggiAir / tinggiWadah) * 100;

        // Tampilkan Level Air di LCD
        lcd.setCursor(0, 1);
        lcd.print("                "); // Clear line first
        lcd.setCursor(0, 1);
        lcd.print("Tank:");
        // Batasi persentase agar tidak melebihi 100%
        int percentToDisplay = (waterLevelPercentage > 100) ? 100 : (int)waterLevelPercentage;
        lcd.print(percentToDisplay);
        lcd.print("% ");
        
        // Tampilkan Jarak Mentah (Raw Distance)
        lcd.print("(");
        lcd.print(int(rawJarak));
        lcd.print("cm)");

        Serial.print("Raw Jarak: ");
        Serial.print(rawJarak);
        Serial.print(" cm | Stabil Jarak: ");
        Serial.print(jarak);
        Serial.print(" cm | Level: ");
        Serial.print(waterLevelPercentage);
        Serial.println("%");
    }

    // ----------------------------------------------------
    // 2. BACA SENSOR FLOW METER
    // ----------------------------------------------------
    if (currentMillis - previousFlowMillis >= flowInterval) {
        detachInterrupt(digitalPinToInterrupt(SENSOR));
        pulse1Sec = pulseCount;
        pulseCount = 0;
        attachInterrupt(digitalPinToInterrupt(SENSOR), pulseCounter, FALLING);

        // Menghitung Laju Aliran (L/menit) - SELALUS dihitung setiap interval
        flowRate = ((1000.0 / (currentMillis - previousFlowMillis)) * pulse1Sec) / calibrationFactor;
        previousFlowMillis = currentMillis;

        flowMilliLitres = (flowRate / 60) * 1000;
        totalMilliLitres += flowMilliLitres;

        // Tampilkan Flow Rate di LCD - SELALUS update setiap interval
        lcd.setCursor(0, 0);
        lcd.print("                "); // Clear line first
        lcd.setCursor(0, 0);
        
        // Tampilkan status flow dengan indikator visual
        if (flowRate > 0.1) {
            lcd.print("Flow:");
            lcd.print(flowRate, 1); // 1 desimal
            lcd.print(" L/m ");
        } else {
            lcd.print("Flow:0.0 L/m ");
        }
        
        // Debug output untuk flow meter - SELALUS tampilkan
        Serial.print("Pulses: ");
        Serial.print(pulse1Sec);
        Serial.print(" | Flow Rate: ");
        Serial.print(flowRate);
        Serial.println(" L/min");
        
        // Status indikator
        if (pulse1Sec == 0) {
            Serial.println("STATUS: No flow detected");
        } else {
            Serial.println("STATUS: Flow detected");
        }
    }

    // ----------------------------------------------------
    // 3. LOGIKA KONTROL OTOMATIS
    // ----------------------------------------------------
    float waterLevelPercentage = (tinggiAir / tinggiWadah) * 100;
    bool adaAliranAir = (flowRate > AMBANG_BATAS_ALIRAN);

    // KASUS 1: TANGKI PENUH
    if (waterLevelPercentage >= AMBANG_BATAS_PENUH) {
        pompaAktifOtomatis = false;
        pompaDihentikanTimeout = false;
        timerTungguRestart = 0;
        timerAliran = 0;
        Serial.println("KONTROL: Tangki Penuh. Matikan Pompa.");
    } 
    // KASUS 2: DALAM JENDELA WAKTU OPERASI (Air belum penuh)
    else if (DalamJendelaWaktu()) {
        // Sub-Kasus 2a: Sedang dalam mode tunggu restart (setelah error non-aliran)
        if (pompaDihentikanTimeout) {
            if (currentMillis - timerTungguRestart >= WAKTU_TUNGGU_RESTART) {
                // Waktu tunggu restart habis, kembali ke operasi normal
                pompaDihentikanTimeout = false;
                timerTungguRestart = 0;
                Serial.println("KONTROL: Timeout habis, mencoba pompa normal kembali.");
            } else {
                // Masih dalam masa tunggu, pompa tetap OFF
                pompaAktifOtomatis = false;
                goto SET_AKTOR; // Lewati logika aliran, langsung set aktor
            }
        }
        
        // Sub-Kasus 2b: Operasi Normal (Aktifkan pompa)
        pompaAktifOtomatis = true;

        if (adaAliranAir) {
            timerAliran = 0; // Reset timer karena ada aliran
        } else {
            // Jika tidak ada aliran dan pompa aktif
            if (timerAliran == 0) timerAliran = currentMillis; // Mulai timer
            
            if (currentMillis - timerAliran >= WAKTU_TUNGGU_NON_ALIRAN) {
                // Timeout: Pompa aktif, tapi tidak ada air yang terdeteksi
                Serial.println("KONTROL ERROR: Tidak ada aliran air (Non-Flow Timeout). Matikan Pompa.");
                pompaAktifOtomatis = false;
                pompaDihentikanTimeout = true;
                timerTungguRestart = currentMillis;
                timerAliran = 0;
            }
        }
    } 
    // KASUS 3: DI LUAR JENDELA WAKTU OPERASI
    else {
        pompaAktifOtomatis = false;
        pompaDihentikanTimeout = false;
        timerTungguRestart = 0;
        timerAliran = 0;
        Serial.println("KONTROL: Di luar jendela waktu operasi.");
    }

    // ----------------------------------------------------
    // 4. SET OUTPUT AKTOR
    // ----------------------------------------------------
SET_AKTOR:
    int currentState = pompaAktifOtomatis ? 1 : 0;
    if (currentState != lastControlState) {
        Serial.print("STATUS PERUBAHAN: Pompa -> ");
        Serial.println(pompaAktifOtomatis ? "AKTIF" : "MATI");
        lastControlState = currentState;
    }
    
    // Relay LOW = AKTIF (biasanya NO/Normally Open)
    if (pompaAktifOtomatis) {
        digitalWrite(relay, LOW); // Solenoid ON
        digitalWrite(relay_pump, LOW); // Pompa ON
    } else {
        digitalWrite(relay, HIGH); // Solenoid OFF
        digitalWrite(relay_pump, HIGH); // Pompa OFF
    }

    // Send data to Firebase periodically
    static unsigned long lastFirebaseSend = 0;
    if (currentMillis - lastFirebaseSend >= 5000) { // Send every 5 seconds
        lastFirebaseSend = currentMillis;
        sendToFirebase(waterLevelPercentage, flowRate, volume);
    }

    delay(100);
}