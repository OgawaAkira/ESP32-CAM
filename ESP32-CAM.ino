#include "WiFi.h"
#include "esp_camera.h"
#include "Arduino.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include "SD_MMC.h"
#include <DNSServer.h>

const char* ssid = "ESP32_Camera";
const char* password = "12345678";

AsyncWebServer server(80);
DNSServer dnsServer;
TaskHandle_t cameraTaskHandle = NULL;

int nextPhotoNumber = 1;
volatile bool camera_ocupada = false;

unsigned long ultimoAcesso = 0;
bool cameraInicializada = false;

// ========================= HTML PRINCIPAL =========================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html>
<head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width, initial-scale=1.0'>
<title>Minha Câmera ESP32</title>
<style>
body {
  text-align:center;
  font-family:'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
  background-color:#f4f4f9;
  margin:0;
  padding:30px 15px;
  color:#333;
}
h2 {
  color:#2c3e50;
  margin-bottom:30px;
}
button {
  background-color:#3498db;
  color:white;
  border:none;
  padding:18px 20px;
  font-size:18px;
  font-weight:bold;
  border-radius:10px;
  margin:10px 0;
  width:100%;
  max-width:320px;
  box-shadow: 0 4px 6px rgba(0,0,0,0.1);
  cursor:pointer;
  transition:0.2s;
}
button:active {
  transform: scale(0.95);
}
button:disabled {
  background-color:#95a5a6;
  cursor:not-allowed;
}
.btn-galeria {
  background-color:#2ecc71;
}
.preview-container {
  margin-top: 25px;
  display: none;
  flex-direction: column;
  align-items: center;
}
.preview-container img {
  max-width: 100%;
  width: 320px;
  border-radius: 10px;
  box-shadow: 0 4px 8px rgba(0,0,0,0.2);
  background-color: #ecf0f1;
  min-height: 200px;
  transition: opacity 0.3s;
}
.preview-title {
  font-weight: bold;
  color: #7f8c8d;
  margin-bottom: 10px;
  font-size: 14px;
}
.status-box {
  margin-top: 14px;
  font-size: 14px;
  color: #7f8c8d;
  font-weight: bold;
}
.toast {
  position: fixed;
  left: 50%;
  bottom: 20px;
  transform: translateX(-50%);
  background: rgba(44,62,80,0.96);
  color: white;
  padding: 14px 18px;
  border-radius: 10px;
  font-size: 14px;
  box-shadow: 0 6px 18px rgba(0,0,0,0.2);
  z-index: 9999;
  display: none;
  max-width: 90%;
}
</style>
</head>
<body>
<h2>📷 Controle ESP32-CAM</h2>
<button onclick="tirarFoto(this)">📸 TIRAR FOTO</button><br>
<button class='btn-galeria' onclick="window.location.href='/galeria'">🖼️ VER GALERIA</button>

<div class="status-box" id="status-camera">Status: verificando...</div>

<div class="preview-container" id="preview-box">
  <div class="preview-title">Visualização Rápida:</div>
  <img id="ultima-foto" alt="Carregando imagem...">
</div>

<div class="toast" id="toast"></div>

<script>
function showToast(msg, tempo = 2200) {
  const toast = document.getElementById('toast');
  toast.innerText = msg;
  toast.style.display = 'block';
  clearTimeout(window.__toastTimer);
  window.__toastTimer = setTimeout(() => {
    toast.style.display = 'none';
  }, tempo);
}

function atualizarStatusCamera() {
  fetch('/status_camera?t=' + Date.now())
    .then(response => response.text())
    .then(txt => {
      const el = document.getElementById('status-camera');
      if (txt === 'ativa') {
        el.innerText = 'Status: câmera ativa';
        el.style.color = '#27ae60';
      } else {
        el.innerText = 'Status: modo economia';
        el.style.color = '#7f8c8d';
      }
    })
    .catch(() => {});
}

