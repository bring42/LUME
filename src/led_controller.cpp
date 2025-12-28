// Force RMT driver for ESP32-S3 - must be before FastLED includes
#define FASTLED_ALL_PINS_HARDWARE_SPI
#define FASTLED_ESP32_FLASH_LOCK 1

#include "led_controller.h"

LedController ledController;

LedController::LedController() :
    ledCount(160),
    dataPin(21),
    lastUpdate(0),
    hue(0),
    frameCounter(0)
{
    memset(cooling, 0, sizeof(cooling));
    memset(heat, 0, sizeof(heat));
}

void LedController::begin(uint8_t pin, uint16_t count) {
    dataPin = pin;
    ledCount = min(count, (uint16_t)MAX_LED_COUNT);
    
    // IMPORTANT: FastLED requires COMPILE-TIME pin configuration for ESP32-S3.
    // The pin number in addLeds<> template MUST match the physical wiring.
    // To change LED pin: modify the line below AND rebuild the firmware.
    // Runtime ledPin config is stored but only applies after firmware rebuild.
    // Default pin: GPIO 21 (confirmed working on T-Display S3)
    
    // Initialize FastLED - using GPIO 21 as default
    // For ESP32-S3, we use the RMT driver
    FastLED.addLeds<WS2812B, 21, GRB>(leds, ledCount);
    FastLED.setBrightness(state.brightness);
    
    // Apply color correction for more accurate colors
    FastLED.setCorrection(TypicalLEDStrip);
    
    // Set power limit to prevent PSU overdraw
    // Default: 5V, 2A (2000mA) - adjust in constants.h for your setup
    FastLED.setMaxPowerInVoltsAndMilliamps(LED_VOLTAGE, LED_MAX_MILLIAMPS);
    
    FastLED.clear();
    FastLED.show();
    
    updatePalette();
}

void LedController::reconfigure(uint8_t pin, uint16_t count) {
    // Note: Changing pin at runtime requires re-init which isn't fully supported
    // For now, we just update the count
    ledCount = min(count, (uint16_t)MAX_LED_COUNT);
    FastLED.clear();
    FastLED.show();
}

void LedController::update() {
    if (!state.power) {
        FastLED.clear();
        FastLED.show();
        return;
    }
    
    // Handle nightlight mode - gradual brightness fade
    if (state.nightlightActive) {
        unsigned long elapsed = millis() - state.nightlightStart;
        unsigned long durationMs = (unsigned long)state.nightlightDuration * 1000UL;
        
        if (elapsed >= durationMs) {
            // Nightlight complete
            state.brightness = state.nightlightTargetBri;
            state.nightlightActive = false;
            if (state.nightlightTargetBri == 0) {
                state.power = false;  // Turn off if faded to 0
            }
        } else {
            // Calculate current brightness (linear interpolation)
            float progress = (float)elapsed / (float)durationMs;
            int16_t briDiff = (int16_t)state.nightlightTargetBri - (int16_t)state.nightlightStartBri;
            state.brightness = state.nightlightStartBri + (int16_t)(briDiff * progress);
        }
    }
    
    unsigned long now = millis();
    uint16_t interval = getUpdateInterval();
    
    if (now - lastUpdate < interval) {
        return;
    }
    lastUpdate = now;
    
    // Update brightness
    FastLED.setBrightness(state.brightness);
    
    // Run current effect
    switch (state.effect) {
        case Effect::SOLID:         effectSolid(); break;
        case Effect::RAINBOW:       effectRainbow(); break;
        case Effect::CONFETTI:      effectConfetti(); break;
        case Effect::FIRE:          effectFire(); break;
        case Effect::COLOR_WAVES:   effectColorWaves(); break;
        case Effect::THEATER_CHASE: effectTheaterChase(); break;
        case Effect::GRADIENT:      effectGradient(); break;
        case Effect::SPARKLE:       effectSparkle(); break;
        case Effect::PULSE:         effectPulse(); break;
        case Effect::NOISE:         effectNoise(); break;
        case Effect::METEOR:        effectMeteor(); break;
        case Effect::TWINKLE:       effectTwinkle(); break;
        case Effect::SINELON:       effectSinelon(); break;
        case Effect::CANDLE:        effectCandle(); break;
        case Effect::BREATHE:       effectBreatheSimple(); break;
        case Effect::CUSTOM_GENERATED: effectCustomGenerated(); break;
        default:                    effectRainbow(); break;
    }
    
    FastLED.show();
    frameCounter++;
}

LedState& LedController::getState() {
    return state;
}

void LedController::setState(const LedState& newState) {
    state = newState;
    updatePalette();
}

void LedController::stateToJson(JsonDocument& doc) {
    doc["power"] = state.power;
    doc["brightness"] = state.brightness;
    doc["effect"] = effectToString(state.effect);
    doc["palette"] = paletteToString(state.palette);
    doc["speed"] = state.speed;
    
    JsonArray primary = doc["primaryColor"].to<JsonArray>();
    primary.add(state.primaryColor.r);
    primary.add(state.primaryColor.g);
    primary.add(state.primaryColor.b);
    
    JsonArray secondary = doc["secondaryColor"].to<JsonArray>();
    secondary.add(state.secondaryColor.r);
    secondary.add(state.secondaryColor.g);
    secondary.add(state.secondaryColor.b);
    
    doc["notes"] = state.notes;
    
    JsonObject custom = doc["custom"].to<JsonObject>();
    custom["type"] = state.custom.type;
    custom["param1"] = state.custom.param1;
    custom["param2"] = state.custom.param2;
    custom["param3"] = state.custom.param3;
    custom["param4"] = state.custom.param4;
    
    // Nightlight status
    JsonObject nightlight = doc["nightlight"].to<JsonObject>();
    nightlight["active"] = state.nightlightActive;
    if (state.nightlightActive) {
        nightlight["progress"] = getNightlightProgress();
        nightlight["duration"] = state.nightlightDuration;
        nightlight["targetBrightness"] = state.nightlightTargetBri;
    }
}

