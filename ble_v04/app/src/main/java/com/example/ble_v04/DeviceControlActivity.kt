package com.example.ble_v04

import android.Manifest
import android.bluetooth.*
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.mutableStateOf
import androidx.core.content.ContextCompat
import java.util.UUID

class DeviceControlActivity : ComponentActivity() {

    private var bluetoothGatt: BluetoothGatt? = null
    private val connectionState = mutableStateOf("Disconnected")
    private val characteristicData = mutableStateOf("")

    private lateinit var permissionLauncher: ActivityResultLauncher<Array<String>>
    private var deviceToConnect: BluetoothDevice? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        permissionLauncher = registerForActivityResult(
            ActivityResultContracts.RequestMultiplePermissions()
        ) { permissions ->
            val allGranted = permissions.entries.all { it.value }
            if (allGranted && deviceToConnect != null) {
                connectToDevice(deviceToConnect!!)
            } else {
                connectionState.value = "Permissions not granted"
            }
        }

        val deviceAddress = intent.getStringExtra("device_address")
        val bluetoothManager = getSystemService(BLUETOOTH_SERVICE) as BluetoothManager
        val bluetoothAdapter = bluetoothManager.adapter
        val device = deviceAddress?.let { bluetoothAdapter.getRemoteDevice(it) }

        setContent {
            DeviceControlScreen(
                connectionState = connectionState.value,
                characteristicData = characteristicData.value
            )
        }

        if (device != null) {
            checkPermissionsAndConnect(device)
        } else {
            connectionState.value = "Device not found"
        }
    }

    private fun checkPermissionsAndConnect(device: BluetoothDevice) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val requiredPermissions = arrayOf(
                Manifest.permission.BLUETOOTH_CONNECT
            )
            val allGranted = requiredPermissions.all {
                ContextCompat.checkSelfPermission(this, it) == PackageManager.PERMISSION_GRANTED
            }
            if (allGranted) {
                connectToDevice(device)
            } else {
                deviceToConnect = device
                permissionLauncher.launch(requiredPermissions)
            }
        } else {
            connectToDevice(device)
        }
    }

    private fun connectToDevice(device: BluetoothDevice) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT)
                != PackageManager.PERMISSION_GRANTED) {
                connectionState.value = "BLUETOOTH_CONNECT permission missing"
                return
            }
        }
        bluetoothGatt = device.connectGatt(this, false, gattCallback)
    }

    private val gattCallback = object : BluetoothGattCallback() {

        override fun onConnectionStateChange(gatt: BluetoothGatt?, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                connectionState.value = "Connected"
                bluetoothGatt = gatt

                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    if (ContextCompat.checkSelfPermission(this@DeviceControlActivity, Manifest.permission.BLUETOOTH_CONNECT)
                        != PackageManager.PERMISSION_GRANTED) {
                        return
                    }
                }
                bluetoothGatt?.discoverServices()

            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                connectionState.value = "Disconnected"
                bluetoothGatt?.close()
                bluetoothGatt = null
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt?, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS || gatt == null) {
                connectionState.value = "Service discovery failed"
                return
            }

            val batteryServiceUUID = UUID.fromString("0000180F-0000-1000-8000-00805f9b34fb")
            val batteryLevelUUID = UUID.fromString("00002A19-0000-1000-8000-00805f9b34fb")
            val cccdUUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

            val service = gatt.getService(batteryServiceUUID)
            val characteristic = service?.getCharacteristic(batteryLevelUUID)

            if (characteristic != null) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    if (ContextCompat.checkSelfPermission(this@DeviceControlActivity, Manifest.permission.BLUETOOTH_CONNECT)
                        != PackageManager.PERMISSION_GRANTED) {
                        connectionState.value = "BLUETOOTH_CONNECT permission missing"
                        return
                    }
                }

                val success = bluetoothGatt?.setCharacteristicNotification(characteristic, true) ?: false
                if (!success) {
                    connectionState.value = "Notificatie-instelling mislukt"
                    return
                }

                val descriptor = characteristic.getDescriptor(cccdUUID)
                if (descriptor != null) {
                    descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                    val result = bluetoothGatt?.writeDescriptor(descriptor)
                    connectionState.value = if (result == true) "Notificaties ingeschakeld" else "Descriptor schrijven mislukt"
                } else {
                    connectionState.value = "CCCD niet gevonden"
                }
            } else {
                connectionState.value = "Battery characteristic niet gevonden"
            }
        }

        @Suppress("OVERRIDE_DEPRECATION")
        override fun onCharacteristicRead(
            gatt: BluetoothGatt?,
            characteristic: BluetoothGattCharacteristic?,
            status: Int
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS && characteristic != null) {
                val value = characteristic.value
                if (value != null && value.isNotEmpty()) {
                    val percentage = value[0].toInt() and 0xFF
                    characteristicData.value = "$percentage %"
                } else {
                    characteristicData.value = "Geen data"
                }
            }
        }

        @Suppress("OVERRIDE_DEPRECATION")
        override fun onCharacteristicChanged(
            gatt: BluetoothGatt?,
            characteristic: BluetoothGattCharacteristic?
        ) {
            if (characteristic != null) {
                val value = characteristic.value
                if (value != null && value.isNotEmpty()) {
                    val percentage = value[0].toInt() and 0xFF
                    characteristicData.value = "$percentage %"
                } else {
                    characteristicData.value = "Geen data"
                }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT)
                != PackageManager.PERMISSION_GRANTED) {
                return
            }
        }
        bluetoothGatt?.close()
        bluetoothGatt = null
    }
}


