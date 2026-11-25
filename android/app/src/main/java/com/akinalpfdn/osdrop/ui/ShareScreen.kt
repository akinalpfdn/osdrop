package com.akinalpfdn.osdrop.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Computer
import androidx.compose.material.icons.filled.DesktopWindows
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import com.akinalpfdn.osdrop.R
import com.akinalpfdn.osdrop.app.MeshState
import com.akinalpfdn.osdrop.core.Member
import com.akinalpfdn.osdrop.core.Platform
import com.akinalpfdn.osdrop.core.SendOutcome
import com.akinalpfdn.osdrop.outbox.OutgoingSend

/** Picks the computer that gets what was shared, then shows how the send goes. */
@Composable
fun ShareScreen(count: Int, state: MeshState, sends: List<OutgoingSend>, onPick: (Member) -> Unit, onDone: () -> Unit) {
    val computers = state.members.filter { !it.isSelf && it.isConnected && it.platform?.isPhone == false }
    Scaffold(modifier = Modifier.fillMaxSize()) { insets ->
        LazyColumn(
            modifier = Modifier.padding(insets).padding(Tokens.screenPadding),
            verticalArrangement = Arrangement.spacedBy(Tokens.gap),
        ) {
            item { Text(pluralStringResource(R.plurals.share_title, count, count), style = MaterialTheme.typography.headlineSmall) }
            if (count == 0) {
                item { Text(stringResource(R.string.share_nothing)) }
            } else if (computers.isEmpty()) {
                item { Text(stringResource(R.string.share_no_computer), color = MaterialTheme.colorScheme.onSurfaceVariant) }
            }
            items(computers, key = { it.deviceId.toString() }) { member ->
                ListItem(
                    headlineContent = { Text(member.name) },
                    leadingContent = {
                        Icon(if (member.platform == Platform.Windows) Icons.Filled.DesktopWindows else Icons.Filled.Computer, contentDescription = null)
                    },
                    trailingContent = { TextButton(onClick = { onPick(member) }) { Text(stringResource(R.string.share_send)) } },
                )
            }
            items(sends, key = { it.listKey }) { send -> Outgoing(send) }
            item { TextButton(onClick = onDone) { Text(stringResource(R.string.share_done)) } }
        }
    }
}

/** How a send from this phone goes. */
@Composable
internal fun Outgoing(send: OutgoingSend) {
    val text = when (send.outcome) {
        null -> stringResource(R.string.share_sending, send.to)
        SendOutcome.Received -> stringResource(R.string.share_received, send.to)
        SendOutcome.Declined -> stringResource(R.string.share_declined, send.to)
        SendOutcome.Failed -> stringResource(R.string.share_failed, send.to)
    }
    Text(text, style = MaterialTheme.typography.bodyMedium)
}
