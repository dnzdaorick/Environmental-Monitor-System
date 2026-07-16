package com.example.environmentalmonitor

import android.Manifest
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Color
import android.os.Build
import android.os.Bundle
import androidx.core.app.ActivityCompat
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.google.gson.annotations.SerializedName
import retrofit2.Call
import retrofit2.Callback
import retrofit2.Response
import retrofit2.Retrofit
import retrofit2.converter.gson.GsonConverterFactory
import retrofit2.http.Body
import retrofit2.http.GET
import retrofit2.http.POST

// Models matching updated per-room thresholds schemas
data class RoomTelemetry(
    @SerializedName("raw_node_id")
    val rawNodeId: String,
    @SerializedName("display_name")
    val displayName: String,
    val temperature: Double,
    val humidity: Double,
    val pressure: Double,
    val threshold: Double
)

data class RoomConfigureRequest(
    @SerializedName("node_id")
    val nodeId: String,
    @SerializedName("custom_name")
    val customName: String,
    @SerializedName("temperature_threshold")
    val temperatureThreshold: Double
)

data class ActionResponse(val status: String)

interface ApiService {
    @GET("api/telemetry/latest")
    fun getLatestTelemetry(): Call<List<RoomTelemetry>> // Processes direct array lists payload formats

    @POST("api/room/configure")
    fun configureRoom(@Body body: RoomConfigureRequest): Call<ActionResponse>
}

class MainActivity : AppCompatActivity() {

    private val localHostIp = "10.0.2.2"
    private lateinit var apiService: ApiService
    private val mainHandler = Handler(Looper.getMainLooper())

    private lateinit var recyclerView: RecyclerView
    private lateinit var roomAdapter: RoomAdapter
    private lateinit var layoutEmptyState: View

    private val CHANNEL_ID = "temp_alerts"
    private val activeHighWarnings = mutableSetOf<String>()

    private val updateRunnable = object : Runnable {
        override fun run() {
            executeFetchTransaction()
            mainHandler.postDelayed(this, 2000)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        createNotificationChannel()
        requestNotificationPermission()

        layoutEmptyState = findViewById(R.id.layoutEmptyState)
        recyclerView = findViewById(R.id.recyclerViewRooms)
        recyclerView.layoutManager = LinearLayoutManager(this)

        roomAdapter = RoomAdapter(ArrayList()) { targetNode -> showConfigureDialog(targetNode) }
        recyclerView.adapter = roomAdapter

        val retrofit = Retrofit.Builder()
            .baseUrl("http://$localHostIp:8000/")
            .addConverterFactory(GsonConverterFactory.create())
            .build()

        apiService = retrofit.create(ApiService::class.java)
    }

    private fun executeFetchTransaction() {
        apiService.getLatestTelemetry().enqueue(object : Callback<List<RoomTelemetry>> {
            override fun onResponse(call: Call<List<RoomTelemetry>>, response: Response<List<RoomTelemetry>>) {
                if (response.isSuccessful && response.body() != null) {
                    val roomList = response.body()!!

                    if (roomList.isEmpty()) {
                        layoutEmptyState.visibility = View.VISIBLE
                        recyclerView.visibility = View.GONE
                    } else {
                        layoutEmptyState.visibility = View.GONE
                        recyclerView.visibility = View.VISIBLE
                        processAnomalies(roomList)
                        roomAdapter.updateData(roomList)
                    }
                } else {
                    Log.e("MONITOR_DEBUG", "Fetch unsuccessful: ${response.code()}")
                }
            }
            override fun onFailure(call: Call<List<RoomTelemetry>>, t: Throwable) {
                Log.e("MONITOR_DEBUG", "Sync frame drop: ", t)
            }
        })
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val name = "Temperature Alerts"
            val descriptionText = "Notifications for temperature anomalies"
            val importance = NotificationManager.IMPORTANCE_HIGH
            val channel = NotificationChannel(CHANNEL_ID, name, importance).apply {
                description = descriptionText
            }
            val notificationManager: NotificationManager =
                getSystemService(NOTIFICATION_SERVICE) as NotificationManager
            notificationManager.createNotificationChannel(channel)
        }
    }

