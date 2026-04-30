#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include "esp_wifi_types.h"
#include "esp_camera.h"

// ============================================================
// Rede
// ============================================================
namespace NetConfig {
  constexpr const char* SSID         = "ESP32_Camera";
  constexpr const char* PASSWORD     = "12345678";
  constexpr uint8_t     WIFI_CHANNEL = 6;
  constexpr uint8_t     MAX_CLIENTS  = 4;
  constexpr uint16_t    HTTP_PORT    = 80;
  constexpr uint16_t    DNS_PORT     = 53;

  constexpr bool TESTAR_WIFI_PS_EXPERIMENTAL_NO_AP = false;
}

// ============================================================
// Câmera principal
// ============================================================
namespace CamConfig {
  constexpr uint32_t XCLK_FREQ_HZ     = 10000000;
  constexpr int      JPEG_QUALITY     = 20;
  constexpr framesize_t FRAME_SIZE    = FRAMESIZE_QVGA;
  constexpr uint8_t  WARMUP_FRAMES    = 1;
  constexpr uint32_t WARMUP_DELAY_MS  = 20;
  constexpr uint32_t SLEEP_TIMEOUT_MS = 15000;
  constexpr uint32_t LOOP_DELAY_MS    = 50;
  constexpr uint32_t INIT_RETRY_MS    = 200;
}

// ============================================================
// Preview da tela principal
// ============================================================
namespace PreviewConfig {
  constexpr bool ENABLE_PREVIEW_FILE  = true;
  constexpr const char* FILE_PATH     = "/preview.jpg";
  constexpr framesize_t FRAME_SIZE    = FRAMESIZE_QQVGA;
  constexpr int JPEG_QUALITY          = 30;
  constexpr uint8_t  WARMUP_FRAMES    = 1;
  constexpr uint32_t WARMUP_DELAY_MS  = 15;
  constexpr uint32_t SETTLE_DELAY_MS  = 15;
  constexpr uint32_t RESTORE_DELAY_MS = 10;
}

// ============================================================
// Galeria
// ============================================================
namespace GalleryConfig {
  constexpr int FOTOS_POR_PAGINA      = 30;
  constexpr int LIMITE_BUSCA_EXTRA    = 50;
  constexpr int SAVE_INDEX_INTERVAL   = 10;
  constexpr int MAX_NOME_ARQUIVO      = 32;
}

// ============================================================
// Pinos ESP32-CAM AI-Thinker
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
  constexpr int LED   = 33;

  constexpr int CAM_POWER_EN = -1;
}

// ============================================================
// Energia
// ============================================================
namespace PowerConfig {
  constexpr bool DISABLE_STATUS_LED_DURING_CAPTURE = true;
  constexpr bool CAMERA_AUTO_SLEEP = true;
  constexpr bool FORCE_FLASH_OFF   = true;

  constexpr uint8_t CPU_IDLE_FREQ_MHZ   = 80;
  constexpr uint8_t CPU_ACTIVE_FREQ_MHZ = 80;

  constexpr bool     CAM_POWER_EN_ACTIVE_HIGH  = true;
  constexpr uint32_t CAM_POWER_SWITCH_DELAY_MS = 10;
}

#endif