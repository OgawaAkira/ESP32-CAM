# 📷 ESP32-CAM Portátil (Modo AP) com Captura Rápida

![Platform](https://img.shields.io/badge/Platform-ESP32-blue)
![Board](https://img.shields.io/badge/Board-ESP32--CAM-green)
![Mode](https://img.shields.io/badge/Wi--Fi-Access%20Point-orange)
![Storage](https://img.shields.io/badge/Storage-microSD-red)
![Interface](https://img.shields.io/badge/UI-Web-lightgrey)
![Status](https://img.shields.io/badge/Status-Ativo-success)

Transforme uma **ESP32-CAM** em uma câmera fotográfica portátil, rápida e independente de redes externas.  
O dispositivo cria sua própria rede Wi‑Fi em **modo Access Point (AP)**, permite tirar fotos pelo navegador do celular e salva tudo diretamente no **cartão microSD**.

---

## ✨ Principais Funcionalidades

- **Modo Access Point (AP):**  
  O ESP32 cria sua própria rede Wi‑Fi (`ESP32_Camera`) sem depender de roteador externo. Ideal para uso portátil, em campo ou offline.

- **Captura Rápida:**  
  O sistema utiliza controle do índice de arquivos em memória e persistência periódica no SD, evitando varreduras desnecessárias no cartão a cada foto.

- **Galeria Web Integrada:**  
  Interface HTML embutida no firmware para:
  - tirar fotos
  - visualizar a última captura
  - acessar a galeria
  - baixar imagens individualmente

- **Download em Lote:**  
  A galeria possui um botão para iniciar o download sequencial de todas as fotos do cartão SD.

- **Captive Portal / Redirecionamento Automático:**  
  Facilita o acesso pelo celular ao redirecionar o usuário para a interface principal.

- **Salvamento no microSD:**  
  As fotos são gravadas como arquivos `.jpg` no cartão SD.

- **Modo Economia Parcial:**  
  A câmera é inicializada somente quando necessário e pode ser desligada automaticamente após inatividade, reduzindo consumo sem desativar o AP.

- **Gerenciamento de Arquivos:**  
  Permite apagar fotos individuais e limpar completamente o cartão SD pela interface web.

---

## 🧰 Hardware Necessário

- 1x **ESP32-CAM** (preferencialmente AI-Thinker)
- 1x **adaptador ESP32-CAM-MB**
- 1x **cartão microSD** formatado em **FAT32**
- 1x fonte 5V estável ou cabo USB de boa qualidade
- Smartphone, tablet ou computador com Wi‑Fi

---

## 🖼️ Interface do Projeto

O sistema possui duas telas principais:

### Página inicial
- Botão para tirar foto
- Botão para abrir a galeria
- Status da câmera
- Pré-visualização da última foto

### Galeria
- Lista das fotos salvas
- Download individual
- Download de todas as fotos
- Exclusão individual
- Limpeza completa do cartão SD

---

## 🚀 Como Usar

1. Conecte a **ESP32-CAM** com o cartão microSD inserido.
2. Grave o firmware na placa.
3. Ligue o dispositivo.
4. No celular ou computador, conecte-se à rede Wi‑Fi:
   - **SSID:** `ESP32_Camera`
   - **Senha:** `12345678`
5. Abra o navegador e acesse:

   ```text
   http://192.168.4.1
6. Use a interface para:
   - tirar fotos
   - visualizar a última imagem
   - baixar arquivos
   - gerenciar a galeria

---

## ⚙️ Configuração da IDE Arduino

### Placa
- `AI Thinker ESP32-CAM`
- `ESP32 Wrover Module`

### Configurações recomendadas

| Opção | Valor recomendado |
|---|---|
| Board | `AI Thinker ESP32-CAM` |
| PSRAM | `Enabled` |
| Partition Scheme | `Huge APP (3MB No OTA / 1MB SPIFFS)` |
| Flash Mode | `QIO` |
| Flash Frequency | `40MHz` |
| Upload Speed | `115200` |

---

## 📁 Estrutura do Projeto

```text
ESP32-CAM/
├── ESP32-CAM.ino
├── config.h
├── web_assets.h
├── README.md
└── .gitignore
```

### Descrição dos arquivos

- **ESP32-CAM.ino**  
  Código principal do projeto: inicialização da câmera, servidor web, rotas, SD e lógica de captura.

- **config.h**  
  Arquivo com as configurações centralizadas:
  - Wi‑Fi
  - câmera
  - galeria
  - pinos da placa

- **web_assets.h**  
  Contém o HTML, CSS e JavaScript embutidos da interface web.

---

## 🛠 Bibliotecas Utilizadas

### Nativas do framework ESP32 / Arduino
- `WiFi.h`
- `esp_camera.h`
- `SD_MMC.h`
- `FS.h`
- `DNSServer.h`

### Externas
- `ESPAsyncWebServer`

---

## 📸 Como as Fotos São Armazenadas

As imagens são gravadas no cartão SD com nomes no formato:

```text
/foto_1.jpg
/foto_2.jpg
/foto_3.jpg
...
```

O sistema mantém controle do próximo índice para evitar procurar arquivos desde o começo a cada nova captura, melhorando a velocidade de operação.

---

## 🔋 Sobre Consumo de Energia

Este projeto mantém o **Access Point sempre ligado** para não perder comunicação com o celular.

- o consumo é maior do que em projetos com **deep sleep**
- a câmera pode ser desligada quando ociosa
- o Wi‑Fi continua ativo para acesso contínuo à interface

### Otimizações implementadas
- desligamento da câmera após inatividade
- redução de consumo parcial mantendo o AP ativo
- inicialização sob demanda da câmera
- ajustes de clock, qualidade JPEG e potência Wi‑Fi via `config.h`

---

## ✅ Vantagens do Projeto

- Funciona sem internet
- Fácil de usar pelo celular
- Interface amigável no navegador
- Galeria embutida
- Download individual e em lote
- Captura rápida
- Projeto portátil e prático
- Não depende de serviços externos

---

## ⚠️ Observações Importantes

- Use uma **fonte estável**
- O cartão microSD deve estar em **FAT32**
- Alguns celulares podem indicar que a rede não tem internet
- Se a câmera não inicializar, revise:
  - modelo da placa
  - PSRAM
  - pinos da câmera
  - qualidade da alimentação
- Se estiver usando a base **ESP32-CAM-MB**, o consumo em bateria pode ser maior

---

## 🧪 Solução de Problemas

### A rede Wi‑Fi aparece, mas a página não abre
- Verifique se o celular está conectado à rede `ESP32_Camera`
- Desative temporariamente os dados móveis
- Acesse manualmente `http://192.168.4.1`

### A câmera não captura imagem
- Confirme se a placa correta foi selecionada na IDE
- Verifique se a **PSRAM** está habilitada
- Confira se o modelo é compatível com o pinout usado

### Erro ao salvar no cartão SD
- Formate o cartão em **FAT32**
- Teste outro cartão
- Verifique se ele está bem encaixado

### Reinicializações aleatórias
- Use cabo USB melhor
- Use fonte mais estável
- Evite portas USB fracas

### A galeria está lenta com muitas fotos
- Use cartão microSD de melhor qualidade
- Faça limpeza periódica se necessário

---

## 🧩 Configurações Personalizáveis

No arquivo `config.h`, é possível ajustar:

### Rede
- nome da rede Wi‑Fi
- senha
- canal
- quantidade máxima de clientes
- potência de transmissão

### Câmera
- qualidade JPEG
- frequência do clock da câmera
- timeout para desligamento
- resolução

### Galeria
- número de fotos por página
- intervalo de salvamento do índice
- limites de busca

### Pinos
- definição completa dos pinos da ESP32-CAM AI-Thinker

---

## 🛣️ Possíveis Melhorias Futuras

- Ajuste de resolução pela interface web
- Controle do flash
- Proteção por senha na galeria
- Ajuste dinâmico de qualidade JPEG
- Upload opcional para nuvem ou servidor local
- Modo híbrido AP + STA
- Indicadores de espaço livre no cartão SD
- Página de diagnóstico do sistema
- Exibição de informações de bateria

---

## 📷 Exemplo de Fluxo de Uso

1. Ligar a ESP32-CAM  
2. Conectar o celular à rede `ESP32_Camera`  
3. Abrir `http://192.168.4.1`  
4. Tocar em **TIRAR FOTO**  
5. Visualizar a última imagem  
6. Abrir a galeria  
7. Baixar ou apagar fotos conforme necessário  

---

## 🤝 Contribuições

Contribuições são bem-vindas.

---

## 📄 Licença

Este projeto está licenciado sob a licença MIT.

---

## ⭐ Apoie o Projeto

- deixe uma estrela no repositório
- compartilhe com outras pessoas
- contribua com melhorias