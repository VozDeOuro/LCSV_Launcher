const { DistributionAPI } = require('helios-core/common')

const ConfigManager = require('./configmanager')

// Old WesterosCraft url.
// exports.REMOTE_DISTRO_URL = 'http://mc.westeroscraft.com/WesterosCraftLauncher/distribution.json'
exports.REMOTE_DISTRO_URL = 'https://mc.lcsv.com.br/lcsv/distribution.json'

const api = new DistributionAPI(
    ConfigManager.getLauncherDirectory(),
    null, // Injected forcefully by the preloader.
    null, // Injected forcefully by the preloader.
    exports.REMOTE_DISTRO_URL,
    false
)

const REMOTE_TIMEOUT_MS = 10000
let remoteLoadStatus = 'unknown'

function timeout(ms) {
    return new Promise(resolve => setTimeout(() => resolve(null), ms))
}

api.getRemoteLoadStatus = function(){
    return remoteLoadStatus
}

api._loadDistributionNullable = async function(){
    if(this.devMode){
        remoteLoadStatus = 'local'
        return await this.pullLocal()
    }

    const remoteDistro = await Promise.race([
        this.pullRemote()
            .then(res => res != null ? res.data : null)
            .catch(() => null),
        timeout(REMOTE_TIMEOUT_MS)
    ])

    if(remoteDistro != null){
        remoteLoadStatus = 'online'
        await this.writeDistributionToDisk(remoteDistro)
        return remoteDistro
    }

    remoteLoadStatus = 'offline'
    return await this.pullLocal()
}

exports.DistroAPI = api