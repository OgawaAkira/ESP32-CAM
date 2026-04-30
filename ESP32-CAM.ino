// ============================================================
// ESP32-CAM — Servidor Web com Galeria e Captura para SD
// Perfil: AP estável + preview rápida dedicada para tela principal
// ============================================================

#include <Arduino.h>
#include <atomic>

#include "WiFi.h"
#include "esp_wifi.h"
#include "esp_camera.h"
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include "SD_MMC.h"
#include <DNSServer.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "config.h"
#include "web_assets.h"

// ============================================================
// Globais
// ============================================================
static AsyncWebServer server(NetConfig::HTTP_PORT);
static DNSServer      dnsServer;
static TaskHandle_t   cameraTaskHandle  = nullptr;
static TaskHandle_t   clearSdTaskHandle = nullptr;

static SemaphoreHandle_t sdMutex = nullptr;

static std::atomic<int>      nextPhotoNumber{1};
static std::atomic<bool>     cameraOcupada{false};
static std::atomic<bool>     cameraInicializada{false};
static std::atomic<bool>     limpezaSD{false};
static std::atomic<uint32_t> ultimoAcessoHttp{0};
static std::atomic<uint32_t> ultimaAtividadeCamera{0};

// ============================================================
// Forward declarations
// ============================================================
static void registrarAtividadeHttp();
static void registrarAtividadeCamera();
static bool nomeArquivoSeguro(const String& nome);
static void responderErro(AsyncWebServerRequest* req, int code, const __FlashStringHelper* msg);

static bool takeSdMutex(uint32_t timeoutMs = 2000);
static void giveSdMutex();
static bool persistirIndiceFotoLocked();

static void setCpuFrequencySafe(uint32_t mhz);
static void cpuModoOcioso();
static void cpuModoAtivo();

static void aplicarEstadoBaixoConsumoPins();
static void energiaCamera(bool ligada);
static bool iniciarCamera();
static void desligarCamera();

static bool inicializarSD();
static bool gerarPreviewRapidoLocked();
static bool encontrarUltimaFotoExistenteLocked(char* outPath, size_t outPathLen, int* outNum = nullptr);

static void tirarFotoSalvarSD();
static void taskCamera(void* parameter);
static void taskLimparSD(void* parameter);

static void aplicarWiFiPerfilEconomia();
static void aplicarTxPowerSeguro();

static void paginaInicial(AsyncWebServerRequest* request);
static void paginaGaleria(AsyncWebServerRequest* request);

static void rotaCapture(AsyncWebServerRequest* request);
static void rotaStatus(AsyncWebServerRequest* request);
static void rotaStatusCamera(AsyncWebServerRequest* request);
static void rotaUltimaFoto(AsyncWebServerRequest* request);
static void rotaPreviewPrincipal(AsyncWebServerRequest* request);
static void servirArquivoSD(AsyncWebServerRequest* request, bool download);
static void rotaVerFoto(AsyncWebServerRequest* r);
static void rotaBaixarFoto(AsyncWebServerRequest* r);
static void rotaApagarFoto(AsyncWebServerRequest* request);
static void rotaLimparSD(AsyncWebServerRequest* request);
static void rotaFavicon(AsyncWebServerRequest* request);

static void responderPortalCativo(AsyncWebServerRequest* request);
static void onNotFound(AsyncWebServerRequest* request);
static void registrarRotas();

// ============================================================
// Utilitários
// ============================================================
static void registrarAtividadeHttp() {
  ultimoAcessoHttp.store(millis(), std::memory_order_relaxed);
}