bool LedController::stateFromJson(const JsonDocument& doc) {
    if (doc["power"].is<bool>()) {
        state.power = doc["power"].as<bool>();
    }
    if (doc["brightness"].is<int>()) {
        state.brightness = constrain(doc["brightness"].as<int>(), 0, 255);
    } else if (doc["brightness"].is<const char*>()) {
        state.brightness = constrain(atoi(doc["brightness"].as<const char*>()), 0, 255);
    }
    if (doc["effect"].is<const char*>()) {
        state.effect = stringToEffect(doc["effect"].as<const char*>());
    }
    if (doc["palette"].is<const char*>()) {
        state.palette = stringToPalette(doc["palette"].as<const char*>());
        updatePalette();
    }
    if (doc["speed"].is<int>()) {
        state.speed = constrain(doc["speed"].as<int>(), 1, 200);
    } else if (doc["speed"].is<const char*>()) {
        state.speed = constrain(atoi(doc["speed"].as<const char*>()), 1, 200);
    }
    
    // Handle primaryColor as array or hex string
    JsonVariantConst primaryVar = doc["primaryColor"];
    if (primaryVar.is<JsonArrayConst>()) {
        JsonArrayConst arr = primaryVar.as<JsonArrayConst>();
        if (arr.size() >= 3) {
            state.primaryColor = CRGB(arr[0].as<uint8_t>(), arr[1].as<uint8_t>(), arr[2].as<uint8_t>());
        }
    } else if (primaryVar.is<const char*>()) {
        String hex = primaryVar.as<String>();
        if (hex.startsWith("#") && hex.length() == 7) {
            long color = strtol(hex.substring(1).c_str(), NULL, 16);
            state.primaryColor = CRGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
        }
    }
    
    // Handle secondaryColor as array or hex string
    JsonVariantConst secondaryVar = doc["secondaryColor"];
    if (secondaryVar.is<JsonArrayConst>()) {
        JsonArrayConst arr = secondaryVar.as<JsonArrayConst>();
        if (arr.size() >= 3) {
            state.secondaryColor = CRGB(arr[0].as<uint8_t>(), arr[1].as<uint8_t>(), arr[2].as<uint8_t>());
        }
    } else if (secondaryVar.is<const char*>()) {
        String hex = secondaryVar.as<String>();
        if (hex.startsWith("#") && hex.length() == 7) {
            long color = strtol(hex.substring(1).c_str(), NULL, 16);
            state.secondaryColor = CRGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
        }
    }
    
    if (doc["notes"].is<const char*>()) {
        state.notes = doc["notes"].as<String>();
    }
    
    // Custom params
    JsonVariantConst customVar = doc["custom"];
    if (customVar.is<JsonObjectConst>()) {
        JsonObjectConst custom = customVar.as<JsonObjectConst>();
        if (custom["type"].is<const char*>()) {
            state.custom.type = custom["type"].as<String>();
        }
        if (custom["param1"].is<int>()) state.custom.param1 = custom["param1"].as<uint8_t>();
        if (custom["param2"].is<int>()) state.custom.param2 = custom["param2"].as<uint8_t>();
        if (custom["param3"].is<int>()) state.custom.param3 = custom["param3"].as<uint8_t>();
        if (custom["param4"].is<int>()) state.custom.param4 = custom["param4"].as<uint8_t>();
    }
    
    return true;
}

