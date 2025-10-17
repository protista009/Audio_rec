#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>

// Audio Objects
AudioInputI2S           i2s_input;
AudioAnalyzeFFT1024     fft1024;
AudioFilterStateVariable bandpass_filter;
AudioAnalyzePeak        peakAnalyzer;
AudioAmplifier          amplifier;
AudioMixer4             mixer;
AudioOutputI2S          i2s_output;
AudioRecordQueue        recordQueue;
AudioControlSGTL5000    sgtl5000_1;

// Audio Connections
AudioConnection patchCord1(i2s_input, 0, fft1024, 0);
AudioConnection patchCord2(i2s_input, 0, bandpass_filter, 0);
AudioConnection patchCord3(bandpass_filter, 0, amplifier, 0);
AudioConnection patchCord4(amplifier, 0, mixer, 0);
AudioConnection patchCord5(mixer, 0, i2s_output, 0);
AudioConnection patchCord6(mixer, 0, i2s_output, 1);
AudioConnection patchCord7(mixer, 0, recordQueue, 0);
AudioConnection patchCord8(i2s_input, 0, peakAnalyzer, 0);

// Variables for adaptive noise threshold
float noiseThreshold = 0.02;
float noiseDecayRate = 0.995;
float targetGain = 1.0;
float noiseProfile[512] = {0}; // Array for storing noise profile
bool voiceActive = false;

// Timer for recording duration
unsigned long startTime;
const unsigned long recordingDuration = 30000;

// SD card file
File audioFile;

// WAV file constants
const int sampleRate = 44100;
const int bitsPerSample = 16;
const int numChannels = 1;

void setup() {
  Serial.begin(115200);

  AudioMemory(40);
  sgtl5000_1.enable();
  sgtl5000_1.inputSelect(AUDIO_INPUT_MIC);
  sgtl5000_1.micGain(30);
  sgtl5000_1.volume(0.8);

  // Configure band-pass filter
  bandpass_filter.frequency(1000);
  bandpass_filter.resonance(1.2);

  // Initialize amplifier
  amplifier.gain(targetGain);

  // Initialize mixer
  mixer.gain(0, 1.0);

  // SD card setup
  if (!SD.begin(10)) {
    Serial.println("SD card initialization failed!");
    while (true);
  }
  audioFile = SD.open("recording.wav", FILE_WRITE);
  if (!audioFile) {
    Serial.println("Failed to open file on SD card!");
    while (true);
  }
  writeWAVHeader();
  recordQueue.begin();
  startTime = millis();
}

void loop() {
  // Record audio to SD card
  if (recordQueue.available() > 0) {
    byte *data = (byte *)recordQueue.readBuffer();
    if (voiceActive) {
      audioFile.write(data, 256); // Write only when voice is detected
    }
    recordQueue.freeBuffer();
  }

  // Adaptive gain control
  if (peakAnalyzer.available()) {
    float currentPeak = peakAnalyzer.read();
    if (currentPeak > 0.05) {
      targetGain = max(0.5, 1.0 / currentPeak);
      voiceActive = true; // Voice detected
    } else {
      targetGain = min(1.0, targetGain * 1.05);
      voiceActive = false; // No voice detected
    }
    amplifier.gain(targetGain);
  }

  // FFT-based noise profiling
  if (fft1024.available()) {
    for (int i = 0; i < 512; i++) {
      float magnitude = fft1024.read(i);
      noiseProfile[i] = max(noiseProfile[i] * noiseDecayRate, magnitude);
      if (magnitude > noiseProfile[i] * 1.5) {
        // Optional: Suppress frequency ranges here if needed
      }
    }
  }

  // Stop after recording duration
  if (millis() - startTime >= recordingDuration) {
    stopRecording();
    while (true);
  }
}

void stopRecording() {
  recordQueue.end();
  if (audioFile) {
    updateWAVHeader();
    audioFile.close();
    Serial.println("Recording saved as WAV.");
  } else {
    Serial.println("No file to close. Recording failed.");
  }
}

void writeWAVHeader() {
  byte header[44];
  int dataSize = 0;
  memcpy(header, "RIFF", 4);
  writeLittleEndian(header + 4, 36 + dataSize, 4);
  memcpy(header + 8, "WAVE", 4);
  memcpy(header + 12, "fmt ", 4);
  writeLittleEndian(header + 16, 16, 4);
  writeLittleEndian(header + 20, 1, 2);
  writeLittleEndian(header + 22, numChannels, 2);
  writeLittleEndian(header + 24, sampleRate, 4);
  writeLittleEndian(header + 28, sampleRate * numChannels * bitsPerSample / 8, 4);
  writeLittleEndian(header + 32, numChannels * bitsPerSample / 8, 2);
  writeLittleEndian(header + 34, bitsPerSample, 2);
  memcpy(header + 36, "data", 4);
  writeLittleEndian(header + 40, dataSize, 4);
  audioFile.write(header, 44);
}

void updateWAVHeader() {
  int fileSize = audioFile.size();
  int dataSize = fileSize - 44;
  audioFile.seek(4);
  writeLittleEndian(audioFile, fileSize - 8, 4);
  audioFile.seek(40);
  writeLittleEndian(audioFile, dataSize, 4);
}

void writeLittleEndian(byte *buffer, unsigned int value, int size) {
  for (int i = 0; i < size; i++) {
    buffer[i] = value & 0xFF;
    value >>= 8;
  }
}

void writeLittleEndian(File &file, unsigned int value, int size) {
  for (int i = 0; i < size; i++) {
    file.write(value & 0xFF);
    value >>= 8;
  }
}