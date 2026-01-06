#ifndef LUME_H
#define LUME_H

/**
 * LUME - Modern FastLED Framework
 * 
 * Main include file - pulls in all core components
 * 
 * Usage:
 *   #include "lume.h"
 *   
 *   void setup() {
 *       lume::controller.begin(160);
 *       auto* strip = lume::controller.createFullStrip();
 *       strip->setEffect("rainbow");
 *   }
 *   
 *   void loop() {
 *       lume::controller.update();
 *   }
 */

// Core types
#include "core/segment_view.h"
#include "core/effect_params.h"
#include "core/effect_registry.h"
#include "core/segment.h"
#include "core/controller.h"

// Effects (include to register them)
#include "visuallib/effects.h"

#endif // LUME_H
