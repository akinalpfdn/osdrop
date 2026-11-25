package com.akinalpfdn.osdrop.inbox

import com.akinalpfdn.osdrop.core.CoreException
import com.akinalpfdn.osdrop.core.CoreResult
import com.akinalpfdn.osdrop.core.DeviceId
import com.akinalpfdn.osdrop.core.Member
import com.akinalpfdn.osdrop.core.Platform
import com.akinalpfdn.osdrop.core.SendOutcome
import com.akinalpfdn.osdrop.core.SentReceiving
import com.akinalpfdn.osdrop.outbox.OutgoingSend
import java.io.File
import java.nio.file.Files
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class InboxTest {
    private val laptop = DeviceId(ByteArray(16) { 1 })
    private lateinit var cache: File
    private val published = mutableListOf<String>()
    private val arrivals = mutableListOf<Triple<String, Int, Boolean>>()
    private val scope = TestScope(UnconfinedTestDispatcher())

    /** Core in memory: hands out transfer IDs, or fails the items it is told to. */
    private inner class FakeMesh(private val failing: Set<Int> = emptySet()) : SentReceiving {
        val requested = mutableListOf<Int>()
        val answers = mutableListOf<Pair<Long, SendOutcome>>()
        var beforeReturning: ((Long) -> Unit)? = null

        override fun members() = listOf(Member(laptop, "Laptop", Platform.MacOS, isSelf = false, isConnected = true, allowsFiles = true))

        override fun receiveSent(device: DeviceId, sendId: Long, index: Int, folder: String): Long {
            if (index in failing) throw CoreException(CoreResult.NOT_FOUND)
            requested += index
            val transfer = (device.bytes[0] - 1) * 10_000L + sendId * 100 + index // unique per computer, as in core
            beforeReturning?.invoke(transfer)
            return transfer
        }

        override fun answerSend(device: DeviceId, sendId: Long, outcome: SendOutcome) {
            answers += sendId to outcome
        }

        val cancelled = mutableListOf<Long>()

        override fun cancelTransfer(transfer: Long) {
            cancelled += transfer
        }
    }

    private fun inbox() = Inbox(
        cache,
        { root -> published += root.name; emptyList() },
        { from, files, failed -> arrivals += Triple(from, files.size, failed) },
        scope,
    )

    @Before
    fun setUp() {
        cache = Files.createTempDirectory("inbox").toFile()
    }

    @After
    fun tearDown() {
        cache.deleteRecursively()
    }

    @Test
    fun `should publish every item and answer received when all of them arrive`() {
        val mesh = FakeMesh()
        val inbox = inbox().apply { attach(mesh) }

        inbox.onSendArrived(laptop, 7, listOf("a.jpg", "b.apk"), listOf(10, 20))
        assertEquals(listOf(0, 1), mesh.requested)
        assertEquals(30L, inbox.incoming.value.single().bytesTotal)

        inbox.onTransferProgress(700, 10, 10)
        assertEquals(10L, inbox.incoming.value.single().bytesDone)
        inbox.onTransferFinished(700, CoreResult.OK, listOf(File(cache, "a.jpg").path))
        assertTrue(mesh.answers.isEmpty())
        inbox.onTransferFinished(701, CoreResult.OK, listOf(File(cache, "b.apk").path))

        assertEquals(listOf("a.jpg", "b.apk"), published)
        assertEquals(listOf(7L to SendOutcome.Received), mesh.answers)
        assertEquals(listOf(Triple("Laptop", 0, false)), arrivals)
        assertTrue(inbox.incoming.value.isEmpty())
        assertFalse(File(cache, "incoming/7").exists())
    }

    @Test
    fun `should answer failed when an item cannot be fetched and the others end`() {
        val mesh = FakeMesh(failing = setOf(1))
        val inbox = inbox().apply { attach(mesh) }

        inbox.onSendArrived(laptop, 8, listOf("a.jpg", "b.jpg"), listOf(5, 5))
        inbox.onTransferFinished(800, CoreResult.OK, listOf(File(cache, "a.jpg").path))

        assertEquals(listOf(8L to SendOutcome.Failed), mesh.answers)
        assertEquals(listOf(Triple("Laptop", 0, true)), arrivals)
    }

    @Test
    fun `should answer only once every item ended when one finishes before it was recorded`() {
        val mesh = FakeMesh()
        val inbox = inbox().apply { attach(mesh) }
        mesh.beforeReturning = { transfer ->
            if (transfer == 900L) inbox.onTransferFinished(transfer, CoreResult.CANCELLED, emptyList())
        }

        inbox.onSendArrived(laptop, 9, listOf("a.jpg", "b.jpg"), listOf(5, 5))
        assertTrue(mesh.answers.isEmpty())
        inbox.onTransferFinished(901, CoreResult.OK, listOf(File(cache, "b.jpg").path))

        assertEquals(listOf(9L to SendOutcome.Failed), mesh.answers)
        assertEquals(listOf("b.jpg"), published)
    }

    @Test
    fun `should cancel every transfer of a send and answer failed without a notification`() {
        val mesh = FakeMesh()
        val inbox = inbox().apply { attach(mesh) }
        inbox.onSendArrived(laptop, 11, listOf("a.jpg", "b.jpg"), listOf(5, 5))

        inbox.cancel(laptop, 11)
        assertEquals(listOf(1100L, 1101L), mesh.cancelled.sorted())
        inbox.onTransferFinished(1100, CoreResult.CANCELLED, emptyList())
        inbox.onTransferFinished(1101, CoreResult.CANCELLED, emptyList())

        assertEquals(listOf(11L to SendOutcome.Failed), mesh.answers)
        assertTrue(arrivals.isEmpty())
        assertTrue(inbox.incoming.value.isEmpty())
    }

    @Test
    fun `should keep sends apart when two computers use the same number`() {
        val mesh = FakeMesh()
        val inbox = inbox().apply { attach(mesh) }
        val desktop = DeviceId(ByteArray(16) { 2 })
        inbox.onSendArrived(laptop, 1, listOf("a.jpg"), listOf(5))
        inbox.onSendArrived(desktop, 1, listOf("b.jpg"), listOf(5))

        val incoming = inbox.incoming.value
        assertEquals(2, incoming.size)
        assertEquals(2, incoming.map { it.listKey }.toSet().size)
        inbox.cancel(laptop, 1)
        assertEquals(1, mesh.cancelled.size)
    }

    @Test
    fun `should give incoming and outgoing sends with the same number different list keys`() {
        val incoming = IncomingFiles(laptop, 1, "Laptop", listOf("a.jpg"), 0, 5)
        val outgoing = OutgoingSend(1, "Laptop", 1)
        assertTrue(incoming.listKey != outgoing.listKey)
    }

    @Test
    fun `should forget sends in progress when detached`() {
        val mesh = FakeMesh()
        val inbox = inbox().apply { attach(mesh) }
        inbox.onSendArrived(laptop, 10, listOf("a.jpg"), listOf(5))

        inbox.detach()
        inbox.onTransferFinished(1000, CoreResult.OK, listOf(File(cache, "a.jpg").path))

        assertTrue(mesh.answers.isEmpty())
        assertTrue(inbox.incoming.value.isEmpty())
    }
}