    private fun requestNotificationPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.POST_NOTIFICATIONS), 101)
            }
        }
    }

    private fun sendNotification(title: String, message: String, id: Int) {
        val builder = NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.ic_dialog_alert)
            .setContentTitle(title)
            .setContentText(message)
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setAutoCancel(true)

        val notificationManager: NotificationManager =
            getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        notificationManager.notify(id, builder.build())
    }

    private fun processAnomalies(rooms: List<RoomTelemetry>) {
        rooms.forEach { room ->
            // High Temp Check
            if (room.temperature > room.threshold) {
                if (!activeHighWarnings.contains(room.rawNodeId)) {
                    sendNotification("High Temperature Alert", "${room.displayName} is at %.1f°C".format(room.temperature), room.rawNodeId.hashCode())
                    activeHighWarnings.add(room.rawNodeId)
                }
            } else {
                activeHighWarnings.remove(room.rawNodeId)
            }
        }
    }

    private fun showConfigureDialog(room: RoomTelemetry) {
        val dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_rename_room, null)
        val editNameInput = dialogView.findViewById<EditText>(R.id.editCustomRoomName)
        val editThreshInput = dialogView.findViewById<EditText>(R.id.editRoomThreshold)
        val btnCancel = dialogView.findViewById<Button>(R.id.btnCancelRename)
        val btnSave = dialogView.findViewById<Button>(R.id.btnSaveRename)

        // Populate baseline fields with active values
        editNameInput.setText(room.displayName)
        editThreshInput.setText(room.threshold.toString())

        val alertDialog = AlertDialog.Builder(this).setView(dialogView).create()
        alertDialog.window?.setBackgroundDrawableResource(android.R.color.transparent)

        btnCancel.setOnClickListener { alertDialog.dismiss() }

        btnSave.setOnClickListener {
            val typedName = editNameInput.text.toString().trim()
            val typedThresh = editThreshInput.text.toString().toDoubleOrNull()

            if (typedName.isNotEmpty() && typedThresh != null) {
                val payload = RoomConfigureRequest(room.rawNodeId, typedName, typedThresh)
                apiService.configureRoom(payload).enqueue(object : Callback<ActionResponse> {
                    override fun onResponse(call: Call<ActionResponse>, response: Response<ActionResponse>) {
                        if (response.isSuccessful) {
                            Toast.makeText(this@MainActivity, "Room updates applied successfully!", Toast.LENGTH_SHORT).show()
                            executeFetchTransaction()
                            alertDialog.dismiss()
                        } else {
                            Toast.makeText(this@MainActivity, "Server error: ${response.code()}", Toast.LENGTH_SHORT).show()
                        }
                    }
                    override fun onFailure(call: Call<ActionResponse>, t: Throwable) {
                        Toast.makeText(this@MainActivity, "Failed updating room configuration.", Toast.LENGTH_SHORT).show()
                    }
                })
            } else {
                Toast.makeText(this, "Please fill out all input parameter values completely.", Toast.LENGTH_SHORT).show()
            }
        }
        alertDialog.show()
    }

    class RoomAdapter(
        private var dataset: List<RoomTelemetry>,
        private val onSettingsClicked: (RoomTelemetry) -> Unit
    ) : RecyclerView.Adapter<RoomAdapter.ViewHolder>() {

        class ViewHolder(view: View) : RecyclerView.ViewHolder(view) {
            val txtRoomName: TextView = view.findViewById(R.id.txtRoomName)
            val txtThresholdDisplay: TextView = view.findViewById(R.id.txtThresholdDisplay)
            val btnSettingsRoom: TextView = view.findViewById(R.id.btnSettingsRoom)
            val txtTemp: TextView = view.findViewById(R.id.txtTemp)
            val txtHum: TextView = view.findViewById(R.id.txtHum)
            val txtPres: TextView = view.findViewById(R.id.txtPres)
            val txtWarningBadge: TextView = view.findViewById(R.id.txtWarningBadge)
        }

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
            val view = LayoutInflater.from(parent.context).inflate(R.layout.room_card_item, parent, false)
            return ViewHolder(view)
        }

        override fun onBindViewHolder(holder: ViewHolder, position: Int) {
            val room = dataset[position]
            holder.txtRoomName.text = room.displayName
            holder.txtThresholdDisplay.text = String.format("Warning Limit: %.1f°C", room.threshold)
            holder.txtTemp.text = String.format("%.1f °C", room.temperature)
            holder.txtHum.text = String.format("%.1f %%", room.humidity)
            holder.txtPres.text = if (room.pressure > 0) String.format("%.0f hPa", room.pressure) else "N/A"

            if (room.temperature > room.threshold) {
                holder.txtWarningBadge.text = "HIGH TEMP"
                holder.txtWarningBadge.setBackgroundResource(R.drawable.warning_badge_bg)
                holder.txtWarningBadge.visibility = View.VISIBLE
                holder.txtTemp.setTextColor(Color.parseColor("#EF4444"))
            } else {
                holder.txtWarningBadge.visibility = View.GONE
                holder.txtTemp.setTextColor(Color.parseColor("#0F172A"))
            }

            holder.btnSettingsRoom.setOnClickListener { onSettingsClicked(room) }
        }

        override fun getItemCount() = dataset.size

        fun updateData(newDataset: List<RoomTelemetry>) {
            this.dataset = newDataset
            notifyDataSetChanged()
        }
    }

    override fun onResume() { super.onResume(); mainHandler.post(updateRunnable) }
    override fun onPause() { super.onPause(); mainHandler.removeCallbacks(updateRunnable) }
    override fun onDestroy() { super.onDestroy(); mainHandler.removeCallbacks(updateRunnable) }
}
