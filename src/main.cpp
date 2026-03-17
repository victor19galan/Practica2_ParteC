#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// =====================================================
// CONFIGURACION
// =====================================================

// Pin de entrada de la señal cuya frecuencia quieres medir.
// Cambialo si en tu montaje usas otro pin.
static const uint8_t SIGNAL_PIN = 4;

// Red WiFi que crea la ESP32
const char* AP_SSID = "ESP32_Frecuencimetro";
const char* AP_PASS = "12345678";

// Tamaño de la cola circular
static const uint16_t QUEUE_SIZE = 128;

// =====================================================
// VARIABLES GLOBALES
// =====================================================

WebServer server(80);

// Timer hardware
hw_timer_t* timerFreq = nullptr;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

// Cola circular con tiempos entre interrupciones (en microsegundos)
volatile uint32_t periodQueue[QUEUE_SIZE];
volatile uint16_t qHead = 0;
volatile uint16_t qTail = 0;
volatile uint16_t qCount = 0;
volatile bool queueOverflow = false;

// Para medir tiempo entre interrupciones
volatile uint32_t lastCaptureUs = 0;
volatile bool firstEdge = true;

// Estadisticas de frecuencia
float fMin = 0.0f;
float fMax = 0.0f;
float fMed = 0.0f;
uint32_t samplesProcessed = 0;

// =====================================================
// PAGINA WEB
// =====================================================

const char MAIN_page[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Frecuencimetro ESP32</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      margin: 0;
      padding: 24px;
      background: #f4f6f8;
      color: #222;
      text-align: center;
    }
    .card {
      max-width: 520px;
      margin: auto;
      background: white;
      border-radius: 14px;
      box-shadow: 0 4px 18px rgba(0,0,0,0.12);
      padding: 24px;
    }
    h1 { margin-top: 0; }
    .dato {
      font-size: 1.4rem;
      margin: 14px 0;
    }
    .valor {
      font-weight: bold;
      color: #0b57d0;
    }
    .estado {
      margin-top: 18px;
      font-size: 0.95rem;
      color: #666;
    }
  </style>
</head>
<body>
  <div class="card">
    <h1>Frecuencimetro ESP32</h1>
    <div class="dato">Fmax: <span class="valor" id="fmax">0</span> Hz</div>
    <div class="dato">Fmed: <span class="valor" id="fmed">0</span> Hz</div>
    <div class="dato">Fmin: <span class="valor" id="fmin">0</span> Hz</div>
    <div class="dato">Muestras procesadas: <span class="valor" id="samples">0</span></div>
    <div class="dato">Elementos en cola: <span class="valor" id="qcount">0</span></div>
    <div class="estado" id="estado">Esperando datos...</div>
  </div>

  <script>
    async function actualizar() {
      try {
        const response = await fetch('/data');
        const data = await response.json();

        document.getElementById('fmax').textContent = data.fmax.toFixed(2);
        document.getElementById('fmed').textContent = data.fmed.toFixed(2);
        document.getElementById('fmin').textContent = data.fmin.toFixed(2);
        document.getElementById('samples').textContent = data.samples;
        document.getElementById('qcount').textContent = data.qcount;
        document.getElementById('estado').textContent =
          data.overflow ? 'Aviso: hubo desbordamiento de cola' : 'Funcionando correctamente';
      } catch (e) {
        document.getElementById('estado').textContent = 'Error al leer datos';
      }
    }

    actualizar();
    setInterval(actualizar, 1000);
  </script>
</body>
</html>
)rawliteral";

// =====================================================
// FUNCIONES AUXILIARES
// =====================================================

void handleRoot() {
  server.send_P(200, "text/html", MAIN_page);
}

