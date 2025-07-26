# TTS Integration with Deepgram API

This project now includes Text-to-Speech (TTS) functionality using the Deepgram API and MAX98357A amplifier for audio output.

## Features

- Convert Gemini vision responses to speech using Deepgram TTS API
- Play audio through MAX98357A I2S amplifier
- Automatic WAV header parsing for PCM data extraction
- Configurable audio settings (44.1kHz, 16-bit PCM)

## Hardware Setup

### MAX98357A Wiring

Connect the MAX98357A breakout to your ESP32-WROVER-CAM:

| MAX98357A Pin | ESP32 Pin | Description |
|---------------|-----------|-------------|
| VIN          | 3V3       | Power supply |
| GND          | GND       | Ground |
| BCLK         | GPIO 14   | I2S Bit Clock |
| LRC (LRCLK)  | GPIO 13   | I2S Word Select |
| DIN          | GPIO 2    | I2S Data Input |

**Note**: These pins were chosen to avoid conflicts with the camera module which uses GPIOs 21, 26, 27, 35, 34, 39, 36, 19, 18, 5, 4, 25, 23, and 22.

### Speaker Connection

Connect a 3W 4Ω or 8Ω speaker to the MAX98357A speaker outputs (+ and -).

## Software Configuration

### 1. Deepgram API Key

Add your Deepgram API key to `src/secrets.cpp`:

```cpp
const char* DEEPGRAM_API_KEY = "your_deepgram_api_key_here";
```

### 2. Audio Settings

The TTS system is configured for:
- Sample Rate: 44.1kHz
- Bit Depth: 16-bit
- Format: PCM (Linear16)
- Container: WAV

These settings are optimized for the MAX98357A and can be modified in `TTS.h` if needed.

## Usage

The TTS system is automatically integrated into the vision assistant. When Gemini provides a response, it will be:

1. Displayed in the serial console
2. Converted to speech via Deepgram API
3. Played through the connected speaker

### Manual TTS Usage

You can also use TTS manually in your code:

```cpp
#include "TTS.h"

TTS tts;

void setup() {
    // Initialize TTS
    if (tts.initialize(DEEPGRAM_API_KEY)) {
        Serial.println("TTS initialized successfully");
    }
}

void loop() {
    // Speak some text
    tts.speakText("Hello, this is a test message");
    delay(5000);
}
```

## API Configuration

The Deepgram API is configured with these parameters:
- Model: `aura-2-thalia-en` (natural female voice)
- Encoding: `linear16` (16-bit PCM)
- Sample Rate: `44100` Hz
- Container: `wav`

You can modify these settings in `TTS.cpp` by updating the `DEEPGRAM_URL` constant.

## Troubleshooting

### No Audio Output

1. **Check Wiring**: Verify all connections between ESP32 and MAX98357A
2. **Power Supply**: Ensure MAX98357A is receiving 3.3V power
3. **Speaker**: Confirm speaker is properly connected and functional
4. **API Key**: Verify your Deepgram API key is valid and has sufficient credits

### Distorted Audio

1. **Sample Rate Mismatch**: Ensure Deepgram returns 44.1kHz audio
2. **Bit Depth**: Confirm 16-bit PCM format
3. **I2S Configuration**: Check I2S settings in `initializeI2S()`

### API Errors

1. **Network Connection**: Verify WiFi connectivity
2. **API Limits**: Check Deepgram account usage and limits
3. **SSL Issues**: The code uses `setInsecure()` for simplicity - consider proper SSL verification for production

## Voice Options

Deepgram offers various voice models. To change the voice, modify the model parameter in the URL:

```cpp
const char* TTS::DEEPGRAM_URL = "https://api.deepgram.com/v1/speak?model=aura-2-zeus-en&encoding=linear16&sample_rate=44100&container=wav";
```

Available models include:
- `aura-2-thalia-en` (female, default)
- `aura-2-zeus-en` (male)
- `aura-2-angus-en` (male, Scottish accent)
- `aura-2-arcas-en` (male, American)
- And many more...

## Performance Notes

- Audio data is streamed directly to I2S without buffering large amounts in memory
- WAV headers are automatically parsed and skipped
- The system handles network delays gracefully
- I2S DMA buffers are configured for low latency

## Future Enhancements

- MP3 decoding support for smaller file sizes
- Audio volume control via software scaling
- Audio caching for repeated phrases
- Multiple voice selection
- Audio compression/decompression
- Real-time audio streaming
