/**
 * main.h - Main application header
 * 
 * Provides overview of main.cpp structure:
 * - WiFi & Network Setup
 * - Web Server & API Handlers
 * - Authentication & Security
 * - Controller State Management
 */

#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "storage.h"
#include "core/controller.h"

// ===========================================================================
// Global State
// ===========================================================================

extern AsyncWebServer server;
extern Config config;
extern bool wifiConnected;
extern bool webUiAvailable;

// ===========================================================================
// Setup Functions
// ===========================================================================

// ===========================================================================
// Authentication & Security
// ===========================================================================

bool checkAuth(AsyncWebServerRequest* request);
void sendUnauthorized(AsyncWebServerRequest* request);

// P0.3: chunked-body assembly guard. beginBody() claims the single body slot at
// the first chunk (returns false -> caller sends 409); endBody() releases it and
// MUST be called on every terminal path after a successful beginBody().
bool beginBody(AsyncWebServerRequest* request);
void endBody(AsyncWebServerRequest* request);

// ===========================================================================
// API Handler Functions - Main Routes
// ===========================================================================
// These handlers remain in main.cpp as they are core to the application

// ===========================================================================
// Modular API Handlers (see src/api/ directory)
// ===========================================================================
// Nightlight: api/nightlight.{h,cpp} - Fade-to-sleep timer functionality
// Pixels:     api/pixels.{h,cpp}     - Direct pixel control
// Config:     api/config.{h,cpp}     - System configuration management
// Status:     api/status.{h,cpp}     - Health and status endpoints
// v2 Segments: api/segments.{h,cpp} - Multi-segment control (v2 architecture)

// ===========================================================================
// Helper Functions - Validation
// ===========================================================================

bool validateRgbArray(JsonArrayConst arr);