bool LedController::applyEffectSpec(const JsonDocument& spec, String& errorMsg) {
    // Check for mode field
    String mode = "effect";  // default
    if (spec["mode"].is<const char*>()) {
        mode = spec["mode"].as<String>();
        mode.toLowerCase();
    }
    
    // Handle pixels mode - direct LED control
    if (mode == "pixels") {
        if (!spec["pixels"].is<JsonObjectConst>()) {
            errorMsg = "Mode 'pixels' requires 'pixels' object";
            return false;
        }
        JsonObjectConst pixels = spec["pixels"].as<JsonObjectConst>();
        
        // Fill all with single color
        if (pixels["fill"].is<JsonArrayConst>()) {
            JsonArrayConst fill = pixels["fill"].as<JsonArrayConst>();
            if (fill.size() >= 3) {
                CRGB color(fill[0].as<uint8_t>(), fill[1].as<uint8_t>(), fill[2].as<uint8_t>());
                fill_solid(leds, ledCount, color);
                FastLED.show();
                state.power = true;
                errorMsg = "";
                return true;
            }
        }
        
        // Gradient
        if (pixels["gradient"].is<JsonObjectConst>()) {
            JsonObjectConst grad = pixels["gradient"].as<JsonObjectConst>();
            JsonArrayConst from = grad["from"].as<JsonArrayConst>();
            JsonArrayConst to = grad["to"].as<JsonArrayConst>();
            
            if (from.size() >= 3 && to.size() >= 3) {
                CRGB startColor(from[0].as<uint8_t>(), from[1].as<uint8_t>(), from[2].as<uint8_t>());
                CRGB endColor(to[0].as<uint8_t>(), to[1].as<uint8_t>(), to[2].as<uint8_t>());
                fill_gradient_RGB(leds, 0, startColor, ledCount - 1, endColor);
                FastLED.show();
                state.power = true;
                errorMsg = "";
                return true;
            }
        }
        
        // Individual pixels array
        if (pixels["pixels"].is<JsonArrayConst>()) {
            JsonArrayConst arr = pixels["pixels"].as<JsonArrayConst>();
            uint16_t count = min((uint16_t)arr.size(), ledCount);
            
            for (uint16_t i = 0; i < count; i++) {
                JsonArrayConst pixel = arr[i].as<JsonArrayConst>();
                if (pixel.size() >= 3) {
                    leds[i].r = pixel[0].as<uint8_t>();
                    leds[i].g = pixel[1].as<uint8_t>();
                    leds[i].b = pixel[2].as<uint8_t>();
                }
            }
            FastLED.show();
            state.power = true;
            errorMsg = "";
            return true;
        }
        
        errorMsg = "No valid pixel data in 'pixels' object";
        return false;
    }
    
    // Effect mode - validate required fields
    if (!spec["effect"].is<const char*>()) {
        errorMsg = "Missing 'effect' field";
        return false;
    }
    
    // Apply the spec
    if (!stateFromJson(spec)) {
        errorMsg = "Failed to parse effect specification";
        return false;
    }
    
    // Ensure power is on when applying an effect
    state.power = true;
    
    errorMsg = "";
    return true;
}

void LedController::setPower(bool on) {
    state.power = on;
}

void LedController::setBrightness(uint8_t brightness) {
    state.brightness = brightness;
}

void LedController::setEffect(Effect effect) {
    state.effect = effect;
}

void LedController::setPalette(PaletteType palette) {
    state.palette = palette;
    updatePalette();
}

void LedController::setSpeed(uint8_t speed) {
    state.speed = constrain(speed, 1, 200);
}

void LedController::setColor(uint8_t r, uint8_t g, uint8_t b) {
    state.primaryColor = CRGB(r, g, b);
}

void LedController::setSecondaryColor(uint8_t r, uint8_t g, uint8_t b) {
    state.secondaryColor = CRGB(r, g, b);
}

const char* LedController::effectToString(Effect effect) {
    switch (effect) {
        case Effect::SOLID:           return "solid";
        case Effect::RAINBOW:         return "rainbow";
        case Effect::CONFETTI:        return "confetti";
        case Effect::FIRE:            return "fire";
        case Effect::COLOR_WAVES:     return "colorwaves";
        case Effect::THEATER_CHASE:   return "theater";
        case Effect::GRADIENT:        return "gradient";
        case Effect::SPARKLE:         return "sparkle";
        case Effect::PULSE:           return "pulse";
        case Effect::NOISE:           return "noise";
        case Effect::METEOR:          return "meteor";
        case Effect::TWINKLE:         return "twinkle";
        case Effect::SINELON:         return "sinelon";
        case Effect::CANDLE:          return "candle";
        case Effect::BREATHE:         return "breathe";
        case Effect::CUSTOM_GENERATED: return "custom";
        default:                      return "rainbow";
    }
}

Effect LedController::stringToEffect(const char* str) {
    if (!str) return Effect::RAINBOW;
    String s = String(str);
    s.toLowerCase();
    
    if (s == "solid") return Effect::SOLID;
    if (s == "rainbow") return Effect::RAINBOW;
    if (s == "confetti") return Effect::CONFETTI;
    if (s == "fire") return Effect::FIRE;
    if (s == "colorwaves" || s == "color_waves") return Effect::COLOR_WAVES;
    if (s == "theater" || s == "theater_chase" || s == "theaterchase") return Effect::THEATER_CHASE;
    if (s == "gradient") return Effect::GRADIENT;
    if (s == "sparkle") return Effect::SPARKLE;
    if (s == "pulse") return Effect::PULSE;
    if (s == "noise") return Effect::NOISE;
    if (s == "meteor" || s == "shooting_star") return Effect::METEOR;
    if (s == "twinkle" || s == "twinkles") return Effect::TWINKLE;
    if (s == "sinelon" || s == "dot") return Effect::SINELON;
    if (s == "candle" || s == "flicker" || s == "candlelight") return Effect::CANDLE;
    if (s == "breathe" || s == "breathing") return Effect::BREATHE;
    if (s == "custom" || s == "custom_generated") return Effect::CUSTOM_GENERATED;
    
    return Effect::RAINBOW;
}

const char* LedController::paletteToString(PaletteType palette) {
    switch (palette) {
        case PaletteType::RAINBOW: return "rainbow";
        case PaletteType::LAVA:    return "lava";
        case PaletteType::OCEAN:   return "ocean";
        case PaletteType::PARTY:   return "party";
        case PaletteType::FOREST:  return "forest";
        case PaletteType::CLOUD:   return "cloud";
        case PaletteType::HEAT:    return "heat";
        case PaletteType::SUNSET:  return "sunset";
        case PaletteType::AUTUMN:  return "autumn";
        case PaletteType::RETRO:   return "retro";
        case PaletteType::ICE:     return "ice";
        case PaletteType::PINK:    return "pink";
        case PaletteType::CUSTOM:  return "custom";
        default:                   return "rainbow";
    }
}

