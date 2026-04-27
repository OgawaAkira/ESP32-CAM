#include "WiFi.h"
#include "esp_camera.h"
#include "Arduino.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include "SD_MMC.h"

const char* ssid = "Easylux";
const char* password = "Easylux123!";

AsyncWebServer server(80);
boolean takeNewPhoto = false;

// Variável global para armazenar o número da próxima foto em RAM (MUITO MAIS RÁPIDO)
int nextPhotoNumber = 1;

// Pinos da Câmera
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

// Função para descobrir qual a última foto salva ao ligar o ESP32
void checkNextPhotoNumber() {
  File root = SD_MMC.open("/");
  File file = root.openNextFile();
  int highestNumber = 0;
  
  while(file){
    String fileName = file.name();
    if(fileName.indexOf("foto_") >= 0 && fileName.indexOf(".jpg") > 0){
      // Extrai o número do nome do arquivo (ex: "foto_12.jpg" -> 12)
      int startIndex = fileName.indexOf('_') + 1;
      int endIndex = fileName.indexOf(".jpg");
      int num = fileName.substring(startIndex, endIndex).toInt();
      if(num > highestNumber) {
        highestNumber = num;
      }
    }
    file = root.openNextFile();
  }
  nextPhotoNumber = highestNumber + 1;
  Serial.print("Próxima foto será a de número: ");
  Serial.println(nextPhotoNumber);
}

// ==========================================
// PÁGINA INICIAL (Menu)
// ==========================================
void paginaInicial(AsyncWebServerRequest *request) {
  String html = "<html><body style='text-align:center; font-family:Arial; margin-top:50px;'>";
  html += "<h2>Controle ESP32-CAM</h2>";
  html += "<button onclick=\"tirarFoto(this)\" style='padding:15px; font-size:18px; margin:10px; cursor:pointer;'>TIRAR FOTO</button><br>";
  html += "<button onclick=\"window.location.href='/galeria'\" style='padding:15px; font-size:18px; margin:10px; cursor:pointer;'>VER GALERIA DE FOTOS</button>";
  
  html += "<script>";
  html += "function tirarFoto(btn) {";
  html += "  btn.innerText = 'Fotografando...';";
  html += "  btn.disabled = true;";
  html += "  fetch('/capture');";
  html += "  setTimeout(function(){ btn.innerText = 'TIRAR FOTO'; btn.disabled = false; }, 1500);";
  html += "}";
  html += "</script>";
  
  html += "</body></html>";
  request->send(200, "text/html", html);
}

