package com.akinalpfdn.osdrop.discovery

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.util.Log
import com.akinalpfdn.osdrop.core.Mesh

/**
 * Registers this phone's service, so the computers list it nearby and dial it while the app is
 * open. A new [start] replaces the registration before it (the group changed, or the port).
 */
class NsdAdvertiser(context: Context) {
    private val manager = context.getSystemService(NsdManager::class.java)
    private var registration: NsdManager.RegistrationListener? = null

    fun start(name: String, port: Int, txt: Map<String, String>) {
        stop()
        val info = NsdServiceInfo().apply {
            serviceName = name
            serviceType = Mesh.SERVICE_TYPE
            setPort(port)
            txt.forEach { (key, value) -> setAttribute(key, value) }
        }
        val listener = object : NsdManager.RegistrationListener {
            override fun onServiceRegistered(serviceInfo: NsdServiceInfo) = Unit

            override fun onRegistrationFailed(serviceInfo: NsdServiceInfo, errorCode: Int) {
                Log.w(TAG, "Advertising failed: $errorCode")
            }

            override fun onServiceUnregistered(serviceInfo: NsdServiceInfo) = Unit

            override fun onUnregistrationFailed(serviceInfo: NsdServiceInfo, errorCode: Int) = Unit
        }
        registration = listener
        manager.registerService(info, NsdManager.PROTOCOL_DNS_SD, listener)
    }

    fun stop() {
        registration?.let { runCatching { manager.unregisterService(it) } }
        registration = null
    }

    private companion object {
        const val TAG = "Osdrop"
    }
}