PaletteType LedController::stringToPalette(const char* str) {
    if (!str) return PaletteType::RAINBOW;
    String s = String(str);
    s.toLowerCase();
    
    if (s == "rainbow") return PaletteType::RAINBOW;
    if (s == "lava") return PaletteType::LAVA;
    if (s == "ocean") return PaletteType::OCEAN;
    if (s == "party") return PaletteType::PARTY;
    if (s == "forest") return PaletteType::FOREST;
    if (s == "cloud") return PaletteType::CLOUD;
    if (s == "heat") return PaletteType::HEAT;
    if (s == "sunset") return PaletteType::SUNSET;
    if (s == "autumn") return PaletteType::AUTUMN;
    if (s == "retro") return PaletteType::RETRO;
    if (s == "ice") return PaletteType::ICE;
    if (s == "pink") return PaletteType::PINK;
    if (s == "custom") return PaletteType::CUSTOM;
    
    return PaletteType::RAINBOW;
}

uint16_t LedController::getUpdateInterval() {
    // Speed 1 = slowest (100ms), Speed 200 = fastest (5ms)
    return map(state.speed, 1, 200, 100, 5);
}

void LedController::updatePalette() {
    switch (state.palette) {
        case PaletteType::RAINBOW:
            currentPalette = RainbowColors_p;
            break;
        case PaletteType::LAVA:
            currentPalette = LavaColors_p;
            break;
        case PaletteType::OCEAN:
            currentPalette = OceanColors_p;
            break;
        case PaletteType::PARTY:
            currentPalette = PartyColors_p;
            break;
        case PaletteType::FOREST:
            currentPalette = ForestColors_p;
            break;
        case PaletteType::CLOUD:
            currentPalette = CloudColors_p;
            break;
        case PaletteType::HEAT:
            currentPalette = HeatColors_p;
            break;
        case PaletteType::SUNSET:
            // Warm sunset: orange -> red -> purple -> deep blue
            currentPalette = CRGBPalette16(
                CRGB(255, 140, 0),   // Orange
                CRGB(255, 69, 0),    // OrangeRed
                CRGB(255, 20, 60),   // Crimson
                CRGB(199, 21, 133),  // MediumVioletRed
                CRGB(148, 0, 211),   // DarkViolet
                CRGB(75, 0, 130),    // Indigo
                CRGB(25, 25, 112),   // MidnightBlue
                CRGB(0, 0, 80),      // Dark blue
                CRGB(25, 25, 112),   // MidnightBlue
                CRGB(75, 0, 130),    // Indigo
                CRGB(148, 0, 211),   // DarkViolet
                CRGB(199, 21, 133),  // MediumVioletRed
                CRGB(255, 20, 60),   // Crimson
                CRGB(255, 69, 0),    // OrangeRed
                CRGB(255, 140, 0),   // Orange
                CRGB(255, 200, 100)  // Light orange
            );
            break;
        case PaletteType::AUTUMN:
            // Fall colors: red, orange, yellow, brown
            currentPalette = CRGBPalette16(
                CRGB(139, 69, 19),   // SaddleBrown
                CRGB(210, 105, 30),  // Chocolate
                CRGB(255, 140, 0),   // DarkOrange
                CRGB(255, 165, 0),   // Orange
                CRGB(255, 200, 0),   // Gold
                CRGB(255, 215, 0),   // Yellow-gold
                CRGB(255, 140, 0),   // DarkOrange
                CRGB(178, 34, 34),   // Firebrick
                CRGB(139, 0, 0),     // DarkRed
                CRGB(165, 42, 42),   // Brown
                CRGB(210, 105, 30),  // Chocolate
                CRGB(184, 134, 11),  // DarkGoldenrod
                CRGB(218, 165, 32),  // Goldenrod
                CRGB(255, 165, 0),   // Orange
                CRGB(205, 92, 92),   // IndianRed
                CRGB(139, 69, 19)    // SaddleBrown
            );
            break;
        case PaletteType::RETRO:
            // Synthwave/retro: cyan, magenta, purple, pink
            currentPalette = CRGBPalette16(
                CRGB(0, 255, 255),   // Cyan
                CRGB(0, 200, 255),   // Light cyan
                CRGB(80, 80, 255),   // Blue-violet
                CRGB(138, 43, 226),  // BlueViolet
                CRGB(186, 85, 211),  // MediumOrchid
                CRGB(255, 0, 255),   // Magenta
                CRGB(255, 20, 147),  // DeepPink
                CRGB(255, 105, 180), // HotPink
                CRGB(255, 0, 255),   // Magenta
                CRGB(186, 85, 211),  // MediumOrchid
                CRGB(138, 43, 226),  // BlueViolet
                CRGB(75, 0, 130),    // Indigo
                CRGB(0, 100, 200),   // Dark cyan
                CRGB(0, 200, 255),   // Light cyan
                CRGB(0, 255, 255),   // Cyan
                CRGB(100, 255, 255)  // Pale cyan
            );
            break;
        case PaletteType::ICE:
            // Cold ice: white, light blue, deep blue
            currentPalette = CRGBPalette16(
                CRGB(255, 255, 255), // White
                CRGB(200, 220, 255), // Ice white
                CRGB(135, 206, 250), // LightSkyBlue
                CRGB(100, 180, 255), // Light blue
                CRGB(70, 130, 180),  // SteelBlue
                CRGB(30, 100, 180),  // Medium blue
                CRGB(0, 80, 160),    // Deep blue
                CRGB(20, 60, 120),   // Dark blue
                CRGB(0, 80, 160),    // Deep blue
                CRGB(30, 100, 180),  // Medium blue
                CRGB(70, 130, 180),  // SteelBlue
                CRGB(100, 180, 255), // Light blue
                CRGB(135, 206, 250), // LightSkyBlue
                CRGB(173, 216, 230), // LightBlue
                CRGB(200, 230, 255), // Pale ice
                CRGB(255, 255, 255)  // White
            );
            break;
        case PaletteType::PINK:
            // Pink variations: light pink to deep magenta
            currentPalette = CRGBPalette16(
                CRGB(255, 182, 193), // LightPink
                CRGB(255, 105, 180), // HotPink
                CRGB(255, 20, 147),  // DeepPink
                CRGB(255, 0, 128),   // Bright pink
                CRGB(219, 112, 147), // PaleVioletRed
                CRGB(199, 21, 133),  // MediumVioletRed
                CRGB(255, 0, 255),   // Magenta
                CRGB(218, 112, 214), // Orchid
                CRGB(238, 130, 238), // Violet
                CRGB(255, 0, 255),   // Magenta
                CRGB(199, 21, 133),  // MediumVioletRed
                CRGB(255, 20, 147),  // DeepPink
                CRGB(255, 105, 180), // HotPink
                CRGB(255, 130, 170), // Soft pink
                CRGB(255, 182, 193), // LightPink
                CRGB(255, 200, 220)  // Pale pink
            );
            break;
        case PaletteType::CUSTOM:
            // Create custom palette from primary/secondary colors
            fill_gradient_RGB(currentPalette, 0, state.primaryColor, 7, state.secondaryColor);
            fill_gradient_RGB(currentPalette, 8, state.secondaryColor, 15, state.primaryColor);
            break;
    }
}

