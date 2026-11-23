package com.akinalpfdn.osdrop.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp

/** Every visual value of the app (CLAUDE.md: from the platform theme source). */
object Tokens {
    val screenPadding = 24.dp
    val gap = 12.dp
}

/** Material 3 with the phone's own colors (Android 12 and later), light or dark as the system is. */
@Composable
fun OsdropTheme(content: @Composable () -> Unit) {
    val context = LocalContext.current
    val colors = if (isSystemInDarkTheme()) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
    MaterialTheme(colorScheme = colors, content = content)
}
