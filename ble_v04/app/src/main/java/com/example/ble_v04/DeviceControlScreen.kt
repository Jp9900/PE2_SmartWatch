package com.example.ble_v04

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp

@Composable
fun DeviceControlScreen(
    connectionState: String,
    characteristicData: String
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp)
    ) {
        Text(
            text = "Bluetooth Verbinding",
            style = MaterialTheme.typography.headlineSmall
        )

        Spacer(modifier = Modifier.height(16.dp))

        Text(
            text = "Status: $connectionState",
            style = MaterialTheme.typography.bodyLarge
        )

        Spacer(modifier = Modifier.height(24.dp))

        Text(
            text = "Gelezen data:",
            style = MaterialTheme.typography.titleSmall
        )

        Text(
            text = characteristicData.ifEmpty { "Nog geen data ontvangen." },
            modifier = Modifier
                .fillMaxWidth()
                .padding(top = 8.dp)
                .background(Color(0xFFE0E0E0))
                .padding(12.dp),
            style = MaterialTheme.typography.bodyMedium
        )
    }
}