static void registrarAtividadeCamera() {
  ultimaAtividadeCamera.store(millis(), std::memory_order_relaxed);
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

// ============================================================
// Mutex do SD
// ============================================================
static bool takeSdMutex(uint32_t timeoutMs) {
  if (!sdMutex) return false;
  return xSemaphoreTake(sdMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

static void giveSdMutex() {
  if (sdMutex) xSemaphoreGive(sdMutex);
}

static bool persistirIndiceFotoLocked() {
  if (SD_MMC.exists("/index.txt")) {
    SD_MMC.remove("/index.txt");
  }

  File indexFile = SD_MMC.open("/index.txt", FILE_WRITE);
  if (!indexFile) {
    log_w("Falha ao abrir /index.txt para escrita");
    return false;
  }

  indexFile.print(nextPhotoNumber.load());
  indexFile.close();
  return true;
}

// ============================================================
// CPU
// ============================================================
static void setCpuFrequencySafe(uint32_t alvoMhz) {
  const uint32_t atual = getCpuFrequencyMhz();
  if (atual == alvoMhz) return;

  if (atual == 240 && alvoMhz == 80) {
    setCpuFrequencyMhz(160);
    delay(1);
    setCpuFrequencyMhz(80);
    return;
  }

  if (atual == 80 && alvoMhz == 40) {
    setCpuFrequencyMhz(40);
    return;
  }

  if (atual == 40 && alvoMhz == 80) {
    setCpuFrequencyMhz(80);
    return;
  }

  setCpuFrequencyMhz(alvoMhz);
}

static void cpuModoOcioso() {
  setCpuFrequencySafe(PowerConfig::CPU_IDLE_FREQ_MHZ);
}

static void cpuModoAtivo() {
  setCpuFrequencySafe(PowerConfig::CPU_ACTIVE_FREQ_MHZ);
}

// ============================================================
// Baixo consumo físico
// ============================================================
static void aplicarEstadoBaixoConsumoPins() {
  pinMode(Pins::LED, OUTPUT);
  digitalWrite(Pins::LED, HIGH);

  pinMode(Pins::FLASH, OUTPUT);
  digitalWrite(Pins::FLASH, LOW);

  pinMode(Pins::PWDN, OUTPUT);
  digitalWrite(Pins::PWDN, HIGH);

  if (Pins::CAM_POWER_EN != -1) {
    pinMode(Pins::CAM_POWER_EN, OUTPUT);
    digitalWrite(Pins::CAM_POWER_EN,
                 PowerConfig::CAM_POWER_EN_ACTIVE_HIGH ? LOW : HIGH);
  }
}

static void energiaCamera(bool ligada) {
  if (Pins::CAM_POWER_EN != -1) {
    digitalWrite(Pins::CAM_POWER_EN,
                 ligada == PowerConfig::CAM_POWER_EN_ACTIVE_HIGH ? HIGH : LOW);
    delay(PowerConfig::CAM_POWER_SWITCH_DELAY_MS);
  }

  digitalWrite(Pins::PWDN, ligada ? LOW : HIGH);

  if (!ligada) {
    digitalWrite(Pins::FLASH, LOW);
    digitalWrite(Pins::LED, HIGH);
  }
}

// ============================================================
// Wi-Fi
// ============================================================
static void aplicarTxPowerSeguro() {
#if defined(WIFI_POWER_11dBm)
  if (WiFi.setTxPower(WIFI_POWER_11dBm)) return;
#endif
#if defined(WIFI_POWER_13dBm)
  if (WiFi.setTxPower(WIFI_POWER_13dBm)) return;
#endif
#if defined(WIFI_POWER_8_5dBm)
  if (WiFi.setTxPower(WIFI_POWER_8_5dBm)) return;
#endif
#if defined(WIFI_POWER_7dBm)
  if (WiFi.setTxPower(WIFI_POWER_7dBm)) return;
#endif
}

static void aplicarWiFiPerfilEconomia() {
  btStop();

  if (NetConfig::TESTAR_WIFI_PS_EXPERIMENTAL_NO_AP) {
    WiFi.setSleep(true);
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  } else {
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
  }

  aplicarTxPowerSeguro();
}

// ============================================================
// Câmera
// ============================================================
static bool iniciarCamera() {
  if (cameraInicializada.load(std::memory_order_relaxed)) {
    registrarAtividadeCamera();
    return true;
  }

  cpuModoAtivo();
  energiaCamera(true);
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
  config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_count     = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    delay(CamConfig::INIT_RETRY_MS);
    err = esp_camera_init(&config);
  }

  if (err != ESP_OK) {
    energiaCamera(false);
    cameraInicializada.store(false, std::memory_order_relaxed);
    cpuModoOcioso();
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_framesize(s, CamConfig::FRAME_SIZE);
    s->set_quality(s, CamConfig::JPEG_QUALITY);
    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
    s->set_saturation(s, 0);
  }

  for (uint8_t i = 0; i < CamConfig::WARMUP_FRAMES; ++i) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    delay(CamConfig::WARMUP_DELAY_MS);
  }

  cameraInicializada.store(true, std::memory_order_relaxed);
  registrarAtividadeCamera();
  return true;
}

static void desligarCamera() {
  if (!cameraInicializada.load(std::memory_order_relaxed)) {
    energiaCamera(false);
    return;
  }

  esp_camera_deinit();
  delay(5);
  energiaCamera(false);
  cameraInicializada.store(false, std::memory_order_relaxed);
}

// ============================================================
// SD / arquivos
// ============================================================
static bool encontrarUltimaFotoExistenteLocked(char* outPath, size_t outPathLen, int* outNum) {
  int start = nextPhotoNumber.load(std::memory_order_relaxed) - 1;
  if (start < 1) return false;

  for (int i = start; i >= 1; --i) {
    snprintf(outPath, outPathLen, "/foto_%d.jpg", i);
    if (SD_MMC.exists(outPath)) {
      if (outNum) *outNum = i;
      return true;
    }
  }
  return false;
}

static bool inicializarSD() {
  if (!SD_MMC.begin("/sdcard", true)) {
    return false;
  }

  int maxNum = 1;

  if (!takeSdMutex(3000)) {
    return false;
  }

  File indexFile = SD_MMC.open("/index.txt", FILE_READ);
  if (indexFile) {
    char buf[16] = {0};
    const size_t len = indexFile.read(reinterpret_cast<uint8_t*>(buf), sizeof(buf) - 1);
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

  nextPhotoNumber.store(maxNum, std::memory_order_relaxed);
  giveSdMutex();
  return true;
}

// ============================================================
// Preview rápida
// ============================================================
static bool gerarPreviewRapidoLocked() {
  if (!PreviewConfig::ENABLE_PREVIEW_FILE) return true;

  sensor_t* s = esp_camera_sensor_get();
  if (!s) return false;

  const framesize_t oldFrameSize = (framesize_t)s->status.framesize;
  const int oldQuality = s->status.quality;

  s->set_framesize(s, PreviewConfig::FRAME_SIZE);
  s->set_quality(s, PreviewConfig::JPEG_QUALITY);
  delay(PreviewConfig::SETTLE_DELAY_MS);

  for (uint8_t i = 0; i < PreviewConfig::WARMUP_FRAMES; ++i) {
    camera_fb_t* warm = esp_camera_fb_get();
    if (warm) esp_camera_fb_return(warm);
    delay(PreviewConfig::WARMUP_DELAY_MS);
  }

  camera_fb_t* fbPrev = esp_camera_fb_get();
  if (!fbPrev) {
    s->set_framesize(s, oldFrameSize);
    s->set_quality(s, oldQuality);
    return false;
  }

  if (SD_MMC.exists(PreviewConfig::FILE_PATH)) {
    SD_MMC.remove(PreviewConfig::FILE_PATH);
  }

  File file = SD_MMC.open(PreviewConfig::FILE_PATH, FILE_WRITE);
  bool ok = false;

  if (file) {
    const size_t written = file.write(fbPrev->buf, fbPrev->len);
    file.close();
    ok = (written == fbPrev->len);
  }

  if (!ok) {
    SD_MMC.remove(PreviewConfig::FILE_PATH);
  }

  esp_camera_fb_return(fbPrev);

  s->set_framesize(s, oldFrameSize);
  s->set_quality(s, oldQuality);
  delay(PreviewConfig::RESTORE_DELAY_MS);

  return ok;
}

// ============================================================
// Captura
// ============================================================
static void tirarFotoSalvarSD() {
  registrarAtividadeHttp();
  registrarAtividadeCamera();
  cpuModoAtivo();

  if (!iniciarCamera()) {
    cameraOcupada.store(false, std::memory_order_relaxed);
    cpuModoOcioso();
    return;
  }

  if (!PowerConfig::DISABLE_STATUS_LED_DURING_CAPTURE) {
    digitalWrite(Pins::LED, LOW);
  }

  camera_fb_t* fb = esp_camera_fb_get();

  if (!PowerConfig::DISABLE_STATUS_LED_DURING_CAPTURE) {
    digitalWrite(Pins::LED, HIGH);
  }

  if (!fb) {
    cameraOcupada.store(false, std::memory_order_relaxed);
    registrarAtividadeCamera();
    cpuModoOcioso();
    return;
  }

  auto cleanup = [&]() {
    if (fb) {
      esp_camera_fb_return(fb);
      fb = nullptr;
    }
    cameraOcupada.store(false, std::memory_order_relaxed);
    registrarAtividadeCamera();
    cpuModoOcioso();
  };

  if (!takeSdMutex(6000)) {
    cleanup();
    return;
  }

  const int photoNum = nextPhotoNumber.load(std::memory_order_relaxed);
  char caminho[32];
  snprintf(caminho, sizeof(caminho), "/foto_%d.jpg", photoNum);

  File file = SD_MMC.open(caminho, FILE_WRITE);
  if (!file) {
    giveSdMutex();
    cleanup();
    return;
  }

  const size_t written = file.write(fb->buf, fb->len);
  file.close();

  if (written != fb->len) {
    SD_MMC.remove(caminho);
    giveSdMutex();
    cleanup();
    return;
  }

  nextPhotoNumber.store(photoNum + 1, std::memory_order_relaxed);

  if (photoNum == 1 || (photoNum % GalleryConfig::SAVE_INDEX_INTERVAL) == 0) {
    persistirIndiceFotoLocked();
  }

  esp_camera_fb_return(fb);
  fb = nullptr;

  if (PreviewConfig::ENABLE_PREVIEW_FILE) {
    gerarPreviewRapidoLocked();
  }

  giveSdMutex();
  cleanup();
}

static void taskCamera(void* /*parameter*/) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    tirarFotoSalvarSD();
  }
}

