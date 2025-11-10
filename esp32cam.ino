//esp32cam
#include <WiFi.h>
#include "esp_camera.h"
#include "esp_http_server.h"

// ---------- CONFIGURACIÓN WiFi ----------
const char* ssid = "iPhone Fede";
const char* password = "federico";

// ---------- COMUNICACIÓN SERIAL ----------
#define RXD2 14  // RX del ESP32-CAM conectado al TX (17) del ESP32 Normal
#define TXD2 15  // TX del ESP32-CAM conectado al RX (16) del ESP32 Normal

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
int currentLives = 3;
int currentShots = 40;
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

.motor-control { margin-top: 18px; display:flex; justify-content:center; gap:24px; flex-wrap:wrap; }
.motor-card { background:#222; padding:12px 16px; border-radius:8px; width:260px; }
.range { width:100%; }
.small { font-size:14px; color:#ccc; }
.btn { margin-top:8px; padding:8px 12px; border-radius:8px; border:none; cursor:pointer; }
.stop { background:#666; color:#fff; }

.move-pad { margin-top:18px; display:flex; justify-content:center; gap:12px; flex-wrap:wrap; }
.move-btn { width:120px; height:56px; font-size:18px; border-radius:10px; border:none; cursor:pointer; background:#2a2a2a; color:#fff; }
.move-btn:active { transform:translateY(2px); }
.stop-all { background:#880000; color:#fff; }
</style>
</head>
<body>
  <h1> Control de Láser y Motores</h1>
  <img id="stream" src="">
  <br>
  <button id="fireButton" class="fire-button" onclick="fire()"> DISPARAR</button>
  <p id="status">Listo para disparar</p>
  <p id="shots">Disparos restantes: 40</p>
  <p id="lives">Vidas restantes: 3</p>

  <div class="move-pad">
    <button class="move-btn" onclick="moveCmd('FWD')">Adelante</button>
    <button class="move-btn" onclick="moveCmd('LEFT')">Derecha</button>
    <button class="move-btn" onclick="moveCmd('RIGHT')">Izquierda</button>
    <button class="move-btn" onclick="moveCmd('BACK')">Atrás</button>
    <button class="move-btn stop-all" onclick="moveCmd('STOP')">Detener</button>
  </div>

<script>
document.getElementById('stream').src = 'http://' + location.hostname + ':81/stream';

function fire() {
  const status = document.getElementById('status');
  const shotsDisplay = document.getElementById('shots');
  const livesDisplay = document.getElementById('lives');
  const fireButton = document.getElementById('fireButton');
  status.innerText = "Disparando...";
  fetch('/fire?t=' + Date.now(), { method: 'GET', cache: 'no-store' })
    .then(response => { if (!response.ok) throw new Error('Error en la solicitud'); return response.text(); })
    .then(data => {
      if (data.startsWith('SHOTS:')) {
        const shots = parseInt(data.split(':')[1]);
        shotsDisplay.innerText = 'Disparos restantes: ' + shots;
        if (shots === 0) {
          fireButton.disabled = true;
          status.innerText = "¡Sin balas! No puedes disparar más.";
        } else {
          status.innerText = "Disparo realizado";
        }
      } else if (data.startsWith('LIVES:')) {
        const lives = parseInt(data.split(':')[1]);
        livesDisplay.innerText = 'Vidas restantes: ' + lives;
        if (lives === 0) {
          fireButton.disabled = true;
          status.innerText = "¡Se te acabaron las vidas!";
        } else {
          status.innerText = "No hay disparo (vidas activas)";
        }
      } else if (data === 'NO_SHOTS') {
        shotsDisplay.innerText = 'Disparos restantes: 0';
        fireButton.disabled = true;
        status.innerText = "¡Sin balas! No puedes disparar más.";
      }
    })
    .catch(error => {
      console.error('Error:', error);
      status.innerText = "Error al disparar";
    });
}

function m1update(v) { document.getElementById('m1val').innerText = v; }
function m2update(v) { document.getElementById('m2val').innerText = v; }

function m1send(v) {
  fetch('/m1?val=' + encodeURIComponent(v) + '&t=' + Date.now(), { method: 'GET', cache: 'no-store' })
    .then(r => { if (!r.ok) throw new Error('m1 error'); return r.text(); })
    .then(txt => { console.log('M1 resp:', txt); })
    .catch(e => console.error(e));
}

function m2send(v) {
  fetch('/m2?val=' + encodeURIComponent(v) + '&t=' + Date.now(), { method: 'GET', cache: 'no-store' })
    .then(r => { if (!r.ok) throw new Error('m2 error'); return r.text(); })
    .then(txt => { console.log('M2 resp:', txt); })
    .catch(e => console.error(e));
}

// Movement buttons: single-press sets tracks to max speed in desired direction.
// FWD: both +255, BACK: both -255, RIGHT: left +255 right -255, LEFT: left -255 right +255, STOP: 0,0
function moveCmd(cmd) {
  fetch('/move?cmd=' + encodeURIComponent(cmd) + '&t=' + Date.now(), { method: 'GET', cache: 'no-store' })
    .then(r => { if (!r.ok) throw new Error('move error'); return r.text(); })
    .then(txt => { console.log('MOVE resp:', txt); })
    .catch(e => console.error(e));
}

// Send on slider change (debounce)
let m1timer = null;
document.getElementById('m1range').addEventListener('input', (e) => {
  const v = e.target.value;
  clearTimeout(m1timer);
  m1timer = setTimeout(()=> m1send(v), 120);
});
let m2timer = null;
document.getElementById('m2range').addEventListener('input', (e) => {
  const v = e.target.value;
  clearTimeout(m2timer);
  m2timer = setTimeout(()=> m2send(v), 120);
});

function updateStatus() {
  const shotsDisplay = document.getElementById('shots');
  const livesDisplay = document.getElementById('lives');
  const fireButton = document.getElementById('fireButton');
  const status = document.getElementById('status');

  fetch('/status?t=' + Date.now(), { method: 'GET', cache: 'no-store' })
    .then(r => { if (!r.ok) throw new Error('status error'); return r.text(); })
    .then(data => {
      const parts = data.split('|');
      let lives = 0, shots = 0;
      if (parts.length >= 2) {
        lives = parseInt(parts[0].split(':')[1]);
        shots = parseInt(parts[1].split(':')[1]);
      }
      livesDisplay.innerText = 'Vidas restantes: ' + lives;
      shotsDisplay.innerText = 'Disparos restantes: ' + shots;

      if (lives === 0) {
        fireButton.disabled = true;
        status.innerText = "¡Se te acabaron las vidas!";
      } else if (shots === 0) {
        fireButton.disabled = true;
        status.innerText = "¡Sin balas! No puedes disparar más.";
      } else {
        fireButton.disabled = false;
        status.innerText = "Listo para disparar";
      }
    })
    .catch(err => { console.log('Status poll error', err); });
}
setInterval(updateStatus, 500);
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
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
  return httpd_resp_send(req, (const char *)INDEX_HTML, strlen(INDEX_HTML));
}

static esp_err_t fire_handler(httpd_req_t *req) {
  Serial2.println("FIRE");
  Serial2.flush();

  String response = "";
  unsigned long start = millis();

  // Leer todas las líneas que lleguen durante 1s y actualizar estado inmediatamente.
  while (millis() - start < 1000) {
    while (Serial2.available()) {
      String line = Serial2.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;

      // Guardar última línea recibida y acumular respuesta para el cliente
      lastSerialLine = line;
      if (response.length() > 0) response += "|";
      response += line;

      // Parsear y actualizar variables compartidas
      if (line.startsWith("SHOTS:")) {
        currentShots = line.substring(6).toInt();
      } else if (line.startsWith("LIVES:")) {
        currentLives = line.substring(6).toInt();
      } else if (line == "NO_SHOTS") {
        currentShots = 0;
      }
    }
    delay(1);
  }

  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
  if (response == "") response = "NO_SHOTS";
  return httpd_resp_send(req, response.c_str(), response.length());
}

static esp_err_t status_handler(httpd_req_t *req) {
  char buf[64];
  int len = snprintf(buf, sizeof(buf), "LIVES:%d|SHOTS:%d", currentLives, currentShots);
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
  return httpd_resp_send(req, buf, len);
}

// EXISTENTES: handlers para /m1 y /m2 (permiten control manual desde sliders)
static esp_err_t m1_handler(httpd_req_t *req) {
  char buf[64];
  char val_str[16] = "0";
  if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
    httpd_query_key_value(buf, "val", val_str, sizeof(val_str));
  }
  int v = atoi(val_str);
  // Enviar comando por Serial2 al ESP32 "normal"
  Serial2.print("M1:");
  Serial2.println(v);
  Serial2.flush();

  const char *resp_fmt = "M1:%d";
  int len = snprintf(buf, sizeof(buf), resp_fmt, v);
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
  return httpd_resp_send(req, buf, len);
}

static esp_err_t m2_handler(httpd_req_t *req) {
  char buf[64];
  char val_str[16] = "0";
  if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
    httpd_query_key_value(buf, "val", val_str, sizeof(val_str));
  }
  int v = atoi(val_str);
  Serial2.print("M2:");
  Serial2.println(v);
  Serial2.flush();

  const char *resp_fmt = "M2:%d";
  int len = snprintf(buf, sizeof(buf), resp_fmt, v);
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
  return httpd_resp_send(req, buf, len);
}

// NUEVO HANDLER: /move?cmd=FWD|BACK|LEFT|RIGHT|STOP
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
  else if (strcmp(cmd, "RIGHT") == 0) { v1 = MAXV; v2 = -MAXV; } // gira a la derecha
  else if (strcmp(cmd, "LEFT") == 0) { v1 = -MAXV; v2 = MAXV; }  // gira a la izquierda
  else { v1 = 0; v2 = 0; } // STOP u otros

  // Enviar comandos individuales (ESP normal los procesa)
  char line[32];
  snprintf(line, sizeof(line), "M1:%d", v1);
  Serial2.println(line);
  snprintf(line, sizeof(line), "M2:%d", v2);
  Serial2.println(line);
  Serial2.flush();

  int len = snprintf(buf, sizeof(buf), "MOVE:%s|M1:%d|M2:%d", cmd, v1, v2);
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
  return httpd_resp_send(req, buf, len);
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.max_uri_handlers = 16; // aumentar para nuevos endpoints
  config.lru_purge_enable = true;
  config.task_priority = 5;

  httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler };
  httpd_uri_t fire_uri = { .uri = "/fire", .method = HTTP_GET, .handler = fire_handler };
  httpd_uri_t status_uri = { .uri = "/status", .method = HTTP_GET, .handler = status_handler };
  httpd_uri_t m1_uri = { .uri = "/m1", .method = HTTP_GET, .handler = m1_handler };
  httpd_uri_t m2_uri = { .uri = "/m2", .method = HTTP_GET, .handler = m2_handler };
  httpd_uri_t move_uri = { .uri = "/move", .method = HTTP_GET, .handler = move_handler };

  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &fire_uri);
    httpd_register_uri_handler(camera_httpd, &status_uri);
    httpd_register_uri_handler(camera_httpd, &m1_uri);
    httpd_register_uri_handler(camera_httpd, &m2_uri);
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

  // ---------- CONFIGURACIÓN DE CÁMARA ----------
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

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Error al inicializar la cámara: 0x%x\n", err);
    return;
  }

  // ---------- CONEXIÓN WIFI ----------
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi conectado. Dirección IP: ");
  Serial.println(WiFi.localIP());

  // ---------- INICIAR SERVIDOR ----------
  startCameraServer();
}

void loop() {
  while (Serial2.available()) {
    String line = Serial2.readStringUntil('\n');
    line.trim();
    if (line.startsWith("LIVES:")) currentLives = line.substring(6).toInt();
    else if (line.startsWith("SHOTS:")) currentShots = line.substring(6).toInt();
    // opcional: recibir confirmación de motores si el otro ESP envía algo
  }
  delay(10);
}