function tirarFoto(btn) {
  btn.innerText = '⏳ Salvando SD...';
  btn.disabled = true;

  let img = document.getElementById('ultima-foto');
  let box = document.getElementById('preview-box');

  if (img && box.style.display !== 'none') {
    img.style.opacity = '0.3';
  }

  fetch('/capture?t=' + Date.now())
    .then(response => {
      if (!response.ok) throw new Error();
      atualizarStatusCamera();
      setTimeout(() => verificarStatus(btn), 120);
    })
    .catch(() => {
      btn.innerText = '📸 TIRAR FOTO';
      btn.disabled = false;
      showToast('Erro ao iniciar captura.');
    });
}

function verificarStatus(btn) {
  fetch('/status?t=' + Date.now())
    .then(response => response.text())
    .then(estado => {
      if (estado === 'pronto') {
        btn.innerText = '📸 TIRAR FOTO';
        btn.disabled = false;
        atualizarPreview();
        atualizarStatusCamera();
        showToast('Foto salva com sucesso!');
      } else {
        setTimeout(() => verificarStatus(btn), 120);
      }
    })
    .catch(() => {
      setTimeout(() => verificarStatus(btn), 150);
    });
}

function atualizarPreview() {
  let img = document.getElementById('ultima-foto');
  let box = document.getElementById('preview-box');

  box.style.display = 'flex';
  img.style.opacity = '0.4';

  img.onload = function() {
    img.style.opacity = '1';
  };

  img.onerror = function() {
    box.style.display = 'none';
  };

  img.src = '/ultima_foto?t=' + Date.now();
}

window.onload = function() {
  atualizarPreview();
  atualizarStatusCamera();
  setInterval(atualizarStatusCamera, 5000);
};
</script>
</body></html>
)rawliteral";

