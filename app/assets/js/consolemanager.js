const fs = require('fs-extra')
const path = require('path')

class ConsoleManager {
    constructor(launcherDirectory) {
        this.launcherDirectory = launcherDirectory
        this.session = null
        this.lines = []
    }

    startSession(serverId, serverName) {
        const timestamp = new Date().toISOString().replace(/[:.]/g, '-')
        const logDir = path.join(this.launcherDirectory, 'logs', 'instances')
        fs.ensureDirSync(logDir)
        const logPath = path.join(logDir, `${serverId}-${timestamp}.log`)
        this.session = {
            serverId,
            serverName,
            logPath,
            startedAt: new Date().toISOString()
        }
        this.lines = []
        fs.writeFileSync(logPath, `Session started: ${this.session.startedAt}\nServer: ${serverName} (${serverId})\n\n`, 'utf8')
        return this.session
    }

    pushLine(stream, level, line) {
        if (!this.session) return
        const entry = `[${stream || 'stdout'}][${level || 'info'}] ${line}`
        this.lines.push(entry)
        fs.appendFileSync(this.session.logPath, `${entry}\n`, 'utf8')
    }

    endSession(code, signal) {
        if (!this.session) return
        const endedAt = new Date().toISOString()
        fs.appendFileSync(this.session.logPath, `\nSession ended: ${endedAt}\nExit code: ${code}\nSignal: ${signal || ''}\n`, 'utf8')
        this.session = null
    }
}

module.exports = ConsoleManager
