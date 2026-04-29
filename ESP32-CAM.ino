// ============================================================
// ESP32-CAM — Servidor Web com Galeria e Captura para SD
// Otimizado para menor consumo mantendo AP sempre ligado
// ============================================================

#include <Arduino.h>
#include <atomic>

#include "WiFi.h"
#include "esp_wifi.h"
#include "esp_camera.h"
#include "esp_pm.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include "SD_MMC.h"
#include <DNSServer.h>

#include "config.h"
#include "web_assets.h"

// ============================================================
// Variáveis globais
// ============================================================
static AsyncWebServer server(NetConfig::HTTP_PORT);
static DNSServer      dnsServer;
static TaskHandle_t   cameraTaskHandle = nullptr;

static std::atomic<int>      nextPhotoNumber{1};
static std::atomic<bool>     camera_ocupada{false};
static std::atomic<bool>     cameraInicializada{false};
static std::atomic<uint32_t> ultimoAcesso{0};

// ============================================================
// Forward declarations
// ============================================================
static bool iniciarCamera();
static void desligarCamera();
static void tirarFotoSalvarSD();
static void registrarAtividade();
static bool nomeArquivoSeguro(const String& nome);
static void responderErro(AsyncWebServerRequest* req, int code, const __FlashStringHelper* msg);
static void servirArquivoSD(AsyncWebServerRequest* request, bool download);
static void persistirIndiceFoto();
static void aplicarOtimizacoesEnergia();
static bool inicializarSD();
static void registrarRotas();

// ============================================================
// Utilitários
// ============================================================
static void registrarAtividade() {
  ultimoAcesso.store(millis(), std::memory_order_relaxed);
}

static bool nomeArquivoSeguro(const String& nome) {
  if (nome.isEmpty() || nome.length() > GalleryConfig::MAX_NOME_ARQUIVO) return false;
  if (nome.indexOf('/')  != -1) return false;
  if (nome.indexOf('\\') != -1) return false;
  if (nome.indexOf("..") != -1) return false;
  if (nome.indexOf(':')  != -1) return false;
  return true;
}

static inline void responderErro(AsyncWebServerRequest* req, int code,
                                 const __FlashStringHelper* msg) {
  req->send(code, F("text/plain"), msg);
}

static void persistirIndiceFoto() {
  File indexFile = SD_MMC.open("/index.txt", FILE_WRITE);
  if (!indexFile) {
    log_w("Falha ao abrir /index.txt para escrita");
    return;
  }
  indexFile.print(nextPhotoNumber.load());
  indexFile.close();
}

// ============================================================
// Câmera — Inicialização e Desligamento
// ============================================================
static bool iniciarCamera() {
  if (cameraInicializada.load()) return true;

  digitalWrite(Pins::PWDN, LOW);
  delay(10);

  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;

  config.pin_d0 = Pins::Y2;
  config.pin_d1 = Pins::Y3;
  config.pin_d2 = Pins::Y4;
  config.pin_d3 = Pins::Y5;
  config.pin_d4 = Pins::Y6;
  config.pin_d5 = Pins::Y7;
  config.pin_d6 = Pins::Y8;
  config.pin_d7 = Pins::Y9;

  config.pin_xclk  = Pins::XCLK;
  config.pin_pclk  = Pins::PCLK;
  config.pin_vsync = Pins::VSYNC;
  config.pin_href  = Pins::HREF;

  config.pin_sccb_sda = Pins::SIOD;
  config.pin_sccb_scl = Pins::SIOC;

  config.pin_pwdn  = Pins::PWDN;
  config.pin_reset = Pins::RESET;

  config.xclk_freq_hz = CamConfig::XCLK_FREQ_HZ;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = CamConfig::FRAME_SIZE;
  config.jpeg_quality = CamConfig::JPEG_QUALITY;
  config.grab_mode    = CAMERA_GRAB_LATEST;
  config.fb_count     = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    delay(CamConfig::INIT_RETRY_MS);
    err = esp_camera_init(&config);
  }

  if (err != ESP_OK) {
    log_e("Falha init camera: 0x%x", err);
    digitalWrite(Pins::PWDN, HIGH);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    // Pequenos ajustes que podem ajudar a estabilizar e evitar retrabalho
    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
    s->set_saturation(s, 0);
  }

  for (uint8_t i = 0; i < CamConfig::WARMUP_FRAMES; ++i) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    delay(60);
  }

  cameraInicializada.store(true);
  log_i("Camera inicializada");
  return true;
}

