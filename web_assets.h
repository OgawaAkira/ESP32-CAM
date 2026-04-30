#ifndef WEB_ASSETS_H
#define WEB_ASSETS_H

#include <Arduino.h>

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32 Camera</title>
<style>
body {
  text-align:center;
  font-family:Arial, sans-serif;
  background:#f4f4f9;
  margin:0;
  padding:25px 15px;
  color:#333;
}
h2 { color:#2c3e50; }
button {
  background:#3498db;
  color:white;
  border:none;
  padding:16px;
  font-size:18px;
  font-weight:bold;
  border-radius:10px;
  margin:10px 0;
  width:100%;
  max-width:320px;
}
button:disabled { background:#95a5a6; }
.btn-galeria { background:#2ecc71; }
.preview {
  margin-top:20px;
  display:none;
  flex-direction:column;
  align-items:center;
}
.preview img {
  width:280px;
  max-width:100%;
  border-radius:10px;
  background:#ddd;
}
.status {
  margin-top:12px;
  font-weight:bold;
  color:#7f8c8d;
}
.toast {
  position:fixed;
  left:50%;
  bottom:20px;
  transform:translateX(-50%);
  background:#2c3e50;
  color:white;
  padding:12px 16px;
  border-radius:8px;
  display:none;
}
</style>
</head>
<body>
<h2>📷 ESP32 Camera</h2>

<button onclick="tirarFoto(this)">📸 TIRAR FOTO</button>
<br>
<button class="btn-galeria" onclick="window.location.href='/galeria'">🖼️ GALERIA</button>

<div class="status" id="status-camera">Status: verificando...</div>

<div class="preview" id="preview-box">
  <p><b>Última foto:</b></p>
  <img id="ultima-foto">
</div>

<div class="toast" id="toast"></div>

<script>
function toast(msg) {
  const t = document.getElementById('toast');
  t.innerText = msg;
  t.style.display = 'block';
  clearTimeout(window.__toastTimer);
  window.__toastTimer = setTimeout(() => t.style.display = 'none', 2200);
}

function atualizarStatusCamera() {
  fetch('/status_camera?t=' + Date.now(), {cache:'no-store'})
    .then(r => r.text())
    .then(txt => {
      const el = document.getElementById('status-camera');
      if (txt === 'ativa') {
        el.innerText = 'Status: câmera ativa';
        el.style.color = '#27ae60';
      } else {
        el.innerText = 'Status: modo economia';
        el.style.color = '#7f8c8d';
      }
    });
}

function atualizarPreview() {
  const box = document.getElementById('preview-box');
  const img = document.getElementById('ultima-foto');

  box.style.display = 'flex';
  img.src = '/preview_principal?t=' + Date.now();
}

function tirarFoto(btn) {
  btn.disabled = true;
  btn.innerText = '⏳ Salvando...';

  fetch('/capture?t=' + Date.now(), {cache:'no-store'})
    .then(r => {
      if (!r.ok) throw new Error();
      setTimeout(() => verificarStatus(btn), 150);
    })
    .catch(() => {
      btn.disabled = false;
      btn.innerText = '📸 TIRAR FOTO';
      toast('Erro ao iniciar captura');
    });
}

function verificarStatus(btn) {
  fetch('/status?t=' + Date.now(), {cache:'no-store'})
    .then(r => r.text())
    .then(txt => {
      if (txt === 'pronto') {
        btn.disabled = false;
        btn.innerText = '📸 TIRAR FOTO';
        atualizarPreview();
        atualizarStatusCamera();
        toast('Foto salva');
      } else if (txt === 'limpeza') {
        btn.disabled = false;
        btn.innerText = '📸 TIRAR FOTO';
        toast('SD em limpeza');
      } else {
        setTimeout(() => verificarStatus(btn), 150);
      }
    })
    .catch(() => setTimeout(() => verificarStatus(btn), 200));
}

window.onload = function() {
  atualizarPreview();
  atualizarStatusCamera();
};
</script>
</body>
</html>
)rawliteral";

const char galeria_header[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Galeria</title>
<style>
body {
  text-align:center;
  font-family:Arial, sans-serif;
  background:#f4f4f9;
  margin:0;
  padding:20px 10px;
}
a {
  color:#3498db;
  font-weight:bold;
  text-decoration:none;
}
button {
  background:#e67e22;
  color:white;
  border:none;
  padding:14px;
  font-size:16px;
  font-weight:bold;
  border-radius:8px;
  margin:8px 0;
  width:100%;
  max-width:320px;
}
.btn-danger { background:#e74c3c; }
.card {
  background:white;
  border-radius:10px;
  padding:12px;
  margin:15px auto;
  max-width:350px;
  box-shadow:0 4px 8px rgba(0,0,0,0.1);
}
.card img {
  width:100%;
  border-radius:8px;
  background:#ddd;
}
.actions {
  display:flex;
  gap:8px;
}
.actions a,
.actions button {
  flex:1;
  font-size:14px;
  padding:10px;
}
.actions a {
  background:#3498db;
  color:white;
  border-radius:8px;
  display:flex;
  align-items:center;
  justify-content:center;
}
</style>
</head>
<body>
<h2>🖼️ Galeria</h2>
<a href="/">🔙 Voltar</a>
<br><br>
<button onclick="baixarTodas()">📥 Baixar fotos desta página</button>
<button class="btn-danger" onclick="limparSD()">🗑️ Apagar TODO o SD</button>

<script>
function baixarFoto(nome) {
  const a = document.createElement('a');
  a.href = '/baixarfoto?nome=' + encodeURIComponent(nome) + '&t=' + Date.now();
  a.download = nome;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
}

async function baixarTodas() {
  const links = Array.from(document.querySelectorAll('.foto-link')).reverse();

  for (let i = 0; i < links.length; i++) {
    links[i].click();
    await new Promise(r => setTimeout(r, 800));
  }
}

function apagarFoto(nome) {
  if (!confirm('Apagar ' + nome + '?')) return;

  fetch('/apagarfoto?nome=' + encodeURIComponent(nome) + '&t=' + Date.now())
    .then(r => {
      if (r.ok) location.reload();
      else alert('Erro ao apagar');
    });
}

function limparSD() {
  if (!confirm('Apagar todas as fotos?')) return;

  fetch('/limparsd?t=' + Date.now())
    .then(r => {
      if (r.ok) setTimeout(() => location.href = '/galeria', 1500);
      else alert('Erro ao limpar SD');
    });
}
</script>
)rawliteral";

const char galeria_footer[] PROGMEM = R"rawliteral(
</body>
</html>
)rawliteral";

#endif