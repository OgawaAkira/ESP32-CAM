# 📷 ESP32-CAM Portátil (Modo AP) com Captura Rápida

Este projeto transforma um módulo **ESP32-CAM** em uma câmera fotográfica portátil, rápida e independente de redes externas. Ele atua como seu próprio roteador (Access Point) e salva as imagens em um cartão SD.

## ✨ Principais Funcionalidades

- **Modo Access Point (AP):** O ESP32 cria sua própria rede Wi-Fi (`ESP32_Camera`). Não depende do roteador de casa. Ideal para uso externo.
- **Captura Ultrarrápida:** Utiliza gerenciamento de índice de arquivos na memória RAM e `fb_count = 2` (aproveitando a PSRAM), evitando varreduras lentas no cartão SD a cada clique.
- **Galeria Web:** Interface HTML embutida para visualizar e baixar as fotos pelo navegador do celular ou PC.
- **Download em Lote:** Script JavaScript integrado para baixar todas as fotos do cartão SD de uma vez com apenas um clique.

## 🚀 Como Usar

1. **Requisitos:** Placa ESP32-CAM, Cartão microSD (formatado em FAT32).
2. **Configuração da IDE:** 
   - Placa: `ESP32 Wrover Module` ou `AI Thinker ESP32-CAM`.
   - PSRAM: `Enabled`.
   - Partition Scheme: `Huge APP (3MB No OTA / 1MB SPIFFS)`.
3. Grave o código e insira o cartão SD.
4. Ligue a placa. Com seu smartphone, busque pela rede Wi-Fi **ESP32_Camera** (Senha: `12345678`).
5. (*Importante: Desligue os Dados Móveis/4G do celular para evitar conflitos de rota*).
6. Abra o navegador e acesse: `http://192.168.4.1`

## 🛠 Bibliotecas Utilizadas
- `WiFi.h` (Nativo)
- `esp_camera.h` (Nativo no pacote ESP32)
- `ESPAsyncWebServer`
- `SD_MMC.h`