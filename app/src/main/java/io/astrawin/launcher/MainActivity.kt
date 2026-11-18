package io.astrawin.launcher

import android.app.Activity
import android.content.Intent
import android.database.Cursor
import android.graphics.Color
import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import java.io.ByteArrayOutputStream

class MainActivity : Activity() {
    private external fun nativeGetEngineInfo(): String
    private external fun nativeRunSelfTest(): String
    private external fun nativeInspectPe(headerBytes: ByteArray): String

    private lateinit var executableInfo: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            setPadding(28, 36, 28, 28)
            setBackgroundColor(Color.rgb(13, 18, 31))
        }

        val title = TextView(this).apply {
            text = "ASTRAWIN"
            textSize = 30f
            setTextColor(Color.WHITE)
            gravity = Gravity.CENTER
        }

        val subtitle = TextView(this).apply {
            text = "Experimental Windows executable compatibility project"
            textSize = 14f
            setTextColor(Color.rgb(170, 184, 207))
            gravity = Gravity.CENTER
            setPadding(0, 8, 0, 24)
        }

        val engineInfo = TextView(this).apply {
            text = nativeGetEngineInfo()
            textSize = 14f
            setTextColor(Color.rgb(220, 229, 244))
            setPadding(16, 16, 16, 16)
            setBackgroundColor(Color.rgb(25, 34, 54))
        }

        val selectExecutableButton = Button(this).apply {
            text = "Choose Windows EXE to inspect"
            setOnClickListener {
                val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                    addCategory(Intent.CATEGORY_OPENABLE)
                    type = "*/*"
                }
                try {
                    startActivityForResult(intent, REQUEST_EXECUTABLE)
                } catch (exception: Exception) {
                    executableInfo.text = "Unable to open the file picker: ${exception.message}"
                }
            }
        }

        executableInfo = TextView(this).apply {
            text = "No executable selected. This tool currently inspects PE headers only; it does not run Windows programs."
            textSize = 14f
            setTextColor(Color.rgb(220, 229, 244))
            setPadding(4, 16, 4, 16)
        }

        val result = TextView(this).apply {
            text = "Native self-test has not been run."
            textSize = 14f
            setTextColor(Color.rgb(220, 229, 244))
            setPadding(4, 20, 4, 20)
        }

        val testButton = Button(this).apply {
            text = "Run native CPU self-test"
            setOnClickListener {
                result.text = nativeRunSelfTest()
            }
        }

        content.addView(title)
        content.addView(subtitle)
        content.addView(engineInfo)
        content.addView(selectExecutableButton)
        content.addView(executableInfo)
        content.addView(testButton)
        content.addView(result)

        ScrollView(this).apply {
            addView(content)
        }.also { setContentView(it) }
    }

    @Deprecated("Use the document picker result callback when migrating to ComponentActivity.")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != REQUEST_EXECUTABLE || resultCode != RESULT_OK) {
            return
        }

        val uri = data?.data
        if (uri == null) {
            executableInfo.text = "No file was returned by the picker."
            return
        }

        val displayName = queryDisplayName(uri)
        executableInfo.text = inspectExecutable(uri, displayName)
    }

    private fun queryDisplayName(uri: Uri): String {
        var cursor: Cursor? = null
        return try {
            cursor = contentResolver.query(
                uri,
                arrayOf(OpenableColumns.DISPLAY_NAME),
                null,
                null,
                null
            )
            if (cursor != null && cursor.moveToFirst()) {
                val column = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                if (column >= 0) cursor.getString(column) ?: "Selected file" else "Selected file"
            } else {
                "Selected file"
            }
        } catch (_: Exception) {
            "Selected file"
        } finally {
            cursor?.close()
        }
    }

    private fun inspectExecutable(uri: Uri, displayName: String): String {
        return try {
            val headerBytes = contentResolver.openInputStream(uri)?.use { input ->
                val output = ByteArrayOutputStream()
                val buffer = ByteArray(8192)
                while (output.size() < MAX_HEADER_BYTES) {
                    val remaining = MAX_HEADER_BYTES - output.size()
                    val count = input.read(buffer, 0, minOf(buffer.size, remaining))
                    if (count <= 0) break
                    output.write(buffer, 0, count)
                }
                output.toByteArray()
            } ?: return "Cannot read: $displayName"

            "File: $displayName\n" +
                "Bytes inspected: ${headerBytes.size} (maximum $MAX_HEADER_BYTES)\n\n" +
                nativeInspectPe(headerBytes) +
                "\n\nHeader inspection only. Windows execution is not implemented yet."
        } catch (exception: Exception) {
            "Could not inspect $displayName: ${exception.message}"
        }
    }

    companion object {
        private const val REQUEST_EXECUTABLE = 1001
        private const val MAX_HEADER_BYTES = 1024 * 1024

        init {
            System.loadLibrary("astrawin_native")
        }
    }
}