// ========================= HTML GALERIA =========================
const char galeria_header[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html>
<head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width, initial-scale=1.0'>
<title>Galeria de Fotos</title>
<style>
body {
  font-family:'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
  text-align:center;
  background-color:#f4f4f9;
  margin:0;
  padding:20px 10px;
  color:#333;
}
a {
  color:#3498db;
  text-decoration:none;
  font-weight:bold;
  font-size:16px;
}
button {
  background-color:#e67e22;
  color:white;
  border:none;
  padding:15px;
  font-size:16px;
  font-weight:bold;
  border-radius:8px;
  margin:10px 0;
  width:100%;
  max-width:320px;
  box-shadow: 0 4px 6px rgba(0,0,0,0.1);
  cursor:pointer;
  transition:0.2s;
}
button:active {
  transform: scale(0.95);
}
button:disabled {
  background-color:#95a5a6;
  cursor:not-allowed;
}
.btn-danger {
  background-color:#e74c3c;
}
.btn-danger-all {
  background-color:#c0392b;
  margin-top:20px;
}
.card {
  background:white;
  border-radius:10px;
  padding:15px;
  margin-bottom:20px;
  box-shadow: 0 4px 8px rgba(0,0,0,0.1);
  width:100%;
  max-width:350px;
  box-sizing:border-box;
}
img {
  border-radius:8px;
  width:100%;
  height:auto;
  margin-top:10px;
  margin-bottom:10px;
  background-color:#ecf0f1;
  min-height:200px;
}
.card-actions {
  display:flex;
  gap:10px;
  justify-content:space-between;
}
.card-actions a, .card-actions button {
  flex: 1;
  padding: 10px;
  font-size:14px;
  margin:0;
}
.card-actions a {
  background-color:#3498db;
  color:white;
  border-radius:8px;
  display:flex;
  align-items:center;
  justify-content:center;
  box-shadow: 0 4px 6px rgba(0,0,0,0.1);
}

.modal-overlay {
  position: fixed;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  background: rgba(0,0,0,0.45);
  display: none;
  align-items: center;
  justify-content: center;
  z-index: 9999;
  padding: 12px;
  box-sizing: border-box;
}

.modal-box {
  background: white;
  width: 100%;
  max-width: 360px;
  border-radius: 14px;
  padding: 22px 18px;
  box-shadow: 0 8px 20px rgba(0,0,0,0.2);
  animation: fadeInScale 0.25s ease;
}

.modal-title {
  font-size: 20px;
  font-weight: bold;
  margin-bottom: 10px;
  color: #2c3e50;
}

.modal-message {
  font-size: 15px;
  color: #555;
  margin-bottom: 18px;
  line-height: 1.5;
}

.modal-actions {
  display: flex;
  gap: 10px;
  justify-content: center;
}

.modal-actions button {
  flex: 1;
  max-width: 140px;
  margin: 0;
}

.toast {
  position: fixed;
  left: 50%;
  bottom: 20px;
  transform: translateX(-50%);
  background: rgba(44,62,80,0.96);
  color: white;
  padding: 14px 18px;
  border-radius: 10px;
  font-size: 14px;
  box-shadow: 0 6px 18px rgba(0,0,0,0.2);
  z-index: 10000;
  display: none;
  max-width: 90%;
}

.progress-box {
  margin-top: 10px;
  font-size: 14px;
  color: #7f8c8d;
}

@keyframes fadeInScale {
  from { opacity: 0; transform: scale(0.92); }
  to { opacity: 1; transform: scale(1); }
}
</style>
</head>
<body>
<h2>🖼️ Sua Galeria</h2>
<a href='/'>🔙 Voltar ao Menu</a><br>
<button id="btnBaixarTodas" onclick='baixarTodas()'>📥 Baixar Todas as Fotos</button>
<button class='btn-danger btn-danger-all' onclick='confirmarLimparSD()'>🗑️ Apagar TODO o SD</button>

<div class="modal-overlay" id="modalOverlay">
  <div class="modal-box">
    <div class="modal-title" id="modalTitle">Mensagem</div>
    <div class="modal-message" id="modalMessage">Texto</div>
    <div class="progress-box" id="modalProgress" style="display:none;"></div>
    <div class="modal-actions" id="modalActions">
      <button onclick="fecharModal()">OK</button>
    </div>
  </div>
</div>

<div class="toast" id="toast"></div>

<script>
function showToast(msg, tempo = 2200) {
  const toast = document.getElementById('toast');
  toast.innerText = msg;
  toast.style.display = 'block';
  clearTimeout(window.__toastTimer);
  window.__toastTimer = setTimeout(() => {
    toast.style.display = 'none';
  }, tempo);
}

function mostrarModal(titulo, mensagem, botoesHTML = '', progressText = '') {
  document.getElementById('modalTitle').innerText = titulo;
  document.getElementById('modalMessage').innerText = mensagem;

  const progress = document.getElementById('modalProgress');
  if (progressText) {
    progress.style.display = 'block';
    progress.innerText = progressText;
  } else {
    progress.style.display = 'none';
    progress.innerText = '';
  }

  const actions = document.getElementById('modalActions');
  actions.innerHTML = botoesHTML || '<button onclick="fecharModal()">OK</button>';

  document.getElementById('modalOverlay').style.display = 'flex';
}

function atualizarModalProgresso(txt) {
  const progress = document.getElementById('modalProgress');
  progress.style.display = 'block';
  progress.innerText = txt;
}

function fecharModal() {
  document.getElementById('modalOverlay').style.display = 'none';
}

function confirmarAcao(titulo, mensagem, onConfirmJS) {
  mostrarModal(
    titulo,
    mensagem,
    '<button style="background:#95a5a6" onclick="fecharModal()">Cancelar</button>' +
    '<button style="background:#e74c3c" onclick="' + onConfirmJS + '">Confirmar</button>'
  );
}

function baixarFoto(nome) {
  mostrarModal(
    'Download iniciado',
    'O navegador irá iniciar o download da foto. Em alguns celulares ela será salva automaticamente na pasta Downloads.',
    '<button onclick="fecharModal()">OK</button>'
  );

  let a = document.createElement('a');
  a.href = '/baixarfoto?nome=' + encodeURIComponent(nome) + '&t=' + Date.now();
  a.download = nome;
  a.setAttribute('download', nome);
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);

  showToast('Download solicitado: ' + nome, 2500);
}