// Effect implementations

void LedController::effectSolid() {
    fill_solid(leds, ledCount, state.primaryColor);
}

void LedController::effectRainbow() {
    // Use the selected palette for rainbow effect
    for (int i = 0; i < ledCount; i++) {
        uint8_t index = (hue + i * 256 / ledCount) % 256;
        leds[i] = ColorFromPalette(currentPalette, index, 255, LINEARBLEND);
    }
    hue++;
}

void LedController::effectConfetti() {
    fadeToBlackBy(leds, ledCount, 10);
    int pos = random16(ledCount);
    // Use palette for confetti colors
    leds[pos] += ColorFromPalette(currentPalette, hue + random8(64), 255, LINEARBLEND);
    hue++;
}

void LedController::effectFire() {
    // Fire2012 by Mark Kriegsman
    #define COOLING  55
    #define SPARKING 120
    
    // Cool down every cell a little
    for (int i = 0; i < ledCount; i++) {
        heat[i] = qsub8(heat[i], random8(0, ((COOLING * 10) / ledCount) + 2));
    }
    
    // Heat from each cell drifts up and diffuses
    for (int k = ledCount - 1; k >= 2; k--) {
        heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2]) / 3;
    }
    
    // Randomly ignite new sparks near bottom
    if (random8() < SPARKING) {
        int y = random8(7);
        heat[y] = qadd8(heat[y], random8(160, 255));
    }
    
    // Map heat to colors - use palette if not default heat palette
    for (int j = 0; j < ledCount; j++) {
        // Use palette to colorize the heat value
        uint8_t heatIndex = heat[j];
        CRGB color;
        if (state.palette == PaletteType::HEAT || state.palette == PaletteType::LAVA) {
            color = HeatColor(heat[j]);  // Traditional fire look
        } else {
            // Map heat to palette - hot = high index
            color = ColorFromPalette(currentPalette, heatIndex, 255, LINEARBLEND);
        }
        leds[j] = color;
    }
}

void LedController::effectColorWaves() {
    static uint16_t sPseudotime = 0;
    static uint16_t sLastMillis = 0;
    static uint16_t sHue16 = 0;
    
    uint8_t sat8 = beatsin88(87, 220, 250);
    uint8_t brightdepth = beatsin88(341, 96, 224);
    uint16_t brightnessthetainc16 = beatsin88(203, (25 * 256), (40 * 256));
    uint8_t msmultiplier = beatsin88(147, 23, 60);
    
    uint16_t hue16 = sHue16;
    uint16_t hueinc16 = beatsin88(113, 1, 3000);
    
    uint16_t ms = millis();
    uint16_t deltams = ms - sLastMillis;
    sLastMillis = ms;
    sPseudotime += deltams * msmultiplier;
    sHue16 += deltams * beatsin88(400, 5, 9);
    uint16_t brightnesstheta16 = sPseudotime;
    
    for (uint16_t i = 0; i < ledCount; i++) {
        hue16 += hueinc16;
        uint8_t hue8 = hue16 / 256;
        
        brightnesstheta16 += brightnessthetainc16;
        uint16_t b16 = sin16(brightnesstheta16) + 32768;
        
        uint16_t bri16 = (uint32_t)((uint32_t)b16 * (uint32_t)b16) / 65536;
        uint8_t bri8 = (uint32_t)(((uint32_t)bri16) * brightdepth) / 65536;
        bri8 += (255 - brightdepth);
        
        CRGB newcolor = CHSV(hue8, sat8, bri8);
        
        uint16_t pixelnumber = i;
        pixelnumber = (ledCount - 1) - pixelnumber;
        
        nblend(leds[pixelnumber], newcolor, 64);
    }
}

