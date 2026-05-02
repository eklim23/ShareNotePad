const tabsContainer = document.querySelector(".tabs");
const editor = document.querySelector("#editor");
const menuLayer = document.querySelector("#menuLayer");
const slashMenu = document.querySelector("#slashMenu");
const previewButton = document.querySelector("#previewButton");
const previewPane = document.querySelector("#previewPane");
const previewContent = document.querySelector("#previewContent");
const shareButton = document.querySelector("#shareButton");
const shareDialog = document.querySelector("#shareDialog");
const createRoomButton = document.querySelector("#createRoomButton");
const joinRoomButton = document.querySelector("#joinRoomButton");
const joinCodeInput = document.querySelector("#joinCodeInput");
const inviteCodeBox = document.querySelector("#inviteCodeBox");
const inviteCodeText = document.querySelector("#inviteCodeText");
const newTabButton = document.querySelector("#newTabButton");
const documentStatus = document.querySelector("#documentStatus");
const saveStatus = document.querySelector("#saveStatus");
const countStatus = document.querySelector("#countStatus");
const connectionStatus = document.querySelector("#connectionStatus");

const maxTabs = 5;
let saveTimer = 0;
let activeTabIndex = 0;
let nextTabNumber = 1;
let tabs = [{ title: "current.txt", content: "", dirty: false }];
let slashContext = null;
let slashMatches = [];
let slashSelectedIndex = 0;
let loadingEditor = false;
let relayApplying = false;
let relaySocket = null;
let relayConnectionId = 0;
let relayRole = "offline";
let relayRoom = "";
let relayPeerId = "";
let relayRevision = 0;
let relayLastText = "";
let relayTimer = 0;
let relayPingTimer = 0;
let relaySyncRevision = 0;
let relaySyncChunks = [];
let relaySyncReceived = [];
let relaySnapshotRevision = 0;
let relaySnapshotChunks = [];
let relaySnapshotReceived = [];
let editorComposing = false;
let pendingRelayDocument = null;

const continuedBlockTypes = new Set(["bullet", "ordered", "todo", "quote", "code"]);
const relayUrl = "ws://127.0.0.1:8080/ws";
const relayChunkSize = 10000;
const relayFastSyncDelayMs = 80;

const slashCommands = [
  {
    name: "text",
    aliases: ["plain"],
    label: "텍스트",
    description: "일반 텍스트 줄",
    icon: "T",
    blockType: "p",
  },
  {
    name: "h1",
    aliases: ["#"],
    label: "제목 1",
    description: "큰 제목",
    icon: "H1",
    blockType: "h1",
  },
  {
    name: "h2",
    aliases: ["##"],
    label: "제목 2",
    description: "중간 제목",
    icon: "H2",
    blockType: "h2",
  },
  {
    name: "h3",
    aliases: ["###"],
    label: "제목 3",
    description: "작은 제목",
    icon: "H3",
    blockType: "h3",
  },
  {
    name: "bullet",
    aliases: ["list", "ul"],
    label: "글머리 목록",
    description: "점 목록",
    icon: "•",
    blockType: "bullet",
  },
  {
    name: "num",
    aliases: ["number", "ol"],
    label: "번호 목록",
    description: "번호 목록",
    icon: "1.",
    blockType: "ordered",
  },
  {
    name: "todo",
    aliases: ["check", "checkbox"],
    label: "체크박스",
    description: "할 일 항목",
    icon: "☐",
    blockType: "todo",
  },
  {
    name: "quote",
    aliases: ["blockquote"],
    label: "인용",
    description: "인용문",
    icon: "❝",
    blockType: "quote",
  },
  {
    name: "code",
    aliases: ["pre"],
    label: "코드 블록",
    description: "고정폭 코드",
    icon: "{}",
    blockType: "code",
  },
  {
    name: "div",
    aliases: ["divider", "hr"],
    label: "구분선",
    description: "가로 구분선",
    icon: "─",
    blockType: "divider",
  },
];

function postNative(type, payload = {}) {
  const message = JSON.stringify({ type, payload });
  if (window.chrome?.webview) {
    window.chrome.webview.postMessage(message);
  }
}

function activeTab() {
  return tabs[activeTabIndex];
}

function setDirty(value) {
  activeTab().dirty = value;
  renderTabs();
}

function setRelayConnectionStatus(text) {
  connectionStatus.textContent = text;
}

function relayIsOpen() {
  return relaySocket !== null && relaySocket.readyState === WebSocket.OPEN;
}

function relaySend(message) {
  if (!relayIsOpen()) {
    setRelayConnectionStatus("릴레이 연결 끊김");
    return false;
  }

  relaySocket.send(JSON.stringify(message));
  return true;
}

function relaySendPayload(payload) {
  return relaySend({ type: "RELAY", payload });
}

function resetRelayChunkState() {
  relaySyncRevision = 0;
  relaySyncChunks = [];
  relaySyncReceived = [];
  relaySnapshotRevision = 0;
  relaySnapshotChunks = [];
  relaySnapshotReceived = [];
}

function clearRelayTimers() {
  clearTimeout(relayTimer);
  relayTimer = 0;
  clearInterval(relayPingTimer);
  relayPingTimer = 0;
}

function closeRelay(status = "오프라인") {
  relayConnectionId += 1;
  clearRelayTimers();
  if (relaySocket) {
    relaySocket.onopen = null;
    relaySocket.onmessage = null;
    relaySocket.onerror = null;
    relaySocket.onclose = null;
    relaySocket.close();
  }
  relaySocket = null;
  relayRole = "offline";
  relayRoom = "";
  relayPeerId = "";
  relayRevision = 0;
  resetRelayChunkState();
  setRelayConnectionStatus(status);
}

function startRelayPing() {
  clearInterval(relayPingTimer);
  relayPingTimer = setInterval(() => {
    if (relayIsOpen()) {
      relaySend({ type: "PING" });
    }
  }, 30000);
}

