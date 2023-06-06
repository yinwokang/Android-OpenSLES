package cn.wk.opensl_demo;

import android.Manifest;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;

import androidx.appcompat.app.AppCompatActivity;

import java.io.File;

import cn.wk.opensl_demo.databinding.ActivityMainBinding;

public class MainActivity extends AppCompatActivity {

    static {
        System.loadLibrary("native-lib");
    }

    private ActivityMainBinding binding;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            requestPermissions(new String[]{Manifest.permission.WRITE_EXTERNAL_STORAGE}, 0);
        }

        binding.button.setOnClickListener(v -> {
            // 路径：sdcard 目录下的 test.mp4
            audioPlayer(new File(Environment.getExternalStorageDirectory(), "test.mp4").getAbsolutePath());
        });
    }

    public native void audioPlayer(String dataStr);

}