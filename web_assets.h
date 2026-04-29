#ifndef WEB_ASSETS_H
#define WEB_ASSETS_H

#include <Arduino.h>

// ============================================================
// HTML PRINCIPAL
// ============================================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html>
<head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width, initial-scale=1.0'>
<title>Minha Câmera ESP32</title>
<style>
body { text-align:center; font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;
  background-color:#f4f4f9; margin:0; padding:30px 15px; color:#333; }
h2 { color:#2c3e50; margin-bottom:30px; }
button { background-color:#3498db; color:white; border:none; padding:18px 20px;
  font-size:18px; font-weight:bold; border-radius:10px; margin:10px 0;
  width:100%; max-width:320px; box-shadow:0 4px 6px rgba(0,0,0,0.1);
  cursor:pointer; transition:0.2s; }
button:active { transform:scale(0.95); }
button:disabled { background-color:#95a5a6; cursor:not-allowed; }
.btn-galeria { background-color:#2ecc71; }
.preview-container { margin-top:25px; display:none; flex-direction:column; align-items:center; }
.preview-container img { max-width:100%; width:320px; border-radius:10px;
  box-shadow:0 4px 8px rgba(0,0,0,0.2); background-color:#ecf0f1;
  min-height:200px; transition:opacity 0.3s; }
.preview-title { font-weight:bold; color:#7f8c8d; margin-bottom:10px; font-size:14px; }
.status-box { margin-top:14px; font-size:14px; color:#7f8c8d; font-weight:bold; }
.toast { position:fixed; left:50%; bottom:20px; transform:translateX(-50%);
  background:rgba(44,62,80,0.96); color:white; padding:14px 18px;
  border-radius:10px; font-size:14px; box-shadow:0 6px 18px rgba(0,0,0,0.2);
  z-index:9999; display:none; max-width:90%; }
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
  window.__toastTimer = setTimeout(() => { toast.style.display = 'none'; }, tempo);
}
function atualizarStatusCamera() {
  fetch('/status_camera?t=' + Date.now())
    .then(r => r.text())
    .then(txt => {
      const el = document.getElementById('status-camera');
      if (txt === 'ativa') { el.innerText = 'Status: câmera ativa'; el.style.color = '#27ae60'; }
      else { el.innerText = 'Status: modo economia'; el.style.color = '#7f8c8d'; }
    }).catch(() => {});
}
function tirarFoto(btn) {
  btn.innerText = '⏳ Salvando SD...';
  btn.disabled = true;
  let img = document.getElementById('ultima-foto');
  let box = document.getElementById('preview-box');
  if (img && box.style.display !== 'none') img.style.opacity = '0.3';
  fetch('/capture?t=' + Date.now())
    .then(r => {
      if (!r.ok) throw new Error();
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
    .then(r => r.text())
    .then(estado => {
      if (estado === 'pronto') {
        btn.innerText = '📸 TIRAR FOTO';
        btn.disabled = false;
        atualizarPreview();
        atualizarStatusCamera();
        showToast('Foto salva com sucesso!');
      } else { setTimeout(() => verificarStatus(btn), 120); }
    })
    .catch(() => setTimeout(() => verificarStatus(btn), 150));
}
function atualizarPreview() {
  let img = document.getElementById('ultima-foto');
  let box = document.getElementById('preview-box');
  box.style.display = 'flex';
  img.style.opacity = '0.4';
  img.onload = () => { img.style.opacity = '1'; };
  img.onerror = () => { box.style.display = 'none'; };
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

// ============================================================
// HTML GALERIA — Cabeçalho
// ============================================================
const char galeria_header[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html>
<head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width, initial-scale=1.0'>
<title>Galeria de Fotos</title>
<style>
body { font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif; text-align:center;
  background-color:#f4f4f9; margin:0; padding:20px 10px; color:#333; }
a { color:#3498db; text-decoration:none; font-weight:bold; font-size:16px; }
button { background-color:#e67e22; color:white; border:none; padding:15px;
  font-size:16px; font-weight:bold; border-radius:8px; margin:10px 0;
  width:100%; max-width:320px; box-shadow:0 4px 6px rgba(0,0,0,0.1);
  cursor:pointer; transition:0.2s; }
button:active { transform:scale(0.95); }
button:disabled { background-color:#95a5a6; cursor:not-allowed; }
.btn-danger { background-color:#e74c3c; }
.btn-danger-all { background-color:#c0392b; margin-top:20px; }
.card { background:white; border-radius:10px; padding:15px; margin-bottom:20px;
  box-shadow:0 4px 8px rgba(0,0,0,0.1); width:100%; max-width:350px; box-sizing:border-box; }
img { border-radius:8px; width:100%; height:auto; margin-top:10px; margin-bottom:10px;
  background-color:#ecf0f1; min-height:200px; }
.card-actions { display:flex; gap:10px; justify-content:space-between; }
.card-actions a, .card-actions button { flex:1; padding:10px; font-size:14px; margin:0; }
.card-actions a { background-color:#3498db; color:white; border-radius:8px;
  display:flex; align-items:center; justify-content:center;
  box-shadow:0 4px 6px rgba(0,0,0,0.1); }
.modal-overlay { position:fixed; top:0; left:0; width:100%; height:100%;
  background:rgba(0,0,0,0.45); display:none; align-items:center;
  justify-content:center; z-index:9999; padding:12px; box-sizing:border-box; }
.modal-box { background:white; width:100%; max-width:360px; border-radius:14px;
  padding:22px 18px; box-shadow:0 8px 20px rgba(0,0,0,0.2);
  animation:fadeInScale 0.25s ease; }
.modal-title { font-size:20px; font-weight:bold; margin-bottom:10px; color:#2c3e50; }
.modal-message { font-size:15px; color:#555; margin-bottom:18px; line-height:1.5; }
.modal-actions { display:flex; gap:10px; justify-content:center; }
.modal-actions button { flex:1; max-width:140px; margin:0; }
.toast { position:fixed; left:50%; bottom:20px; transform:translateX(-50%);
  background:rgba(44,62,80,0.96); color:white; padding:14px 18px;
  border-radius:10px; font-size:14px; box-shadow:0 6px 18px rgba(0,0,0,0.2);
  z-index:10000; display:none; max-width:90%; }
.progress-box { margin-top:10px; font-size:14px; color:#7f8c8d; }
@keyframes fadeInScale {
  from { opacity:0; transform:scale(0.92); }
  to { opacity:1; transform:scale(1); }
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
  window.__toastTimer = setTimeout(() => { toast.style.display = 'none'; }, tempo);
}
function mostrarModal(titulo, mensagem, botoesHTML = '', progressText = '') {
  document.getElementById('modalTitle').innerText = titulo;
  document.getElementById('modalMessage').innerText = mensagem;
  const progress = document.getElementById('modalProgress');
  if (progressText) { progress.style.display = 'block'; progress.innerText = progressText; }
  else { progress.style.display = 'none'; progress.innerText = ''; }
  const actions = document.getElementById('modalActions');
  actions.innerHTML = botoesHTML || '<button onclick="fecharModal()">OK</button>';
  document.getElementById('modalOverlay').style.display = 'flex';
}
function atualizarModalProgresso(txt) {
  const p = document.getElementById('modalProgress');
  p.style.display = 'block';
  p.innerText = txt;
}
function fecharModal() { document.getElementById('modalOverlay').style.display = 'none'; }
function confirmarAcao(titulo, mensagem, onConfirmJS) {
  mostrarModal(titulo, mensagem,
    '<button style="background:#95a5a6" onclick="fecharModal()">Cancelar</button>' +
    '<button style="background:#e74c3c" onclick="' + onConfirmJS + '">Confirmar</button>');
}
function baixarFoto(nome) {
  mostrarModal('Download iniciado',
    'O navegador irá iniciar o download da foto. Em alguns celulares ela será salva automaticamente na pasta Downloads.',
    '<button onclick="fecharModal()">OK</button>');
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
  if (links.length === 0) { mostrarModal('Aviso', 'Nenhuma foto disponível para baixar.'); return; }
  const btn = document.getElementById('btnBaixarTodas');
  btn.disabled = true;
  btn.innerText = '⏳ Baixando...';
  mostrarModal('Baixando fotos', 'Os downloads serão iniciados em sequência.',
    '<button onclick="fecharModal()">Fechar</button>', 'Preparando...');
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
  confirmarAcao('Apagar foto', 'Tem certeza que deseja apagar ' + nome + '?',
    "executarApagarFoto('" + nome + "')");
}
function executarApagarFoto(nome) {
  fecharModal();
  showToast('Apagando ' + nome + '...');
  fetch('/apagarfoto?nome=' + encodeURIComponent(nome) + '&t=' + Date.now())
    .then(res => {
      if (res.ok) {
        showToast('Foto apagada com sucesso!');
        setTimeout(() => { window.location.href = window.location.pathname + '?t=' + Date.now(); }, 700);
      } else { mostrarModal('Erro', 'Não foi possível apagar a foto.'); }
    })
    .catch(() => mostrarModal('Erro', 'Falha de comunicação ao apagar a foto.'));
}
function confirmarLimparSD() {
  confirmarAcao('Limpar cartão SD',
    'ATENÇÃO: isso apagará todas as fotos permanentemente. Deseja continuar?',
    'executarLimparSD()');
}
function executarLimparSD() {
  fecharModal();
  mostrarModal('Limpando cartão', 'A limpeza foi iniciada. Aguarde alguns segundos...',
    '<button onclick="fecharModal()">Fechar</button>', 'Processando...');
  fetch('/limparsd?t=' + Date.now())
    .then(() => {
      atualizarModalProgresso('Concluído.');
      showToast('Cartão SD limpo com sucesso!');
      setTimeout(() => { window.location.href = '/galeria?t=' + Date.now(); }, 1200);
    })
    .catch(() => mostrarModal('Erro', 'Falha ao limpar o cartão SD.'));
}
</script>
<div style='display:flex; flex-direction:column; align-items:center;'>
)rawliteral";

// ============================================================
// HTML GALERIA — Rodapé
// ============================================================
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

#endif // WEB_ASSETS_H