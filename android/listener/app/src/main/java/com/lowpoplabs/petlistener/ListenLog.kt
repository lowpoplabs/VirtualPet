package com.lowpoplabs.petlistener

import android.content.Context
import android.os.BatteryManager
import android.os.PowerManager
import java.io.File
import java.io.FileWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/*
 * The log: one row a minute saying how much was heard, and how the phone
 * was at the time. Counts only. Addresses are held in memory for a minute,
 * to count each device once, and are never written; names are not kept at
 * all.
 *
 * A minute in which nothing at all reached the app has no row. A gap in
 * the time column is itself the finding: the phone was not letting the app
 * hear anything.
 *
 * A second file is written only when "names" is switched on, for tuning:
 * one row the first time each network or device is heard after starting,
 * with its name and what it says of itself, in the manner of the pet's own
 * Dev log, so the two can be laid side by side. Addresses are still not
 * written. That file holds real names and is private: it is not for
 * sharing, and is to be deleted when the tuning is done.
 */
object ListenLog {
    const val FILE_NAME = "pet-listen.csv"
    private const val HEADER =
        "time,screen,idle,battery,ble_filtered_adverts,ble_filtered_devices,ble_open_adverts," +
            "ble_open_devices,ble_named,wifi_events,wifi_fresh,wifi_networks,wifi_asked,wifi_ask_ok,note\n"

    const val DEVICES_NAME = "pet-devices.csv"
    private const val DEVICES_HEADER = "time,kind,screen,idle,rssi,name,detail\n"

    private val lock = Any()
    private var file: File? = null
    private var devices: File? = null
    private val met = HashSet<String>()
    private var app: Context? = null
    private var minute = -1L
    private var screenAtStart = false
    private var minuteRow = false

    // This minute's tallies.
    private var filteredAdverts = 0
    private var openAdverts = 0
    private val filteredDevices = HashSet<String>()
    private val openDevices = HashSet<String>()
    private val named = HashSet<String>()
    private val trials = java.util.TreeMap<String, HashSet<String>>()
    private var wifiEvents = 0
    private var wifiFresh = 0
    private var wifiNetworks = 0
    private var wifiAsked = 0
    private var wifiAskOk = 0

    // For the screen.
    @Volatile var rows = 0; private set
    @Volatile var lastRow = ""; private set
    @Volatile var running = false
    @Volatile var names = false; private set
    @Volatile var deviceRows = 0; private set

    fun file(context: Context): File {
        val dir = context.getExternalFilesDir(null) ?: context.filesDir
        return File(dir, FILE_NAME)
    }

    fun devicesFile(context: Context): File {
        val dir = context.getExternalFilesDir(null) ?: context.filesDir
        return File(dir, DEVICES_NAME)
    }

    /** Turns the private names file on or off for this run. Call after open(). */
    fun names(on: Boolean) = synchronized(lock) {
        names = on
        met.clear()
        deviceRows = 0
        devices = null
        val c = app
        if (on && c != null) {
            val f = devicesFile(c)
            if (!f.exists() || f.length() == 0L) {
                f.writeText(DEVICES_HEADER)
            }
            devices = f
        }
    }

    /**
     * A network or device, written the first time it is heard in this run and only when names are on.
     * `key` tells one from another (an address); it is kept in memory and never written.
     */
    fun device(kind: String, key: String, rssi: Int, name: String?, detail: String) = synchronized(lock) {
        val f = devices ?: return@synchronized
        if (!met.add("$kind $key")) {
            return@synchronized
        }
        val c = app
        val power = c?.getSystemService(PowerManager::class.java)
        val screen = if (power?.isInteractive == true) "on" else "off"
        val idle = if (power?.isDeviceIdleMode == true) 1 else 0
        // Some devices pad their names with zero bytes or other control characters, which break a CSV reader.
        val clean = (name ?: "").replace(Regex("[\\u0000-\\u001F\\u007F]"), " ").trim().replace("\"", "\"\"")
        val row = stamp(System.currentTimeMillis()) + "," + kind + "," + screen + "," + idle + "," + rssi +
            ",\"" + clean + "\"," + detail.replace(',', ';') + "\n"
        try {
            FileWriter(f, true).use { it.write(row) }
            deviceRows++
        } catch (e: Exception) {
            // A full or missing disk must not stop the listening.
        }
    }