void LedController::effectTheaterChase() {
    fadeToBlackBy(leds, ledCount, 100);
    
    for (int i = 0; i < ledCount; i += 3) {
        int idx = (i + frameCounter) % ledCount;
        leds[idx] = ColorFromPalette(currentPalette, (hue + i * 4) % 256);
    }
    hue++;
}

void LedController::effectGradient() {
    // Use palette for gradient
    for (int i = 0; i < ledCount; i++) {
        uint8_t index = map(i, 0, ledCount - 1, 0, 255);
        leds[i] = ColorFromPalette(currentPalette, index, 255, LINEARBLEND);
    }
}

void LedController::effectSparkle() {
    // Fade all
    fadeToBlackBy(leds, ledCount, 20);
    
    // Add sparkles using palette colors
    int sparks = state.speed / 20;
    for (int i = 0; i < sparks; i++) {
        int pos = random16(ledCount);
        leds[pos] = ColorFromPalette(currentPalette, random8(), 255, LINEARBLEND);
    }
}

void LedController::effectPulse() {
    uint8_t beat = beatsin8(state.speed / 5, 0, 255);
    // Use palette for pulse colors
    CRGB color = ColorFromPalette(currentPalette, beat, 255, LINEARBLEND);
    
    // Pulse from center
    uint16_t center = ledCount / 2;
    uint8_t width = map(beat, 0, 255, 10, ledCount / 2);
    
    fill_solid(leds, ledCount, CRGB::Black);
    
    for (int i = 0; i < width; i++) {
        uint8_t brightness = map(i, 0, width, 255, 50);
        if (center + i < ledCount) leds[center + i] = color;
        if (center - i >= 0) leds[center - i] = color;
        leds[center + i].nscale8(brightness);
        if (center - i >= 0) leds[center - i].nscale8(brightness);
    }
}

void LedController::effectNoise() {
    static uint16_t x = random16();
    static uint16_t y = random16();
    static uint16_t z = random16();
    
    uint16_t scale = 30;
    
    for (int i = 0; i < ledCount; i++) {
        uint8_t noise = inoise8(x + i * scale, y, z);
        uint8_t index = noise;
        leds[i] = ColorFromPalette(currentPalette, index, 255, LINEARBLEND);
    }
    
    z += state.speed / 10;
}

// ============================================
// New Effects (v1.1)
// ============================================

void LedController::effectMeteor() {
    // Meteor/shooting star with fading tail
    static uint16_t meteorPos = 0;
    
    uint8_t tailDecay = 64;  // How fast the tail fades
    uint8_t meteorSize = 4;  // Size of the bright head
    
    // Fade all LEDs
    for (int i = 0; i < ledCount; i++) {
        if (random8() < tailDecay) {
            leds[i].fadeToBlackBy(tailDecay);
        }
    }
    
    // Draw meteor head
    for (int i = 0; i < meteorSize; i++) {
        int pos = meteorPos - i;
        if (pos >= 0 && pos < ledCount) {
            leds[pos] = state.primaryColor;
        }
    }
    
    meteorPos++;
    if (meteorPos >= ledCount + meteorSize) {
        meteorPos = 0;
    }
}

void LedController::effectTwinkle() {
    // Random LEDs fade in and out - cozy twinkling
    static uint8_t twinkleState[MAX_LED_COUNT] = {0};  // 0=off, 1-127=fading in, 128-255=fading out
    
    uint8_t spawnChance = map(state.speed, 1, 200, 5, 40);
    
    for (int i = 0; i < ledCount; i++) {
        if (twinkleState[i] == 0) {
            // Maybe start a new twinkle
            if (random8() < spawnChance) {
                twinkleState[i] = 1;
            }
        } else if (twinkleState[i] < 128) {
            // Fading in
            twinkleState[i] += 4;
            if (twinkleState[i] >= 128) twinkleState[i] = 128;
            uint8_t bri = twinkleState[i] * 2;
            leds[i] = state.primaryColor;
            leds[i].nscale8(bri);
        } else {
            // Fading out
            twinkleState[i] += 2;
            if (twinkleState[i] < 128) twinkleState[i] = 0;  // Wrapped around
            uint8_t bri = (255 - twinkleState[i]) * 2;
            leds[i] = state.primaryColor;
            leds[i].nscale8(bri);
            if (twinkleState[i] >= 254) twinkleState[i] = 0;
        }
    }
}

void LedController::effectSinelon() {
    // Bouncing dot with rainbow trail
    fadeToBlackBy(leds, ledCount, 20);
    
    // Use beatsin16 for smoother motion
    uint16_t pos = beatsin16(state.speed / 10 + 5, 0, ledCount - 1);
    
    // Color cycles with position
    leds[pos] += ColorFromPalette(currentPalette, hue, 255, LINEARBLEND);
    hue++;
}

