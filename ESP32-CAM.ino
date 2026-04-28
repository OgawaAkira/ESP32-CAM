#include "WiFi.h"
#include "esp_camera.h"
#include "Arduino.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include "SD_MMC.h"

const char* ssid = "ESP32_Camera";
const char* password = "12345678"; 

AsyncWebServer server(80);
TaskHandle_t cameraTaskHandle = NULL;

int nextPhotoNumber = 1;

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width, initial-scale=1.0'>
<link rel='prefetch' href='/galeria'> 
<style>
body { text-align:center; font-family:'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color:#f4f4f9; margin:0; padding:30px 15px; color:#333; }
h2 { color:#2c3e50; margin-bottom:30px; }
button { background-color:#3498db; color:white; border:none; padding:18px 20px; font-size:18px; font-weight:bold; border-radius:10px; margin:10px 0; width:100%; max-width:320px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); cursor:pointer; transition:0.2s; }
button:active { transform: scale(0.95); }
button:disabled { background-color:#95a5a6; cursor:not-allowed; }
.btn-galeria { background-color:#2ecc71; }
</style></head><body>
<h2>📷 Controle ESP32-CAM</h2>
<button onclick="tirarFoto(this)">📸 TIRAR FOTO</button><br>
<button class='btn-galeria' onclick="window.location.href='/galeria'">🖼️ VER GALERIA</button>
<script>
function tirarFoto(btn) {
  btn.innerText = '⏳ Fotografando...';
  btn.disabled = true;
  fetch('/capture');
  setTimeout(function(){ btn.innerText = '📸 TIRAR FOTO'; btn.disabled = false; }, 3500);
}
</script>
</body></html>
)rawliteral";

const char galeria_header[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width, initial-scale=1.0'>
<style>
body { font-family:'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; text-align:center; background-color:#f4f4f9; margin:0; padding:20px 10px; color:#333; }
a { color:#3498db; text-decoration:none; font-weight:bold; font-size:16px; }
button { background-color:#e67e22; color:white; border:none; padding:15px; font-size:16px; font-weight:bold; border-radius:8px; margin:10px 0; width:100%; max-width:320px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); cursor:pointer; transition:0.2s; }
button:active { transform: scale(0.95); }
.btn-danger { background-color:#e74c3c; }
.btn-danger-all { background-color:#c0392b; margin-top:20px; }
.card { background:white; border-radius:10px; padding:15px; margin-bottom:20px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); width:100%; max-width:350px; box-sizing:border-box; }
img { border-radius:8px; width:100%; height:auto; margin-top:10px; margin-bottom:10px; background-color:#ecf0f1; min-height:200px; display:flex; align-items:center; justify-content:center; color:#7f8c8d; font-size:14px; }
.card-actions { display:flex; gap:10px; justify-content:space-between; }
.card-actions a, .card-actions button { flex: 1; padding: 10px; font-size:14px; margin:0; }
.card-actions a { background-color:#3498db; color:white; border-radius:8px; display:flex; align-items:center; justify-content:center; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }
</style></head><body>
<h2>🖼️ Sua Galeria</h2>
<a href='/'>🔙 Voltar ao Menu</a><br>
<button onclick='baixarTodas()'>📥 Baixar Todas as Fotos</button>
<button class='btn-danger btn-danger-all' onclick='limparSD()'>🗑️ Apagar TODO o SD</button>
<script>
async function baixarTodas() {
  let links = Array.from(document.querySelectorAll('.foto-link')).reverse();
  if(links.length === 0) { alert('Nenhuma foto para baixar!'); return; }
  
  alert('Iniciando downloads em sequência... Por favor, não feche a página.');
  
  for (let link of links) {
    let a = document.createElement('a'); 
    a.href = link.href; 
    a.download = link.dataset.filename;
    document.body.appendChild(a); 
    a.click(); 
    document.body.removeChild(a);
    
    // Espera 1.5 segundos entre cada download para não sobrecarregar o ESP32
    await new Promise(resolve => setTimeout(resolve, 1500)); 
  }
  alert('Comando de downloads finalizado!');
}
function apagarFoto(nome) {
  if(confirm('Tem certeza que deseja apagar a ' + nome + '?')) {
    fetch('/apagarfoto?nome=' + nome).then(res => { if(res.ok) { location.reload(); } else { alert('Erro ao apagar!'); } });
  }
}
function limparSD() {
  if(confirm('ATENÇÃO: Isso apagará TODAS AS FOTOS permanentemente! Deseja continuar?')) {
    alert('Comando enviado! A limpeza pode levar alguns segundos. A página vai recarregar sozinha.');
    fetch('/limparsd').then(() => { location.reload(); }).catch(() => { location.reload(); });
  }
}
</script>
<div style='display:flex; flex-direction:column; align-items:center;'>
)rawliteral";

const char galeria_footer[] PROGMEM = R"rawliteral(
</div>
<script>
async function carregarFotos() {
  let imgs = Array.from(document.querySelectorAll('img[data-src]')).reverse();
  for(let img of imgs) {
    await new Promise(resolve => {
      img.onload = resolve; img.onerror = resolve; img.src = img.getAttribute('data-src');
    });
  }
}
window.onload = carregarFotos;
</script></body></html>
)rawliteral";

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

void paginaInicial(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", index_html);
  response->addHeader("Cache-Control", "public, max-age=31536000"); 
  request->send(response);
}

void rotaCapture(AsyncWebServerRequest *request) {
  if(cameraTaskHandle != NULL) {
    xTaskNotifyGive(cameraTaskHandle);
  }
  request->send(200, "text/plain", F("OK"));
}

void paginaGaleria(AsyncWebServerRequest *request) {
  AsyncResponseStream *response = request->beginResponseStream("text/html");
  response->print(FPSTR(galeria_header));

  // Lógica de Paginação
  int page = 0;
  if(request->hasParam("page")) {
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
    for(int i = fotoInicial; i >= limite; i--) {
      snprintf(fileName, sizeof(fileName), "foto_%d.jpg", i);
      snprintf(path, sizeof(path), "/%s", fileName);
      
      if(SD_MMC.exists(path)) {
        temFoto = true;
        response->print(F("<div class='card'>"));
        response->printf("<img loading='lazy' data-src='/verfoto?nome=%s' alt='Carregando...'>", fileName);
        response->print(F("<div class='card-actions'>"));
        response->printf("<a class='foto-link' data-filename='%s' href='/baixarfoto?nome=%s'>📥 Baixar</a>", fileName, fileName);
        response->printf("<button class='btn-danger' onclick=\"apagarFoto('%s')\">🗑️ Apagar</button>", fileName);
        response->print(F("</div></div>"));
      }
    }
  }
  
  if(!temFoto) response->print(F("<p style='margin-top:20px; font-weight:bold;'>Nenhuma foto encontrada nesta página.</p>"));

  // Botões de navegação
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
    
    // TRAVA DE SEGURANÇA: Impede acesso a pastas superiores
    if (param.indexOf("/") != -1 || param.indexOf("\\") != -1 || param.indexOf("..") != -1) {
      request->send(400, "text/plain", F("Acesso negado"));
      return;
    }
    
    String fileName = "/" + param;
    AsyncWebServerResponse *response = request->beginResponse(SD_MMC, fileName, "image/jpeg", false);
    response->addHeader("Cache-Control", "public, max-age=31536000"); 
    request->send(response);
  } else {
    request->send(400, "text/plain", F("Arquivo nao encontrado"));
  }
}

void rotaBaixarFoto(AsyncWebServerRequest *request) {
  if (request->hasParam("nome")) {
    String param = request->getParam("nome")->value(); 
    
    // TRAVA DE SEGURANÇA
    if (param.indexOf("/") != -1 || param.indexOf("\\") != -1 || param.indexOf("..") != -1) {
      request->send(400, "text/plain", F("Acesso negado"));
      return;
    }
    
    String fileName = "/" + param;
    // 'true' para forçar download e cabeçalho de 'attachment' restaurados
    AsyncWebServerResponse *response = request->beginResponse(SD_MMC, fileName, "image/jpeg", true);
    response->addHeader("Content-Disposition", "attachment; filename=\"" + fileName + "\"");
    request->send(response);
  } else {
    request->send(400, "text/plain", F("Arquivo nao encontrado"));
  }
}

void rotaApagarFoto(AsyncWebServerRequest *request) {
  if (request->hasParam("nome")) {
    String param = request->getParam("nome")->value(); 
    
    // TRAVA DE SEGURANÇA
    if (param.indexOf("/") != -1 || param.indexOf("\\") != -1 || param.indexOf("..") != -1) {
      request->send(400, "text/plain", F("Acesso negado"));
      return;
    }
    
    String fileName = "/" + param;
    // Restaurada a função que realmente deleta a foto do SD
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
  char filePath[32];
  
  // Apaga as fotos diretamente pelo índice - 100x mais rápido que varrer o diretório
  // Varre até nextPhotoNumber + 50 como margem de segurança
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

void taskCamera(void * parameter) {
  for(;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY); 
    tirarFotoSalvarSD();
  }
}

void setup() {
  Serial.begin(115200);
  
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  btStop(); 
  
  pinMode(33, OUTPUT);
  digitalWrite(33, HIGH); 

  WiFi.softAP(ssid, password, 6, 0, 2);
  WiFi.setSleep(false); 
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  
  // Inicializa no modo 1-bit (true) e usa a velocidade máxima padrão (20MHz)
  if (!SD_MMC.begin("/sdcard", true)) { 
    Serial.println(F("Falha no SD"));
    return; // É bom parar por aqui se o SD falhar
  }

  int maxNum = 1;
  File indexFile = SD_MMC.open("/index.txt", FILE_READ);
  if(indexFile) {
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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000; 
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_UXGA; 
  config.jpeg_quality = 10; 
  config.grab_mode = CAMERA_GRAB_LATEST; 
  
  if(psramFound()) config.fb_count = 2; 
  else config.fb_count = 1;

  esp_camera_init(&config);

  for (int i = 0; i < 3; i++) {
    camera_fb_t * fb_dummy = esp_camera_fb_get();
    if (fb_dummy) esp_camera_fb_return(fb_dummy);
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }

  server.on("/", HTTP_GET, paginaInicial);
  server.on("/capture", HTTP_GET, rotaCapture);
  server.on("/galeria", HTTP_GET, paginaGaleria);
  server.on("/verfoto", HTTP_GET, rotaVerFoto);
  server.on("/baixarfoto", HTTP_GET, rotaBaixarFoto);
  server.on("/apagarfoto", HTTP_GET, rotaApagarFoto);
  server.on("/limparsd", HTTP_GET, rotaLimparSD);
  server.on("/favicon.ico", HTTP_GET, rotaFavicon); 

  DefaultHeaders::Instance().addHeader("Connection", "keep-alive");
  server.begin();
  
  xTaskCreatePinnedToCore(taskCamera, "CamTask", 8192, NULL, 3, &cameraTaskHandle, 1);
  
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 1);
  vTaskDelete(NULL); 
} 

void loop() {}

void tirarFotoSalvarSD() {
  digitalWrite(33, LOW); 
  camera_fb_t * fb = esp_camera_fb_get();
  digitalWrite(33, HIGH); 
  
  if (!fb) return;

  char caminho[32]; 
  snprintf(caminho, sizeof(caminho), "/foto_%d.jpg", nextPhotoNumber);
  
  File file = SD_MMC.open(caminho, FILE_WRITE);
  if (file) {
    file.write(fb->buf, fb->len); 
    file.close(); 
    
    if (nextPhotoNumber == 1 || nextPhotoNumber % 10 == 0) {
      File indexFile = SD_MMC.open("/index.txt", FILE_WRITE);
      if(indexFile) {
        indexFile.print(nextPhotoNumber);
        indexFile.close();
      }
    }
    nextPhotoNumber++;
  }
  esp_camera_fb_return(fb);
}