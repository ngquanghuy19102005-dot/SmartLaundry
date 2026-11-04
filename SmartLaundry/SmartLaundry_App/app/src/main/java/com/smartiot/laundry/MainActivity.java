package com.smartiot.laundry;

import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;
import androidx.cardview.widget.CardView;

import org.eclipse.paho.android.service.MqttAndroidClient;
import org.eclipse.paho.client.mqttv3.IMqttActionListener;
import org.eclipse.paho.client.mqttv3.IMqttDeliveryToken;
import org.eclipse.paho.client.mqttv3.IMqttToken;
import org.eclipse.paho.client.mqttv3.MqttCallback;
import org.eclipse.paho.client.mqttv3.MqttConnectOptions;
import org.eclipse.paho.client.mqttv3.MqttException;
import org.eclipse.paho.client.mqttv3.MqttMessage;
import org.json.JSONObject;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

public class MainActivity extends AppCompatActivity {

    private static final String TAG = "SmartLaundry";

    // ===== CẤU HÌNH MQTT =====
    private static final String BROKER_URL   = "tcp://broker.hivemq.com:1883";
    private static final String CLIENT_ID    = "LaundryApp_" + System.currentTimeMillis();
    private static final String TOPIC_SENSOR = "phoido/sensor";
    private static final String TOPIC_STATUS = "phoido/status";
    private static final String TOPIC_MODE   = "phoido/control/mode";
    private static final String TOPIC_RACK   = "phoido/control/rack";
    private static final String TOPIC_FAN    = "phoido/control/fan";

    private MqttAndroidClient mqttClient;

    // ===== UI SENSORS =====
    private TextView tvTemperature, tvHumidity, tvRainStatus, tvRainAnalog;
    private TextView tvRackStatus, tvFanStatus, tvCurrentMode, tvLastUpdate;
    private TextView tvConnectionStatus;
    private CardView cardRainWarning;

    // ===== UI CONTROLS =====
    private Switch switchAutoMode;
    private Button btnOpenRack, btnCloseRack;
    private Switch switchFanControl;
    private Button btnConnectMqtt;

    // ===== AI CHATBOT UI =====
    private ScrollView chatScrollView;
    private LinearLayout chatMessagesContainer;
    private EditText etAIQuestion;
    private ImageButton btnAskAI;
    private TextView tvAIStatus;
    private Button btnQuickDryingTime, btnQuickSilkTips, btnQuickRainCheck;

    private AiLaundryHelper aiHelper;
    private PhoiDoNotificationHelper notificationHelper;

    // State data
    private float currentTemp = 0.0f;
    private float currentHum = 0.0f;
    private boolean isRaining = false;
    private boolean isRackOpen = false;
    private boolean isFanOn = false;
    private boolean isAuto = true;
    private boolean previousRainState = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        initViews();
        notificationHelper = new PhoiDoNotificationHelper(this);
        aiHelper = new AiLaundryHelper(this);

