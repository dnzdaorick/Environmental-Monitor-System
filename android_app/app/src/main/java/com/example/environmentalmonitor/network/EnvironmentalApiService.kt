package com.example.environmentalmonitor.network

import com.google.gson.annotations.SerializedName
import retrofit2.Call
import retrofit2.http.Body
import retrofit2.http.GET
import retrofit2.http.POST

// 1. Data structure for receiving environmental metrics
data class TelemetryData(
    val temperature: Double,
    val humidity: Double,
    val pressure: Double,
    val threshold: Double,
)

// 2. Data structure for sending a new threshold limit
data class ThresholdPayload(
    @SerializedName("max_temp")
    val maxTemp: Double,
)

// 3. Data structure for reading the server's success response
data class ActionResponse(
    val status: String,
    @SerializedName("target_threshold")
    val targetThreshold: Double,
)

interface EnvironmentalApiService {

    // Fetch Transaction (Information Processing)
    @GET("api/telemetry/latest")
    fun getLatestMetrics(): Call<TelemetryData>

    // Push Trigger Update Transaction (Process Automation)
    @POST("api/automation/threshold")
    fun pushThresholdUpdate(@Body payload: ThresholdPayload): Call<ActionResponse>
}