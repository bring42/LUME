/**
 * Solid effect - All LEDs same color (Schema-aware)
 * 
 * Migrated to ParamSchema for dynamic UI controls
 */

#include "../../core/effect_registry.h"
#include "../../core/param_schema.h"

namespace lume {

// Define parameter slots
namespace solid {
    constexpr uint8_t COLOR = 0;
}

// Define schema (lives in flash)
DEFINE_EFFECT_SCHEMA(solidSchema,
    ParamDesc::Color("color", "Color", CRGB::Red)
);

void effectSolid(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame) {
    (void)frame;
    (void)firstFrame;
    
    // Read from ParamValues slots (schema-aware)
    CRGB color = params.getColor(solid::COLOR);
    
    view.fill(color);
}

// Register with schema
REGISTER_EFFECT_SCHEMA(effectSolid, "solid", "Solid Color", Solid, solidSchema, 0);

} // namespace lume
