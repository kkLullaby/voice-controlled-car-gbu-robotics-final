const state = {
  browserRecognition: null,
  browserListening: false,
};

const els = {
  dryRunPill: document.getElementById("dryRunPill"),
  asrPill: document.getElementById("asrPill"),
  btPill: document.getElementById("btPill"),
  browserMicBtn: document.getElementById("browserMicBtn"),
  browserSpeechStatus: document.getElementById("browserSpeechStatus"),
  manualText: document.getElementById("manualText"),
  sendTextBtn: document.getElementById("sendTextBtn"),
  dryRunInput: document.getElementById("dryRunInput"),
  btPortInput: document.getElementById("btPortInput"),
  btBaudInput: document.getElementById("btBaudInput"),
  saveConfigBtn: document.getElementById("saveConfigBtn"),
  startAsrBtn: document.getElementById("startAsrBtn"),
  stopAsrBtn: document.getElementById("stopAsrBtn"),
  lastText: document.getElementById("lastText"),
  lastCommand: document.getElementById("lastCommand"),
  eventLog: document.getElementById("eventLog"),
  keyStatus: document.getElementById("keyStatus"),
};

async function api(path, options = {}) {
  const response = await fetch(path, {
    headers: { "Content-Type": "application/json" },
    ...options,
  });
  const data = await response.json();
  if (!response.ok || data.error) {
    throw new Error(data.error || `HTTP ${response.status}`);
  }
  return data;
}

async function refreshStatus() {
  try {
    const data = await api("/api/status");
    renderStatus(data);
  } catch (error) {
    renderLocalError(error.message);
  }
}

function renderStatus(data) {
  els.dryRunInput.checked = Boolean(data.dry_run);
  els.btPortInput.value = data.port || "/dev/rfcomm0";
  els.btBaudInput.value = String(data.baudrate || 9600);
  els.dryRunPill.textContent = data.dry_run ? "Dry Run" : "Bluetooth";
  els.dryRunPill.className = data.dry_run ? "pill warn" : "pill ok";
  els.asrPill.textContent = data.asr_running ? "ASR Running" : "ASR Idle";
  els.asrPill.className = data.asr_running ? "pill ok" : "pill";
  els.btPill.textContent = data.port || "-";
  els.lastText.textContent = data.last_text || "-";
  els.lastCommand.textContent = data.last_command || "-";
  els.keyStatus.textContent = data.dashscope_key_present ? "Key: present" : "Key: missing";
  renderEvents(data.events || []);
}

function renderEvents(events) {
  els.eventLog.innerHTML = "";
  for (const event of events) {
    const item = document.createElement("li");
    item.innerHTML = `
      <span>${escapeHtml(event.time || "")}</span>
      <span class="event-kind">${escapeHtml(event.kind || "")}</span>
      <span class="event-message">${escapeHtml(event.message || "")}</span>
    `;
    els.eventLog.appendChild(item);
  }
}

function renderLocalError(message) {
  els.eventLog.innerHTML = `
    <li>
      <span>local</span>
      <span class="event-kind">error</span>
      <span class="event-message">${escapeHtml(message)}</span>
    </li>
  `;
}

function escapeHtml(value) {
  return String(value || "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

async function sendText(text) {
  const value = String(text || "").trim();
  if (!value) return;
  await api("/api/command/text", {
    method: "POST",
    body: JSON.stringify({ text: value }),
  });
  await refreshStatus();
}

async function sendCode(code) {
  await api("/api/command/code", {
    method: "POST",
    body: JSON.stringify({ code }),
  });
  await refreshStatus();
}

async function saveConfig() {
  await api("/api/config", {
    method: "POST",
    body: JSON.stringify({
      dry_run: els.dryRunInput.checked,
      port: els.btPortInput.value,
      baudrate: Number(els.btBaudInput.value || 9600),
    }),
  });
  await refreshStatus();
}

function initBrowserSpeech() {
  const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
  if (!SpeechRecognition) {
    els.browserSpeechStatus.textContent = "不可用";
    els.browserMicBtn.disabled = true;
    return;
  }

  const recognition = new SpeechRecognition();
  recognition.lang = "zh-CN";
  recognition.continuous = true;
  recognition.interimResults = true;

  recognition.onstart = () => {
    state.browserListening = true;
    els.browserSpeechStatus.textContent = "监听中";
    els.browserMicBtn.classList.add("listening");
  };

  recognition.onend = () => {
    state.browserListening = false;
    els.browserSpeechStatus.textContent = "待机";
    els.browserMicBtn.classList.remove("listening");
  };

  recognition.onerror = event => {
    els.browserSpeechStatus.textContent = event.error || "错误";
    els.browserMicBtn.classList.remove("listening");
  };

  recognition.onresult = event => {
    let interim = "";
    for (let i = event.resultIndex; i < event.results.length; i += 1) {
      const transcript = event.results[i][0].transcript.trim();
      if (event.results[i].isFinal) {
        els.manualText.value = transcript;
        sendText(transcript).catch(error => renderLocalError(error.message));
      } else {
        interim += transcript;
      }
    }
    if (interim) {
      els.manualText.value = interim;
    }
  };

  state.browserRecognition = recognition;
}

function toggleBrowserSpeech() {
  if (!state.browserRecognition) return;
  if (state.browserListening) {
    state.browserRecognition.stop();
  } else {
    state.browserRecognition.start();
  }
}

els.browserMicBtn.addEventListener("click", toggleBrowserSpeech);
els.sendTextBtn.addEventListener("click", () => {
  sendText(els.manualText.value).catch(error => renderLocalError(error.message));
});
els.manualText.addEventListener("keydown", event => {
  if (event.key === "Enter") {
    sendText(els.manualText.value).catch(error => renderLocalError(error.message));
  }
});
els.saveConfigBtn.addEventListener("click", () => {
  saveConfig().catch(error => renderLocalError(error.message));
});
els.startAsrBtn.addEventListener("click", async () => {
  try {
    await api("/api/asr/start", { method: "POST", body: "{}" });
    await refreshStatus();
  } catch (error) {
    renderLocalError(error.message);
    await refreshStatus();
  }
});
els.stopAsrBtn.addEventListener("click", async () => {
  await api("/api/asr/stop", { method: "POST", body: "{}" });
  await refreshStatus();
});
document.querySelectorAll("[data-code]").forEach(button => {
  button.addEventListener("click", () => {
    sendCode(button.dataset.code).catch(error => renderLocalError(error.message));
  });
});

initBrowserSpeech();
refreshStatus();
setInterval(refreshStatus, 1500);
