/**
 * Rainbow effect - Smooth cycling rainbow across all LEDs (Schema-aware)
 * 
 * Migrated to ParamSchema for dynamic UI controls
 */

#include "../../core/effect_registry.h"
#include "../../core/param_schema.h"

namespace lume {

// Define parameter slots
namespace rainbow {
    constexpr uint8_t SPEED = 0;
    constexpr uint8_t DENSITY = 1;
}

// Define schema (lives in flash)
DEFINE_EFFECT_SCHEMA(rainbowSchema,
    ParamDesc::Int("speed", "Speed", 128, 1, 255),
    ParamDesc::Int("density", "Density", 85, 1, 255)
);

void effectRainbow(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame) {
    (void)firstFrame;
    
    // Read from ParamValues slots (schema-aware)
    uint8_t speed = params.getInt(rainbow::SPEED);
    uint8_t density = params.getInt(rainbow::DENSITY);
    
    // Speed controls how fast the rainbow moves
    uint8_t hue = (frame * speed) >> 6;
    
    // Density controls how spread out the rainbow is
    uint8_t deltaHue = density;
    
    view.rainbow(hue, deltaHue);
}

// Register with schema
REGISTER_EFFECT_SCHEMA(effectRainbow, "rainbow", "Rainbow", Animated, rainbowSchema, 0);

} // namespace lume
