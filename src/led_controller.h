#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Arduino.h>
#include <FastLED.h>
#include <ArduinoJson.h>
#include "constants.h"

// Effect types enum
enum class Effect : uint8_t {
    SOLID = 0,
    RAINBOW,
    CONFETTI,
    FIRE,
    COLOR_WAVES,
    THEATER_CHASE,
    GRADIENT,
    SPARKLE,
    PULSE,
    NOISE,
    // New effects (v1.1)
    METEOR,
    TWINKLE,
    SINELON,
    CANDLE,
    BREATHE,
    CUSTOM_GENERATED,
    EFFECT_COUNT
};

// Palette types enum
enum class PaletteType : uint8_t {
    RAINBOW = 0,
    LAVA,
    OCEAN,
    PARTY,
    FOREST,
    CLOUD,
    // New palettes (v1.1)
    HEAT,
    SUNSET,
    AUTUMN,
    RETRO,
    ICE,
    PINK,
    // Custom must be last
    CUSTOM,
    PALETTE_COUNT
};

// Custom effect parameters
struct CustomParams {
    String type;  // "gradient", "sparkle", "pulse", "noise"
    uint8_t param1;
    uint8_t param2;
    uint8_t param3;
    uint8_t param4;
};

// LED State structure
struct LedState {
    bool power;
    uint8_t brightness;
    Effect effect;
    PaletteType palette;
    uint8_t speed;  // 1-200
    CRGB primaryColor;
    CRGB secondaryColor;
    CustomParams custom;
    String notes;
    
    // Nightlight mode
    bool nightlightActive;
    uint16_t nightlightDuration;   // Duration in seconds
    uint8_t nightlightTargetBri;   // Target brightness (usually 0)
    unsigned long nightlightStart; // millis() when started
    uint8_t nightlightStartBri;    // Brightness when nightlight started
    
    LedState() :
        power(true),
        brightness(128),
        effect(Effect::RAINBOW),
        palette(PaletteType::RAINBOW),
        speed(100),
        primaryColor(CRGB::Blue),
        secondaryColor(CRGB::Purple),
        custom(),
        notes(""),
        nightlightActive(false),
        nightlightDuration(60),
        nightlightTargetBri(0),
        nightlightStart(0),
        nightlightStartBri(128) {}
};

class LedController {
public:
    LedController();
    
    // Initialize LEDs
    void begin(uint8_t pin, uint16_t count);
    
    // Reconfigure at runtime (changes pin or count)
    void reconfigure(uint8_t pin, uint16_t count);
    
    // Non-blocking update - call this frequently
    void update();
    
    // State management
    LedState& getState();
    void setState(const LedState& newState);
    
    // JSON serialization
    void stateToJson(JsonDocument& doc);
    bool stateFromJson(const JsonDocument& doc);
    
    // Apply effect spec from OpenRouter response
    bool applyEffectSpec(const JsonDocument& spec, String& errorMsg);
    
    // Individual controls
    void setPower(bool on);
    void setBrightness(uint8_t brightness);
    void setEffect(Effect effect);
    void setPalette(PaletteType palette);
    void setSpeed(uint8_t speed);
    void setColor(uint8_t r, uint8_t g, uint8_t b);
    void setSecondaryColor(uint8_t r, uint8_t g, uint8_t b);
    
    // Nightlight control
    void startNightlight(uint16_t durationSeconds, uint8_t targetBrightness = 0);
    void stopNightlight();
    bool isNightlightActive() const;
    float getNightlightProgress() const;  // Returns 0.0-1.0
    
    // Power management
    void setMaxPower(uint8_t volts, uint16_t milliamps);
    
    // Effect name conversions
    static const char* effectToString(Effect effect);
    static Effect stringToEffect(const char* str);
    static const char* paletteToString(PaletteType palette);
    static PaletteType stringToPalette(const char* str);
    
    // Get current LED array (for diagnostics)
    CRGB* getLeds() { return leds; }
    uint16_t getLedCount() { return ledCount; }
    
private:
    CRGB leds[MAX_LED_COUNT];
    uint16_t ledCount;
    uint8_t dataPin;
    LedState state;
    
    // Animation state
    unsigned long lastUpdate;
    uint8_t hue;
    uint16_t frameCounter;
    uint8_t cooling[MAX_LED_COUNT];  // For fire effect
    uint8_t heat[MAX_LED_COUNT];
    
    // Current palette
    CRGBPalette16 currentPalette;
    
    // Effect implementations (non-blocking, frame-based)
    void effectSolid();
    void effectRainbow();
    void effectConfetti();
    void effectFire();
    void effectColorWaves();
    void effectTheaterChase();
    void effectGradient();
    void effectSparkle();
    void effectPulse();
    void effectNoise();
    void effectCustomGenerated();
    
    // New main effects (v1.1)
    void effectMeteor();
    void effectTwinkle();
    void effectSinelon();
    void effectCandle();
    void effectBreatheSimple();  // Uniform intensity breathing
    
    // Advanced custom effects (AI-generated sub-effects)
    void effectWave(const String& direction);
    void effectBreathe();  // Legacy directional breathe
    void effectScanner();
    void effectComet();
    void effectRain();
    void effectFireUp();
    
    // Utility
    void updatePalette();
    uint16_t getUpdateInterval();
};

extern LedController ledController;

#endif // LED_CONTROLLER_H
