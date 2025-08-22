// DeviceScanScreen.kt
package com.example.esp32scanner.ui

import android.bluetooth.BluetoothDevice
import android.content.pm.PackageManager
import android.os.Build
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp

@Composable
fun DeviceScanScreen(
    scannedDevices: List<BluetoothDevice>,
    scanning: Boolean,
    onScanClick: () -> Unit,
    onDeviceClick: (BluetoothDevice) -> Unit
) {
    Column(modifier = Modifier.fillMaxSize().padding(16.dp)) {
        // Simpele "AppBar"
        Text(
            text = "ESP32 Scanner",
            style = MaterialTheme.typography.headlineMedium,
            modifier = Modifier.padding(bottom = 16.dp)
        )

        Button(
            onClick = onScanClick,
            modifier = Modifier.fillMaxWidth()
        ) {
            Text(if (scanning) "Scanning..." else "Scan")
        }

        Spacer(modifier = Modifier.height(16.dp))

        LazyColumn {
            items(scannedDevices) { device ->
                DeviceItem(device = device, onClick = { onDeviceClick(device) })
            }
        }
    }
}

@Composable
fun DeviceItem(device: BluetoothDevice, onClick: () -> Unit) {
    val context = LocalContext.current
    val hasPermission = Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
            (context.checkSelfPermission(android.Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED)

    val deviceName = if (hasPermission) device.name ?: "Onbekend apparaat" else "Permission nodig"
    val deviceAddress = if (hasPermission) device.address else "Permission nodig"

    Card(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp)
            .clickable { onClick() },
        elevation = CardDefaults.cardElevation(defaultElevation = 4.dp)
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text(text = deviceName, style = MaterialTheme.typography.bodyLarge)
            Text(text = deviceAddress, style = MaterialTheme.typography.bodySmall)
        }
    }
}