async function baixarTodas() {
  let links = Array.from(document.querySelectorAll('.foto-link')).reverse();

  if (links.length === 0) {
    mostrarModal('Aviso', 'Nenhuma foto disponível para baixar.');
    return;
  }

  const btn = document.getElementById('btnBaixarTodas');
  btn.disabled = true;
  btn.innerText = '⏳ Baixando...';

  mostrarModal(
    'Baixando fotos',
    'Os downloads serão iniciados em sequência.',
    '<button onclick="fecharModal()">Fechar</button>',
    'Preparando...'
  );

  for (let i = 0; i < links.length; i++) {
    let link = links[i];
    let nome = link.dataset.filename;
    let href = link.dataset.href;

    atualizarModalProgresso('Baixando ' + (i + 1) + ' de ' + links.length + ': ' + nome);

    let a = document.createElement('a');
    a.href = href + '&t=' + Date.now();
    a.download = nome;
    a.setAttribute('download', nome);
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);

    await new Promise(resolve => setTimeout(resolve, 1400));
  }

  atualizarModalProgresso('Concluído.');
  btn.disabled = false;
  btn.innerText = '📥 Baixar Todas as Fotos';
  showToast('Todos os downloads foram solicitados.', 2800);
}

function confirmarApagarFoto(nome) {
  confirmarAcao(
    'Apagar foto',
    'Tem certeza que deseja apagar ' + nome + '?',
    "executarApagarFoto('" + nome + "')"
  );
}

function executarApagarFoto(nome) {
  fecharModal();
  showToast('Apagando ' + nome + '...');

  fetch('/apagarfoto?nome=' + encodeURIComponent(nome) + '&t=' + Date.now())
    .then(res => {
      if (res.ok) {
        showToast('Foto apagada com sucesso!');
        setTimeout(() => {
          window.location.href = window.location.pathname + '?t=' + Date.now();
        }, 700);
      } else {
        mostrarModal('Erro', 'Não foi possível apagar a foto.');
      }
    })
    .catch(() => {
      mostrarModal('Erro', 'Falha de comunicação ao apagar a foto.');
    });
}

function confirmarLimparSD() {
  confirmarAcao(
    'Limpar cartão SD',
    'ATENÇÃO: isso apagará todas as fotos permanentemente. Deseja continuar?',
    'executarLimparSD()'
  );
}

function executarLimparSD() {
  fecharModal();
  mostrarModal(
    'Limpando cartão',
    'A limpeza foi iniciada. Aguarde alguns segundos...',
    '<button onclick="fecharModal()">Fechar</button>',
    'Processando...'
  );

  fetch('/limparsd?t=' + Date.now())
    .then(() => {
      atualizarModalProgresso('Concluído.');
      showToast('Cartão SD limpo com sucesso!');
      setTimeout(() => {
        window.location.href = '/galeria?t=' + Date.now();
      }, 1200);
    })
    .catch(() => {
      mostrarModal('Erro', 'Falha ao limpar o cartão SD.');
    });
}
</script>
<div style='display:flex; flex-direction:column; align-items:center;'>
)rawliteral";

const char galeria_footer[] PROGMEM = R"rawliteral(
</div>
<script>
async function carregarFotos() {
  let imgs = Array.from(document.querySelectorAll('img[data-src]'));
  for (let img of imgs) {
    await new Promise(resolve => {
      img.onload = resolve;
      img.onerror = resolve;
      img.src = img.getAttribute('data-src');
    });
  }
}
window.onload = carregarFotos;
</script>
</body></html>
)rawliteral";

// ========================= PINOS ESP32-CAM =========================
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
#define FLASH_GPIO_NUM     4

void tirarFotoSalvarSD();
bool iniciarCamera();
void desligarCamera();
void registrarAtividade();

void registrarAtividade() {
  ultimoAcesso = millis();
}

bool iniciarCamera() {
  if (cameraInicializada) return true;

  digitalWrite(PWDN_GPIO_NUM, LOW);
  delay(10);

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

  // Se sua versão do core não aceitar pin_sscb_*, troque por pin_sccb_*
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 30;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    delay(200);
    err = esp_camera_init(&config);
    if (err != ESP_OK) {
      Serial.printf("Erro ao iniciar camera: 0x%x\n", err);
      digitalWrite(PWDN_GPIO_NUM, HIGH);
      return false;
    }
  }

  for (int i = 0; i < 2; i++) {
    camera_fb_t * fb_dummy = esp_camera_fb_get();
    if (fb_dummy) esp_camera_fb_return(fb_dummy);
    delay(100);
  }

  cameraInicializada = true;
  Serial.println(F("Camera inicializada"));
  return true;
}

