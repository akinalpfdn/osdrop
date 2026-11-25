package com.akinalpfdn.osdrop.outbox

import org.junit.Assert.assertEquals
import org.junit.Test

class OutboxTest {
    @Test
    fun `should name known types by their uniform type identifier`() {
        assertEquals("public.jpeg", Outbox.utiOf("image/jpeg"))
        assertEquals("com.android.package-archive", Outbox.utiOf("application/vnd.android.package-archive"))
        assertEquals("com.adobe.pdf", Outbox.utiOf("APPLICATION/PDF"))
    }

    @Test
    fun `should fall back to the family or to plain data when the type is unknown`() {
        assertEquals("public.image", Outbox.utiOf("image/avif"))
        assertEquals("public.movie", Outbox.utiOf("video/x-matroska"))
        assertEquals("public.audio", Outbox.utiOf("audio/ogg"))
        assertEquals("public.data", Outbox.utiOf("application/x-unknown"))
        assertEquals("public.data", Outbox.utiOf(null))
    }
}