function openRelay(mode, code = "") {
  closeRelay("릴레이 연결 중");
  const connectionId = ++relayConnectionId;
  relaySocket = new WebSocket(relayUrl);

  relaySocket.addEventListener("open", () => {
    if (connectionId !== relayConnectionId) {
      return;
    }

    if (mode === "create") {
      relaySend({ type: "CREATE" });
    } else {
      relaySend({ type: "JOIN", room: code });
    }
    startRelayPing();
  });

  relaySocket.addEventListener("message", (event) => {
    if (connectionId !== relayConnectionId) {
      return;
    }

    try {
      handleRelayMessage(JSON.parse(event.data));
    } catch {
      setRelayConnectionStatus("릴레이 메시지 오류");
    }
  });

  relaySocket.addEventListener("error", () => {
    if (connectionId === relayConnectionId) {
      setRelayConnectionStatus("릴레이 연결 오류");
    }
  });

  relaySocket.addEventListener("close", () => {
    if (connectionId !== relayConnectionId) {
      return;
    }
    clearRelayTimers();
    relaySocket = null;
    relayRole = "offline";
    setRelayConnectionStatus("릴레이 연결 끊김");
  });
}

function createRelayRoom() {
  saveNow();
  syncActiveTabFromEditor();
  relayLastText = serializeEditor();
  relayRevision = 0;
  resetRelayChunkState();
  openRelay("create");
}

function joinRelayRoom(code) {
  syncActiveTabFromEditor();
  relayLastText = serializeEditor();
  relayRevision = 0;
  resetRelayChunkState();
  openRelay("join", code.toUpperCase());
}

function handleRelayMessage(message) {
  if (message.type === "ROOM_CREATED") {
    relayRole = "host";
    relayRoom = message.room ?? "";
    relayPeerId = message.peer ?? "A";
    relayRevision = 0;
    relayLastText = serializeEditor();
    inviteCodeText.textContent = relayRoom;
    inviteCodeBox.hidden = !relayRoom;
    joinCodeInput.value = relayRoom;
    setRelayConnectionStatus(`릴레이 방 생성됨: ${relayRoom}`);
    return;
  }

  if (message.type === "JOIN_OK") {
    relayRole = "guest";
    relayRoom = message.room ?? "";
    relayPeerId = message.peer ?? "B";
    relayRevision = 0;
    relayLastText = serializeEditor();
    shareDialog.close();
    setRelayConnectionStatus("릴레이 연결됨");
    relaySendPayload({ type: "SYNC_REQUEST", base_rev: relayRevision });
    return;
  }

  if (message.type === "JOIN_FAIL") {
    setRelayConnectionStatus(`방 들어가기 실패: ${message.reason ?? "오류"}`);
    return;
  }

  if (message.type === "PEER_JOINED") {
    setRelayConnectionStatus("상대 접속됨");
    if (relayRole === "host") {
      sendRelayFullSync();
    }
    return;
  }

  if (message.type === "RELAY") {
    handleRelayPayload(message.payload ?? {}, message.from ?? "");
    return;
  }

  if (message.type === "ERROR") {
    setRelayConnectionStatus(`릴레이 오류: ${message.reason ?? "unknown"}`);
  }
}

function handleRelayPayload(payload, fromPeer) {
  if (payload.type === "SYNC_REQUEST" && relayRole === "host") {
    sendRelayFullSync();
    return;
  }

  if (payload.type === "SYNC_RESPONSE") {
    receiveRelayFullSync(payload);
    return;
  }

  if (payload.type === "DOCUMENT_SNAPSHOT" && relayRole === "host") {
    receiveRelaySnapshot(payload);
    return;
  }

  if (payload.type === "APPLY") {
    applyRelayApply(payload);
    return;
  }

  if ((payload.type === "INSERT" || payload.type === "DELETE") && relayRole === "host") {
    applyRelayPeerOperation(payload, fromPeer);
  }
}

function chunkText(text) {
  const chunks = [];
  for (let index = 0; index < text.length; index += relayChunkSize) {
    chunks.push(text.slice(index, index + relayChunkSize));
  }
  return chunks.length > 0 ? chunks : [""];
}

function sendRelayFullSync() {
  if (!relayIsOpen()) {
    return;
  }

  const currentText = serializeEditor();
  if (relayRole === "host" && currentText !== relayLastText) {
    relayRevision += 1;
    relayLastText = currentText;
  }

  const chunks = chunkText(relayLastText);
  chunks.forEach((text, index) => {
    relaySendPayload({
      type: "SYNC_RESPONSE",
      rev: relayRevision,
      chunk_index: index,
      chunk_count: chunks.length,
      text,
    });
  });
}

function receiveRelayFullSync(payload) {
  const revision = Number(payload.rev ?? 0);
  const chunkIndex = Number(payload.chunk_index ?? 0);
  const chunkCount = Number(payload.chunk_count ?? 1);
  if (chunkCount < 1 || chunkIndex < 0 || chunkIndex >= chunkCount) {
    setRelayConnectionStatus("전체 동기화 오류");
    return;
  }

  if (relaySyncRevision !== revision || relaySyncChunks.length !== chunkCount) {
    relaySyncRevision = revision;
    relaySyncChunks = Array(chunkCount).fill("");
    relaySyncReceived = Array(chunkCount).fill(false);
  }

  relaySyncChunks[chunkIndex] = String(payload.text ?? "");
  relaySyncReceived[chunkIndex] = true;

  if (!relaySyncReceived.every(Boolean)) {
    setRelayConnectionStatus("전체 동기화 중");
    return;
  }

  const text = relaySyncChunks.join("");
  relaySyncChunks = [];
  relaySyncReceived = [];
  relayLastText = text;
  relayRevision = revision;
  applyRelayDocument(text, "릴레이 연결됨");
}

function sendRelaySnapshot(text) {
  if (!relayIsOpen()) {
    return;
  }

  const chunks = chunkText(text);
  chunks.forEach((chunk, index) => {
    relaySendPayload({
      type: "DOCUMENT_SNAPSHOT",
      base_rev: relayRevision,
      chunk_index: index,
      chunk_count: chunks.length,
      text: chunk,
    });
  });
}

function receiveRelaySnapshot(payload) {
  const baseRevision = Number(payload.base_rev ?? 0);
  const chunkIndex = Number(payload.chunk_index ?? 0);
  const chunkCount = Number(payload.chunk_count ?? 1);
  if (chunkCount < 1 || chunkIndex < 0 || chunkIndex >= chunkCount) {
    sendRelayFullSync();
    return;
  }

  if (relaySnapshotRevision !== baseRevision || relaySnapshotChunks.length !== chunkCount) {
    relaySnapshotRevision = baseRevision;
    relaySnapshotChunks = Array(chunkCount).fill("");
    relaySnapshotReceived = Array(chunkCount).fill(false);
  }

  relaySnapshotChunks[chunkIndex] = String(payload.text ?? "");
  relaySnapshotReceived[chunkIndex] = true;

  if (!relaySnapshotReceived.every(Boolean)) {
    return;
  }

  const text = relaySnapshotChunks.join("");
  relaySnapshotChunks = [];
  relaySnapshotReceived = [];
  relayRevision += 1;
  relayLastText = text;
  applyRelayDocument(text, "릴레이 동기화됨");
  sendRelayFullSync();
}