void desligarCamera() {
  if (!cameraInicializada) return;

  esp_camera_deinit();
  digitalWrite(PWDN_GPIO_NUM, HIGH);
  cameraInicializada = false;
  Serial.println(F("Camera desligada para economia"));
}

void responderPortalCativo(AsyncWebServerRequest *request) {
  request->redirect("http://" + WiFi.softAPIP().toString() + "/");
}

void paginaInicial(AsyncWebServerRequest *request) {
  registrarAtividade();
  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", index_html);
  request->send(response);
}

void rotaCapture(AsyncWebServerRequest *request) {
  registrarAtividade();

  if (camera_ocupada) {
    request->send(429, "text/plain", F("Ocupada"));
    return;
  }

  camera_ocupada = true;
  if (cameraTaskHandle != NULL) {
    xTaskNotifyGive(cameraTaskHandle);
  }
  request->send(200, "text/plain", F("OK"));
}

void rotaStatus(AsyncWebServerRequest *request) {
  request->send(200, "text/plain", camera_ocupada ? F("ocupada") : F("pronto"));
}

void rotaStatusCamera(AsyncWebServerRequest *request) {
  request->send(200, "text/plain", cameraInicializada ? F("ativa") : F("economia"));
}

void rotaUltimaFoto(AsyncWebServerRequest *request) {
  int ultima = nextPhotoNumber - 1;
  if (ultima < 1) {
    request->send(404, "text/plain", F("Nenhuma foto no cartao"));
    return;
  }

  char fileName[32];
  snprintf(fileName, sizeof(fileName), "/foto_%d.jpg", ultima);

  if (SD_MMC.exists(fileName)) {
    AsyncWebServerResponse *response = request->beginResponse(SD_MMC, fileName, "image/jpeg", false);
    request->send(response);
  } else {
    request->send(404, "text/plain", F("Arquivo nao encontrado"));
  }
}

void paginaGaleria(AsyncWebServerRequest *request) {
  AsyncResponseStream *response = request->beginResponseStream("text/html");
  response->print(FPSTR(galeria_header));

  int page = 0;
  if (request->hasParam("page")) {
    page = request->getParam("page")->value().toInt();
  }

  int fotosPorPagina = 30;
  int fotoInicial = (nextPhotoNumber - 1) - (page * fotosPorPagina);
  int limite = fotoInicial - fotosPorPagina + 1;
  if (limite < 1) limite = 1;

  bool temFoto = false;
  char fileName[32];
  char path[35];

  if (fotoInicial >= 1) {
    for (int i = fotoInicial; i >= limite; i--) {
      snprintf(fileName, sizeof(fileName), "foto_%d.jpg", i);
      snprintf(path, sizeof(path), "/%s", fileName);

      if (SD_MMC.exists(path)) {
        temFoto = true;
        response->print(F("<div class='card'>"));
        response->printf("<img loading='lazy' data-src='/verfoto?nome=%s&t=%lu' alt='Carregando...'>", fileName, millis());
        response->print(F("<div class='card-actions'>"));
        response->printf("<a href='javascript:void(0)' class='foto-link' data-filename='%s' data-href='/baixarfoto?nome=%s' onclick=\"baixarFoto('%s')\">📥 Baixar</a>", fileName, fileName, fileName);
        response->printf("<button class='btn-danger' onclick=\"confirmarApagarFoto('%s')\">🗑️ Apagar</button>", fileName);
        response->print(F("</div></div>"));
      }
    }
  }

  if (!temFoto) {
    response->print(F("<p style='margin-top:20px; font-weight:bold;'>Nenhuma foto encontrada nesta página.</p>"));
  }

  if (page > 0) {
    response->printf("<button style='background-color:#34495e;' onclick=\"window.location.href='/galeria?page=%d'\">⬆️ Mais Recentes</button>", page - 1);
  }
  if (limite > 1) {
    response->printf("<button style='background-color:#34495e;' onclick=\"window.location.href='/galeria?page=%d'\">⬇️ Mais Antigas</button>", page + 1);
  }

  response->print(FPSTR(galeria_footer));
  request->send(response);
}

