package com.akinalpfdn.osdrop.discovery

import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.channelFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/**
 * Browses with several [PeerDiscovering] sources at once. A computer stays listed while any source
 * still sees it, and browsing fails only when every source has failed.
 */
class CombinedDiscovery(private val sources: List<PeerDiscovering>) : PeerDiscovering {
    override fun browse(): Flow<DiscoveryEvent> = channelFlow {
        val mutex = Mutex()
        val seenBy = mutableMapOf<String, MutableSet<Int>>() // label → sources that see it
        val failed = mutableSetOf<Int>()

        suspend fun drop(source: Int, label: String) {
            val holders = seenBy[label] ?: return
            holders.remove(source)
            if (holders.isEmpty()) {
                seenBy.remove(label)
                send(DiscoveryEvent.Lost(label))
            }
        }

        sources.forEachIndexed { source, discovery ->
            launch {
                discovery.browse().collect { event ->
                    mutex.withLock {
                        when (event) {
                            is DiscoveryEvent.Found -> {
                                seenBy.getOrPut(event.device.label) { mutableSetOf() }.add(source)
                                send(event)
                            }
                            is DiscoveryEvent.Lost -> drop(source, event.label)
                            is DiscoveryEvent.Failed -> {
                                failed += source
                                seenBy.keys.toList().forEach { drop(source, it) }
                                if (failed.size == sources.size) {
                                    send(event)
                                    close()
                                }
                            }
                        }
                    }
                }
            }
        }
        awaitClose()
    }
}
