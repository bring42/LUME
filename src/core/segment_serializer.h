#ifndef LUME_SEGMENT_SERIALIZER_H
#define LUME_SEGMENT_SERIALIZER_H

#include <ArduinoJson.h>
#include "segment.h"
#include "param_codec.h"

// The one canonical "segment as JSON" projection (RFC 0001 §4; TECH_DEBT P1.7).
//
// Previously three transports serialized a segment three different ways — the v2
// endpoints (hex colors, has `stop`), the v1 /api/segments list (effect as an
// object), and the WebSocket broadcast (colors as [r,g,b] arrays). They also
// re-implemented the param switch the shared codec already owns (P1.1). Routing
// all of them through this one function gives a single shape, with colors as
// #rrggbb hex via the shared codec. The web UI accepts either color form, so
// unifying to hex is compatible.

namespace lume {

inline void serializeSegment(JsonObject obj, const Segment* seg) {
    const Region& r = seg->getRegion();
    obj["id"]         = seg->getId();
    obj["start"]      = r.start;
    obj["stop"]       = r.stop();       // inclusive last index (P1.3)
    obj["length"]     = r.size();
    obj["reverse"]    = seg->isReversed();
    obj["brightness"] = seg->getBrightness();
    obj["effect"]     = seg->getEffectId();

    const EffectInfo* info = seg->getEffect();
    if (info && info->hasSchema()) {
        JsonObject params = obj["params"].to<JsonObject>();
        paramsToJson(params, *info->schema, seg->getParamValues());
    }
}

} // namespace lume

#endif // LUME_SEGMENT_SERIALIZER_H
