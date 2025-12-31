#pragma once

#include <ESPAsyncWebServer.h>

// Setup web server routes and handlers
void setupServer();

// Periodic maintenance tasks (WebSocket cleanup, broadcast)
void loopServer();
