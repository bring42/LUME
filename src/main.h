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

// ===========================================================================
// API Handler Functions - Main Routes
// ===========================================================================
// These handlers remain in main.cpp as they are core to the application

// ===========================================================================
// API Handler Functions - Segments (v2 Architecture)
// ===========================================================================
// New segment-based control for multi-zone LED management

void handleApiSegments(AsyncWebServerRequest* request);
void handleApiSegmentsPost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);

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
