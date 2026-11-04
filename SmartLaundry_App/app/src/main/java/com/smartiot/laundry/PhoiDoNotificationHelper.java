package com.smartiot.laundry;

import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.Context;
import android.os.Build;

import androidx.core.app.NotificationCompat;

public class PhoiDoNotificationHelper {

    private static final String CHANNEL_ID = "laundry_rain_alerts";
    private static final String CHANNEL_NAME = "Cảnh Báo Giàn Phơi Thông Minh";
    private final Context context;
    private final NotificationManager notificationManager;

    public PhoiDoNotificationHelper(Context context) {
        this.context = context;
        this.notificationManager = (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);
        createNotificationChannel();
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID,
                    CHANNEL_NAME,
                    NotificationManager.IMPORTANCE_HIGH
            );
            channel.setDescription("Thông báo khẩn cấp khi trời mưa hoặc sự cố");
            channel.enableVibration(true);
            if (notificationManager != null) {
                notificationManager.createNotificationChannel(channel);
            }
        }
    }

    public void showRainAlertNotification() {
        NotificationCompat.Builder builder = new NotificationCompat.Builder(context, CHANNEL_ID)
                .setSmallIcon(android.R.drawable.ic_dialog_alert)
                .setContentTitle("⚠️ Phát hiện trời mưa!")
                .setContentText("Hệ thống đã tự động thu giàn phơi và kích hoạt sấy bảo vệ quần áo.")
                .setPriority(NotificationCompat.PRIORITY_HIGH)
                .setAutoCancel(true)
                .setVibrate(new long[]{0, 500, 200, 500});

        if (notificationManager != null) {
            notificationManager.notify(101, builder.build());
        }
    }
}
