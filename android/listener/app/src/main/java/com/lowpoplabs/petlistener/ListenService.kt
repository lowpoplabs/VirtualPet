package com.lowpoplabs.petlistener

import android.Manifest
import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.bluetooth.BluetoothManager
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.net.wifi.WifiManager
import android.os.Build
import android.os.IBinder
import android.os.ParcelUuid
import android.os.SystemClock

/*
 * Listens, and nothing else: it never connects to, pairs with or answers
 * anything it hears.
 *
 * Two Bluetooth scans run side by side. The "open" one asks for
 * everything, and Android pauses it whenever the screen is off. The
 * "filtered" one names what it is after, which is what Android wants from
 * a scan that is to go on with the screen off. Comparing the two in the
 * log says how much the filters miss while the screen is on, and how much
 * still arrives once it is off.
 *
 * The first day's filters were a list of makers and services, and caught
 * a third of what the open scan did. Filters that name a kind of field
 * (Android 13's setAdvertisingDataType) matched everything with the
 * screen on and nothing at all with it off on a Galaxy S22, so they are
 * no use here. So did a service filter masked down to nothing ("any
 * service at all"), and one fixing only the long part that every short
 * registered service shares heard nothing even with the screen on.
 *
 * What does work is a pair of service filters that each fix a single
 * bit: one wants it set, the other clear, so between them they match any
 * device that lists a service, and with the screen off they heard five or
 * six devices where the list of makers heard two. They are part of the
 * filtered scan now. A device that lists no service and is not from a
 * listed maker is still missed.
 *
 * A longer list of makers runs alongside as a "trial", a scan of its own
 * counted in the note column of the log, to see whether it is worth
 * adding.
 *
 * Wi-Fi is heard two ways: whenever the phone finishes a scan for its own
 * reasons, and by asking for one every half minute. Asking is
 * rationed by Android, so the log notes whether each request was granted.
 */
class ListenService : Service() {

    companion object {
        const val EXTRA_WIFI = "wifi"
        const val EXTRA_NAMES = "names"
        private const val CHANNEL = "listening"
        /*
         * Android lets an app with a service in view ask four times in two minutes.
         * Every two minutes was too seldom on a drive: the pet, asking every half
         * minute, heard fifty named networks this missed. A second over the half
         * minute keeps a fifth ask from ever landing inside the two.
         */
        private const val WIFI_ASK_EVERY_MS = 31 * 1000L
        /* Android quietly weakens a scan left running for 30 minutes. */
        private const val BLE_RESTART_EVERY_MS = 25 * 60 * 1000L

        /* Bluetooth company numbers of makers whose devices are everywhere. */
        private val MAKERS = intArrayOf(
            0x004C,     // Apple
            0x0075,     // Samsung
            0x0006,     // Microsoft
            0x00E0,     // Google
            0x0087,     // Garmin
            0x009E,     // Bose
            0x012D,     // Sony
        )

        /* More makers, tried on their own first: chip makers turn up in other people's products. */
        private val MORE_MAKERS = intArrayOf(
            0x0002,     // Intel: laptops
            0x000A,     // Qualcomm (was CSR): headsets, speakers
            0x000D,     // Texas Instruments
            0x000F,     // Broadcom
            0x001D,     // Qualcomm
            0x0057,     // Harman: JBL and car audio
            0x0059,     // Nordic Semiconductor: trackers, sensors, tools
            0x005D,     // Realtek
            0x006B,     // Polar
            0x00C4,     // LG
            0x0171,     // Amazon
            0x01AB,     // Meta
            0x01DA,     // Logitech
            0x027D,     // Huawei
            0x02E5,     // Espressif: home-made and smart-home things
            0x038F,     // Xiaomi
            0x05A7,     // Sonos
        )

        /* Sixteen-bit services that common devices announce. */
        private val SERVICES = intArrayOf(
            0xFE2C,     // Google Fast Pair: earbuds, headphones
            0xFD5A,     // Samsung trackers
            0xFEED,     // Tile trackers
            0xFD6F,     // exposure notification, still sent by some phones
            0xFEAA,     // Eddystone beacons
            0xFEF3,     // Google: Android phones nearby, which the pet eats as phones
            0xFCF1,     // Google: also sent by Android things, crumbs to the pet
            0x180F,     // battery service
            0x180D,     // heart rate: watches and straps
            0x1812,     // keyboards, mice, remotes
        )
    }

    private var scanner: BluetoothLeScanner? = null
    private var wifi: WifiManager? = null
    private var wifiOn = false
    private var lastWifiAsk = 0L
    private var bleStarted = 0L
    private var started = false

    private val filtered = Callback(open = false)
    private val open = Callback(open = true)
    private val moreMakers = Trial("moremakers")

