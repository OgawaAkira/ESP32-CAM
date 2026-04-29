#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================
// Configurações de Rede
// ============================================================
namespace NetConfig {
  constexpr const char* SSID         = "ESP32_Camera";
  constexpr const char* PASSWORD     = "12345678";   // ⚠️ Trocar por senha forte!
  constexpr uint8_t     WIFI_CHANNEL = 6;
  constexpr uint8_t     MAX_CLIENTS  = 1;
  constexpr uint16_t    HTTP_PORT    = 80;
  constexpr uint16_t    DNS_PORT     = 53;

  // Potência TX menor = menor consumo, menor alcance.
  // Ajuste se precisar mais estabilidade/distância.
  constexpr wifi_power_t TX_POWER    = WIFI_POWER_2dBm;

  // Mantém o AP sempre ligado, mas com power save habilitado no rádio.
  constexpr bool WIFI_SLEEP_ENABLED  = true;
}

// ============================================================
// Configurações da Câmera
// ============================================================
namespace CamConfig {
  // Reduzido de 20 MHz para 10 MHz para tentar menor consumo.
  // Se houver instabilidade no seu sensor, volte para 20 MHz.
  constexpr uint32_t XCLK_FREQ_HZ      = 10000000;

  // Maior número = maior compressão = menor arquivo.
  // Economia melhor em gravação/transmissão.
  constexpr int      JPEG_QUALITY      = 16;

  constexpr framesize_t FRAME_SIZE     = FRAMESIZE_QVGA;

  constexpr uint8_t  WARMUP_FRAMES     = 2;
  constexpr uint32_t SLEEP_TIMEOUT_MS  = 8000;
  constexpr uint32_t LOOP_DELAY_MS     = 50;
  constexpr uint32_t INIT_RETRY_MS     = 200;
}

// ============================================================
// Configurações da Galeria
// ============================================================
namespace GalleryConfig {
  constexpr int FOTOS_POR_PAGINA      = 30;
  constexpr int LIMITE_BUSCA_EXTRA    = 50;
  constexpr int SAVE_INDEX_INTERVAL   = 10;
  constexpr int MAX_NOME_ARQUIVO      = 32;
}

// ============================================================
// Pinos ESP32-CAM (AI-Thinker)
// ============================================================
namespace Pins {
  constexpr int PWDN  = 32;
  constexpr int RESET = -1;
  constexpr int XCLK  = 0;
  constexpr int SIOD  = 26;
  constexpr int SIOC  = 27;
  constexpr int Y9    = 35;
  constexpr int Y8    = 34;
  constexpr int Y7    = 39;
  constexpr int Y6    = 36;
  constexpr int Y5    = 21;
  constexpr int Y4    = 19;
  constexpr int Y3    = 18;
  constexpr int Y2    = 5;
  constexpr int VSYNC = 25;
  constexpr int HREF  = 23;
  constexpr int PCLK  = 22;
  constexpr int FLASH = 4;
  constexpr int LED   = 33;  // LED vermelho onboard (lógica invertida)
}

// ============================================================
// Configurações de Economia
// ============================================================
namespace PowerConfig {
  // Se true, não acende o LED onboard durante captura.
  constexpr bool DISABLE_STATUS_LED_DURING_CAPTURE = true;

  // Mantém câmera energizada só quando necessário.
  constexpr bool CAMERA_AUTO_SLEEP = true;

  // Se true, mantém o flash sempre desligado.
  constexpr bool FORCE_FLASH_OFF = true;

  // CPU em 80 MHz é um bom equilíbrio para AP + SD + câmera.
  constexpr uint8_t CPU_FREQ_MHZ = 80;
}

#endif // CONFIG_H