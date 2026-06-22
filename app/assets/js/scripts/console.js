const { ipcRenderer } = require('electron')

const CONSOLE_OPCODE = {
    LOG_LINE: 'CONSOLE_LOG_LINE',
    SESSION_STARTED: 'CONSOLE_SESSION_STARTED',
    SESSION_ENDED: 'CONSOLE_SESSION_ENDED',
    UPLOAD_LOG: 'CONSOLE_UPLOAD_LOG',
    UPLOAD_RESULT: 'CONSOLE_UPLOAD_RESULT',
    OPEN_LOG_FILE: 'CONSOLE_OPEN_LOG_FILE'
}

let logLines = []
const logEl = document.getElementById('log')
const statusEl = document.getElementById('status')
const MAX_LINES = 5000

function appendLine(text, level) {
    logLines.push(text)
    if (logLines.length > MAX_LINES) logLines.shift()
    const div = document.createElement('div')
    div.className = level || 'info'
    div.textContent = text
    logEl.appendChild(div)
    logEl.scrollTop = logEl.scrollHeight
}

function clearLog() {
    logLines = []
    logEl.innerHTML = ''
}

ipcRenderer.on(CONSOLE_OPCODE.SESSION_STARTED, (event, meta) => {
    statusEl.textContent = 'Running - ' + (meta.serverId || '')
    document.title = meta.serverName + ' Console'
    clearLog()
})

ipcRenderer.on(CONSOLE_OPCODE.LOG_LINE, (event, logEntry) => {
    const level = logEntry.level || 'info'
    appendLine(logEntry.text || logEntry.line || String(logEntry), level)
})

ipcRenderer.on(CONSOLE_OPCODE.SESSION_ENDED, (event, endData) => {
    statusEl.textContent = 'Exited with code ' + (endData.exitCode || '?')
})

ipcRenderer.on(CONSOLE_OPCODE.UPLOAD_RESULT, (event, result) => {
    if (result.success) {
        appendLine('[mclo.gs] Log uploaded: ' + result.url, 'info')
    } else {
        appendLine('[mclo.gs] Upload failed: ' + result.error, 'error')
    }
})

document.getElementById('btn-upload').addEventListener('click', () => {
    const content = logLines.join('\n')
    ipcRenderer.invoke(CONSOLE_OPCODE.UPLOAD_LOG, { content, sessionMeta: {} })
})

document.getElementById('btn-clear').addEventListener('click', clearLog)