void LedController::effectCandle() {
    // Symmetric candle flicker across whole strip - cozy and warm
    // All LEDs flicker together with slight variations
    
    static uint8_t baseFlicker = 200;
    static uint8_t flickerTarget = 200;
    static unsigned long lastFlickerChange = 0;
    
    unsigned long now = millis();
    
    // Occasionally pick a new flicker target
    if (now - lastFlickerChange > random8(50, 150)) {
        lastFlickerChange = now;
        // Mostly stay bright, occasionally dip
        if (random8() < 30) {
            // Bigger dip
            flickerTarget = random8(100, 160);
        } else if (random8() < 100) {
            // Small dip
            flickerTarget = random8(180, 220);
        } else {
            // Stay bright
            flickerTarget = random8(220, 255);
        }
    }
    
    // Smooth transition to target
    if (baseFlicker < flickerTarget) baseFlicker += 3;
    if (baseFlicker > flickerTarget) baseFlicker -= 5;  // Faster dim than brighten
    
    // Apply to all LEDs with tiny per-LED variation
    for (int i = 0; i < ledCount; i++) {
        uint8_t variation = random8(0, 15);
        uint8_t brightness = qadd8(baseFlicker, variation) - 7;
        
        // Warm candle color (or use primary color)
        CRGB color = state.primaryColor;
        // If using default orange-ish, make it more candle-like
        if (state.primaryColor.r > 200 && state.primaryColor.g < 100) {
            color = CRGB(255, 100 + random8(30), 10);
        }
        
        leds[i] = color;
        leds[i].nscale8(brightness);
    }
}

void LedController::effectBreatheSimple() {
    // Simple uniform breathing - entire strip pulses together
    // This is what people actually expect from "breathe"
    
    // Sine wave for smooth breathing, speed affects rate
    uint8_t bpm = map(state.speed, 1, 200, 5, 30);
    uint8_t breath = beatsin8(bpm, 20, 255);  // Never fully off, looks weird
    
    // Apply primary color at breathing brightness
    CRGB color = state.primaryColor;
    color.nscale8(breath);
    
    fill_solid(leds, ledCount, color);
}

void LedController::effectCustomGenerated() {
    // Use custom params to determine sub-effect
    String type = state.custom.type;
    type.toLowerCase();
    
    if (type == "gradient") {
        effectGradient();
    } else if (type == "sparkle") {
        effectSparkle();
    } else if (type == "pulse") {
        effectPulse();
    } else if (type == "noise") {
        effectNoise();
    } else if (type == "wave_up" || type == "wave_down" || type == "wave_center") {
        effectWave(type);
    } else if (type == "breathe") {
        effectBreathe();
    } else if (type == "scanner") {
        effectScanner();
    } else if (type == "comet") {
        effectComet();
    } else if (type == "rain") {
        effectRain();
    } else if (type == "fire_up") {
        effectFireUp();
    } else {
        // Default to color waves with custom palette
        effectColorWaves();
    }
}

// Wave effect - rising, falling, or from center
void LedController::effectWave(const String& direction) {
    static uint16_t position = 0;
    
    uint8_t waveWidth = state.custom.param1 > 0 ? state.custom.param1 : 40;
    uint8_t fadeAmount = state.custom.param2 > 0 ? state.custom.param2 : 20;
    
    fadeToBlackBy(leds, ledCount, fadeAmount);
    
    uint16_t pos = position % (ledCount + waveWidth);
    
    for (int i = 0; i < waveWidth; i++) {
        int pixelPos;
        uint8_t brightness = sin8((i * 255) / waveWidth);
        
        if (direction == "wave_up") {
            pixelPos = pos - waveWidth + i;
        } else if (direction == "wave_down") {
            pixelPos = ledCount - pos + waveWidth - i - 1;
        } else { // wave_center
            int center = ledCount / 2;
            int halfPos = (pos % (ledCount / 2 + waveWidth));
            pixelPos = center + halfPos - waveWidth + i;
            int mirrorPos = center - halfPos + waveWidth - i - 1;
            if (mirrorPos >= 0 && mirrorPos < ledCount) {
                leds[mirrorPos] = state.primaryColor;
                leds[mirrorPos].nscale8(brightness);
            }
        }
        
        if (pixelPos >= 0 && pixelPos < ledCount) {
            leds[pixelPos] = state.primaryColor;
            leds[pixelPos].nscale8(brightness);
        }
    }
    
    position += max(1, state.speed / 20);
}

// Breathing effect with optional direction
void LedController::effectBreathe() {
    uint8_t direction = state.custom.param1; // 0=all, 1=up, 2=down
    
    // Smooth sine wave breathing
    uint8_t breath = beatsin8(state.speed / 10 + 5, 0, 255);
    CRGB color = blend(CRGB::Black, state.primaryColor, breath);
    
    if (direction == 0) {
        // All LEDs breathe together
        fill_solid(leds, ledCount, color);
    } else {
        // Directional breathing - intensity varies along strip
        for (int i = 0; i < ledCount; i++) {
            float factor;
            if (direction == 1) { // up
                factor = (float)i / ledCount;
            } else { // down
                factor = 1.0 - ((float)i / ledCount);
            }
            
            // Phase shift based on position
            uint8_t phase = (uint8_t)(factor * 128);
            uint8_t localBreath = sin8(beat8(state.speed / 10 + 5) + phase);
            leds[i] = blend(state.secondaryColor, state.primaryColor, localBreath);
        }
    }
}

