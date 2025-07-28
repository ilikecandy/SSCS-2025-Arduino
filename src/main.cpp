#include <driver/i2s.h>
#include <Arduino.h>

// I2S Audio Configuration (shared pins for microphone and speaker)
#define SAMPLE_BUFFER_SIZE 512
#define SAMPLE_RATE 8000
#define RECORDING_TIME_MS 5000  // 5 seconds recording
#define TOTAL_SAMPLES (SAMPLE_RATE * RECORDING_TIME_MS / 1000)

// I2S pin configuration - shared between microphone and speaker
#define I2S_SERIAL_CLOCK GPIO_NUM_14     // SCK - Serial Clock
#define I2S_LEFT_RIGHT_CLOCK GPIO_NUM_13 // WS - Word Select  
#define I2S_SERIAL_DATA GPIO_NUM_2       // SD - Serial Data

// I2S configuration for microphone (recording)
i2s_config_t i2s_mic_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
};

// I2S configuration for speaker (playback)
i2s_config_t i2s_speaker_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
};

// Pin configuration (same pins used for both devices)
i2s_pin_config_t i2s_pins = {
    .bck_io_num = I2S_SERIAL_CLOCK,
    .ws_io_num = I2S_LEFT_RIGHT_CLOCK,
    .data_out_num = I2S_SERIAL_DATA,  // For speaker output
    .data_in_num = I2S_SERIAL_DATA    // For microphone input
};

// Audio buffer to store recorded audio
int16_t* audio_buffer = nullptr;

bool setupI2SForRecording() {
  Serial.println("Setting up I2S for recording...");
  
  // Uninstall if already installed
  i2s_driver_uninstall(I2S_NUM_1);
  
  // Install I2S driver for recording
  esp_err_t err = i2s_driver_install(I2S_NUM_1, &i2s_mic_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("ERROR: Failed to install I2S driver for recording: %s\n", esp_err_to_name(err));
    return false;
  }
  
  // Set pins for recording
  i2s_pin_config_t recording_pins = i2s_pins;
  recording_pins.data_out_num = I2S_PIN_NO_CHANGE;  // No output for recording
  
  err = i2s_set_pin(I2S_NUM_1, &recording_pins);
  if (err != ESP_OK) {
    Serial.printf("ERROR: Failed to set I2S pins for recording: %s\n", esp_err_to_name(err));
    return false;
  }
  
  // Start I2S
  err = i2s_start(I2S_NUM_1);
  if (err != ESP_OK) {
    Serial.printf("ERROR: Failed to start I2S for recording: %s\n", esp_err_to_name(err));
    return false;
  }
  
  Serial.println("✓ I2S configured for recording");
  return true;
}

bool setupI2SForPlayback() {
  Serial.println("Setting up I2S for playback...");
  
  // Uninstall previous driver
  i2s_driver_uninstall(I2S_NUM_1);
  
  // Install I2S driver for playback
  esp_err_t err = i2s_driver_install(I2S_NUM_1, &i2s_speaker_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("ERROR: Failed to install I2S driver for playback: %s\n", esp_err_to_name(err));
    return false;
  }
  
  // Set pins for playback
  i2s_pin_config_t playback_pins = i2s_pins;
  playback_pins.data_in_num = I2S_PIN_NO_CHANGE;  // No input for playback
  
  err = i2s_set_pin(I2S_NUM_1, &playback_pins);
  if (err != ESP_OK) {
    Serial.printf("ERROR: Failed to set I2S pins for playback: %s\n", esp_err_to_name(err));
    return false;
  }
  
  // Start I2S
  err = i2s_start(I2S_NUM_1);
  if (err != ESP_OK) {
    Serial.printf("ERROR: Failed to start I2S for playback: %s\n", esp_err_to_name(err));
    return false;
  }
  
  Serial.println("✓ I2S configured for playback");
  return true;
}

