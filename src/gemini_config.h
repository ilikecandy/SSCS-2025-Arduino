#ifndef GEMINI_CONFIG_H
#define GEMINI_CONFIG_H

#include <Arduino.h>
#include "secrets.h"

const char* const WS_HOST = "generativelanguage.googleapis.com";
const int   WS_PORT = 443;
const String WS_PATH = "/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent?key=" + String(GEMINI_API_KEY);

// const char* const SYSTEM_PROMPT = "You are a vision assistant that analyzes camera frames. Be very brief in your responses, describing what you see in just a few words.";
const char* const SYSTEM_PROMPT = R"(You are an embedded assistant for a wearable device that helps blind or visually impaired users navigate safely, with the camera worn around chest level. You only respond with function calls to the systemAction function, no text responses.

Only speak when necessary (danger, user asks something, or helpful real-time context). Log or route silently when appropriate. Here are the main examples/rules:

DANGER DETECTION:
- If a cyclist is approaching: 'Warning. Someone is biking toward you.'
- If approaching stairs/drop: 'Caution. Stairs ahead in 2 meters.'
- If a head-level obstacle is ahead: 'Watch out. Head-level obstacle ahead.'
-> Always log and speak in these cases.

CONTEXT-AWARE ASSISTANCE:
- At crosswalk with active traffic: 'You're at a crosswalk. Wait, traffic is active.'
- When it's safe to cross: 'It's safe to cross now.'
- At notable GPS landmark: 'You are at [location name].'
-> Speak only if context is timely and helpful.

USER QUERIES:
- 'Where's the nearest bus stop?' -> 'Looking for the nearest bus stop.' Then route internally.
- 'Remember my wallet is on the dresser.' -> 'Saved.' Then log/store.

FALLS & EMERGENCIES:
- Fall + no response in 5-10s: 'Are you okay? Contacting your caregiver.'
- Cancel confirmed: 'Okay. No emergency sent.'
-> Always speak and trigger alert when unresponsive.

PASSIVE EVENTS (DO NOT SPEAK):
- Location updates or minor ambient noise
-> Log silently. No need to speak unless context changes meaningfully.

MEMORY & OBJECT TRACKING:
- Store memory when asked ('My keys are in my bag').
- Retrieve on request ('Where's my wallet?').
-> Speak on retrieval, not during passive storage.

BEHAVIORAL RULES:
- Always be concise, calm, and relevant.
- Speak only when it improves safety, understanding, or was explicitly asked.
- No redundant announcements or chatter.
- Only return a valid function call. Never output natural language outside of it.

ALSO FOR NOW(!!!):
- When someone is making a hand gesture, call the systemAction function with the action and shouldSpeak true.

If the location is unavailable, and there is no other reason/context to respond, don't.)";

// const char* const TOOLS_JSON = R"({
//   "function_declarations": [
//     {
//       "name": "speakMessage",
//       "description": "Speaks a given message out loud using text-to-speech. It is called when a hand is detected in the camera frame with the number of fingers held up.",
//       "parameters": {
//         "type": "object",
//         "properties": {
//           "message": {
//             "type": "string",
//             "description": "The message to be spoken."
//           }
//         },
//         "required": ["message"]
//       }
//     }
//   ]
// })";

const char* const TOOLS_JSON = R"({
  "function_declarations": [
    {
      "name": "systemAction",
      "description": "Handles user interactions, logging, and system events. Controls if a message should be spoken, logs data, and routes functionally to system modules.",
      "parameters": {
        "type": "object",
        "properties": {
          "intent": {
            "type": "string",
            "description": "The main intent or action the system should respond to. Examples: obstacle_alert, navigation_query, memory_store, emergency_protocol."
          },
          "shouldSpeak": {
            "type": "boolean",
            "description": "Whether the message should be spoken aloud to the user."
          },
          "message": {
            "type": "string",
            "description": "The message to be spoken if shouldSpeak is true."
          },
          "logEntry": {
            "type": "string",
            "description": "A short log message for internal history or caregiver access."
          },
          "routeTo": {
            "type": "string",
            "description": "Optional system function to route this request to, such as alertCaregiver, fetchDirections, storeMemory, etc."
          },
          "routeParams": {
            "type": "object",
            "description": "Parameters to pass to the routed function."
          }
        },
        "required": ["intent"]
      }
    }
  ]
})";

#endif
