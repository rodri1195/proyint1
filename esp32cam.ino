// =========================
// ======= ESP32-CAM =======
// =========================

#include <WiFi.h>
#include "esp_camera.h"
#include "esp_http_server.h"

// ---------- CONFIGURACIÓN WiFi ----------
const char* ssid = "iPhone Fede";
const char* password = "federico";

// ---------- COMUNICACIÓN SERIAL ----------
#define RXD2 14
#define TXD2 15

// ---------- PINES DE CÁMARA (AI Thinker) ----------
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ---------- VARIABLES DE ESTADO ----------
int shotsReceived = 0;      // TIROS RECIBIDOS
int shotsRemaining = 40;    // DISPAROS RESTANTES
String lastSerialLine = "";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

// ---------- PÁGINA HTML ----------
static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32-CAM Láser + Motores</title>
<style>
body { background: #111; color: white; text-align: center; font-family: Arial; }
h1 { color: #ff4444; }
#stream { width: 90%; max-width: 640px; border: 3px solid #ff4444; border-radius: 10px; margin-top: 10px; }
.fire-button {
  background: linear-gradient(145deg,#ff3333,#cc0000);
  color: white; border: none; padding: 20px 40px; font-size: 26px;
  border-radius: 15px; cursor: pointer; margin-top: 20px;
}
.fire-button:hover { background: linear-gradient(145deg,#ff5555,#ee0000); }
.fire-button:disabled { background: #555; cursor: not-allowed; }

.move-pad {
  margin: 18px auto 0 auto;
  display: grid;
  grid-template-columns: repeat(3, 70px);
  grid-template-rows: repeat(3, 70px);
  gap: 10px;
  justify-content: center;
  align-items: center;
}
.move-btn {
  width: 70px;
  height: 70px;
  font-size: 24px;
  border-radius: 10px;
  border: none;
  cursor: pointer;
  background: #2a2a2a;
  color: #fff;
}
.move-btn:active { transform: translateY(2px); }
.stop-all { background:#880000; color:#fff; }
.placeholder {
  visibility: hidden;
}
</style>
</head>
<body>
  <h1> Control de Láser y Motores</h1>
  <img id="stream" src="">
  <br>
  <button id="fireButton" class="fire-button" onclick="fire()">DISPARAR</button>
  <p id="status">Listo para disparar</p>
  <p id="shots">Disparos restantes: 40</p>
  <p id="hits">Tiros recibidos: 0</p>

  <!-- Cruceta tipo joystick -->
  <div class="move-pad">
    <button class="move-btn placeholder"></button>
    <button class="move-btn" onclick="moveCmd('RIGHT')">↑</button>
    <button class="move-btn placeholder"></button>

    <button class="move-btn" onclick="moveCmd('FWD')">←</button>
    <button class="move-btn stop-all" onclick="moveCmd('STOP')">■</button>
    <button class="move-btn" onclick="moveCmd('BACK')">→</button>

    <button class="move-btn placeholder"></button>
    <button class="move-btn" onclick="moveCmd('LEFT')">↓</button>
    <button class="move-btn placeholder"></button>
  </div>

<script>
document.getElementById('stream').src = 'http://' + location.hostname + ':81/stream';

function fire() {
  const status = document.getElementById('status');
  status.innerText = "Disparando...";

  fetch('/fire?t=' + Date.now())
    .then(r => r.text())
    .then(data => {
      const parts = data.split('|');
      let shots = 0;
      let hits = 0;

      for (let part of parts) {
        part = part.trim();
        if (part.startsWith('SHOTS:')) shots = parseInt(part.split(':')[1]);
        if (part.startsWith('SHOTS_TAKEN:')) hits = parseInt(part.split(':')[1]);
      }

      document.getElementById('shots').innerText = 'Disparos restantes: ' + shots;
      document.getElementById('hits').innerText = 'Tiros recibidos: ' + hits;

      if (shots <= 0) {
        document.getElementById('fireButton').disabled = true;
        status.innerText = "¡Sin balas!";
      } else {
        status.innerText = "Disparo realizado";
      }
    });
}

function moveCmd(cmd) {
  fetch('/move?cmd=' + encodeURIComponent(cmd) + '&t=' + Date.now());
}

function updateStatus() {
  fetch('/status?t=' + Date.now())
    .then(r => r.text())
    .then(data => {
      const parts = data.split('|');
      let shots = parseInt(parts[0].split(':')[1]);
      let hits = parseInt(parts[1].split(':')[1]);

      document.getElementById('shots').innerText = 'Disparos restantes: ' + shots;
      document.getElementById('hits').innerText = 'Tiros recibidos: ' + hits;

      if (shots <= 0) {
        document.getElementById('fireButton').disabled = true;
      } else {
        document.getElementById('fireButton').disabled = false;
      }
    });
}

setInterval(updateStatus, 1000);
updateStatus();
</script>
</body>
</html>
)rawliteral";

// ---------- HANDLERS HTTP ----------
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t * fb = NULL;
  esp_err_t res = httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
  if(res != ESP_OK) return res;

  while(true) {
    fb = esp_camera_fb_get();
    if (!fb) continue;
    httpd_resp_send_chunk(req, "--frame\r\n", 9);
    httpd_resp_send_chunk(req, "Content-Type: image/jpeg\r\n\r\n", 28);
    httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    httpd_resp_send_chunk(req, "\r\n", 2);
    esp_camera_fb_return(fb);
  }
  return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, (const char *)INDEX_HTML, strlen(INDEX_HTML));
}

static esp_err_t fire_handler(httpd_req_t *req) {
  Serial2.println("FIRE");
  Serial2.flush();

  String response = "";
  unsigned long start = millis();

  while (millis() - start < 1000) {
    while (Serial2.available()) {
      String line = Serial2.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      lastSerialLine = line;

      if (response.length() > 0) response += "|";
      response += line;

      if (line.startsWith("SHOTS:"))
        shotsRemaining = line.substring(6).toInt();

      else if (line.startsWith("SHOTS_TAKEN:"))
        shotsReceived = line.substring(12).toInt();

      else if (line == "NO_SHOTS")
        shotsRemaining = 0;
    }
    delay(1);
  }

  if (response.length() == 0) {
    response = String("SHOTS:") + shotsRemaining + "|SHOTS_TAKEN:" + shotsReceived;
  }

  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, response.c_str(), response.length());
}

static esp_err_t status_handler(httpd_req_t *req) {
  char buf[64];
  int len = snprintf(buf, sizeof(buf), "SHOTS:%d|SHOTS_TAKEN:%d", shotsRemaining, shotsReceived);
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, buf, len);
}

static esp_err_t move_handler(httpd_req_t *req) {
  char buf[128];
  char cmd[16] = "STOP";
  if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
    httpd_query_key_value(buf, "cmd", cmd, sizeof(cmd));
  }

  int v1 = 0, v2 = 0;
  const int MAXV = 255;

  if (strcmp(cmd, "FWD") == 0) { v1 = MAXV; v2 = MAXV; }
  else if (strcmp(cmd, "BACK") == 0) { v1 = -MAXV; v2 = -MAXV; }
  else if (strcmp(cmd, "RIGHT") == 0) { v1 = MAXV; v2 = -MAXV; }
  else if (strcmp(cmd, "LEFT") == 0) { v1 = -MAXV; v2 = MAXV; }

  Serial2.printf("M1:%d\n", v1);
  Serial2.printf("M2:%d\n", v2);
  Serial2.flush();

  int len = snprintf(buf, sizeof(buf),
                     "MOVE:%s|M1:%d|M2:%d", cmd, v1, v2);

  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, buf, len);
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.max_uri_handlers = 16;

  httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler };
  httpd_uri_t fire_uri = { .uri = "/fire", .method = HTTP_GET, .handler = fire_handler };
  httpd_uri_t status_uri = { .uri = "/status", .method = HTTP_GET, .handler = status_handler };
  httpd_uri_t move_uri = { .uri = "/move", .method = HTTP_GET, .handler = move_handler };

  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &fire_uri);
    httpd_register_uri_handler(camera_httpd, &status_uri);
    httpd_register_uri_handler(camera_httpd, &move_uri);
  }

  config.server_port += 1;
  config.ctrl_port += 1;
  httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler };

  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Error al inicializar la cámara");
    return;
  }

  sensor_t * s = esp_camera_sensor_get();
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);   // prueba así; si ves que queda peor, probá con 0

  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi conectado. IP: ");
  Serial.println(WiFi.localIP());

  startCameraServer();
}

void loop() {
  while (Serial2.available()) {
    String line = Serial2.readStringUntil('\n');
    line.trim();

    if (line.startsWith("SHOTS:"))
      shotsRemaining = line.substring(6).toInt();

    else if (line.startsWith("SHOTS_TAKEN:"))
      shotsReceived = line.substring(12).toInt();
  }

  delay(5);
}