function computeRelayDiff(before, after) {
  if (before === after) {
    return [];
  }

  let prefix = 0;
  const minLength = Math.min(before.length, after.length);
  while (prefix < minLength && before[prefix] === after[prefix]) {
    prefix += 1;
  }

  let suffix = 0;
  while (
    suffix < before.length - prefix &&
    suffix < after.length - prefix &&
    before[before.length - 1 - suffix] === after[after.length - 1 - suffix]
  ) {
    suffix += 1;
  }

  const deleteLength = before.length - prefix - suffix;
  const insertText = after.slice(prefix, after.length - suffix);
  const operations = [];
  if (deleteLength > 0) {
    operations.push({ type: "DELETE", pos: prefix, len: deleteLength });
  }
  if (insertText.length > 0) {
    operations.push({ type: "INSERT", pos: prefix, text: insertText });
  }
  return operations;
}

function applyRelayOperation(text, operation) {
  const position = Number(operation.pos ?? 0);
  if (position < 0 || position > text.length) {
    return null;
  }

  if (operation.type === "INSERT") {
    const inserted = String(operation.text ?? "");
    return `${text.slice(0, position)}${inserted}${text.slice(position)}`;
  }

  if (operation.type === "DELETE") {
    const length = Number(operation.len ?? 0);
    if (length < 0 || position + length > text.length) {
      return null;
    }
    return `${text.slice(0, position)}${text.slice(position + length)}`;
  }

  return null;
}

function applyRelayDocument(text, status, options = {}) {
  if (editorComposing) {
    pendingRelayDocument = { text, status, options };
    setRelayConnectionStatus("상대 입력 반영 대기");
    return;
  }

  const hadEditorFocus = selectionInsideEditor();
  const caretOffset = hadEditorFocus
    ? transformedCaretOffset(documentCaretOffset(), options.operation)
    : null;
  relayApplying = true;
  activeTab().content = text;
  loadActiveTabIntoEditor({ focus: hadEditorFocus, caretOffset });
  relayApplying = false;
  setDirty(false);
  postNative("editorChanged", { text });
  saveStatus.textContent = "자동저장 요청";
  setRelayConnectionStatus(status);
}

function sendRelayApply(operation, clientId) {
  relayRevision += 1;
  const payload = {
    type: "APPLY",
    rev: relayRevision,
    op: operation.type,
    client: clientId,
    pos: operation.pos,
  };

  if (operation.type === "INSERT") {
    payload.text = operation.text ?? "";
  } else {
    payload.len = operation.len ?? 0;
  }

  relaySendPayload(payload);
}

function applyRelayPeerOperation(operation, fromPeer) {
  let nextText = relayLastText;
  const applied = applyRelayOperation(nextText, operation);
  if (applied === null) {
    sendRelayFullSync();
    return;
  }

  nextText = applied;
  relayLastText = nextText;
  applyRelayDocument(nextText, "릴레이 동기화됨", { operation });
  sendRelayApply(operation, operation.client ?? fromPeer ?? "B");
}

function applyRelayApply(payload) {
  const revision = Number(payload.rev ?? 0);
  if (revision <= relayRevision) {
    return;
  }

  if (payload.client === relayPeerId) {
    relayRevision = revision;
    relayLastText = serializeEditor();
    setRelayConnectionStatus("릴레이 동기화됨");
    return;
  }

  const operation = {
    type: payload.op,
    pos: payload.pos,
    text: payload.text,
    len: payload.len,
  };
  const currentText = serializeEditor();
  const nextText = applyRelayOperation(currentText, operation);
  if (nextText === null) {
    relaySendPayload({ type: "SYNC_REQUEST", base_rev: relayRevision });
    return;
  }

  relayRevision = revision;
  relayLastText = nextText;
  applyRelayDocument(nextText, "릴레이 동기화됨", { operation });
}

function flushRelayLocalChange() {
  if (relayApplying || relayRole === "offline" || !relayIsOpen()) {
    return;
  }

  syncActiveTabFromEditor();
  const currentText = serializeEditor();
  const operations = computeRelayDiff(relayLastText, currentText);
  if (operations.length === 0) {
    return;
  }

  const hasLargeInsert = operations.some((operation) => (operation.text ?? "").length > relayChunkSize);
  if (hasLargeInsert && relayRole === "guest") {
    sendRelaySnapshot(currentText);
    relayLastText = currentText;
    setRelayConnectionStatus("릴레이 동기화 중");
    return;
  }

  if (relayRole === "host") {
    for (const operation of operations) {
      sendRelayApply(operation, relayPeerId || "A");
    }
    relayLastText = currentText;
    setRelayConnectionStatus("릴레이 동기화됨");
    return;
  }

  for (const operation of operations) {
    relaySendPayload({
      type: operation.type,
      base_rev: relayRevision,
      client: relayPeerId || "B",
      pos: operation.pos,
      text: operation.text,
      len: operation.len,
    });
  }
  relayLastText = currentText;
  setRelayConnectionStatus("릴레이 동기화 중");
}

function scheduleRelaySync() {
  if (relayApplying || editorComposing || relayRole === "offline") {
    return;
  }

  clearTimeout(relayTimer);
  relayTimer = setTimeout(flushRelayLocalChange, relayFastSyncDelayMs);
}

function blockText(block) {
  if (!block || block.dataset.type === "divider") {
    return "";
  }
  return block.textContent.replace(/\u200b/g, "");
}

function setBlockText(block, text) {
  block.textContent = "";
  if (text.length === 0) {
    block.appendChild(document.createElement("br"));
    return;
  }
  block.appendChild(document.createTextNode(text));
}

function blockClass(type) {
  return `editor-block editor-${type}`;
}

function createBlock(type = "p", text = "", options = {}) {
  const block = document.createElement("div");
  block.className = blockClass(type);
  block.dataset.type = type;

  if (options.checked) {
    block.dataset.checked = "true";
  }

  if (type === "divider") {
    block.contentEditable = "false";
    block.innerHTML = "<hr>";
    return block;
  }

  block.contentEditable = "true";
  setBlockText(block, text);
  return block;
}