static void desligarCamera() {
  if (!cameraInicializada.load()) return;

  esp_camera_deinit();
  digitalWrite(Pins::PWDN, HIGH);
  cameraInicializada.store(false);
  log_i("Camera desligada (economia)");
}

// ============================================================
// Captura e gravação no SD
// ============================================================
static void tirarFotoSalvarSD() {
  registrarAtividade();

  if (!iniciarCamera()) {
    camera_ocupada.store(false);
    return;
  }

  if (!PowerConfig::DISABLE_STATUS_LED_DURING_CAPTURE) {
    digitalWrite(Pins::LED, LOW);   // LED aceso (invertido)
  }

  camera_fb_t* fb = esp_camera_fb_get();

  if (!PowerConfig::DISABLE_STATUS_LED_DURING_CAPTURE) {
    digitalWrite(Pins::LED, HIGH);  // LED apagado
  }

  if (!fb) {
    log_e("Falha ao capturar frame");
    camera_ocupada.store(false);
    if (PowerConfig::CAMERA_AUTO_SLEEP) desligarCamera();
    return;
  }

  auto cleanup = [&]() {
    esp_camera_fb_return(fb);
    camera_ocupada.store(false);
    if (PowerConfig::CAMERA_AUTO_SLEEP) desligarCamera();
  };

  const int photoNum = nextPhotoNumber.load();
  char caminho[32];
  snprintf(caminho, sizeof(caminho), "/foto_%d.jpg", photoNum);

  File file = SD_MMC.open(caminho, FILE_WRITE);
  if (!file) {
    log_e("Erro ao abrir %s", caminho);
    cleanup();
    return;
  }

  const size_t written = file.write(fb->buf, fb->len);
  file.close();

  if (written != fb->len) {
    log_e("Escrita incompleta: %u/%u", (unsigned)written, (unsigned)fb->len);
    SD_MMC.remove(caminho);
    cleanup();
    return;
  }

  nextPhotoNumber.store(photoNum + 1);

  if (photoNum == 1 || (photoNum % GalleryConfig::SAVE_INDEX_INTERVAL) == 0) {
    persistirIndiceFoto();
  }

  cleanup();
}

// ============================================================
// Task da câmera
// ============================================================
static void taskCamera(void* /*parameter*/) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    tirarFotoSalvarSD();
  }
}

// ============================================================
// Rotas — Páginas
// ============================================================
static void paginaInicial(AsyncWebServerRequest* request) {
  registrarAtividade();
  AsyncWebServerResponse* response = request->beginResponse_P(200, "text/html", index_html);
  request->send(response);
}