// ==========================================
// PÁGINA DA GALERIA (Carregamento em Fila)
// ==========================================
void paginaGaleria(AsyncWebServerRequest *request) {
  String html = "<html><head><meta charset='UTF-8'></head><body style='font-family:Arial; text-align:center;'><h2>Sua Galeria</h2>";
  html += "<a href='/'>[ Voltar ao Menu ]</a><br><br>";
  
  // Botão e Script para baixar todas as fotos
  html += "<button onclick='baixarTodas()' style='padding:10px; font-size:16px; background-color:#4CAF50; color:white; border:none; cursor:pointer; margin-bottom:20px;'>Baixar Todas as Fotos</button><br>";
  html += "<script>";
  html += "function baixarTodas() {";
  html += "  let links = document.querySelectorAll('.foto-link');";
  html += "  let delay = 0;";
  html += "  if(links.length === 0) { alert('Nenhuma foto para baixar!'); return; }";
  html += "  alert('Iniciando download... Aguarde.');";
  html += "  links.forEach(link => {";
  html += "    setTimeout(() => {";
  html += "      let a = document.createElement('a');";
  html += "      a.href = link.href;";
  html += "      a.download = link.innerText;";
  html += "      document.body.appendChild(a);";
  html += "      a.click();";
  html += "      document.body.removeChild(a);";
  html += "    }, delay);";
  html += "    delay += 800;";
  html += "  });";
  html += "}";
  html += "</script>";

  File root = SD_MMC.open("/");
  File file = root.openNextFile();
  bool temFoto = false;
  
  while(file){
    String fileName = file.name();
    if(fileName.indexOf(".jpg") > 0){
      temFoto = true;
      html += "<div style='margin-bottom:20px; border:1px solid #ccc; padding:10px; display:inline-block;'>";
      html += "<a class='foto-link' href='/baixarfoto?nome=" + fileName + "'>" + fileName + "</a><br><br>";
      
      // O TRUQUE ESTÁ AQUI: Trocamos 'src' por 'data-src'. O navegador não baixa na hora.
      html += "<img data-src='/verfoto?nome=" + fileName + "' width='300' alt='Carregando foto...'>";
      
      html += "</div><br>";
    }
    file = root.openNextFile();
  }
  
  if(!temFoto) html += "<p>Nenhuma foto no cartao ainda.</p>";
  
  // SCRIPT MÁGICO: Baixa uma imagem de cada vez
  html += "<script>";
  html += "async function carregarFotos() {";
  html += "  let imgs = document.querySelectorAll('img[data-src]');";
  html += "  for(let img of imgs) {";
  html += "    await new Promise(resolve => {";
  html += "      img.onload = resolve;";   // Quando terminar de baixar, libera a próxima
  html += "      img.onerror = resolve;";  // Se der erro, pula pra próxima
  html += "      img.src = img.getAttribute('data-src');"; // Inicia o download desta
  html += "    });";
  html += "  }";
  html += "}";
  html += "window.onload = carregarFotos;"; // Roda assim que a página abre
  html += "</script>";

  html += "</body></html>";
  request->send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.println("Criando rede Wi-Fi própria...");
  WiFi.softAP("ESP32_Camera", "12345678"); // Nome da rede e senha (min 8 caracteres)
  Serial.print("Conecte no Wi-Fi 'ESP32_Camera' e acesse o IP: ");
  Serial.println(WiFi.softAPIP()); // O IP padrão será sempre 192.168.4.1

  Serial.println("Inicializando SD Card...");
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("Falha no SD Card!");
  } else {
    // Lê o cartão SD apenas UMA VEZ na inicialização para achar o número da foto
    checkNextPhotoNumber();
  }

  Serial.print("Pronto! Digite no navegador: http://");
  Serial.println(WiFi.softAPIP());

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
  config.frame_size = FRAMESIZE_VGA; 
  config.jpeg_quality = 12; 
  
  // AUMENTO DE VELOCIDADE AQUI: Se tiver PSRAM, usa 2 buffers
  if(psramFound()){
    config.fb_count = 2; // Permite capturar e processar ao mesmo tempo
  } else {
    config.fb_count = 1;
  }

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Erro na camera!");
    ESP.restart();
  }

  // ROTA 1: Página Inicial
  server.on("/", HTTP_GET, paginaInicial);

  // ROTA 2: Comando para tirar a foto
  server.on("/capture", HTTP_GET,[](AsyncWebServerRequest *request){
    takeNewPhoto = true;
    request->send(200, "text/plain", "OK");
  });

  // ROTA 3: Mostra a lista de fotos (Galeria + Função de Baixar Todas)
  server.on("/galeria", HTTP_GET, paginaGaleria);

  // ROTA 4: Ver a foto no navegador
  server.on("/verfoto", HTTP_GET,[](AsyncWebServerRequest *request) {
    if (request->hasParam("nome")) {
      String fileName = "/" + request->getParam("nome")->value();
      request->send(SD_MMC, fileName, "image/jpeg", false); // false = exibe no navegador
    } else {
      request->send(400, "text/plain", "Arquivo nao encontrado");
    }
  });

  // ROTA 5: Baixar a foto como anexo (Download forçado)
  server.on("/baixarfoto", HTTP_GET,[](AsyncWebServerRequest *request) {
    if (request->hasParam("nome")) {
      String fileName = "/" + request->getParam("nome")->value();
      // O parâmetro 'true' no final cria o cabeçalho HTTP que obriga o navegador a fazer o download
      AsyncWebServerResponse *response = request->beginResponse(SD_MMC, fileName, "image/jpeg", true);
      request->send(response);
    } else {
      request->send(400, "text/plain", "Arquivo nao encontrado");
    }
  });

  // Inicia o servidor web
  server.begin();

  Serial.printf("Tamanho da PSRAM: %d bytes\n", ESP.getPsramSize());

} // <--- FIM DO VOID SETUP()


void loop() {
  if (takeNewPhoto) {
    tirarFotoSalvarSD();
    takeNewPhoto = false;
  }
  delay(1);
}

// ==========================================
// FUNÇÃO DE CAPTURA RÁPIDA DE FOTOS
// ==========================================
void tirarFotoSalvarSD() {
  Serial.println("Tirando foto...");
  camera_fb_t * fb = esp_camera_fb_get();
  
  if (!fb) {
    Serial.println("Falha na captura da camera");
    return;
  }

  // AQUI ESTÁ O SEGREDO DA VELOCIDADE:
  // Em vez de varrer o cartão SD com um while(SD_MMC.exists), 
  // usamos a variável nextPhotoNumber que está na memória RAM!
  String caminho = "/foto_" + String(nextPhotoNumber) + ".jpg";
  nextPhotoNumber++; // Já incrementa na RAM para a próxima vez

  Serial.println("Salvando imagem como: " + caminho);

  // Cria o arquivo com o novo nome gerado
  File file = SD_MMC.open(caminho, FILE_WRITE);

  if (!file) {
    Serial.println("Falha ao abrir o arquivo para escrita no Cartao SD");
  } else {
    file.write(fb->buf, fb->len); 
    Serial.print("Salvo com sucesso! Tamanho: ");
    Serial.print(file.size());
    Serial.println(" bytes");
    file.close();
  }
  
  esp_camera_fb_return(fb);
}