function setBlockType(block, type, options = {}) {
  const text = blockText(block);
  block.className = blockClass(type);
  block.dataset.type = type;
  delete block.dataset.checked;

  if (options.checked) {
    block.dataset.checked = "true";
  }

  if (type === "divider") {
    block.contentEditable = "false";
    block.innerHTML = "<hr>";
    return;
  }

  block.contentEditable = "true";
  setBlockText(block, text);
}

function ensureEditableBlock() {
  let block = getCurrentBlock();
  if (block) {
    return block;
  }

  block = editor.querySelector(".editor-block");
  if (!block) {
    block = createBlock("p");
    editor.appendChild(block);
  }
  setCaretInBlock(block, blockText(block).length);
  return block;
}

function parseMarkdown(text) {
  const normalized = text.replace(/\r\n/g, "\n");
  const lines = normalized.length === 0 ? [""] : normalized.split("\n");
  const blocks = [];
  let inCode = false;

  for (const line of lines) {
    if (line.startsWith("```")) {
      inCode = !inCode;
      continue;
    }

    if (inCode) {
      blocks.push(createBlock("code", line));
      continue;
    }

    const trimmed = line.trim();
    const todoMatch = line.match(/^- \[([ xX])\]\s?(.*)$/);
    const orderedMatch = line.match(/^(\d+|[a-zA-Z]|[ivxlcdmIVXLCDM]+)\.\s+(.*)$/);

    if (/^-{3,}$/.test(trimmed)) {
      blocks.push(createBlock("divider"));
    } else if (todoMatch) {
      blocks.push(createBlock("todo", todoMatch[2], { checked: todoMatch[1].toLowerCase() === "x" }));
    } else if (line.startsWith("### ")) {
      blocks.push(createBlock("h3", line.slice(4)));
    } else if (line.startsWith("## ")) {
      blocks.push(createBlock("h2", line.slice(3)));
    } else if (line.startsWith("# ")) {
      blocks.push(createBlock("h1", line.slice(2)));
    } else if (line.startsWith("- ") || line.startsWith("* ")) {
      blocks.push(createBlock("bullet", line.slice(2)));
    } else if (orderedMatch) {
      blocks.push(createBlock("ordered", orderedMatch[2]));
    } else if (line.startsWith("> ")) {
      blocks.push(createBlock("quote", line.slice(2)));
    } else {
      blocks.push(createBlock("p", line));
    }
  }

  return blocks.length > 0 ? blocks : [createBlock("p")];
}

function setEditorFromMarkdown(text) {
  loadingEditor = true;
  editor.innerHTML = "";
  for (const block of parseMarkdown(text)) {
    editor.appendChild(block);
  }
  loadingEditor = false;
}

function collectEditorBlocks() {
  const blocks = [...editor.children].filter((child) => child.classList.contains("editor-block"));
  if (blocks.length > 0) {
    return blocks;
  }

  const text = editor.innerText.replace(/\r\n/g, "\n");
  editor.innerHTML = "";
  for (const block of parseMarkdown(text)) {
    editor.appendChild(block);
  }
  return [...editor.children].filter((child) => child.classList.contains("editor-block"));
}

function serializeBlock(block) {
  const type = block.dataset.type ?? "p";
  const text = blockText(block);

  if (type === "h1") {
    return `# ${text}`;
  }
  if (type === "h2") {
    return `## ${text}`;
  }
  if (type === "h3") {
    return `### ${text}`;
  }
  if (type === "bullet") {
    return `- ${text}`;
  }
  if (type === "ordered") {
    return `1. ${text}`;
  }
  if (type === "todo") {
    return `- [${block.dataset.checked === "true" ? "x" : " "}] ${text}`;
  }
  if (type === "quote") {
    return `> ${text}`;
  }
  if (type === "divider") {
    return "---";
  }
  return text;
}

function serializedBlockPrefixLength(block) {
  const type = block.dataset.type ?? "p";
  if (type === "h1") {
    return 2;
  }
  if (type === "h2") {
    return 3;
  }
  if (type === "h3") {
    return 4;
  }
  if (type === "bullet" || type === "quote") {
    return 2;
  }
  if (type === "ordered") {
    return 3;
  }
  if (type === "todo") {
    return 6;
  }
  return 0;
}

function serializedBlockSpans() {
  const spans = [];
  let offset = 0;
  let firstLine = true;
  let inCode = false;

  function appendLine(line) {
    if (!firstLine) {
      offset += 1;
    }
    firstLine = false;
    const start = offset;
    offset += line.length;
    return start;
  }

  for (const block of collectEditorBlocks()) {
    const type = block.dataset.type ?? "p";
    if (type === "code") {
      if (!inCode) {
        appendLine("```");
        inCode = true;
      }
      const text = blockText(block);
      const start = appendLine(text);
      spans.push({
        block,
        contentStart: start,
        contentEnd: start + text.length,
      });
      continue;
    }

    if (inCode) {
      appendLine("```");
      inCode = false;
    }

    const line = serializeBlock(block);
    const start = appendLine(line);
    const prefix = serializedBlockPrefixLength(block);
    const textLength = block.dataset.type === "divider" ? 0 : blockText(block).length;
    spans.push({
      block,
      contentStart: start + prefix,
      contentEnd: start + prefix + textLength,
    });
  }

  return spans;
}

function serializeEditor() {
  const lines = [];
  let inCode = false;

  for (const block of collectEditorBlocks()) {
    const type = block.dataset.type ?? "p";
    if (type === "code") {
      if (!inCode) {
        lines.push("```");
        inCode = true;
      }
      lines.push(blockText(block));
      continue;
    }

    if (inCode) {
      lines.push("```");
      inCode = false;
    }
    lines.push(serializeBlock(block));
  }

  if (inCode) {
    lines.push("```");
  }

  if (lines.length === 1 && lines[0] === "") {
    return "";
  }
  return lines.join("\n");
}

function updateCounts() {
  countStatus.textContent = `${serializeEditor().length}자`;
}

function syncActiveTabFromEditor() {
  activeTab().content = serializeEditor();
}

function documentCaretOffset() {
  if (!selectionInsideEditor()) {
    return null;
  }

  const block = getCurrentBlock();
  if (!block || block.dataset.type === "divider") {
    return null;
  }

  const span = serializedBlockSpans().find((candidate) => candidate.block === block);
  if (!span) {
    return null;
  }

  return span.contentStart + caretOffsetInBlock(block);
}

