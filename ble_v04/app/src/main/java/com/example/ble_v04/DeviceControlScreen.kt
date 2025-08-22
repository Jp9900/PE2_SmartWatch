package com.example.ble_v04

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.text.BasicText
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
fun DeviceControlScreen(
    connectionState: String,
    characteristicData: String
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(20.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Card(
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer),
            modifier = Modifier.fillMaxWidth()
        ) {
            Box(modifier = Modifier.padding(16.dp), contentAlignment = Alignment.Center) {
                Text(
                    text = "Status: $connectionState",
                    fontSize = 20.sp,
                    color = MaterialTheme.colorScheme.onPrimaryContainer
                )
            }
        }

        if (characteristicData.isNotEmpty() && characteristicData.contains("Battery")) {
            val parts = characteristicData.split("Battery: ", " %, Time: ", ":")
                .filter { it.isNotEmpty() }
            val battery = parts.getOrNull(0) ?: "N/A"
            val hours = parts.getOrNull(1) ?: "N/A"
            val minutes = parts.getOrNull(2) ?: "N/A"

            Card(
                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.secondaryContainer),
                modifier = Modifier.fillMaxWidth()
            ) {
                Column(
                    modifier = Modifier.padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    Text(text = "Battery: $battery %", fontSize = 18.sp)
                    Text(text = "Hours: $hours", fontSize = 18.sp)
                    Text(text = "Minutes: $minutes", fontSize = 18.sp)
                }
            }
        } else {
            Card(
                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.secondaryContainer),
                modifier = Modifier.fillMaxWidth()
            ) {
                Box(modifier = Modifier.padding(16.dp), contentAlignment = Alignment.Center) {
                    Text(
                        text = "Geen data ontvangen",
                        fontSize = 18.sp
                    )
                }
            }
        }
    }
}