static void paginaGaleria(AsyncWebServerRequest* request) {
  registrarAtividade();

  AsyncResponseStream* response = request->beginResponseStream("text/html");
  response->print(FPSTR(galeria_header));

  int page = 0;
  if (request->hasParam("page")) {
    page = request->getParam("page")->value().toInt();
    if (page < 0) page = 0;
  }

  const int fotosPorPagina = GalleryConfig::FOTOS_POR_PAGINA;
  const int totalFotos = nextPhotoNumber.load() - 1;

  int fotoInicial = totalFotos - (page * fotosPorPagina);
  int limite      = fotoInicial - fotosPorPagina + 1;
  if (limite < 1) limite = 1;

  bool temFoto = false;
  char fileName[32];
  char path[35];

  if (fotoInicial >= 1) {
    for (int i = fotoInicial; i >= limite; --i) {
      snprintf(fileName, sizeof(fileName), "foto_%d.jpg", i);
      snprintf(path, sizeof(path), "/%s", fileName);

      if (SD_MMC.exists(path)) {
        temFoto = true;
        response->print(F("<div class='card'>"));
        response->printf(
          "<img loading='lazy' data-src='/verfoto?nome=%s&t=%lu' alt='Carregando...'>",
          fileName, (unsigned long)millis());
        response->print(F("<div class='card-actions'>"));
        response->printf(
          "<a href='javascript:void(0)' class='foto-link' data-filename='%s' "
          "data-href='/baixarfoto?nome=%s' onclick=\"baixarFoto('%s')\">📥 Baixar</a>",
          fileName, fileName, fileName);
        response->printf(
          "<button class='btn-danger' onclick=\"confirmarApagarFoto('%s')\">🗑️ Apagar</button>",
          fileName);
        response->print(F("</div></div>"));
      }
    }
  }

  if (!temFoto) {
    response->print(F("<p style='margin-top:20px; font-weight:bold;'>"
                      "Nenhuma foto encontrada nesta página.</p>"));
  }

  if (page > 0) {
    response->printf(
      "<button style='background-color:#34495e;' "
      "onclick=\"window.location.href='/galeria?page=%d'\">⬆️ Mais Recentes</button>",
      page - 1);
  }

  if (limite > 1) {
    response->printf(
      "<button style='background-color:#34495e;' "
      "onclick=\"window.location.href='/galeria?page=%d'\">⬇️ Mais Antigas</button>",
      page + 1);
  }

  response->print(FPSTR(galeria_footer));
  request->send(response);
}

// ============================================================
// Rotas — API
// ============================================================
static void rotaCapture(AsyncWebServerRequest* request) {
  registrarAtividade();

  if (camera_ocupada.exchange(true)) {
    responderErro(request, 429, F("Ocupada"));
    return;
  }

  if (cameraTaskHandle != nullptr) {
    xTaskNotifyGive(cameraTaskHandle);
    request->send(200, F("text/plain"), F("OK"));
  } else {
    camera_ocupada.store(false);
    responderErro(request, 500, F("Task indisponivel"));
  }
}

static void rotaStatus(AsyncWebServerRequest* request) {
  registrarAtividade();
  request->send(200, F("text/plain"),
                camera_ocupada.load() ? F("ocupada") : F("pronto"));
}

static void rotaStatusCamera(AsyncWebServerRequest* request) {
  registrarAtividade();
  request->send(200, F("text/plain"),
                cameraInicializada.load() ? F("ativa") : F("economia"));
}

static void rotaUltimaFoto(AsyncWebServerRequest* request) {
  registrarAtividade();

  const int ultima = nextPhotoNumber.load() - 1;
  if (ultima < 1) {
    responderErro(request, 404, F("Nenhuma foto"));
    return;
  }

  char fileName[32];
  snprintf(fileName, sizeof(fileName), "/foto_%d.jpg", ultima);

  if (SD_MMC.exists(fileName)) {
    AsyncWebServerResponse* response =
        request->beginResponse(SD_MMC, fileName, "image/jpeg", false);
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    request->send(response);
  } else {
    responderErro(request, 404, F("Arquivo nao encontrado"));
  }
}

static void servirArquivoSD(AsyncWebServerRequest* request, bool download) {
  registrarAtividade();

  if (!request->hasParam("nome")) {
    responderErro(request, 400, F("Parametro ausente"));
    return;
  }

  const String param = request->getParam("nome")->value();
  if (!nomeArquivoSeguro(param)) {
    responderErro(request, 400, F("Nome invalido"));
    return;
  }

  const String fileName = "/" + param;
  if (!SD_MMC.exists(fileName)) {
    responderErro(request, 404, F("Arquivo nao encontrado"));
    return;
  }

  AsyncWebServerResponse* response =
      request->beginResponse(SD_MMC, fileName, "image/jpeg", download);

  response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");

  if (download) {
    response->addHeader("Content-Disposition",
                        "attachment; filename=\"" + param + "\"");
  }

  request->send(response);
}