function transformedCaretOffset(offset, operation) {
  if (offset === null || operation == null) {
    return offset;
  }

  const position = Number(operation.pos ?? 0);
  if (operation.type === "INSERT") {
    return position <= offset ? offset + String(operation.text ?? "").length : offset;
  }

  if (operation.type === "DELETE") {
    const length = Number(operation.len ?? 0);
    if (position >= offset) {
      return offset;
    }
    return Math.max(position, offset - Math.min(length, offset - position));
  }

  return offset;
}

function restoreDocumentCaret(offset) {
  if (offset === null) {
    return false;
  }

  const spans = serializedBlockSpans();
  if (spans.length === 0) {
    return false;
  }

  const clampedOffset = Math.max(0, Math.min(offset, serializeEditor().length));
  let fallback = spans[spans.length - 1];
  for (const span of spans) {
    if (clampedOffset <= span.contentEnd) {
      fallback = span;
      break;
    }
  }

  const blockOffset = Math.max(0, Math.min(clampedOffset - fallback.contentStart, blockText(fallback.block).length));
  editor.focus();
  setCaretInBlock(fallback.block, blockOffset);
  return true;
}

function loadActiveTabIntoEditor(options = {}) {
  const { focus = true, caretOffset = null } = options;
  setEditorFromMarkdown(activeTab().content);
  documentStatus.textContent = activeTab().title;
  updateCounts();
  renderTabs();
  if (!previewPane.hidden) {
    renderMarkdown(serializeEditor());
  }
  if (caretOffset !== null && restoreDocumentCaret(caretOffset)) {
    return;
  }
  if (focus) {
    editor.focus();
    const firstBlock = editor.querySelector(".editor-block");
    if (firstBlock) {
      setCaretInBlock(firstBlock, blockText(firstBlock).length);
    }
  }
}

function renderTabs() {
  const existingTabs = [...tabsContainer.querySelectorAll(".tab")];
  for (const tab of existingTabs) {
    tab.remove();
  }

  tabs.forEach((tab, index) => {
    const button = document.createElement("button");
    button.className = `tab${index === activeTabIndex ? " active" : ""}`;
    button.type = "button";
    button.role = "tab";
    button.setAttribute("aria-selected", String(index === activeTabIndex));
    button.title = tab.title;

    const title = document.createElement("span");
    title.className = "tab-title";
    title.textContent = tab.title;
    button.appendChild(title);

    if (tab.dirty) {
      const dirty = document.createElement("span");
      dirty.className = "tab-dirty";
      dirty.setAttribute("aria-hidden", "true");
      button.appendChild(dirty);
    }

    const close = document.createElement("span");
    close.className = "tab-close";
    close.title = "탭 닫기";
    close.textContent = "×";
    close.addEventListener("click", (event) => {
      event.stopPropagation();
      closeTab(index);
    });
    button.appendChild(close);

    button.addEventListener("click", () => switchTab(index));
    tabsContainer.insertBefore(button, newTabButton);
  });

  newTabButton.disabled = tabs.length >= maxTabs;
}

function switchTab(index) {
  if (index === activeTabIndex || index < 0 || index >= tabs.length) {
    return;
  }

  syncActiveTabFromEditor();
  activeTabIndex = index;
  loadActiveTabIntoEditor();
  saveStatus.textContent = `탭 전환: ${activeTab().title}`;
}

function closeTab(index) {
  if (tabs.length <= 1) {
    saveStatus.textContent = "마지막 탭은 닫을 수 없음";
    return;
  }
  if (index < 0 || index >= tabs.length) {
    return;
  }

  syncActiveTabFromEditor();
  const closedTitle = tabs[index].title;
  tabs.splice(index, 1);
  if (activeTabIndex === index) {
    activeTabIndex = Math.min(index, tabs.length - 1);
    loadActiveTabIntoEditor();
  } else {
    if (activeTabIndex > index) {
      activeTabIndex -= 1;
    }
    renderTabs();
  }
  saveStatus.textContent = `${closedTitle} 닫힘`;
}

function createTab() {
  if (tabs.length >= maxTabs) {
    saveStatus.textContent = "탭은 5개까지 지원";
    return;
  }

  syncActiveTabFromEditor();
  const title = `새 탭 ${nextTabNumber++}`;
  tabs.push({ title, content: "", dirty: false });
  activeTabIndex = tabs.length - 1;
  loadActiveTabIntoEditor();
  saveStatus.textContent = `${title} 생성`;
}

function scheduleAutosave() {
  if (loadingEditor) {
    return;
  }

  syncActiveTabFromEditor();
  setDirty(true);
  scheduleRelaySync();
  saveStatus.textContent = "자동저장 대기";
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => {
    postNative("editorChanged", { text: serializeEditor() });
    saveStatus.textContent = "자동저장 요청";
  }, 700);
}

function saveNow() {
  syncActiveTabFromEditor();
  flushRelayLocalChange();
  postNative("editorChanged", { text: serializeEditor() });
  setDirty(false);
  saveStatus.textContent = "저장 요청";
}

function escapeHtml(text) {
  return text
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;");
}

function renderMarkdown(text) {
  const lines = text.split(/\r?\n/);
  let inCode = false;
  const html = [];

  for (const rawLine of lines) {
    const line = escapeHtml(rawLine);
    const trimmed = line.trim();
    const todoMatch = line.match(/^- \[([ xX])\]\s*(.*)$/);
    const orderedMatch = line.match(/^(\d+|[a-zA-Z]|[ivxlcdmIVXLCDM]+)\.\s+(.*)$/);
    if (line.startsWith("```")) {
      html.push(inCode ? "</pre>" : "<pre>");
      inCode = !inCode;
      continue;
    }
    if (inCode) {
      html.push(`${line}\n`);
    } else if (/^-{3,}$/.test(trimmed)) {
      html.push("<hr>");
    } else if (todoMatch) {
      const checked = todoMatch[1].toLowerCase() === "x" ? " checked" : "";
      html.push(`<p class="todo"><input type="checkbox" disabled${checked}> <span>${todoMatch[2]}</span></p>`);
    } else if (line.startsWith("### ")) {
      html.push(`<h3>${line.slice(4)}</h3>`);
    } else if (line.startsWith("## ")) {
      html.push(`<h2>${line.slice(3)}</h2>`);
    } else if (line.startsWith("# ")) {
      html.push(`<h1>${line.slice(2)}</h1>`);
    } else if (line.startsWith("- ") || line.startsWith("* ")) {
      html.push(`<p>• ${line.slice(2)}</p>`);
    } else if (orderedMatch) {
      html.push(`<p>${orderedMatch[1]}. ${orderedMatch[2]}</p>`);
    } else if (line.startsWith("> ")) {
      html.push(`<blockquote>${line.slice(2)}</blockquote>`);
    } else if (line.trim() === "") {
      html.push("<p>&nbsp;</p>");
    } else {
      html.push(`<p>${line}</p>`);
    }
  }

  if (inCode) {
    html.push("</pre>");
  }

  previewContent.innerHTML = html.join("");
}