void handleData() {
  uint16_t localQCount;
  bool localOverflow;

  portENTER_CRITICAL(&timerMux);
  localQCount = qCount;
  localOverflow = queueOverflow;
  portEXIT_CRITICAL(&timerMux);

  String json = "{";
  json += "\"fmax\":" + String(fMax, 2) + ",";
  json += "\"fmed\":" + String(fMed, 2) + ",";
  json += "\"fmin\":" + String(fMin, 2) + ",";
  json += "\"samples\":" + String(samplesProcessed) + ",";
  json += "\"qcount\":" + String(localQCount) + ",";
  json += "\"overflow\":" + String(localOverflow ? "true" : "false");
  json += "}";

  server.send(200, "application/json", json);
}

// ISR de la interrupcion externa
void IRAM_ATTR onSignalEdge() {
  portENTER_CRITICAL_ISR(&timerMux);

  uint32_t nowUs = timerRead(timerFreq);

  if (firstEdge) {
    firstEdge = false;
    lastCaptureUs = nowUs;
  } else {
    uint32_t periodUs = nowUs - lastCaptureUs;
    lastCaptureUs = nowUs;

    if (qCount < QUEUE_SIZE) {
      periodQueue[qHead] = periodUs;
      qHead = (qHead + 1) % QUEUE_SIZE;
      qCount++;
    } else {
      queueOverflow = true;
    }
  }

  portEXIT_CRITICAL_ISR(&timerMux);
}

// Saca un periodo de la cola circular.
// Devuelve true si habia dato, false si estaba vacia.
bool popPeriod(uint32_t& periodUs) {
  bool ok = false;

  portENTER_CRITICAL(&timerMux);
  if (qCount > 0) {
    periodUs = periodQueue[qTail];
    qTail = (qTail + 1) % QUEUE_SIZE;
    qCount--;
    ok = true;
  }
  portEXIT_CRITICAL(&timerMux);

  return ok;
}

void processQueue() {
  uint32_t periodUs;

  while (popPeriod(periodUs)) {
    if (periodUs == 0) {
      continue;
    }

    float freq = 1000000.0f / static_cast<float>(periodUs);

    if (samplesProcessed == 0) {
      fMin = freq;
      fMax = freq;
      fMed = freq;
    } else {
      if (freq < fMin) fMin = freq;
      if (freq > fMax) fMax = freq;

      // Media acumulada sin guardar todas las muestras
      fMed = ((fMed * samplesProcessed) + freq) / (samplesProcessed + 1);
    }

    samplesProcessed++;
  }
}

// =====================================================
// SETUP Y LOOP
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("=== PRACTICA 2 - PARTE C ===");
  Serial.println("Medidor de frecuencia con cola circular y pagina web");

  // Configuracion del pin de entrada
  pinMode(SIGNAL_PIN, INPUT_PULLUP);

  // Timer hardware a 1 MHz -> 1 tick = 1 us
  timerFreq = timerBegin(0, 80, true);
  timerStart(timerFreq);

  // Interrupcion por flanco de subida
  attachInterrupt(digitalPinToInterrupt(SIGNAL_PIN), onSignalEdge, RISING);

  // Crear punto de acceso
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  IPAddress ip = WiFi.softAPIP();
  Serial.print("Punto de acceso creado. Conectate a: ");
  Serial.println(AP_SSID);
  Serial.print("IP pagina web: http://");
  Serial.println(ip);

  // Rutas web
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();

  Serial.print("Pin de medida: GPIO ");
  Serial.println(SIGNAL_PIN);
  Serial.println("Esperando interrupciones...");
}

void loop() {
  server.handleClient();
  processQueue();

  static uint32_t lastPrint = 0;
  if (millis() - lastPrint >= 2000) {
    lastPrint = millis();

    Serial.print("Fmax = ");
    Serial.print(fMax, 2);
    Serial.print(" Hz | Fmed = ");
    Serial.print(fMed, 2);
    Serial.print(" Hz | Fmin = ");
    Serial.print(fMin, 2);
    Serial.print(" Hz | Muestras = ");
    Serial.println(samplesProcessed);
  }
}