static void rotaVerFoto(AsyncWebServerRequest* r)    { servirArquivoSD(r, false); }
static void rotaBaixarFoto(AsyncWebServerRequest* r) { servirArquivoSD(r, true);  }

static void rotaApagarFoto(AsyncWebServerRequest* request) {
  registrarAtividade();

  if (!request->hasParam("nome")) {
    responderErro(request, 400, F("Parametro ausente"));
    return;
  }

  const String param = request->getParam("nome")->value();
  if (!nomeArquivoSeguro(param)) {
    responderErro(request, 400, F("Nome invalido"));
    return;
  }

  const String fileName = "/" + param;
  if (SD_MMC.remove(fileName)) {
    request->send(200, F("text/plain"), F("Foto apagada"));
  } else {
    responderErro(request, 500, F("Erro ao apagar"));
  }
}

static void rotaLimparSD(AsyncWebServerRequest* request) {
  registrarAtividade();
  request->send(200, F("text/plain"), F("Limpeza iniciada"));

  xTaskCreate([](void*) {
    char filePath[32];
    const int limite = nextPhotoNumber.load() + GalleryConfig::LIMITE_BUSCA_EXTRA;

    for (int i = 1; i <= limite; ++i) {
      snprintf(filePath, sizeof(filePath), "/foto_%d.jpg", i);
      if (SD_MMC.exists(filePath)) {
        SD_MMC.remove(filePath);
      }
      if (i % 10 == 0) {
        vTaskDelay(pdMS_TO_TICKS(5));
      }
    }

    SD_MMC.remove("/index.txt");
    nextPhotoNumber.store(1);
    persistirIndiceFoto();
    log_i("SD limpo");
    vTaskDelete(nullptr);
  }, "ClearSD", 4096, nullptr, 1, nullptr);
}

static void rotaFavicon(AsyncWebServerRequest* request) {
  registrarAtividade();
  request->send(204);
}

// ============================================================
// Captive Portal
// ============================================================
static void responderPortalCativo(AsyncWebServerRequest* request) {
  registrarAtividade();
  request->redirect("http://" + WiFi.softAPIP().toString() + "/");
}

static void onNotFound(AsyncWebServerRequest* request) {
  registrarAtividade();
  request->redirect("http://" + WiFi.softAPIP().toString() + "/");
}

// ============================================================
// SD e índice inicial
// ============================================================
static bool inicializarSD() {
  if (!SD_MMC.begin("/sdcard", true)) {
    log_e("Falha ao montar SD");
    return false;
  }

  int maxNum = 1;

  File indexFile = SD_MMC.open("/index.txt", FILE_READ);
  if (indexFile) {
    char buf[16] = {0};
    size_t len = indexFile.read(reinterpret_cast<uint8_t*>(buf), sizeof(buf) - 1);
    buf[len] = '\0';
    maxNum = atoi(buf);
    indexFile.close();
  }

  if (maxNum < 1) maxNum = 1;

  char testPath[32];
  snprintf(testPath, sizeof(testPath), "/foto_%d.jpg", maxNum);

  while (SD_MMC.exists(testPath)) {
    ++maxNum;
    snprintf(testPath, sizeof(testPath), "/foto_%d.jpg", maxNum);
  }

  nextPhotoNumber.store(maxNum);

  log_i("Proximo numero de foto: %d", nextPhotoNumber.load());
  return true;
}