function togglePreview() {
  previewPane.hidden = !previewPane.hidden;
  previewButton.classList.toggle("is-active", !previewPane.hidden);
  if (!previewPane.hidden) {
    renderMarkdown(serializeEditor());
  }
}

function selectionInsideEditor() {
  const selection = window.getSelection();
  if (!selection || selection.rangeCount === 0) {
    return false;
  }
  const node = selection.anchorNode;
  return node === editor || editor.contains(node);
}

function getCurrentBlock() {
  const selection = window.getSelection();
  if (!selection || selection.rangeCount === 0) {
    return null;
  }

  let node = selection.anchorNode;
  if (node === editor) {
    const child = editor.children[Math.max(0, selection.anchorOffset - 1)];
    return child?.classList?.contains("editor-block") ? child : null;
  }

  if (node?.nodeType === Node.TEXT_NODE) {
    node = node.parentElement;
  }

  while (node && node !== editor) {
    if (node.classList?.contains("editor-block")) {
      return node;
    }
    node = node.parentElement;
  }

  return null;
}

function caretOffsetInBlock(block) {
  const selection = window.getSelection();
  if (!selection || selection.rangeCount === 0 || !selection.isCollapsed) {
    return 0;
  }

  const range = selection.getRangeAt(0);
  const preRange = document.createRange();
  preRange.selectNodeContents(block);
  preRange.setEnd(range.endContainer, range.endOffset);
  return preRange.toString().length;
}

function setCaretInBlock(block, offset) {
  if (!block || block.dataset.type === "divider") {
    return;
  }

  const range = document.createRange();
  const selection = window.getSelection();
  let remaining = Math.max(0, offset);
  const walker = document.createTreeWalker(block, NodeFilter.SHOW_TEXT);
  let textNode = walker.nextNode();

  while (textNode) {
    const length = textNode.textContent.length;
    if (remaining <= length) {
      range.setStart(textNode, remaining);
      range.collapse(true);
      selection.removeAllRanges();
      selection.addRange(range);
      return;
    }
    remaining -= length;
    textNode = walker.nextNode();
  }

  range.selectNodeContents(block);
  range.collapse(false);
  selection.removeAllRanges();
  selection.addRange(range);
}

function replaceBlockTextRange(block, start, end, replacement) {
  const text = blockText(block);
  const nextText = `${text.slice(0, start)}${replacement}${text.slice(end)}`;
  setBlockText(block, nextText);
  setCaretInBlock(block, start + replacement.length);
}

function setCurrentBlockType(type, options = {}) {
  const block = ensureEditableBlock();
  if (type === "divider") {
    setBlockType(block, "divider");
    const next = createBlock("p");
    block.after(next);
    setCaretInBlock(next, 0);
  } else {
    setBlockType(block, type, options);
    setCaretInBlock(block, blockText(block).length);
  }
  updateCounts();
  scheduleAutosave();
}

function insertTextAtSelection(text) {
  editor.focus();
  document.execCommand("insertText", false, text);
  updateCounts();
  scheduleAutosave();
}

function wrapSelection(before, after = before, placeholder = "텍스트") {
  editor.focus();
  const selection = window.getSelection();
  const selected = selection && !selection.isCollapsed ? selection.toString() : placeholder;
  insertTextAtSelection(`${before}${selected}${after}`);
}

function applyCommand(command) {
  editor.focus();
  if (command === "heading") {
    setCurrentBlockType("h1");
  } else if (command === "list") {
    setCurrentBlockType("bullet");
  } else if (command === "bold") {
    wrapSelection("**");
  } else if (command === "italic") {
    wrapSelection("*");
  } else if (command === "link") {
    wrapSelection("[", "](https://)", "링크");
  } else if (command === "preview") {
    togglePreview();
  }
}

function conversionFromMarker(marker) {
  const normalized = marker.toLowerCase();
  if (marker === "#") {
    return { blockType: "h1" };
  }
  if (marker === "##") {
    return { blockType: "h2" };
  }
  if (marker === "###") {
    return { blockType: "h3" };
  }
  if (marker === "-" || marker === "*" || marker === "+") {
    return { blockType: "bullet" };
  }
  if (/^\d+\.$/.test(marker)) {
    return { blockType: "ordered" };
  }
  if (marker === "[]") {
    return { blockType: "todo" };
  }
  if (normalized === "[x]") {
    return { blockType: "todo", checked: true };
  }
  if (marker === ">") {
    return { blockType: "quote" };
  }
  if (marker === "```") {
    return { blockType: "code" };
  }
  if (marker === "---") {
    return { blockType: "divider" };
  }
  return null;
}