    fun open(context: Context) = synchronized(lock) {
        app = context.applicationContext
        val f = file(context)
        if (!f.exists() || f.length() == 0L) {
            f.writeText(HEADER)
        }
        file = f
        rows = (f.readLines().size - 1).coerceAtLeast(0)
        minute = System.currentTimeMillis() / 60000
        screenAtStart = screenOn()
    }

    /** Heard over Bluetooth. `open` is the scan with no filters, which the phone pauses when the screen is off. */
    fun ble(address: String, hasName: Boolean, open: Boolean) = synchronized(lock) {
        roll()
        if (open) {
            openAdverts++
            openDevices.add(address)
        } else {
            filteredAdverts++
            filteredDevices.add(address)
        }
        if (hasName) {
            named.add(address)
        }
    }

    /** Heard by a filter that is on trial. Counted per minute in the note column, as name=devices. */
    fun trial(name: String, address: String) = synchronized(lock) {
        roll()
        trials.getOrPut(name) { HashSet() }.add(address)
    }

    /** The phone finished a Wi-Fi scan, ours or anyone's. `fresh` is false when it only re-reported old results. */
    fun wifi(fresh: Boolean, networks: Int) = synchronized(lock) {
        roll()
        wifiEvents++
        if (fresh) {
            wifiFresh++
        }
        if (networks > wifiNetworks) {
            wifiNetworks = networks
        }
    }

    /** We asked for a Wi-Fi scan; `ok` is whether the phone agreed to do one. */
    fun wifiAsked(ok: Boolean) = synchronized(lock) {
        roll()
        wifiAsked++
        if (ok) {
            wifiAskOk++
        }
    }

    /** Something worth a line of its own: started, stopped, a scan refused. */
    fun note(text: String) = synchronized(lock) {
        roll()
        write(stamp(System.currentTimeMillis()) + "," + state() + ",,,,,,,,,,," + text.replace(',', ';') + "\n")
    }

    fun close() = synchronized(lock) {
        flush(minute)
        minute = -1
    }

    // Moves on to the current minute, writing the one just ended if anything was heard in it.
    private fun roll() {
        val now = System.currentTimeMillis() / 60000
        if (minute < 0) {
            minute = now
        }
        if (now != minute) {
            flush(minute)
            minute = now
            screenAtStart = screenOn()
        }
    }

    private fun screenOn(): Boolean =
        app?.getSystemService(PowerManager::class.java)?.isInteractive ?: false

    private fun flush(of: Long) {
        val any = filteredAdverts + openAdverts + wifiEvents + wifiAsked + trials.size > 0
        if (any && of >= 0) {
            minuteRow = true
            val was = state()
            minuteRow = false
            val row = stamp(of * 60000) + "," + was + "," + filteredAdverts + "," + filteredDevices.size + "," +
                openAdverts + "," + openDevices.size + "," + named.size + "," + wifiEvents + "," + wifiFresh + "," +
                wifiNetworks + "," + wifiAsked + "," + wifiAskOk + "," +
                trials.entries.joinToString(" ") { it.key + "=" + it.value.size } + "\n"
            write(row)
            lastRow = "Bluetooth " + filteredDevices.size + " filtered, " + openDevices.size + " open; Wi-Fi " +
                wifiNetworks + " networks in " + wifiEvents + " scans"
        }
        filteredAdverts = 0
        openAdverts = 0
        filteredDevices.clear()
        openDevices.clear()
        named.clear()
        trials.clear()
        wifiEvents = 0
        wifiFresh = 0
        wifiNetworks = 0
        wifiAsked = 0
        wifiAskOk = 0
    }

    private fun write(row: String) {
        val f = file ?: return
        try {
            FileWriter(f, true).use { it.write(row) }
            rows++
        } catch (e: Exception) {
            // A full or missing disk must not stop the listening.
        }
    }

    // screen on/off, deep idle (Doze) or not, and the battery, as they are right now.
    private fun state(): String {
        val c = app ?: return ",,"
        val power = c.getSystemService(PowerManager::class.java)
        val battery = c.getSystemService(BatteryManager::class.java)
        val now = power.isInteractive
        // A minute's row is written when the minute ends, so say what the screen did across it.
        val screen = when {
            !minuteRow -> if (now) "on" else "off"
            now && screenAtStart -> "on"
            !now && !screenAtStart -> "off"
            else -> "mixed"
        }
        val idle = if (power.isDeviceIdleMode) 1 else 0
        val level = battery.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY)
        return "$screen,$idle,$level"
    }

    private fun stamp(ms: Long): String =
        SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(Date(ms))
}