void recordAudio() {
  Serial.println("🎤 Recording audio for 5 seconds...");
  
  // Clear the buffer
  memset(audio_buffer, 0, TOTAL_SAMPLES * sizeof(int16_t));
  
  int32_t raw_samples[SAMPLE_BUFFER_SIZE];
  size_t samples_recorded = 0;
  unsigned long start_time = millis();
  
  while (samples_recorded < TOTAL_SAMPLES && (millis() - start_time) < RECORDING_TIME_MS) {
    size_t bytes_read = 0;
    esp_err_t result = i2s_read(I2S_NUM_1, raw_samples, sizeof(int32_t) * SAMPLE_BUFFER_SIZE, &bytes_read, 100);
    
    if (result == ESP_OK && bytes_read > 0) {
      int samples_read = bytes_read / sizeof(int32_t);
      
      // Convert 32-bit samples to 16-bit and store
      for (int i = 0; i < samples_read && samples_recorded < TOTAL_SAMPLES; i++) {
        // INMP441 has data in upper 24 bits, convert to 16-bit
        audio_buffer[samples_recorded] = raw_samples[i] >> 8;
        samples_recorded++;
      }
    }
    
    // Show progress
    if ((millis() - start_time) % 1000 == 0) {
      Serial.printf("Recording... %lu/%d samples\n", samples_recorded, TOTAL_SAMPLES);
    }
  }
  
  Serial.printf("✓ Recording complete! Captured %lu samples\n", samples_recorded);
}

void playbackAudio() {
  Serial.println("🔊 Playing back recorded audio...");
  
  size_t samples_played = 0;
  size_t bytes_written = 0;
  
  while (samples_played < TOTAL_SAMPLES) {
    size_t samples_to_play = min((size_t)SAMPLE_BUFFER_SIZE, TOTAL_SAMPLES - samples_played);
    
    // For stereo output, duplicate mono samples to both channels
    int16_t stereo_buffer[SAMPLE_BUFFER_SIZE * 2];
    for (size_t i = 0; i < samples_to_play; i++) {
      stereo_buffer[i * 2] = audio_buffer[samples_played + i];      // Left channel
      stereo_buffer[i * 2 + 1] = audio_buffer[samples_played + i];  // Right channel
    }
    
    esp_err_t result = i2s_write(I2S_NUM_1, stereo_buffer, samples_to_play * 2 * sizeof(int16_t), &bytes_written, 1000);
    
    if (result == ESP_OK) {
      samples_played += samples_to_play;
    } else {
      Serial.printf("ERROR: I2S write failed: %s\n", esp_err_to_name(result));
      break;
    }
    
    // Show progress
    if (samples_played % (SAMPLE_RATE / 2) == 0) {  // Every 0.5 seconds
      Serial.printf("Playing... %lu/%d samples\n", samples_played, TOTAL_SAMPLES);
    }
  }
  
  Serial.println("✓ Playback complete!");
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("=== I2S Audio Record & Playback Test ===");
  Serial.println("Hardware Setup:");
  Serial.println("  Microphone (INMP441) and Speaker share same pins:");
  Serial.printf("  SCK → GPIO %d\n", I2S_SERIAL_CLOCK);
  Serial.printf("  WS  → GPIO %d\n", I2S_LEFT_RIGHT_CLOCK);
  Serial.printf("  SD  → GPIO %d\n", I2S_SERIAL_DATA);
  Serial.printf("Sample Rate: %d Hz\n", SAMPLE_RATE);
  Serial.printf("Recording Time: %d ms\n", RECORDING_TIME_MS);
  Serial.printf("Total Samples: %d\n", TOTAL_SAMPLES);
  
  // Allocate memory for audio buffer
  audio_buffer = (int16_t*)malloc(TOTAL_SAMPLES * sizeof(int16_t));
  if (audio_buffer == nullptr) {
    Serial.println("ERROR: Failed to allocate memory for audio buffer!");
    while(1) delay(1000);
  }
  Serial.println("✓ Audio buffer allocated");
  
  Serial.println("=== Ready to start recording cycle ===");
}

void loop() {
  Serial.println("\n=== Starting Record & Playback Cycle ===");
  
  // Step 1: Setup I2S for recording
  if (!setupI2SForRecording()) {
    Serial.println("Failed to setup recording, retrying in 5 seconds...");
    delay(5000);
    return;
  }
  
  // Give microphone time to stabilize
  delay(500);
  
  // Step 2: Record audio
  recordAudio();
  
  // Step 3: Setup I2S for playback
  if (!setupI2SForPlayback()) {
    Serial.println("Failed to setup playback, retrying in 5 seconds...");
    delay(5000);
    return;
  }
  
  // Give speaker time to stabilize
  delay(500);
  
  // Step 4: Play back the recorded audio
  playbackAudio();
  
  // Wait before next cycle
  Serial.println("\n⏳ Waiting 3 seconds before next cycle...");
  delay(3000);
}