// ============================================================
// Limpeza do SD
// ============================================================
static void taskLimparSD(void* /*parameter*/) {
  cpuModoAtivo();
  limpezaSD.store(true, std::memory_order_relaxed);

  if (cameraInicializada.load(std::memory_order_relaxed) &&
      !cameraOcupada.load(std::memory_order_relaxed)) {
    desligarCamera();
  }

  if (!takeSdMutex(15000)) {
    limpezaSD.store(false, std::memory_order_relaxed);
    clearSdTaskHandle = nullptr;
    cpuModoOcioso();
    vTaskDelete(nullptr);
    return;
  }

  char filePath[32];
  const int limite = nextPhotoNumber.load(std::memory_order_relaxed) + GalleryConfig::LIMITE_BUSCA_EXTRA;

  for (int i = 1; i <= limite; ++i) {
    snprintf(filePath, sizeof(filePath), "/foto_%d.jpg", i);
    if (SD_MMC.exists(filePath)) {
      SD_MMC.remove(filePath);
    }
    if ((i % 10) == 0) {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }

  if (SD_MMC.exists(PreviewConfig::FILE_PATH)) {
    SD_MMC.remove(PreviewConfig::FILE_PATH);
  }

  SD_MMC.remove("/index.txt");
  nextPhotoNumber.store(1, std::memory_order_relaxed);
  persistirIndiceFotoLocked();
  giveSdMutex();

  limpezaSD.store(false, std::memory_order_relaxed);
  clearSdTaskHandle = nullptr;
  cpuModoOcioso();
  vTaskDelete(nullptr);
}

// ============================================================
// Páginas
// ============================================================
static void paginaInicial(AsyncWebServerRequest* request) {
  registrarAtividadeHttp();
  AsyncWebServerResponse* response = request->beginResponse_P(200, "text/html", index_html);
  request->send(response);
}

static void paginaGaleria(AsyncWebServerRequest* request) {
  registrarAtividadeHttp();

  AsyncResponseStream* response = request->beginResponseStream("text/html");
  response->print(FPSTR(galeria_header));

  int page = 0;
  if (request->hasParam("page")) {
    page = request->getParam("page")->value().toInt();
    if (page < 0) page = 0;
  }

  const int fotosPorPagina = GalleryConfig::FOTOS_POR_PAGINA;
  const int totalFotos = nextPhotoNumber.load(std::memory_order_relaxed) - 1;

  int fotoInicial = totalFotos - (page * fotosPorPagina);
  int limite      = fotoInicial - fotosPorPagina + 1;
  if (limite < 1) limite = 1;

  bool temFoto = false;
  char fileName[32];
  char path[35];

  if (fotoInicial >= 1) {
    if (takeSdMutex(3000)) {
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
      giveSdMutex();
    }
  }

  if (!temFoto) {
    response->print(F("<p style='margin-top:20px; font-weight:bold;'>Nenhuma foto encontrada nesta página.</p>"));
  }

  if (page > 0) {
    response->printf(
      "<button style='background-color:#34495e;' onclick=\"window.location.href='/galeria?page=%d'\">⬆️ Mais Recentes</button>",
      page - 1);
  }

  if (limite > 1) {
    response->printf(
      "<button style='background-color:#34495e;' onclick=\"window.location.href='/galeria?page=%d'\">⬇️ Mais Antigas</button>",
      page + 1);
  }

  response->print(FPSTR(galeria_footer));
  request->send(response);
}

// ============================================================
// API
// ============================================================
static void rotaCapture(AsyncWebServerRequest* request) {
  registrarAtividadeHttp();

  if (limpezaSD.load(std::memory_order_relaxed)) {
    responderErro(request, 423, F("SD em limpeza"));
    return;
  }

  if (cameraOcupada.exchange(true)) {
    responderErro(request, 429, F("Ocupada"));
    return;
  }

  if (cameraTaskHandle != nullptr) {
    xTaskNotifyGive(cameraTaskHandle);
    request->send(200, F("text/plain"), F("OK"));
  } else {
    cameraOcupada.store(false, std::memory_order_relaxed);
    responderErro(request, 500, F("Task indisponivel"));
  }
}

static void rotaStatus(AsyncWebServerRequest* request) {
  registrarAtividadeHttp();

  if (limpezaSD.load(std::memory_order_relaxed)) {
    request->send(200, F("text/plain"), F("limpeza"));
    return;
  }

  request->send(200, F("text/plain"),
                cameraOcupada.load(std::memory_order_relaxed) ? F("ocupada") : F("pronto"));
}

static void rotaStatusCamera(AsyncWebServerRequest* request) {
  registrarAtividadeHttp();
  request->send(200, F("text/plain"),
                cameraInicializada.load(std::memory_order_relaxed) ? F("ativa") : F("economia"));
}

static void rotaUltimaFoto(AsyncWebServerRequest* request) {
  registrarAtividadeHttp();

  if (!takeSdMutex(3000)) {
    responderErro(request, 503, F("SD ocupado"));
    return;
  }

  char fileName[32];
  const bool existe = encontrarUltimaFotoExistenteLocked(fileName, sizeof(fileName), nullptr);
  giveSdMutex();

  if (!existe) {
    responderErro(request, 404, F("Nenhuma foto"));
    return;
  }

  AsyncWebServerResponse* response =
      request->beginResponse(SD_MMC, fileName, "image/jpeg", false);

  response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  request->send(response);
}

static void rotaPreviewPrincipal(AsyncWebServerRequest* request) {
  registrarAtividadeHttp();

  if (!takeSdMutex(3000)) {
    responderErro(request, 503, F("SD ocupado"));
    return;
  }

  bool existePreview = false;
  if (PreviewConfig::ENABLE_PREVIEW_FILE) {
    existePreview = SD_MMC.exists(PreviewConfig::FILE_PATH);
  }
  giveSdMutex();

  if (existePreview) {
    AsyncWebServerResponse* response =
        request->beginResponse(SD_MMC, PreviewConfig::FILE_PATH, "image/jpeg", false);
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    request->send(response);
    return;
  }

  rotaUltimaFoto(request);
}

static void servirArquivoSD(AsyncWebServerRequest* request, bool download) {
  registrarAtividadeHttp();

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

  if (!takeSdMutex(3000)) {
    responderErro(request, 503, F("SD ocupado"));
    return;
  }

  const bool existe = SD_MMC.exists(fileName);
  giveSdMutex();

  if (!existe) {
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
  registrarAtividadeHttp();

  if (limpezaSD.load(std::memory_order_relaxed)) {
    responderErro(request, 423, F("SD em limpeza"));
    return;
  }

  if (!request->hasParam("nome")) {
    responderErro(request, 400, F("Parametro ausente"));
    return;
  }

  const String param = request->getParam("nome")->value();
  if (!nomeArquivoSeguro(param)) {
    responderErro(request, 400, F("Nome invalido"));
    return;
  }

  if (!takeSdMutex(4000)) {
    responderErro(request, 503, F("SD ocupado"));
    return;
  }

  const String fileName = "/" + param;
  const bool ok = SD_MMC.remove(fileName);
  giveSdMutex();

  if (ok) {
    request->send(200, F("text/plain"), F("Foto apagada"));
  } else {
    responderErro(request, 500, F("Erro ao apagar"));
  }
}

static void rotaLimparSD(AsyncWebServerRequest* request) {
  registrarAtividadeHttp();

  if (limpezaSD.load(std::memory_order_relaxed) || clearSdTaskHandle != nullptr) {
    responderErro(request, 409, F("Limpeza ja em andamento"));
    return;
  }

  if (cameraOcupada.load(std::memory_order_relaxed)) {
    responderErro(request, 409, F("Camera ocupada"));
    return;
  }

  BaseType_t ok = xTaskCreatePinnedToCore(
    taskLimparSD,
    "ClearSD",
    4096,
    nullptr,
    1,
    &clearSdTaskHandle,
    1
  );

  if (ok == pdPASS) {
    request->send(200, F("text/plain"), F("Limpeza iniciada"));
  } else {
    clearSdTaskHandle = nullptr;
    responderErro(request, 500, F("Falha ao iniciar limpeza"));
  }
}

static void rotaFavicon(AsyncWebServerRequest* request) {
  registrarAtividadeHttp();
  request->send(204);
}

// ============================================================
// Captive portal
// ============================================================
static void responderPortalCativo(AsyncWebServerRequest* request) {
  registrarAtividadeHttp();
  request->redirect("http://" + WiFi.softAPIP().toString() + "/");
}

static void onNotFound(AsyncWebServerRequest* request) {
  registrarAtividadeHttp();
  request->redirect("http://" + WiFi.softAPIP().toString() + "/");
}

// ============================================================
// Rotas
// ============================================================
static void registrarRotas() {
  server.on("/",                  HTTP_GET, paginaInicial);
  server.on("/capture",           HTTP_GET, rotaCapture);
  server.on("/status",            HTTP_GET, rotaStatus);
  server.on("/status_camera",     HTTP_GET, rotaStatusCamera);
  server.on("/ultima_foto",       HTTP_GET, rotaUltimaFoto);
  server.on("/preview_principal", HTTP_GET, rotaPreviewPrincipal);
  server.on("/galeria",           HTTP_GET, paginaGaleria);
  server.on("/verfoto",           HTTP_GET, rotaVerFoto);
  server.on("/baixarfoto",        HTTP_GET, rotaBaixarFoto);
  server.on("/apagarfoto",        HTTP_GET, rotaApagarFoto);
  server.on("/limparsd",          HTTP_GET, rotaLimparSD);
  server.on("/favicon.ico",       HTTP_GET, rotaFavicon);

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
  delay(500);

  aplicarEstadoBaixoConsumoPins();

  sdMutex = xSemaphoreCreateMutex();
  if (!sdMutex) {
    return;
  }

  cpuModoAtivo();
  delay(100);

  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  delay(200);

  WiFi.mode(WIFI_OFF);
  delay(200);

  WiFi.mode(WIFI_AP);
  delay(300);

  bool apOk = WiFi.softAP(
    NetConfig::SSID,
    NetConfig::PASSWORD,
    NetConfig::WIFI_CHANNEL,
    0,
    NetConfig::MAX_CLIENTS
  );

  if (!apOk) {
    return;
  }

  aplicarWiFiPerfilEconomia();

  IPAddress ip = WiFi.softAPIP();
  dnsServer.start(NetConfig::DNS_PORT, "*", ip);

  inicializarSD();

  ultimoAcessoHttp.store(millis(), std::memory_order_relaxed);
  ultimaAtividadeCamera.store(millis(), std::memory_order_relaxed);
  cameraInicializada.store(false, std::memory_order_relaxed);

  DefaultHeaders::Instance().addHeader(
    "Cache-Control",
    "no-store, no-cache, must-revalidate, max-age=0"
  );
  DefaultHeaders::Instance().addHeader("Pragma", "no-cache");

  registrarRotas();
  server.begin();

  xTaskCreatePinnedToCore(
    taskCamera,
    "CamTask",
    8192,
    nullptr,
    3,
    &cameraTaskHandle,
    1
  );

  cpuModoOcioso();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  dnsServer.processNextRequest();

  const uint32_t agora = millis();

  if (PowerConfig::CAMERA_AUTO_SLEEP &&
      cameraInicializada.load(std::memory_order_relaxed) &&
      !cameraOcupada.load(std::memory_order_relaxed) &&
      !limpezaSD.load(std::memory_order_relaxed) &&
      (agora - ultimaAtividadeCamera.load(std::memory_order_relaxed) > CamConfig::SLEEP_TIMEOUT_MS)) {
    desligarCamera();
    cpuModoOcioso();
  }

  vTaskDelay(pdMS_TO_TICKS(CamConfig::LOOP_DELAY_MS));
}