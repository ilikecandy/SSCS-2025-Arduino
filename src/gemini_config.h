#ifndef GEMINI_CONFIG_H
#define GEMINI_CONFIG_H

#include <Arduino.h>
#include "secrets.h"

const char* const WS_HOST = "generativelanguage.googleapis.com";
const int   WS_PORT = 443;
const String WS_PATH = "/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent?key=" + String(GEMINI_API_KEY);

const char* const SYSTEM_PROMPT = "You are a vision assistant that analyzes camera frames. Be very brief in your responses, describing what you see in just a few words.";

#endif
