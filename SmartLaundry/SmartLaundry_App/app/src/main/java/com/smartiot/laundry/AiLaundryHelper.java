package com.smartiot.laundry;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class AiLaundryHelper {

    private static final String TAG = "AiLaundryHelper";
    private static final String GEMINI_API_KEY = "AIzaSyCheOba3OFaVtj1xLaKE--l3ts1HVh7-og";
    private static final String GEMINI_ENDPOINT =
            "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent?key=" + GEMINI_API_KEY;

    private final Context context;
    private final Handler mainHandler;
    private final ExecutorService executorService;

    private float temperature = 30.0f;
    private float humidity = 65.0f;
    private boolean isRaining = false;
    private boolean isRackOpen = true;
    private boolean isFanOn = false;

    public interface AiCallback {
        void onResponse(String answer);
        void onError(String error);
    }

    private AiCallback callback;

    public AiLaundryHelper(Context context) {
        this.context = context;
        this.mainHandler = new Handler(Looper.getMainLooper());
        this.executorService = Executors.newSingleThreadExecutor();
    }

    public void setCallback(AiCallback callback) {
        this.callback = callback;
    }

    public void updateEnvironment(float temp, float hum, boolean raining, boolean rackOpen, boolean fanOn) {
        this.temperature = temp;
        this.humidity = hum;
        this.isRaining = raining;
        this.isRackOpen = rackOpen;
        this.isFanOn = fanOn;
    }

    public void askAssistant(String userPrompt) {
        executorService.execute(() -> {
            try {
                String systemContext = String.format(
                        "Bạn là Trợ lý AI Giặt Sấy & Giàn Phơi Thông Minh. Dữ liệu thực tế từ giàn phơi hiện tại: " +
                        "Nhiệt độ môi trường: %.1f°C, Độ ẩm: %.1f%%, Thời tiết: %s, Giàn phơi đang %s, Quạt sấy %s. " +
                        "Hãy trả lời ngắn gọn, thân thiện, thực tế và hữu ích bằng tiếng Việt.",
                        temperature, humidity,
                        isRaining ? "Đang có mưa" : "Tạnh ráo, nắng",
                        isRackOpen ? "Mở ra ngoài trời" : "Thu vào trong nhà",
                        isFanOn ? "Đang bật" : "Đang tắt"
                );

                JSONObject contentObj = new JSONObject();
                contentObj.put("role", "user");

                JSONArray partsArr = new JSONArray();
                JSONObject partText = new JSONObject();
                partText.put("text", systemContext + "\nNgười dùng hỏi: " + userPrompt);
                partsArr.put(partText);
                contentObj.put("parts", partsArr);

                JSONArray contentsArr = new JSONArray();
                contentsArr.put(contentObj);

                JSONObject requestBody = new JSONObject();
                requestBody.put("contents", contentsArr);

                URL url = new URL(GEMINI_ENDPOINT);
                HttpURLConnection conn = (HttpURLConnection) url.openConnection();
                conn.setRequestMethod("POST");
                conn.setRequestProperty("Content-Type", "application/json; utf-8");
                conn.setDoOutput(true);
                conn.setConnectTimeout(10000);
                conn.setReadTimeout(15000);

                try (OutputStream os = conn.getOutputStream()) {
                    byte[] input = requestBody.toString().getBytes(StandardCharsets.UTF_8);
                    os.write(input, 0, input.length);
                }

                int code = conn.getResponseCode();
                if (code == 200) {
                    BufferedReader br = new BufferedReader(new InputStreamReader(conn.getInputStream(), StandardCharsets.UTF_8));
                    StringBuilder response = new StringBuilder();
                    String line;
                    while ((line = br.readLine()) != null) response.append(line.trim());

                    JSONObject responseJson = new JSONObject(response.toString());
                    JSONArray candidates = responseJson.optJSONArray("candidates");
                    if (candidates != null && candidates.length() > 0) {
                        JSONObject content = candidates.getJSONObject(0).optJSONObject("content");
                        JSONArray parts = content != null ? content.optJSONArray("parts") : null;
                        if (parts != null && parts.length() > 0) {
                            String reply = parts.getJSONObject(0).optString("text", "Không có phản hồi.");
                            notifyResponse(reply);
                            return;
                        }
                    }
                    notifyResponse(getFallbackResponse(userPrompt));
                } else {
                    notifyResponse(getFallbackResponse(userPrompt));
                }

            } catch (Exception e) {
                notifyResponse(getFallbackResponse(userPrompt));
            }
        });
    }

    private String getFallbackResponse(String question) {
        String q = question.toLowerCase();
        if (q.contains("bao lâu") || q.contains("mấy tiếng") || q.contains("khô")) {
            if (temperature >= 32 && humidity <= 60) {
                return String.format("Hiện tại nhiệt độ %.1f°C và độ ẩm %.1f%% rất thuận lợi! Quần áo cotton mỏng sẽ khô sau khoảng 2 - 3 giờ nắng.", temperature, humidity);
            } else if (isRaining || humidity > 75) {
                return String.format("Độ ẩm hiện tại cao (%.1f%%) %s. Nên thu giàn phơi và bật quạt sấy, dự kiến cần 4 - 6 giờ mới khô.", humidity, isRaining ? "và trời đang mưa" : "");
            } else {
                return String.format("Với nhiệt độ %.1f°C và độ ẩm %.1f%%, thời gian khô dự kiến khoảng 3.5 - 4.5 giờ đối với quần áo thông thường.", temperature, humidity);
            }
        } else if (q.contains("lụa") || q.contains("len") || q.contains("vải")) {
            return "Mẹo chăm sóc vải: Đồ len nên phơi trải phẳng nằm ngang để tránh bị dão sợi; đồ lụa không nên phơi dưới nắng gắt trực tiếp > 35°C mà nên phơi nơi thoáng gió, râm mát.";
        } else if (q.contains("mưa") || q.contains("thời tiết")) {
            return isRaining ? "Hệ thống đang phát hiện trời mưa! Giàn phơi đã tự động thu vào và bật sấy bảo vệ." : "Hiện tại không có mưa, điều kiện phơi đồ ngoài trời khá an toàn.";
        }
        return "Hệ thống giàn phơi thông minh đang hoạt động tốt. Bạn có thể hỏi về thời gian khô đồ, cách bảo quản sợi vải, hoặc trạng thái cảm biến hiện tại!";
    }

    private void notifyResponse(String reply) {
        mainHandler.post(() -> {
            if (callback != null) callback.onResponse(reply);
        });
    }
}
