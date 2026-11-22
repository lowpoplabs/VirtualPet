package com.lowpoplabs.petlistener

import android.Manifest
import android.app.Activity
import android.content.ContentValues
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.provider.MediaStore
import android.view.Gravity
import android.widget.Button
import android.widget.CheckBox
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/* One screen: start, stop, see that it is alive, and save the log where it can be found. */
class MainActivity : Activity() {

    private lateinit var status: TextView
    private lateinit var wifiBox: CheckBox
    private lateinit var namesBox: CheckBox
    private lateinit var startStop: Button
    private val handler = Handler(Looper.getMainLooper())
    private val refresh = object : Runnable {
        override fun run() {
            show()
            handler.postDelayed(this, 1000)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val pad = (20 * resources.displayMetrics.density).toInt()
        val column = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad * 2, pad, pad)
        }

        column.addView(TextView(this).apply {
            text = "Pet Listener"
            textSize = 26f
        })
        column.addView(TextView(this).apply {
            text = "A test for Baandwidth Farms. It counts the Wi-Fi networks and Bluetooth devices " +
                "this phone can hear, minute by minute, to find out how much still gets through " +
                "with the screen off.\n\nIt only listens. It never connects to anything, and it " +
                "writes down counts only: no names, no addresses, no location. Names are written " +
                "only if you tick the last box below."
            textSize = 16f
            setPadding(0, pad / 2, 0, pad)
        })

        wifiBox = CheckBox(this).apply {
            text = "Listen for Wi-Fi too. Android only allows this with the Location permission, " +
                "and with Location switched on. No position is read."
            textSize = 16f
            isChecked = true
        }
        column.addView(wifiBox)

        namesBox = CheckBox(this).apply {
            text = "Names too, for tuning. Writes a second, private file with the name of each " +
                "network and device the first time it is heard. Still no addresses and no " +
                "location. Do not share that file; delete it when the tuning is done."
            textSize = 16f
            isChecked = false
        }
        column.addView(namesBox)

        column.addView(Button(this).apply {
            text = "Delete the names file"
            textSize = 18f
            setOnClickListener { deleteNames() }
        })

        startStop = Button(this).apply {
            textSize = 18f
            setOnClickListener { if (ListenLog.running) stop() else askThenStart() }
        }
        column.addView(startStop)

        column.addView(Button(this).apply {
            text = "Save the log to Downloads"
            textSize = 18f
            setOnClickListener { saveCopy() }
        })

        status = TextView(this).apply {
            textSize = 16f
            gravity = Gravity.START
            setPadding(0, pad, 0, 0)
        }
        column.addView(status)

        setContentView(ScrollView(this).apply { addView(column) })

        // For testing over a cable: "am start ... --ez start true" presses the button, "--ez stop true" the other,
        // and "--ez names true" ticks the names box first.
        if (intent.getBooleanExtra("names", false)) {
            namesBox.isChecked = true
        }
        if (intent.getBooleanExtra("stop", false)) {
            stop()
        } else if (intent.getBooleanExtra("start", false) && !ListenLog.running) {
            askThenStart()
        }
    }

    override fun onResume() {
        super.onResume()
        handler.post(refresh)
    }

    override fun onPause() {
        handler.removeCallbacks(refresh)
        super.onPause()
    }

    private fun show() {
        val on = ListenLog.running
        startStop.text = if (on) "Stop listening" else "Start listening"
        wifiBox.isEnabled = !on
        namesBox.isEnabled = !on
        val file = ListenLog.file(this)
        status.text = buildString {
            append(if (on) "Listening.\n" else "Not listening.\n")
            if (on) {
                append("Rows written: ").append(ListenLog.rows).append('\n')
                if (ListenLog.names) {
                    append("Names written: ").append(ListenLog.deviceRows).append('\n')
                }
                if (ListenLog.lastRow.isNotEmpty()) {
                    append("Last full minute: ").append(ListenLog.lastRow).append('\n')
                }
                append("\nLeave it running, lock the phone and carry on with your day. ")
                append("Come back here to stop it and save the log.")
            } else if (file.exists()) {
                append("A log of ").append(file.length() / 1024).append(" KB is waiting to be saved.")
            }
        }
    }

    private fun askThenStart() {
        val wanted = ArrayList<String>()
        wanted.add(Manifest.permission.BLUETOOTH_SCAN)
        if (Build.VERSION.SDK_INT >= 33) {
            wanted.add(Manifest.permission.POST_NOTIFICATIONS)
        }
        if (wifiBox.isChecked) {
            wanted.add(Manifest.permission.ACCESS_FINE_LOCATION)
        }
        val missing = wanted.filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
        if (missing.isEmpty()) {
            start()
        } else {
            requestPermissions(missing.toTypedArray(), 1)
        }
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<String>, results: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, results)
        if (checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED) {
            start()
        } else {
            Toast.makeText(this, "It cannot listen without the Nearby devices permission.", Toast.LENGTH_LONG).show()
        }
    }

    private fun start() {
        val wifi = wifiBox.isChecked &&
            checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
        if (wifiBox.isChecked && !wifi) {
            Toast.makeText(this, "No Location permission: listening for Bluetooth only.", Toast.LENGTH_LONG).show()
        }
        startForegroundService(
            Intent(this, ListenService::class.java)
                .putExtra(ListenService.EXTRA_WIFI, wifi)
                .putExtra(ListenService.EXTRA_NAMES, namesBox.isChecked)
        )
    }

    private fun stop() {
        stopService(Intent(this, ListenService::class.java))
    }

    private fun deleteNames() {
        if (ListenLog.running && ListenLog.names) {
            Toast.makeText(this, "Stop listening first.", Toast.LENGTH_SHORT).show()
            return
        }
        val file = ListenLog.devicesFile(this)
        val had = file.exists()
        file.delete()
        Toast.makeText(this, if (had) "The names file is deleted." else "There is no names file.", Toast.LENGTH_SHORT).show()
    }

    /* Copies the log into the phone's Downloads folder, where a file manager or a USB cable can reach it. */
    private fun saveCopy() {
        val file = ListenLog.file(this)
        if (!file.exists()) {
            Toast.makeText(this, "There is no log yet.", Toast.LENGTH_SHORT).show()
            return
        }
        val name = "pet-listen-" + SimpleDateFormat("yyyyMMdd-HHmm", Locale.US).format(Date()) + ".csv"
        val values = ContentValues().apply {
            put(MediaStore.Downloads.DISPLAY_NAME, name)
            put(MediaStore.Downloads.MIME_TYPE, "text/csv")
            put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS)
        }
        try {
            val uri = contentResolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
                ?: throw IllegalStateException("no place to save")
            contentResolver.openOutputStream(uri).use { out ->
                file.inputStream().use { it.copyTo(out!!) }
            }
            Toast.makeText(this, "Saved to Downloads as $name", Toast.LENGTH_LONG).show()
        } catch (e: Exception) {
            Toast.makeText(this, "Could not save: " + e.message, Toast.LENGTH_LONG).show()
        }
    }
}