function leadingConversion(text) {
  const match = text.match(/^(###|##|#|-|\*|\+|\d+\.|\[\]|\[[xX]\]|>|```|---)\s+(.*)$/);
  if (!match) {
    return null;
  }
  const conversion = conversionFromMarker(match[1]);
  if (!conversion) {
    return null;
  }
  return { ...conversion, markerLength: match[1].length + 1, content: match[2] };
}

function applyBlockConversion(block, conversion, content, caretOffset = 0) {
  if (conversion.blockType === "divider") {
    setBlockType(block, "divider");
    const next = createBlock("p");
    block.after(next);
    setCaretInBlock(next, 0);
    return;
  }

  setBlockType(block, conversion.blockType, { checked: conversion.checked });
  setBlockText(block, content);
  setCaretInBlock(block, Math.max(0, caretOffset));
}

function applyMarkdownShortcut(event) {
  if (event.key !== " " || !selectionInsideEditor()) {
    return;
  }

  const block = getCurrentBlock();
  if (!block || (block.dataset.type ?? "p") !== "p") {
    return;
  }

  const text = blockText(block);
  const offset = caretOffsetInBlock(block);
  const beforeCursor = text.slice(0, offset);
  const directConversion = conversionFromMarker(beforeCursor);

  if (directConversion) {
    event.preventDefault();
    applyBlockConversion(block, directConversion, text.slice(offset), 0);
    updateCounts();
    scheduleAutosave();
    return;
  }

  const lineConversion = leadingConversion(text);
  if (!lineConversion || offset < lineConversion.markerLength) {
    return;
  }

  event.preventDefault();
  applyBlockConversion(
    block,
    lineConversion,
    lineConversion.content,
    Math.max(0, offset - lineConversion.markerLength),
  );
  updateCounts();
  scheduleAutosave();
}

function getSlashContext() {
  const selection = window.getSelection();
  if (!selection || !selection.isCollapsed || !selectionInsideEditor()) {
    return null;
  }

  const block = getCurrentBlock();
  if (!block || block.dataset.type === "divider") {
    return null;
  }

  const position = caretOffsetInBlock(block);
  const beforeCursor = blockText(block).slice(0, position);
  const match = beforeCursor.match(/\/([A-Za-z0-9#]*)$/);
  if (!match) {
    return null;
  }

  return {
    block,
    start: position - match[0].length,
    end: position,
    query: match[1].toLowerCase(),
  };
}

function commandMatchesQuery(command, query) {
  if (!query) {
    return true;
  }

  const candidates = [command.name, command.label, ...(command.aliases ?? [])];
  return candidates.some((candidate) => candidate.toLowerCase().includes(query));
}

function updateSlashMenu() {
  slashContext = getSlashContext();
  if (!slashContext) {
    hideSlashMenu();
    return;
  }

  slashMatches = slashCommands.filter((command) => commandMatchesQuery(command, slashContext.query));
  slashSelectedIndex = Math.min(slashSelectedIndex, Math.max(0, slashMatches.length - 1));
  if (slashMatches.length === 0) {
    hideSlashMenu();
    return;
  }

  renderSlashMenu();
  const rect = slashContext.block.getBoundingClientRect();
  slashMenu.style.left = `${Math.max(16, Math.min(rect.left, window.innerWidth - 356))}px`;
  slashMenu.style.top = `${Math.max(16, Math.min(rect.bottom + 6, window.innerHeight - 380))}px`;
  slashMenu.hidden = false;
}

function renderSlashMenu() {
  slashMenu.innerHTML = "";
  slashMatches.forEach((command, index) => {
    const button = document.createElement("button");
    button.type = "button";
    button.className = `slash-item${index === slashSelectedIndex ? " is-selected" : ""}`;
    button.innerHTML = `
      <span class="slash-icon">${escapeHtml(command.icon)}</span>
      <span>
        <span class="slash-label">/${escapeHtml(command.name)} · ${escapeHtml(command.label)}</span>
        <span class="slash-description">${escapeHtml(command.description)}</span>
      </span>
    `;
    button.addEventListener("mousedown", (event) => {
      event.preventDefault();
      applySlashCommand(command);
    });
    slashMenu.appendChild(button);
  });
}

function hideSlashMenu() {
  slashMenu.hidden = true;
  slashContext = null;
  slashMatches = [];
  slashSelectedIndex = 0;
}

function applySlashCommand(command) {
  const context = slashContext ?? getSlashContext();
  if (!context) {
    return;
  }

  const text = blockText(context.block);
  const nextText = `${text.slice(0, context.start)}${text.slice(context.end)}`;
  applyBlockConversion(
    context.block,
    { blockType: command.blockType },
    nextText,
    context.start,
  );
  hideSlashMenu();
  updateCounts();
  scheduleAutosave();
  if (!previewPane.hidden) {
    renderMarkdown(serializeEditor());
  }
}

function splitCurrentBlock() {
  const block = ensureEditableBlock();
  if (block.dataset.type === "divider") {
    const next = createBlock("p");
    block.after(next);
    setCaretInBlock(next, 0);
    return;
  }

  const type = block.dataset.type ?? "p";
  const text = blockText(block);
  const offset = caretOffsetInBlock(block);
  const before = text.slice(0, offset);
  const after = text.slice(offset);

  if (before.length === 0 && continuedBlockTypes.has(type) && after.length === 0) {
    setBlockType(block, "p");
    setCaretInBlock(block, 0);
    return;
  }

  setBlockText(block, before);
  const nextType = continuedBlockTypes.has(type) && before.length > 0 ? type : "p";
  const next = createBlock(nextType, after);
  block.after(next);
  setCaretInBlock(next, 0);
}

function mergeWithPreviousBlock() {
  const block = getCurrentBlock();
  if (!block || block === editor.firstElementChild || caretOffsetInBlock(block) !== 0) {
    return false;
  }

  if ((block.dataset.type ?? "p") !== "p" && blockText(block).length === 0) {
    setBlockType(block, "p");
    return true;
  }

  const previous = block.previousElementSibling;
  if (!previous || previous.dataset.type === "divider" || block.dataset.type === "divider") {
    return false;
  }

  const previousLength = blockText(previous).length;
  setBlockText(previous, `${blockText(previous)}${blockText(block)}`);
  block.remove();
  setCaretInBlock(previous, previousLength);
  return true;
}

function selectEditor() {
  const range = document.createRange();
  range.selectNodeContents(editor);
  const selection = window.getSelection();
  selection.removeAllRanges();
  selection.addRange(range);
}

const menuActions = {
  file: [
    ["새 탭", createTab],
    ["저장", saveNow],
    ["공유", () => shareDialog.showModal()],
    "separator",
    ["끝내기", () => postNative("quit")],
  ],
  edit: [
    ["실행 취소", () => document.execCommand("undo")],
    ["다시 실행", () => document.execCommand("redo")],
    "separator",
    ["잘라내기", () => document.execCommand("cut")],
    ["복사", () => document.execCommand("copy")],
    ["붙여넣기", () => document.execCommand("paste")],
    ["모두 선택", selectEditor],
  ],
  view: [
    ["마크다운 미리보기", togglePreview],
    ["편집기에 포커스", () => editor.focus()],
  ],
};

function showMenu(menuName, anchor) {
  const items = menuActions[menuName];
  if (!items) {
    return;
  }

  menuLayer.innerHTML = "";
  for (const item of items) {
    if (item === "separator") {
      const separator = document.createElement("div");
      separator.className = "separator";
      menuLayer.appendChild(separator);
      continue;
    }

    const [label, action] = item;
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = label;
    button.addEventListener("click", () => {
      hideMenu();
      action();
    });
    menuLayer.appendChild(button);
  }

  const rect = anchor.getBoundingClientRect();
  menuLayer.style.left = `${rect.left}px`;
  menuLayer.style.top = `${rect.bottom + 4}px`;
  menuLayer.hidden = false;
}

function hideMenu() {
  menuLayer.hidden = true;
}

function insertPlainText(text) {
  const block = ensureEditableBlock();
  const offset = caretOffsetInBlock(block);
  const normalized = text.replace(/\r\n/g, "\n");
  const parts = normalized.split("\n");
  const currentText = blockText(block);
  const before = currentText.slice(0, offset);
  const after = currentText.slice(offset);

  if (parts.length === 1) {
    setBlockText(block, `${before}${parts[0]}${after}`);
    setCaretInBlock(block, before.length + parts[0].length);
    return;
  }

  setBlockText(block, `${before}${parts[0]}`);
  let previous = block;
  for (let index = 1; index < parts.length; index += 1) {
    const textPart = index === parts.length - 1 ? `${parts[index]}${after}` : parts[index];
    const next = createBlock("p", textPart);
    previous.after(next);
    previous = next;
  }
  setCaretInBlock(previous, parts[parts.length - 1].length);
}

editor.addEventListener("keydown", (event) => {
  if (!slashMenu.hidden) {
    if (event.key === "ArrowDown") {
      event.preventDefault();
      slashSelectedIndex = (slashSelectedIndex + 1) % slashMatches.length;
      renderSlashMenu();
      return;
    }
    if (event.key === "ArrowUp") {
      event.preventDefault();
      slashSelectedIndex = (slashSelectedIndex - 1 + slashMatches.length) % slashMatches.length;
      renderSlashMenu();
      return;
    }
    if (event.key === "Enter") {
      event.preventDefault();
      applySlashCommand(slashMatches[slashSelectedIndex]);
      return;
    }
    if (event.key === "Escape") {
      event.preventDefault();
      hideSlashMenu();
      return;
    }
  }

  if (event.ctrlKey && event.key.toLowerCase() === "t") {
    event.preventDefault();
    createTab();
    return;
  }

  if (event.key === "Enter" && !event.shiftKey) {
    event.preventDefault();
    splitCurrentBlock();
    hideSlashMenu();
    updateCounts();
    scheduleAutosave();
    if (!previewPane.hidden) {
      renderMarkdown(serializeEditor());
    }
    return;
  }

  if (event.key === "Backspace" && mergeWithPreviousBlock()) {
    event.preventDefault();
    updateCounts();
    scheduleAutosave();
    return;
  }

  applyMarkdownShortcut(event);
});

editor.addEventListener("input", () => {
  if (loadingEditor) {
    return;
  }
  updateCounts();
  scheduleAutosave();
  updateSlashMenu();
  if (!previewPane.hidden) {
    renderMarkdown(serializeEditor());
  }
});

editor.addEventListener("compositionstart", () => {
  editorComposing = true;
});

editor.addEventListener("compositionend", () => {
  editorComposing = false;
  scheduleRelaySync();
  if (pendingRelayDocument) {
    const pending = pendingRelayDocument;
    pendingRelayDocument = null;
    setTimeout(() => applyRelayDocument(pending.text, pending.status, pending.options), 0);
  }
});

editor.addEventListener("click", updateSlashMenu);
editor.addEventListener("keyup", updateSlashMenu);

editor.addEventListener("paste", (event) => {
  event.preventDefault();
  insertPlainText(event.clipboardData?.getData("text/plain") ?? "");
  updateCounts();
  scheduleAutosave();
  if (!previewPane.hidden) {
    renderMarkdown(serializeEditor());
  }
});

document.querySelectorAll("[data-menu]").forEach((button) => {
  button.addEventListener("click", (event) => {
    event.stopPropagation();
    showMenu(button.dataset.menu, button);
  });
});

document.querySelectorAll("[data-command]").forEach((button) => {
  button.addEventListener("click", () => {
    applyCommand(button.dataset.command);
  });
});

document.addEventListener("click", (event) => {
  if (!menuLayer.hidden && !menuLayer.contains(event.target)) {
    hideMenu();
  }
  if (!slashMenu.hidden && !slashMenu.contains(event.target) && !editor.contains(event.target)) {
    hideSlashMenu();
  }
});

shareButton.addEventListener("click", () => {
  shareDialog.showModal();
});

createRoomButton.addEventListener("click", () => {
  createRelayRoom();
  connectionStatus.textContent = "릴레이 방 만들기 요청";
});

joinRoomButton.addEventListener("click", () => {
  const code = joinCodeInput.value.trim();
  if (!code) {
    connectionStatus.textContent = "초대 코드 필요";
    joinCodeInput.focus();
    return;
  }
  joinRelayRoom(code);
  connectionStatus.textContent = "릴레이 방 들어가기 요청";
});

newTabButton.addEventListener("click", createTab);

window.chrome?.webview?.addEventListener("message", (event) => {
  const message = typeof event.data === "string" ? JSON.parse(event.data) : event.data;
  if (message.type === "hydrateDocument") {
    tabs[0].content = message.payload?.text ?? "";
    tabs[0].dirty = false;
    if (relayRole === "offline") {
      relayLastText = tabs[0].content;
    }
    if (activeTabIndex === 0) {
      loadActiveTabIntoEditor();
    }
  }
  if (message.type === "statusChanged") {
    saveStatus.textContent = message.payload?.saveStatus ?? saveStatus.textContent;
    connectionStatus.textContent = message.payload?.connectionStatus ?? connectionStatus.textContent;
    if (relayRole !== "offline") {
      setRelayConnectionStatus(relayRoom ? `릴레이 연결됨: ${relayRoom}` : "릴레이 연결됨");
    }
    if ((message.payload?.saveStatus ?? "").includes("완료")) {
      setDirty(false);
    }
  }
  if (message.type === "roomCreated") {
    const code = message.payload?.code ?? "";
    inviteCodeText.textContent = code;
    inviteCodeBox.hidden = !code;
    joinCodeInput.value = code;
    connectionStatus.textContent = `방 생성됨: ${code}`;
  }
});

window.addEventListener("DOMContentLoaded", () => {
  renderTabs();
  loadActiveTabIntoEditor();
  postNative("uiReady");
});