// Larson scanner / Cylon
void LedController::effectScanner() {
    static int16_t pos = 0;
    static int8_t dir = 1;
    
    uint8_t tailLength = state.custom.param2 > 0 ? state.custom.param2 : 20;
    
    fadeToBlackBy(leds, ledCount, 40);
    
    // Main dot
    if (pos >= 0 && pos < ledCount) {
        leds[pos] = state.primaryColor;
    }
    
    // Tail
    for (int i = 1; i <= tailLength; i++) {
        int tailPos = pos - (dir * i);
        if (tailPos >= 0 && tailPos < ledCount) {
            uint8_t fade = 255 - (i * 255 / tailLength);
            leds[tailPos] = state.primaryColor;
            leds[tailPos].nscale8(fade);
        }
    }
    
    pos += dir;
    if (pos >= ledCount || pos < 0) {
        dir = -dir;
        pos = constrain(pos, 0, ledCount - 1);
    }
}

// Comet effect
void LedController::effectComet() {
    static uint16_t pos = 0;
    
    uint8_t direction = state.custom.param1; // 0=up, 1=down
    uint8_t tailLength = state.custom.param2 > 0 ? state.custom.param2 : 30;
    
    fadeToBlackBy(leds, ledCount, 30);
    
    int headPos;
    if (direction == 0) {
        headPos = pos % ledCount;
    } else {
        headPos = ledCount - 1 - (pos % ledCount);
    }
    
    // Comet head
    leds[headPos] = state.primaryColor;
    
    // Comet tail
    for (int i = 1; i <= tailLength; i++) {
        int tailPos;
        if (direction == 0) {
            tailPos = headPos - i;
        } else {
            tailPos = headPos + i;
        }
        
        if (tailPos >= 0 && tailPos < ledCount) {
            uint8_t fade = 255 - (i * 255 / tailLength);
            CRGB tailColor = blend(state.secondaryColor, state.primaryColor, fade);
            leds[tailPos] = tailColor;
            leds[tailPos].nscale8(fade);
        }
    }
    
    pos += max(1, state.speed / 25);
}

// Rain/drops effect
void LedController::effectRain() {
    static uint8_t drops[10] = {0};
    static uint8_t dropPos[10] = {0};
    
    uint8_t density = state.custom.param3 > 0 ? state.custom.param3 : 50;
    
    fadeToBlackBy(leds, ledCount, 50);
    
    // Update and draw drops
    for (int d = 0; d < 10; d++) {
        if (drops[d] > 0) {
            dropPos[d] += max(1, state.speed / 30);
            if (dropPos[d] < ledCount) {
                leds[ledCount - 1 - dropPos[d]] = state.primaryColor;
                leds[ledCount - 1 - dropPos[d]].nscale8(drops[d]);
            } else {
                drops[d] = 0;
            }
        }
    }
    
    // Spawn new drops
    if (random8() < density / 10) {
        for (int d = 0; d < 10; d++) {
            if (drops[d] == 0) {
                drops[d] = random8(150, 255);
                dropPos[d] = 0;
                break;
            }
        }
    }
}

// Fire rising upward (opposite of normal fire)
void LedController::effectFireUp() {
    #define FIRE_COOLING  55
    #define FIRE_SPARKING 120
    
    // Cool down every cell
    for (int i = 0; i < ledCount; i++) {
        heat[i] = qsub8(heat[i], random8(0, ((FIRE_COOLING * 10) / ledCount) + 2));
    }
    
    // Heat drifts up (reversed from normal fire)
    for (int k = 0; k < ledCount - 2; k++) {
        heat[k] = (heat[k + 1] + heat[k + 2] + heat[k + 2]) / 3;
    }
    
    // Randomly ignite new sparks at TOP
    if (random8() < FIRE_SPARKING) {
        int y = ledCount - 1 - random8(7);
        heat[y] = qadd8(heat[y], random8(160, 255));
    }
    
    // Map heat to LED colors using primary color as tint
    for (int j = 0; j < ledCount; j++) {
        CRGB color = HeatColor(heat[j]);
        // Blend with primary color for custom fire colors
        color = blend(color, state.primaryColor, 60);
        leds[j] = color;
    }
}

// ============================================================================
// Nightlight methods
// ============================================================================

void LedController::startNightlight(uint16_t durationSeconds, uint8_t targetBrightness) {
    state.nightlightActive = true;
    state.nightlightDuration = durationSeconds;
    state.nightlightTargetBri = targetBrightness;
    state.nightlightStart = millis();
    state.nightlightStartBri = state.brightness;
    
    // Ensure power is on
    if (!state.power) {
        state.power = true;
    }
}

void LedController::stopNightlight() {
    state.nightlightActive = false;
}

bool LedController::isNightlightActive() const {
    return state.nightlightActive;
}

float LedController::getNightlightProgress() const {
    if (!state.nightlightActive) {
        return 0.0f;
    }
    
    unsigned long elapsed = millis() - state.nightlightStart;
    unsigned long durationMs = (unsigned long)state.nightlightDuration * 1000UL;
    
    if (elapsed >= durationMs) {
        return 1.0f;
    }
    
    return (float)elapsed / (float)durationMs;
}

void LedController::setMaxPower(uint8_t volts, uint16_t milliamps) {
    FastLED.setMaxPowerInVoltsAndMilliamps(volts, milliamps);
}