        setupEventListeners();
        setupAiCallback();
        connectToMqttBroker();
    }

    private void initViews() {
        tvConnectionStatus = findViewById(R.id.tvConnectionStatus);
        btnConnectMqtt = findViewById(R.id.btnConnectMqtt);

        tvTemperature = findViewById(R.id.tvTemperature);
        tvHumidity = findViewById(R.id.tvHumidity);
        tvRainStatus = findViewById(R.id.tvRainStatus);
        tvRainAnalog = findViewById(R.id.tvRainAnalog);
        tvRackStatus = findViewById(R.id.tvRackStatus);
        tvFanStatus = findViewById(R.id.tvFanStatus);
        tvCurrentMode = findViewById(R.id.tvCurrentMode);
        tvLastUpdate = findViewById(R.id.tvLastUpdate);
        cardRainWarning = findViewById(R.id.cardRainWarning);

        switchAutoMode = findViewById(R.id.switchAutoMode);
        btnOpenRack = findViewById(R.id.btnOpenRack);
        btnCloseRack = findViewById(R.id.btnCloseRack);
        switchFanControl = findViewById(R.id.switchFanControl);

        chatScrollView = findViewById(R.id.chatScrollView);
        chatMessagesContainer = findViewById(R.id.chatMessagesContainer);
        etAIQuestion = findViewById(R.id.etAIQuestion);
        btnAskAI = findViewById(R.id.btnAskAI);
        tvAIStatus = findViewById(R.id.tvAIStatus);

        btnQuickDryingTime = findViewById(R.id.btnQuickDryingTime);
        btnQuickSilkTips = findViewById(R.id.btnQuickSilkTips);
        btnQuickRainCheck = findViewById(R.id.btnQuickRainCheck);
    }

    private void setupEventListeners() {
        switchAutoMode.setOnCheckedChangeListener((buttonView, isChecked) -> {
            isAuto = isChecked;
            publishMessage(TOPIC_MODE, isChecked ? "AUTO" : "MANUAL");
            updateControlState(isChecked);
            Toast.makeText(this, isChecked ? "Đã bật Chế độ Tự Động" : "Đã chuyển Chế độ Thủ Công", Toast.LENGTH_SHORT).show();
        });

        btnOpenRack.setOnClickListener(v -> {
            if (!isAuto) {
                publishMessage(TOPIC_RACK, "OPEN");
                Toast.makeText(this, "Đang mở giàn phơi ra ngoài...", Toast.LENGTH_SHORT).show();
            } else {
                Toast.makeText(this, "Vui lòng tắt chế độ Auto để điều khiển thủ công", Toast.LENGTH_SHORT).show();
            }
        });

        btnCloseRack.setOnClickListener(v -> {
            if (!isAuto) {
                publishMessage(TOPIC_RACK, "CLOSE");
                Toast.makeText(this, "Đang thu giàn phơi vào trong...", Toast.LENGTH_SHORT).show();
            } else {
                Toast.makeText(this, "Vui lòng tắt chế độ Auto để điều khiển thủ công", Toast.LENGTH_SHORT).show();
            }
        });

        switchFanControl.setOnCheckedChangeListener((buttonView, isChecked) -> {
            if (!isAuto) {
                publishMessage(TOPIC_FAN, isChecked ? "ON" : "OFF");
            }
        });

        btnConnectMqtt.setOnClickListener(v -> connectToMqttBroker());

        btnAskAI.setOnClickListener(v -> {
            String question = etAIQuestion.getText().toString().trim();
            if (!question.isEmpty()) {
                sendAiMessage(question);
                etAIQuestion.setText("");
            }
        });

        btnQuickDryingTime.setOnClickListener(v -> sendAiMessage("Dựa vào nhiệt độ " + currentTemp + "°C và độ ẩm " + currentHum + "%, quần áo phơi khoảng bao lâu thì khô?"));
        btnQuickSilkTips.setOnClickListener(v -> sendAiMessage("Hướng dẫn cách phơi và sấy đồ lụa, đồ len để không bị hỏng vải?"));
        btnQuickRainCheck.setOnClickListener(v -> sendAiMessage("Hôm nay thời tiết có ẩm không? Có nên phơi đồ dày ngoài trời không?"));
    }

    private void updateControlState(boolean isAutoMode) {
        btnOpenRack.setEnabled(!isAutoMode);
        btnCloseRack.setEnabled(!isAutoMode);
        switchFanControl.setEnabled(!isAutoMode);
        tvCurrentMode.setText(isAutoMode ? "TỰ ĐỘNG (AUTO)" : "THỦ CÔNG (MANUAL)");
    }

    // ===== MQTT CONNECTION =====
    private void connectToMqttBroker() {
        tvConnectionStatus.setText("Đang kết nối HiveMQ...");
        mqttClient = new MqttAndroidClient(getApplicationContext(), BROKER_URL, CLIENT_ID);

        mqttClient.setCallback(new MqttCallback() {
            @Override
            public void connectionLost(Throwable cause) {
                runOnUiThread(() -> tvConnectionStatus.setText("Mất kết nối MQTT!"));
            }

            @Override
            public void messageArrived(String topic, MqttMessage message) {
                String payload = new String(message.getPayload());
                runOnUiThread(() -> handleMqttMessage(topic, payload));
            }

            @Override
            public void deliveryComplete(IMqttDeliveryToken token) {}
        });

        MqttConnectOptions options = new MqttConnectOptions();
        options.setAutomaticReconnect(true);
        options.setCleanSession(true);

        try {
            mqttClient.connect(options, null, new IMqttActionListener() {
                @Override
                public void onSuccess(IMqttToken asyncActionToken) {
                    runOnUiThread(() -> {
                        tvConnectionStatus.setText("● Đã kết nối HiveMQ");
                        subscribeTopics();
                    });
                }

                @Override
                public void onFailure(IMqttToken asyncActionToken, Throwable exception) {
                    runOnUiThread(() -> tvConnectionStatus.setText("Kết nối thất bại!"));
                }
            });
        } catch (MqttException e) {
            Log.e(TAG, "Lỗi MQTT: ", e);
        }
    }

    private void subscribeTopics() {
        try {
            mqttClient.subscribe(TOPIC_SENSOR, 0);
            mqttClient.subscribe(TOPIC_STATUS, 0);
        } catch (MqttException e) {
            Log.e(TAG, "Subscribe error: ", e);
        }
    }

    private void publishMessage(String topic, String payload) {
        if (mqttClient != null && mqttClient.isConnected()) {
            try {
                MqttMessage message = new MqttMessage(payload.getBytes());
                message.setQos(1);
                mqttClient.publish(topic, message);
            } catch (MqttException e) {
                Log.e(TAG, "Publish error: ", e);
            }
        }
    }

    private void handleMqttMessage(String topic, String payload) {
        if (TOPIC_SENSOR.equals(topic)) {
            try {
                JSONObject json = new JSONObject(payload);
                currentTemp = (float) json.optDouble("temp", 0.0);
                currentHum = (float) json.optDouble("hum", 0.0);
                int rainAnalog = json.optInt("rain_analog", 1024);
                isRaining = json.optBoolean("is_raining", false);
                String rackStr = json.optString("rack", "CLOSE");
                String fanStr = json.optString("fan", "OFF");
                String modeStr = json.optString("mode", "AUTO");

                isRackOpen = "OPEN".equalsIgnoreCase(rackStr);
                isFanOn = "ON".equalsIgnoreCase(fanStr);
                isAuto = "AUTO".equalsIgnoreCase(modeStr);

                // Update UI
                tvTemperature.setText(String.format(Locale.getDefault(), "%.1f °C", currentTemp));
                tvHumidity.setText(String.format(Locale.getDefault(), "%.1f %%", currentHum));
                tvRainAnalog.setText("Analog: " + rainAnalog);
                tvRainStatus.setText(isRaining ? "TRỜI MƯA" : "TẠNH RÁO");
                tvRainStatus.setTextColor(isRaining ? 0xFFFF4444 : 0xFF00C853);

                cardRainWarning.setVisibility(isRaining ? View.VISIBLE : View.GONE);

                tvRackStatus.setText(isRackOpen ? "Đang Mở (Phơi Đồ)" : "Đã Thu Vào (An Toàn)");
                tvFanStatus.setText(isFanOn ? "BẬT" : "TẮT");
                tvCurrentMode.setText(isAuto ? "TỰ ĐỘNG (AUTO)" : "THỦ CÔNG (MANUAL)");

                switchAutoMode.setChecked(isAuto);
                switchFanControl.setChecked(isFanOn);

                String time = new SimpleDateFormat("HH:mm:ss", Locale.getDefault()).format(new Date());
                tvLastUpdate.setText("Cập nhật: " + time);

                // Cảnh báo thông báo đẩy khi trời bắt đầu mưa
                if (isRaining && !previousRainState) {
                    notificationHelper.showRainAlertNotification();
                }
                previousRainState = isRaining;

                aiHelper.updateEnvironment(currentTemp, currentHum, isRaining, isRackOpen, isFanOn);

            } catch (Exception e) {
                Log.e(TAG, "Lỗi JSON: ", e);
            }
        }
    }

    // ===== GEMINI AI CHAT =====
    private void sendAiMessage(String userText) {
        addChatMessage("Bạn", userText, true);
        tvAIStatus.setText("AI đang suy nghĩ...");
        tvAIStatus.setVisibility(View.VISIBLE);

        aiHelper.askAssistant(userText);
    }

    private void setupAiCallback() {
        aiHelper.setCallback(new AiLaundryHelper.AiCallback() {
            @Override
            public void onResponse(String answer) {
                runOnUiThread(() -> {
                    tvAIStatus.setVisibility(View.GONE);
                    addChatMessage("Trợ Lý Giặt Sấy AI", answer, false);
                });
            }

            @Override
            public void onError(String error) {
                runOnUiThread(() -> {
                    tvAIStatus.setText("Lỗi: " + error);
                    Toast.makeText(MainActivity.this, error, Toast.LENGTH_SHORT).show();
                });
            }
        });
    }

    private void addChatMessage(String sender, String message, boolean isUser) {
        TextView tv = new TextView(this);
        tv.setText(String.format("%s: %s", sender, message));
        tv.setTextSize(14);
        tv.setPadding(24, 16, 24, 16);
        tv.setTextColor(isUser ? 0xFFFFFFFF : 0xFF1B1B1B);
        tv.setBackgroundResource(isUser ? android.R.color.holo_blue_dark : android.R.color.white);

        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
        );
        params.setMargins(16, 8, 16, 8);
        params.gravity = isUser ? android.view.Gravity.END : android.view.Gravity.START;
        tv.setLayoutParams(params);

        chatMessagesContainer.addView(tv);
        chatScrollView.post(() -> chatScrollView.fullScroll(ScrollView.FOCUS_DOWN));
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (mqttClient != null && mqttClient.isConnected()) {
            try {
                mqttClient.disconnect();
            } catch (MqttException e) {
                Log.e(TAG, "Disconnect error", e);
            }
        }
    }
}
