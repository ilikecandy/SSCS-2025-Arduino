# SSCS-2025-Arduino

Meet Halo | IEEE SSCS Arduino Contest 2025
https://www.youtube.com/watch?v=oZpIQhcpVok&t=1s

Halo is a wearable smart assistant that helps people with vision loss navigate the world safely using live video, AI, and natural voice interaction. It detects hazards in real time, answers questions with contextual awareness, supports over 130 languages, and connects to a mobile companion app for alerts and settings.

Made by Aayan Karmali and Jacob Tian, representing the University of Waterloo.

Hardware:
- ESP32-Wrover-E CAM board
- GY-NEO6MV2 GPS module
- INMP441 microphone
- MAX98357A speaker amplifier
- Push button for SOS/push-to-talk
- LED for areas with low light to assist with vision and Halo functionality
- Battery with power switch

Software:
- Arduino source code: github.com/ilikecandy/SSCS-2025-Arduino
- Live video analysis via Gemini Live API through WebSocket connections
- Speech-to-text and wake word detection via Deepgram API
- Multilingual text-to-speech via Deepgram and Google Translate API (130+ languages)
- Navigation & place information via Google Maps Platform API
- Supabase for realtime alerts and preferences (language, wake word, etc.)
- Firebase Cloud Messaging for push notifications
- Vercel API backend for communication between the ESP32, the database, and the app
- Source code: github.com/StockerMC/SSCS-2025-API
- Companion app built with Expo and React Native for managing settings, reviewing alerts, and SOS notifications
- Source code: github.com/StockerMC/SSCS-2025-App

Design & Prototyping:
- Interactive design: p3d.in/jKlt1
- Developed through five hardware iterations: from a breadboard prototype to a 3D-printed wearable model with a neck strap design
- Improvements included a camera cover for privacy, better strap comfort, dedicated space for components and ports, and integrated battery housing