    /* A way of filtering on trial: only counted, device by device. */
    private inner class Trial(val name: String) : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            ListenLog.trial(name, result.device.address)
        }

        override fun onBatchScanResults(results: List<ScanResult>) {
            for (r in results) {
                ListenLog.trial(name, r.device.address)
            }
        }

        override fun onScanFailed(errorCode: Int) {
            ListenLog.note("$name trial scan failed: code $errorCode")
        }
    }

    private inner class Callback(val open: Boolean) : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            heard(result)
            if (!open) {
                heartbeat()
            }
        }

        override fun onBatchScanResults(results: List<ScanResult>) {
            for (r in results) {
                heard(r)
            }
        }

        private fun heard(r: ScanResult) {
            val record = r.scanRecord
            ListenLog.ble(r.device.address, record?.deviceName != null, open)
            if (ListenLog.names) {
                ListenLog.device("ble", r.device.address, r.rssi, record?.deviceName, describe(r) + (if (open) " via=open" else " via=filter"))
            }
        }

        override fun onScanFailed(errorCode: Int) {
            ListenLog.note((if (open) "open" else "filtered") + " bluetooth scan failed: code " + errorCode)
        }
    }

    /*
     * What a device says of itself, as the pet's Dev log writes it: its
     * maker's number and the first byte of what the maker sent, then the
     * first service it lists. "co=004C/10 u=FEF3".
     */
    private fun describe(r: ScanResult): String {
        val record = r.scanRecord ?: return ""
        val out = StringBuilder()
        val makers = record.manufacturerSpecificData
        if (makers != null && makers.size() > 0) {
            val data = makers.valueAt(0)
            out.append(String.format("co=%04X/", makers.keyAt(0)))
            out.append(if (data != null && data.isNotEmpty()) String.format("%02X", data[0].toInt() and 0xFF) else "--")
        }
        var service = record.serviceUuids?.firstOrNull()
        if (service == null) {
            service = record.serviceData?.keys?.firstOrNull()
        }
        if (service != null) {
            val text = service.uuid.toString().uppercase()
            val short = text.endsWith("-0000-1000-8000-00805F9B34FB") && text.startsWith("0000")
            if (out.isNotEmpty()) {
                out.append(' ')
            }
            out.append("u=").append(if (short) text.substring(4, 8) else text.substring(0, 8) + "..")
        }
        return out.toString()
    }

    /* A network: how it is locked, its channel and its band. The pet hears the 2.4 band only. */
    @Suppress("DEPRECATION")
    private fun describe(n: android.net.wifi.ScanResult): String {
        val caps = n.capabilities ?: ""
        val lock = when {
            caps.contains("SAE") && caps.contains("PSK") -> "wpa2/3"
            caps.contains("SAE") -> "wpa3"
            caps.contains("EAP") -> "eap"
            caps.contains("PSK") -> "wpa2"
            caps.contains("WEP") -> "wep"
            caps.contains("OWE") -> "owe"
            else -> "open"
        }
        val f = n.frequency
        val band = when {
            f < 3000 -> "2.4"
            f < 5925 -> "5"
            else -> "6"
        }
        val channel = when {
            f == 2484 -> 14
            f < 3000 -> (f - 2407) / 5
            f < 5925 -> (f - 5000) / 5
            else -> (f - 5950) / 5
        }
        return "sec=$lock ch=$channel band=$band"
    }

    private val wifiResults = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val fresh = intent.getBooleanExtra(WifiManager.EXTRA_RESULTS_UPDATED, false)
            val count = try {
                @SuppressLint("MissingPermission")
                val results = wifi?.scanResults ?: emptyList()
                if (ListenLog.names) {
                    for (n in results) {
                        @Suppress("DEPRECATION")
                        ListenLog.device("wifi", n.BSSID ?: "", n.level, n.SSID, describe(n))
                    }
                }
                results.map { it.BSSID }.toSet().size
            } catch (e: SecurityException) {
                -1      // location permission taken away, or Location switched off
            }
            ListenLog.wifi(fresh, count)
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (started) {
            return START_STICKY
        }
        wifiOn = (intent?.getBooleanExtra(EXTRA_WIFI, false) ?: false) &&
            checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED

        // Android 14 wants the kind of service stated, and the matching permission already granted.
        var type = ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
        if (wifiOn) {
            type = type or ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION
        }
        try {
            startForeground(1, notification(), type)
        } catch (e: Exception) {
            ListenLog.open(this)
            ListenLog.note("could not start: " + e.javaClass.simpleName)
            stopSelf()
            return START_NOT_STICKY
        }

        started = true
        ListenLog.open(this)
        ListenLog.running = true
        val names = intent?.getBooleanExtra(EXTRA_NAMES, false) ?: false
        ListenLog.names(names)
        ListenLog.note(
            "started: wifi " + (if (wifiOn) "on" else "off") + "; names " + (if (names) "on" else "off") +
                "; android " + Build.VERSION.RELEASE +
                "; " + Build.MANUFACTURER + " " + Build.MODEL
        )
        startBluetooth()
        if (wifiOn) {
            wifi = applicationContext.getSystemService(WifiManager::class.java)
            val filter = IntentFilter(WifiManager.SCAN_RESULTS_AVAILABLE_ACTION)
            if (Build.VERSION.SDK_INT >= 33) {
                registerReceiver(wifiResults, filter, Context.RECEIVER_NOT_EXPORTED)
            } else {
                registerReceiver(wifiResults, filter)
            }
            askWifi()
        }
        return START_STICKY
    }

    override fun onDestroy() {
        if (started) {
            stopBluetooth()
            if (wifiOn) {
                try {
                    unregisterReceiver(wifiResults)
                } catch (e: IllegalArgumentException) {
                    // never registered
                }
            }
            ListenLog.note("stopped")
            ListenLog.close()
            ListenLog.running = false
        }
        super.onDestroy()
    }

    @SuppressLint("MissingPermission")
    private fun startBluetooth() {
        val adapter = getSystemService(BluetoothManager::class.java)?.adapter
        val s = adapter?.bluetoothLeScanner
        if (adapter == null || !adapter.isEnabled || s == null) {
            ListenLog.note("bluetooth is off")
            return
        }
        scanner = s
        val gentle = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_POWER).build()
        try {
            s.startScan(filters(), gentle, filtered)
            s.startScan(null, gentle, open)
            bleStarted = SystemClock.elapsedRealtime()
        } catch (e: Exception) {
            ListenLog.note("bluetooth scan refused: " + e.javaClass.simpleName)
            return
        }
        try {
            val more = ArrayList<ScanFilter>()
            for (maker in MORE_MAKERS) {
                more.add(ScanFilter.Builder().setManufacturerData(maker, ByteArray(0)).build())
            }
            s.startScan(more, gentle, moreMakers)
        } catch (e: Exception) {
            ListenLog.note("trial scan refused: " + e.javaClass.simpleName)
        }
    }

    private fun filters(): List<ScanFilter> {
        val filters = ArrayList<ScanFilter>()
        // Any service at all, as two filters on one bit of it. A single filter masked to nothing stops with the screen off.
        val bit = ParcelUuid.fromString("00000001-0000-0000-0000-000000000000")
        val clear = ParcelUuid.fromString("00000000-0000-0000-0000-000000000000")
        filters.add(ScanFilter.Builder().setServiceUuid(bit, bit).build())
        filters.add(ScanFilter.Builder().setServiceUuid(clear, bit).build())
        for (maker in MAKERS) {
            filters.add(ScanFilter.Builder().setManufacturerData(maker, ByteArray(0)).build())
        }
        for (service in SERVICES) {
            val uuid = ParcelUuid.fromString(String.format("0000%04x-0000-1000-8000-00805f9b34fb", service))
            filters.add(ScanFilter.Builder().setServiceUuid(uuid).build())
            filters.add(ScanFilter.Builder().setServiceData(uuid, ByteArray(0)).build())
        }
        return filters
    }

    @SuppressLint("MissingPermission")
    private fun stopBluetooth() {
        try {
            scanner?.stopScan(filtered)
            scanner?.stopScan(open)
            scanner?.stopScan(moreMakers)
        } catch (e: Exception) {
            // Bluetooth was switched off under us
        }
    }

    /*
     * Runs on every filtered Bluetooth result. Those arrive even while the
     * phone sleeps, which an ordinary timer does not, so the jobs that must
     * go on with the screen off hang from here.
     */
    private fun heartbeat() {
        val now = SystemClock.elapsedRealtime()
        if (wifiOn && now - lastWifiAsk >= WIFI_ASK_EVERY_MS) {
            askWifi()
        }
        if (now - bleStarted >= BLE_RESTART_EVERY_MS) {
            bleStarted = now
            stopBluetooth()
            startBluetooth()
            ListenLog.note("bluetooth scans restarted")
        }
    }

    private fun askWifi() {
        lastWifiAsk = SystemClock.elapsedRealtime()
        val ok = try {
            @Suppress("DEPRECATION")
            wifi?.startScan() ?: false
        } catch (e: SecurityException) {
            false
        }
        ListenLog.wifiAsked(ok)
    }

    private fun notification(): Notification {
        val manager = getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(
            NotificationChannel(CHANNEL, "Listening", NotificationManager.IMPORTANCE_LOW)
        )
        val tap = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE
        )
        return Notification.Builder(this, CHANNEL)
            .setContentTitle("Pet Listener is listening")
            .setContentText(
                if (ListenLog.names) "Writing down nearby Wi-Fi and Bluetooth, with names. Nothing is connected to."
                else "Counting nearby Wi-Fi and Bluetooth. Nothing is connected to."
            )
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setContentIntent(tap)
            .setOngoing(true)
            .build()
    }
}
