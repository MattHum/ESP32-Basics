#include <Arduino.h>
#include <Wire.h>
#include "audio_player.h"
#include "SD_MMC.h"
#include "user_config.h"

static es8311_handle_t s_codec = nullptr;

AudioPlayer::AudioPlayer() : _codec(nullptr), _playing(false) {}

bool AudioPlayer::begin() {
    Serial.println("[AUDIO] Initializing...");

    pinMode(PA_EN_PIN, OUTPUT);
    pinMode(PA_CTRL_PIN, OUTPUT);
    digitalWrite(PA_EN_PIN, LOW);
    digitalWrite(PA_CTRL_PIN, HIGH);

    Wire.begin(I2C_SDA, I2C_SCL);

    _codec = es8311_create(I2C_NUM_0, ES8311_ADDRRES_0);
    s_codec = _codec;

    const es8311_clock_config_t clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = SAMPLE_RATE * 256,
        .sample_frequency = SAMPLE_RATE
    };

    esp_err_t err = es8311_init(_codec, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
    if (err != ESP_OK) {
        Serial.printf("[AUDIO] ES8311 init FAILED: %d\n", err);
        return false;
    }

    es8311_voice_volume_set(_codec, VOICE_VOLUME, NULL);
    es8311_microphone_config(_codec, false);
    Serial.println("[AUDIO] ES8311 configured via I2C");

    _audio.setPinout(I2S_BCK_PIN, I2S_LRCK_PIN, I2S_DOUT_PIN, I2S_MCK_PIN);
    _audio.setVolume(VOICE_VOLUME);

    Serial.println("[AUDIO] Ready (MP3/WAV/AAC/FLAC)");
    return true;
}

void AudioPlayer::playFile(const char *filepath) {
    if (_playing) stop();

    es8311_sample_frequency_config(_codec, SAMPLE_RATE * 256, SAMPLE_RATE);

    Serial.printf("[AUDIO] Playing: %s\n", filepath);
    _audio.connecttoFS(SD_MMC, filepath);
    _playing = true;
}

void AudioPlayer::loop() {
    if (_playing) {
        _audio.loop();
        if (!_audio.isRunning()) {
            _playing = false;
            Serial.println("[AUDIO] Done");
        }
    }
}

void AudioPlayer::stop() {
    _audio.stopSong();
    _playing = false;
}

bool AudioPlayer::isPlaying() const {
    return _playing;
}
