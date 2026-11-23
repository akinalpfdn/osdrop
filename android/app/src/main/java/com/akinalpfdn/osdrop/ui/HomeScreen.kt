package com.akinalpfdn.osdrop.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Computer
import androidx.compose.material.icons.filled.DesktopWindows
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.akinalpfdn.osdrop.R
import com.akinalpfdn.osdrop.app.MeshState
import com.akinalpfdn.osdrop.app.PairingPrompt
import com.akinalpfdn.osdrop.core.Platform
import com.akinalpfdn.osdrop.discovery.DiscoveredDevice
import com.akinalpfdn.osdrop.inbox.IncomingFiles

/** What the screen can ask for. */
interface HomeActions {
    fun pair(device: DiscoveredDevice)

    fun answerPairing(accepted: Boolean)

    fun leaveGroup()
}

@Composable
fun HomeScreen(state: MeshState, incoming: List<IncomingFiles>, actions: HomeActions) {
    Scaffold(modifier = Modifier.fillMaxSize()) { insets ->
        LazyColumn(
            modifier = Modifier.padding(insets).padding(Tokens.screenPadding),
            verticalArrangement = Arrangement.spacedBy(Tokens.gap),
        ) {
            item { Text(stringResource(R.string.app_name), style = MaterialTheme.typography.headlineMedium) }
            val computers = state.members.filterNot { it.isSelf }
            if (computers.isEmpty()) {
                item { Section(stringResource(R.string.pair_title), stringResource(R.string.pair_hint)) }
                items(state.nearby, key = { it.advertisement.deviceId.toString() }) { device ->
                    ListItem(
                        headlineContent = { Text(device.advertisement.name) },
                        leadingContent = { PlatformIcon(device.advertisement.platform) },
                        trailingContent = { TextButton(onClick = { actions.pair(device) }) { Text(stringResource(R.string.pair_action)) } },
                    )
                }
            } else {
                item { Section(stringResource(R.string.group_title), stringResource(R.string.group_hint)) }
                items(computers, key = { it.deviceId.toString() }) { member ->
                    ListItem(
                        headlineContent = { Text(member.name) },
                        supportingContent = {
                            Text(stringResource(if (member.isConnected) R.string.member_connected else R.string.member_not_connected))
                        },
                        leadingContent = { PlatformIcon(member.platform) },
                    )
                }
                item { TextButton(onClick = actions::leaveGroup) { Text(stringResource(R.string.group_leave)) } }
            }
            items(incoming, key = { it.sendId }) { files -> Incoming(files) }
        }
    }
    state.pairing?.let { PairingDialog(it, actions) }
}

@Composable
private fun Section(title: String, hint: String) {
    Column(verticalArrangement = Arrangement.spacedBy(Tokens.smallGap)) {
        Text(title, style = MaterialTheme.typography.titleMedium)
        Text(hint, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

@Composable
private fun PlatformIcon(platform: Platform?) {
    Icon(if (platform == Platform.Windows) Icons.Filled.DesktopWindows else Icons.Filled.Computer, contentDescription = null)
}

@Composable
private fun Incoming(files: IncomingFiles) {
    Column(modifier = Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(Tokens.smallGap)) {
        Text(stringResource(R.string.incoming_from, files.from), style = MaterialTheme.typography.titleSmall)
        Text(files.names.joinToString(", "), style = MaterialTheme.typography.bodySmall, maxLines = 2)
        LinearProgressIndicator(
            progress = { if (files.bytesTotal > 0) files.bytesDone.toFloat() / files.bytesTotal else 0f },
            modifier = Modifier.fillMaxWidth(),
        )
    }
}

@Composable
private fun PairingDialog(prompt: PairingPrompt, actions: HomeActions) {
    AlertDialog(
        onDismissRequest = { actions.answerPairing(false) },
        title = { Text(stringResource(R.string.pairing_title, prompt.name)) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(Tokens.gap)) {
                Row { Text("%06d".format(prompt.code), style = MaterialTheme.typography.displaySmall) }
                Text(stringResource(R.string.pairing_hint))
            }
        },
        confirmButton = { TextButton(onClick = { actions.answerPairing(true) }) { Text(stringResource(R.string.pairing_accept)) } },
        dismissButton = { TextButton(onClick = { actions.answerPairing(false) }) { Text(stringResource(R.string.pairing_reject)) } },
    )
}
