#pragma once
#include "rewind_buffer.h"
#include <dsp/stream.h>
#include <string>

namespace beige::rewind {
dsp::stream<dsp::stereo_t>* connect(void* key, dsp::stream<dsp::stereo_t>* input, int rate);
void start(void* key);
void stop(void* key);
void remove(void* key);
void setRate(void* key, int rate);
void setReceiving(bool value);
bool playbackActive(void* key);
void shutdown();
void select(const std::string& name);
State state();
void seek(double seconds);
void pause(bool value);
void live();
bool enabled();
void enable(bool value);
struct ExportState { bool busy = false; std::string path, error; double seconds = 0; };
ExportState exportState();
void save(int seconds);
void reveal();
float barHeight();
void draw(const std::string& streamName, bool receiving);
}