void rotaVerFoto(AsyncWebServerRequest *request) {
  if (request->hasParam("nome")) {
    String param = request->getParam("nome")->value();

    if (param.indexOf("/") != -1 || param.indexOf("\\") != -1 || param.indexOf("..") != -1) {
      request->send(400, "text/plain", F("Acesso negado"));
      return;
    }

    String fileName = "/" + param;
    if (!SD_MMC.exists(fileName)) {
      request->send(404, "text/plain", F("Arquivo nao encontrado"));
      return;
    }

    AsyncWebServerResponse *response = request->beginResponse(SD_MMC, fileName, "image/jpeg", false);
    request->send(response);
  } else {
    request->send(400, "text/plain", F("Arquivo nao encontrado"));
  }
}

void rotaBaixarFoto(AsyncWebServerRequest *request) {
  if (request->hasParam("nome")) {
    String param = request->getParam("nome")->value();

    if (param.indexOf("/") != -1 || param.indexOf("\\") != -1 || param.indexOf("..") != -1) {
      request->send(400, "text/plain", F("Acesso negado"));
      return;
    }

    String fileName = "/" + param;
    if (!SD_MMC.exists(fileName)) {
      request->send(404, "text/plain", F("Arquivo nao encontrado"));
      return;
    }

    AsyncWebServerResponse *response = request->beginResponse(SD_MMC, fileName, "image/jpeg", true);
    response->addHeader("Content-Disposition", "attachment; filename=\"" + param + "\"");
    request->send(response);
  } else {
    request->send(400, "text/plain", F("Arquivo nao encontrado"));
  }
}

void rotaApagarFoto(AsyncWebServerRequest *request) {
  registrarAtividade();

  if (request->hasParam("nome")) {
    String param = request->getParam("nome")->value();

    if (param.indexOf("/") != -1 || param.indexOf("\\") != -1 || param.indexOf("..") != -1) {
      request->send(400, "text/plain", F("Acesso negado"));
      return;
    }

    String fileName = "/" + param;
    if (SD_MMC.remove(fileName)) {
      request->send(200, "text/plain", F("Foto apagada"));
    } else {
      request->send(500, "text/plain", F("Erro ao apagar no SD"));
    }
  } else {
    request->send(400, "text/plain", F("Arquivo nao encontrado"));
  }
}

void rotaLimparSD(AsyncWebServerRequest *request) {
  registrarAtividade();

  char filePath[32];
  int limiteBusca = nextPhotoNumber + 50;

  for (int i = 1; i <= limiteBusca; i++) {
    snprintf(filePath, sizeof(filePath), "/foto_%d.jpg", i);
    if (SD_MMC.exists(filePath)) {
      SD_MMC.remove(filePath);
    }
  }

  SD_MMC.remove("/index.txt");
  nextPhotoNumber = 1;
  request->send(200, "text/plain", F("SD Limpo"));
}

void rotaFavicon(AsyncWebServerRequest *request) {
  request->send(204);
}

void onNotFound(AsyncWebServerRequest *request) {
  registrarAtividade();
  request->redirect("http://" + WiFi.softAPIP().toString() + "/");
}

void taskCamera(void * parameter) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    tirarFotoSalvarSD();
  }
}

