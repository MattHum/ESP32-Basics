#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <Arduino.h>
#include "Audio.h"
#include "es8311.h"

class AudioPlayer {
public:
    AudioPlayer();
    bool begin();
    void playFile(const char *filepath);
    void loop();
    void stop();
    bool isPlaying() const;

private:
    Audio _audio;
    es8311_handle_t _codec;
    bool _playing;
};

#endif
