package com.britney.myregisternative;

import androidx.appcompat.app.AppCompatActivity;

import android.os.Bundle;
import android.widget.TextView;

import com.britney.myregisternative.databinding.ActivityMainBinding;

public class MainActivity extends AppCompatActivity {

    // Used to load the 'myregisternative' library on application startup.
    static {
        System.loadLibrary("myregisternative");
    }

    private ActivityMainBinding binding;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());

        // Example of a call to a native method
        TextView tv = binding.sampleText;
        tv.setText(str1());
    }

    /**
     * A native method that is implemented by the 'myregisternative' native library,
     * which is packaged with this application.
     */
    public native String str1();
}