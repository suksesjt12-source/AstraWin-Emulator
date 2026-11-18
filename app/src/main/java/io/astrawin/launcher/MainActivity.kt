package io.astrawin.launcher

import android.app.Activity
import android.graphics.Color
import android.os.Bundle
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

class MainActivity : Activity() {
    private external fun nativeGetEngineInfo(): String
    private external fun nativeRunSelfTest(): String

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
        content.addView(testButton)
        content.addView(result)

        ScrollView(this).apply {
            addView(content)
        }.also { setContentView(it) }
    }

    companion object {
        init {
            System.loadLibrary("astrawin_native")
        }
    }
}
