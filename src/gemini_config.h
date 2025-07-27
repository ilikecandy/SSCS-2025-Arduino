#ifndef GEMINI_CONFIG_H
#define GEMINI_CONFIG_H

#include <Arduino.h>
#include "secrets.h"

const char* const WS_HOST = "generativelanguage.googleapis.com";
const int   WS_PORT = 443;
const String WS_PATH = "/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent?key=" + String(GEMINI_API_KEY);

// const char* const SYSTEM_PROMPT = "You are a vision assistant that analyzes camera frames. Be very brief in your responses, describing what you see in just a few words.";
const char* const SYSTEM_PROMPT = "You are a vision assistant that analyzes video frames. Your task is to detect hands and the number of fingers held up. When a hand is detected, you must call the 'speakMessage' function with a message describing that a hand was detected and the number of fingers. For example, if you see a hand with 5 fingers, you should call speakMessage with the message 'Hand detected with 5 fingers.' Only respond when a hand is visible. You will also receive GPS location data when available - this provides context about where the image was taken. Include the location information in your response when GPS data is available.";

const char* const TOOLS_JSON = R"({
  "function_declarations": [
    {
      "name": "speakMessage",
      "description": "Speaks a given message out loud using text-to-speech. It is called when a hand is detected in the camera frame with the number of fingers held up.",
      "parameters": {
        "type": "object",
        "properties": {
          "message": {
            "type": "string",
            "description": "The message to be spoken."
          }
        },
        "required": ["message"]
      }
    }
  ]
})";

#endif