void setup() {
  Serial.begin(115200);

  setCpuFrequencyMhz(80);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  btStop();
  // Se der erro de compilação, comente a linha abaixo
  // esp_bt_controller_disable();

  pinMode(33, OUTPUT);
  digitalWrite(33, HIGH);

  pinMode(PWDN_GPIO_NUM, OUTPUT);
  digitalWrite(PWDN_GPIO_NUM, HIGH);

  pinMode(FLASH_GPIO_NUM, OUTPUT);
  digitalWrite(FLASH_GPIO_NUM, LOW);

  WiFi.softAP(ssid, password, 6, 0, 1);
  WiFi.setSleep(false);

  // Use o menor valor estável no seu ambiente
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  dnsServer.start(53, "*", WiFi.softAPIP());

  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println(F("Falha no SD"));
    return;
  }

  int maxNum = 1;
  File indexFile = SD_MMC.open("/index.txt", FILE_READ);
  if (indexFile) {
    char buf[16];
    size_t len = indexFile.read((uint8_t*)buf, sizeof(buf) - 1);
    buf[len] = '\0';
    maxNum = atoi(buf);
    indexFile.close();
  }
  if (maxNum < 1) maxNum = 1;

  char testPath[32];
  snprintf(testPath, sizeof(testPath), "/foto_%d.jpg", maxNum);
  while (SD_MMC.exists(testPath)) {
    maxNum++;
    snprintf(testPath, sizeof(testPath), "/foto_%d.jpg", maxNum);
  }
  nextPhotoNumber = maxNum;

  ultimoAcesso = millis();
  cameraInicializada = false;

  server.on("/", HTTP_GET, paginaInicial);
  server.on("/capture", HTTP_GET, rotaCapture);
  server.on("/status", HTTP_GET, rotaStatus);
  server.on("/status_camera", HTTP_GET, rotaStatusCamera);
  server.on("/ultima_foto", HTTP_GET, rotaUltimaFoto);
  server.on("/galeria", HTTP_GET, paginaGaleria);
  server.on("/verfoto", HTTP_GET, rotaVerFoto);
  server.on("/baixarfoto", HTTP_GET, rotaBaixarFoto);
  server.on("/apagarfoto", HTTP_GET, rotaApagarFoto);
  server.on("/limparsd", HTTP_GET, rotaLimparSD);
  server.on("/favicon.ico", HTTP_GET, rotaFavicon);

  // Rotas de captive portal
  server.on("/generate_204", HTTP_GET, responderPortalCativo);
  server.on("/gen_204", HTTP_GET, responderPortalCativo);
  server.on("/fwlink", HTTP_GET, responderPortalCativo);
  server.on("/hotspot-detect.html", HTTP_GET, responderPortalCativo);
  server.on("/canonical.html", HTTP_GET, responderPortalCativo);
  server.on("/success.txt", HTTP_GET, responderPortalCativo);
  server.on("/success.html", HTTP_GET, responderPortalCativo);
  server.on("/ncsi.txt", HTTP_GET, responderPortalCativo);
  server.on("/connecttest.txt", HTTP_GET, responderPortalCativo);
  server.on("/redirect", HTTP_GET, responderPortalCativo);
  server.on("/mobile/status.php", HTTP_GET, responderPortalCativo);
  server.on("/library/test/success.html", HTTP_GET, responderPortalCativo);

  server.onNotFound(onNotFound);

  DefaultHeaders::Instance().addHeader("Connection", "keep-alive");
  DefaultHeaders::Instance().addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");

  server.begin();

  xTaskCreatePinnedToCore(taskCamera, "CamTask", 8192, NULL, 3, &cameraTaskHandle, 1);

  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 1);
}

void loop() {
  dnsServer.processNextRequest();

  if (cameraInicializada && !camera_ocupada && (millis() - ultimoAcesso > 10000)) {
    desligarCamera();
  }

  delay(10);
}

void tirarFotoSalvarSD() {
  registrarAtividade();

  if (!iniciarCamera()) {
    camera_ocupada = false;
    return;
  }

  digitalWrite(33, LOW);
  camera_fb_t * fb = esp_camera_fb_get();
  digitalWrite(33, HIGH);

  if (!fb) {
    camera_ocupada = false;
    desligarCamera();
    return;
  }

  char caminho[32];
  snprintf(caminho, sizeof(caminho), "/foto_%d.jpg", nextPhotoNumber);

  File file = SD_MMC.open(caminho, FILE_WRITE);
  if (file) {
    size_t written = file.write(fb->buf, fb->len);
    file.close();

    if (written != fb->len) {
      Serial.println(F("Erro: escrita incompleta no SD"));
    } else {
      if (nextPhotoNumber == 1 || nextPhotoNumber % 10 == 0) {
        File indexFile = SD_MMC.open("/index.txt", FILE_WRITE);
        if (indexFile) {
          indexFile.print(nextPhotoNumber);
          indexFile.close();
        }
      }
      nextPhotoNumber++;
    }
  } else {
    Serial.println(F("Erro ao abrir arquivo no SD"));
  }

  esp_camera_fb_return(fb);
  camera_ocupada = false;

  // Economia máxima: desliga a câmera logo após a captura
  desligarCamera();
}