// ============================================================
// Economia de energia sem desligar AP
// ============================================================
static void aplicarOtimizacoesEnergia() {
  setCpuFrequencyMhz(PowerConfig::CPU_FREQ_MHZ);

  btStop();

  if (NetConfig::WIFI_SLEEP_ENABLED) {
    WiFi.setSleep(true);
  } else {
    WiFi.setSleep(false);
  }

  WiFi.setTxPower(NetConfig::TX_POWER);

  // Tenta ativar modem sleep / power save no Wi-Fi
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

  // Configuração dinâmica de frequência (best effort)
  esp_pm_config_esp32_t pm_config = {};
  pm_config.max_freq_mhz = PowerConfig::CPU_FREQ_MHZ;
  pm_config.min_freq_mhz = 40;
  pm_config.light_sleep_enable = false; // AP contínuo: não usar light sleep aqui
  esp_pm_configure(&pm_config);

  log_i("Otimizacoes de energia aplicadas");
}

// ============================================================
// Registro de rotas
// ============================================================
static void registrarRotas() {
  server.on("/",              HTTP_GET, paginaInicial);
  server.on("/capture",       HTTP_GET, rotaCapture);
  server.on("/status",        HTTP_GET, rotaStatus);
  server.on("/status_camera", HTTP_GET, rotaStatusCamera);
  server.on("/ultima_foto",   HTTP_GET, rotaUltimaFoto);
  server.on("/galeria",       HTTP_GET, paginaGaleria);
  server.on("/verfoto",       HTTP_GET, rotaVerFoto);
  server.on("/baixarfoto",    HTTP_GET, rotaBaixarFoto);
  server.on("/apagarfoto",    HTTP_GET, rotaApagarFoto);
  server.on("/limparsd",      HTTP_GET, rotaLimparSD);
  server.on("/favicon.ico",   HTTP_GET, rotaFavicon);

  static const char* portalPaths[] = {
    "/generate_204", "/gen_204", "/fwlink",
    "/hotspot-detect.html", "/canonical.html",
    "/success.txt", "/success.html",
    "/ncsi.txt", "/connecttest.txt", "/redirect",
    "/mobile/status.php", "/library/test/success.html"
  };

  for (const char* p : portalPaths) {
    server.on(p, HTTP_GET, responderPortalCativo);
  }

  server.onNotFound(onNotFound);
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // Pinos
  pinMode(Pins::LED, OUTPUT);
  digitalWrite(Pins::LED, HIGH);   // LED apagado (invertido)

  pinMode(Pins::PWDN, OUTPUT);
  digitalWrite(Pins::PWDN, HIGH);  // câmera desligada

  pinMode(Pins::FLASH, OUTPUT);
  digitalWrite(Pins::FLASH, PowerConfig::FORCE_FLASH_OFF ? LOW : LOW);

  aplicarOtimizacoesEnergia();

  // Wi-Fi Access Point sempre ativo
  WiFi.mode(WIFI_AP);
  WiFi.softAP(NetConfig::SSID, NetConfig::PASSWORD,
              NetConfig::WIFI_CHANNEL, 0, NetConfig::MAX_CLIENTS);

  dnsServer.start(NetConfig::DNS_PORT, "*", WiFi.softAPIP());

  if (!inicializarSD()) {
    log_e("Sistema sem SD — abortando");
    return;
  }

  ultimoAcesso.store(millis());
  cameraInicializada.store(false);

  DefaultHeaders::Instance().addHeader("Cache-Control",
      "no-store, no-cache, must-revalidate, max-age=0");
  DefaultHeaders::Instance().addHeader("Pragma", "no-cache");

  registrarRotas();
  server.begin();

  xTaskCreatePinnedToCore(taskCamera, "CamTask", 8192,
                          nullptr, 3, &cameraTaskHandle, 1);

  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 1);

  log_i("Setup concluido. IP: %s", WiFi.softAPIP().toString().c_str());
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  dnsServer.processNextRequest();

  const uint32_t agora = millis();

  if (PowerConfig::CAMERA_AUTO_SLEEP &&
      cameraInicializada.load() &&
      !camera_ocupada.load() &&
      (agora - ultimoAcesso.load() > CamConfig::SLEEP_TIMEOUT_MS)) {
    desligarCamera();
  }

  // Em vez de delay(), usa scheduler do FreeRTOS
  vTaskDelay(pdMS_TO_TICKS(CamConfig::LOOP_DELAY_MS));
}