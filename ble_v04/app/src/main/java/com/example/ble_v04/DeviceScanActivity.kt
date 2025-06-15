package com.example.ble_v04

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.core.content.ContextCompat

class DeviceScanActivity : ComponentActivity() {

    private val TAG = "DeviceScanActivity"

    private val bluetoothManager by lazy {
        getSystemService(BLUETOOTH_SERVICE) as BluetoothManager
    }
    private val bluetoothAdapter by lazy {
        bluetoothManager.adapter
    }
    private val bluetoothLeScanner by lazy {
        bluetoothAdapter.bluetoothLeScanner
    }

    private val scanning = mutableStateOf(false)
    private val scannedDevices = mutableStateListOf<BluetoothDevice>()

    private lateinit var permissionLauncher: ActivityResultLauncher<Array<String>>

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val device = result.device
            if (!scannedDevices.any { it.address == device.address }) {
                scannedDevices.add(device)
            }
        }

        override fun onScanFailed(errorCode: Int) {
            Log.e(TAG, "Scan failed with error code: $errorCode")
            scanning.value = false
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        permissionLauncher = registerForActivityResult(
            ActivityResultContracts.RequestMultiplePermissions()
        ) { permissions ->
            val allGranted = permissions.entries.all { it.value }
            if (allGranted) {
                startScan()
            } else {
                Log.w(TAG, "Required Bluetooth permissions not granted.")
            }
        }

        setContent {
            DeviceScanScreen(
                devices = scannedDevices,
                isScanning = scanning.value,
                onStartScan = { checkPermissionsAndStartScan() },
                onStopScan = { stopScan() },
                onDeviceClick = { device ->
                    val intent = Intent(this, DeviceControlActivity::class.java).apply {
                        putExtra("device_address", device.address)
                    }
                    startActivity(intent)
                }
            )
        }
    }

    private fun checkPermissionsAndStartScan() {
        val requiredPermissions = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }

        val allGranted = requiredPermissions.all {
            ContextCompat.checkSelfPermission(this, it) == PackageManager.PERMISSION_GRANTED
        }

        if (allGranted) {
            startScan()
        } else {
            permissionLauncher.launch(requiredPermissions)
        }
    }

    @SuppressLint("MissingPermission")
    private fun startScan() {
        if (scanning.value) return

        // Safety check vlak vóór starten scan:
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_SCAN) != PackageManager.PERMISSION_GRANTED) {
                Log.w(TAG, "BLUETOOTH_SCAN permission missing, cannot start scan")
                return
            }
        }

        scannedDevices.clear()

        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()

        bluetoothLeScanner.startScan(null, settings, scanCallback)
        scanning.value = true
    }

    @SuppressLint("MissingPermission")
    private fun stopScan() {
        if (!scanning.value) return

        bluetoothLeScanner.stopScan(scanCallback)
        scanning.value = false
    }

    override fun onPause() {
        super.onPause()
        stopScan()
    }
}




