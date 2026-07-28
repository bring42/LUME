// Native (host-compiled) tests for the command bus (RFC 0001 §3 step 1).
// Proves the single-writer path: a producer enqueues a self-contained command,
// and the mutation only takes effect when the render loop drains the queue in
// update() — never on the producer's task. Runs in CI via `pio test -e native`.
#include <unity.h>
#include <ArduinoJson.h>

#include "core/controller.cpp"
#include "core/segment_serializer.h"

using namespace lume;

// --- Linker-satisfying definitions for the FastLED stub globals. ---
CFastLED FastLED;
const CRGBPalette16 RainbowColors_p;
const CRGBPalette16 LavaColors_p;
const CRGBPalette16 OceanColors_p;
const CRGBPalette16 PartyColors_p;
const CRGBPalette16 ForestColors_p;
const CRGBPalette16 CloudColors_p;
const CRGBPalette16 HeatColors_p;

// --- Test-only effect with a schema (slot 0 = an Int we can round-trip). ---
static void testfx(SegmentView&, const ParamValues&, uint32_t, bool) {}

DEFINE_EFFECT_SCHEMA(kBusSchema,
    ParamDesc::Int("speed", "Speed", 128, 1, 255),
    ParamDesc::Color("color", "Color", CRGB(0, 0, 0))
);
REGISTER_EFFECT_SCHEMA(testfx, "testfx", "Test FX", Animated, kBusSchema, 0);

// A two-color effect for exercising setColor's index -> ordered-slot mapping.
static void twocolorfx(SegmentView&, const ParamValues&, uint32_t, bool) {}
DEFINE_EFFECT_SCHEMA(kTwoColorSchema,
    ParamDesc::Int("speed", "Speed", 128, 1, 255),
    ParamDesc::Color("colorStart", "Start", CRGB(0, 0, 0)),   // slot 1
    ParamDesc::Color("colorEnd",   "End",   CRGB(0, 0, 0))    // slot 2
);
REGISTER_EFFECT_SCHEMA(twocolorfx, "twocolor", "Two Color", Animated, kTwoColorSchema, 0);

// Build the spec a web handler would for a create.
static EffectSpec makeCreateSpec(uint16_t start, uint16_t length, uint8_t speed) {
    EffectSpec spec = {};
    spec.create = true;
    spec.start = start;
    spec.length = length;
    spec.hasEffect = true;
    std::strcpy(spec.effectId, "testfx");
    spec.hasParams = true;
    spec.slots[0].intVal = speed;
    return spec;
}

// A recording effect: captures the param values it is actually rendered with,
// so a test can prove the loop feeds it *interpolated* params during a
// transition (slot 0 = Int level, slot 1 = Color).
static uint8_t g_recInt = 0;
static CRGB    g_recColor;
static void recfx(SegmentView&, const ParamValues& p, uint32_t, bool) {
    g_recInt = p.getInt(0);
    g_recColor = p.getColor(1);
}
DEFINE_EFFECT_SCHEMA(kRecSchema,
    ParamDesc::Int("level", "Level", 0, 0, 255),
    ParamDesc::Color("color", "Color", CRGB(0, 0, 0))
);
REGISTER_EFFECT_SCHEMA(recfx, "recfx", "Rec FX", Animated, kRecSchema, 0);

// A full-white fill, so the 16-bit output pipeline (brightness × dim-to-warm →
// IIR → gamma → correction → dither → floor) has lit content to shape and tests
// can assert on the composed 8-bit leds[] instead of the removed global stage.
static void whitefx(SegmentView& v, const ParamValues&, uint32_t, bool) {
    v.fill(CRGB16(65535, 65535, 65535));
}
DEFINE_EFFECT_SCHEMA(kWhiteSchema, ParamDesc::Int("x", "X", 0, 0, 255));
REGISTER_EFFECT_SCHEMA(whitefx, "whitefx", "White FX", Solid, kWhiteSchema, 0);

// A black fill — a distinct incoming mode for exercising the crossfade (it
// actively writes black, so the canvas differs from a stale white frame).
static void blackfx(SegmentView& v, const ParamValues&, uint32_t, bool) {
    v.clear();
}
DEFINE_EFFECT_SCHEMA(kBlackSchema, ParamDesc::Int("x", "X", 0, 0, 255));
REGISTER_EFFECT_SCHEMA(blackfx, "blackfx", "Black FX", Solid, kBlackSchema, 0);

void setUp() {}
void tearDown() {}

// Create a full-strip white segment, then run the loop enough times for the
// output IIR to settle to its target (the pipeline is a low-pass, so a single
// update() is mid-approach). Tests then read the settled leds[].
static void makeWhiteStrip(LumeController& c, uint16_t len) {
    EffectSpec spec = {};
    spec.create = true;
    spec.start = 0;
    spec.length = len;
    spec.hasEffect = true;
    std::strcpy(spec.effectId, "whitefx");
    c.enqueueCommand(Command::applyEffectSpec(255, spec));
    c.update();
}
static void settle(LumeController& c, int frames = 400) {
    for (int i = 0; i < frames; i++) c.update();
}

// A queued create is applied only on update(), and carries effect + params.
void test_create_is_deferred_then_applied() {
    LumeController c;
    c.begin(60);

    c.enqueueCommand(Command::applyEffectSpec(255, makeCreateSpec(0, 10, 200)));

    // Single-writer: nothing has changed on the producer side yet.
    TEST_ASSERT_EQUAL_UINT8(0, c.getSegmentCount());

    c.update();  // render loop drains the queue

    TEST_ASSERT_EQUAL_UINT8(1, c.getSegmentCount());
    Segment* s = c.getSegment(0);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_UINT16(0, s->getStart());
    TEST_ASSERT_EQUAL_UINT16(10, s->getLength());
    TEST_ASSERT_EQUAL_STRING("testfx", s->getEffectId());
    TEST_ASSERT_EQUAL_UINT8(200, s->getParamValues().getInt(0));
}

// An update command targets an existing segment by id and changes its params.
void test_update_changes_params() {
    LumeController c;
    c.begin(60);
    c.enqueueCommand(Command::applyEffectSpec(255, makeCreateSpec(0, 10, 50)));
    c.update();
    TEST_ASSERT_EQUAL_UINT8(50, c.getSegment(0)->getParamValues().getInt(0));

    EffectSpec upd = {};
    upd.hasEffect = true;
    std::strcpy(upd.effectId, "testfx");
    upd.hasParams = true;
    upd.slots[0].intVal = 222;
    c.enqueueCommand(Command::applyEffectSpec(0, upd));
    c.update();

    TEST_ASSERT_EQUAL_UINT8(1, c.getSegmentCount());
    TEST_ASSERT_EQUAL_UINT8(222, c.getSegment(0)->getParamValues().getInt(0));
}

// Editable boundaries: a non-create update carrying geometry resizes the target
// segment in place (start/length change), and out-of-range geometry is clamped
// to the strip. This is the single-writer resize path behind carving zones.
void test_update_resizes_segment_geometry() {
    LumeController c;
    c.begin(60);
    c.enqueueCommand(Command::applyEffectSpec(255, makeCreateSpec(0, 60, 128)));  // full strip
    c.update();
    TEST_ASSERT_EQUAL_UINT16(0,  c.getSegment(0)->getStart());
    TEST_ASSERT_EQUAL_UINT16(60, c.getSegment(0)->getLength());

    // Plain resize: start 0->10, length 60->20.
    EffectSpec resize = {};
    resize.hasGeometry = true;
    resize.start = 10;
    resize.length = 20;
    c.enqueueCommand(Command::applyEffectSpec(0, resize));
    c.update();
    TEST_ASSERT_EQUAL_UINT16(10, c.getSegment(0)->getStart());
    TEST_ASSERT_EQUAL_UINT16(20, c.getSegment(0)->getLength());

    // Length running off the end is clamped to ledCount - start (55 -> 5 left).
    EffectSpec oob = {};
    oob.hasGeometry = true;
    oob.start = 55;
    oob.length = 999;
    c.enqueueCommand(Command::applyEffectSpec(0, oob));
    c.update();
    TEST_ASSERT_EQUAL_UINT16(55, c.getSegment(0)->getStart());
    TEST_ASSERT_EQUAL_UINT16(5,  c.getSegment(0)->getLength());   // 60 - 55

    // Start past the end clamps to the last pixel, length floors at 1.
    EffectSpec past = {};
    past.hasGeometry = true;
    past.start = 200;
    past.length = 10;
    c.enqueueCommand(Command::applyEffectSpec(0, past));
    c.update();
    TEST_ASSERT_EQUAL_UINT16(59, c.getSegment(0)->getStart());    // ledCount - 1
    TEST_ASSERT_EQUAL_UINT16(1,  c.getSegment(0)->getLength());
}

// A queued remove is likewise deferred to the loop (the P0.1 array-shift race
// only ever happens on the single writer now).
void test_remove_is_deferred_then_applied() {
    LumeController c;
    c.begin(60);
    c.enqueueCommand(Command::applyEffectSpec(255, makeCreateSpec(0, 10, 128)));
    c.update();
    TEST_ASSERT_EQUAL_UINT8(1, c.getSegmentCount());

    c.enqueueCommand(Command::removeSegment(0));
    TEST_ASSERT_EQUAL_UINT8(1, c.getSegmentCount());  // not yet
    c.update();
    TEST_ASSERT_EQUAL_UINT8(0, c.getSegmentCount());
}

// Global power/brightness commands are likewise deferred to the loop.
void test_power_and_brightness_via_bus() {
    LumeController c;
    c.begin(60);
    TEST_ASSERT_TRUE(c.getPower());            // constructor defaults
    TEST_ASSERT_EQUAL_UINT8(255, c.getBrightness());

    c.enqueueCommand(Command::setPower(false));
    c.enqueueCommand(Command::setGlobalBrightness(40));
    TEST_ASSERT_TRUE(c.getPower());            // not applied yet (single writer)
    TEST_ASSERT_EQUAL_UINT8(255, c.getBrightness());

    c.update();
    TEST_ASSERT_FALSE(c.getPower());
    TEST_ASSERT_EQUAL_UINT8(40, c.getBrightness());
}

// "Nightlight" is no longer a bus command or controller state — it decomposes
// into an eased brightness fade plus a power-off-at-zero rider. A fade to zero
// carrying the rider powers the strip off once it settles.
void test_fade_to_zero_rider_powers_off() {
    LumeController c;
    c.begin(60);
    TEST_ASSERT_TRUE(c.getPower());

    c.enqueueCommand(Command::setGlobalBrightness(0)
                         .withTransition(3000).withPowerOffAtZero(true));
    c.update();
    TEST_ASSERT_TRUE(c.isBrightnessFading());   // fading, not yet settled
    TEST_ASSERT_TRUE(c.getPower());             // still on mid-fade

    for (int i = 0; i < 50 && c.isBrightnessFading(); i++) c.update();
    TEST_ASSERT_FALSE(c.isBrightnessFading());
    TEST_ASSERT_FALSE(c.getPower());            // powered off once settled
}

// Reported ("target") state is what a fade is heading to, not the mid-fade
// value — this is what stops the WS reconcile from bouncing a just-moved slider.
void test_reported_state_is_target_not_midfade() {
    LumeController c;
    c.begin(60);
    c.enqueueCommand(Command::setGlobalBrightness(200));   // start at a known level
    c.update();
    // Eased change to 20: the live value walks down, but the reported target is
    // 20 immediately (from the frame the transition starts).
    c.enqueueCommand(Command::setGlobalBrightness(20).withTransition(5000));
    c.update();
    TEST_ASSERT_TRUE(c.isBrightnessFading());
    TEST_ASSERT_EQUAL_UINT8(20, c.getTargetBrightness());  // heading to 20
    TEST_ASSERT_TRUE(c.getBrightness() > 20);              // live value still mid-fade

    // Power fade-out: still logically on mid-fade, but target power is already off.
    c.enqueueCommand(Command::setPower(false).withTransition(5000));
    c.update();
    TEST_ASSERT_TRUE(c.getPower());          // live: still on (rendering, dimming)
    TEST_ASSERT_FALSE(c.getTargetPower());   // reported: heading off
}

// Eased power-off keeps the strip logically on (rendering, dimming) until the
// fade envelope settles, then flips it off. The brightness level is preserved.
void test_power_off_fade_defers_then_settles() {
    LumeController c;
    c.begin(60);
    TEST_ASSERT_TRUE(c.getPower());

    c.enqueueCommand(Command::setPower(false).withTransition(3000));
    c.update();
    TEST_ASSERT_TRUE(c.isPowerFading());       // envelope animating
    TEST_ASSERT_TRUE(c.getPower());            // still logically on mid-fade

    for (int i = 0; i < 50 && c.isPowerFading(); i++) c.update();
    TEST_ASSERT_FALSE(c.getPower());           // flipped off once settled
    TEST_ASSERT_EQUAL_UINT8(255, c.getBrightness());  // level preserved across toggle
}

// Eased power-on flips the strip logically on immediately (so it renders while
// fading up from black) rather than deferring like power-off.
void test_power_on_fade_is_immediate_logical() {
    LumeController c;
    c.begin(60);
    c.enqueueCommand(Command::setPower(false));   // start off (instant)
    c.update();
    TEST_ASSERT_FALSE(c.getPower());

    c.enqueueCommand(Command::setPower(true).withTransition(3000));
    c.update();
    TEST_ASSERT_TRUE(c.getPower());            // on immediately, envelope rising
}

// A fade to a dim, non-zero target is an ordinary eased change: it must land on
// the target and leave the strip on (no rider).
void test_fade_to_dim_stays_on() {
    LumeController c;
    c.begin(60);
    c.enqueueCommand(Command::setGlobalBrightness(50)
                         .withTransition(3000).withPowerOffAtZero(false));
    c.update();                                  // apply the command -> fade starts
    TEST_ASSERT_TRUE(c.isBrightnessFading());
    for (int i = 0; i < 50 && c.isBrightnessFading(); i++) c.update();
    TEST_ASSERT_FALSE(c.isBrightnessFading());
    TEST_ASSERT_TRUE(c.getPower());
    TEST_ASSERT_EQUAL_UINT8(50, c.getBrightness());
}

// A manual brightness set mid-fade overrides the fade AND clears the pending
// power-off rider — the strip must not switch off behind the user.
void test_manual_set_clears_power_off_rider() {
    LumeController c;
    c.begin(60);
    c.enqueueCommand(Command::setGlobalBrightness(0)
                         .withTransition(5000).withPowerOffAtZero(true));
    c.update();
    TEST_ASSERT_TRUE(c.isBrightnessFading());

    c.enqueueCommand(Command::setGlobalBrightness(200));  // instant manual set
    for (int i = 0; i < 50 && c.isBrightnessFading(); i++) c.update();
    TEST_ASSERT_TRUE(c.getPower());             // never powered off
    TEST_ASSERT_EQUAL_UINT8(200, c.getBrightness());
}

// An eased ApplyEffectSpec update interpolates continuous params/colors over the
// transition window rather than snapping — the effect is rendered with values
// strictly between old and new before landing exactly on the target.
void test_param_transition_eases_then_lands() {
    LumeController c;
    c.begin(60);

    // Create a full-strip segment running recfx (level defaults to 0, black).
    EffectSpec create = {};
    create.create = true; create.start = 0; create.length = 60;
    create.hasEffect = true; std::strcpy(create.effectId, "recfx");
    c.enqueueCommand(Command::applyEffectSpec(255, create));
    c.update();
    TEST_ASSERT_EQUAL_UINT8(0, g_recInt);   // starts at the default

    // Eased update: level 0 -> 200, color black -> red, over 4s.
    EffectSpec upd = {};
    upd.hasParams = true;
    upd.slots[0].intVal = 200;
    upd.slots[1].colorVal = CRGB(255, 0, 0);
    c.enqueueCommand(Command::applyEffectSpec(0, upd).withTransition(4000));
    c.update();  // apply the command -> transition starts
    TEST_ASSERT_TRUE(c.getSegment(0)->isParamsTransitioning());

    bool sawIntermediate = false;
    for (int i = 0; i < 80 && c.getSegment(0)->isParamsTransitioning(); i++) {
        c.update();
        if (g_recInt > 0 && g_recInt < 200) sawIntermediate = true;
    }

    TEST_ASSERT_TRUE(sawIntermediate);          // eased, did not snap
    TEST_ASSERT_EQUAL_UINT8(200, g_recInt);     // landed exactly
    TEST_ASSERT_EQUAL_UINT8(255, g_recColor.r); // color reached red
    TEST_ASSERT_EQUAL_UINT8(0,   g_recColor.g);
    TEST_ASSERT_EQUAL_UINT8(0,   g_recColor.b);
}

// An eased spec that also *changes the effect* must snap (new schema / defaults
// — nothing coherent to ease from), never leaving a transition running.
void test_effect_change_ignores_transition() {
    LumeController c;
    c.begin(60);
    EffectSpec create = {};
    create.create = true; create.start = 0; create.length = 60;
    create.hasEffect = true; std::strcpy(create.effectId, "recfx");
    c.enqueueCommand(Command::applyEffectSpec(255, create));
    c.update();

    EffectSpec chg = {};
    chg.hasEffect = true; std::strcpy(chg.effectId, "testfx");
    c.enqueueCommand(Command::applyEffectSpec(0, chg).withTransition(4000));
    c.update();
    TEST_ASSERT_FALSE(c.getSegment(0)->isParamsTransitioning());
}

// The AI path carries semantic params (speed/color) that resolve to schema slots
// on the loop: testfx maps "speed" -> slot 0, "color" -> slot 1.
void test_ai_semantic_params_via_bus() {
    LumeController c;
    c.begin(60);
    c.enqueueCommand(Command::applyEffectSpec(255, makeCreateSpec(0, 10, 128)));
    c.update();

    EffectSpec es = {};
    es.hasSpeed = true;
    es.speed = 77;
    es.colorCount = 1;
    es.colors[0] = CRGB(0x10, 0x20, 0x30);
    c.enqueueCommand(Command::applyEffectSpec(0, es));
    c.update();

    Segment* s = c.getSegment(0);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_UINT8(77, s->getParamValues().getInt(0));   // speed -> slot 0
    CRGB col = s->getParamValues().getColor(1);                   // color -> slot 1
    TEST_ASSERT_EQUAL_UINT8(0x10, col.r);
    TEST_ASSERT_EQUAL_UINT8(0x20, col.g);
    TEST_ASSERT_EQUAL_UINT8(0x30, col.b);
}

// setColor(i) maps to the i-th color param in schema order — not always the
// first (the P0.6 bug, which collapsed multi-color effects into one slot).
void test_setcolor_maps_index_to_ordered_color_slot() {
    Segment seg;
    TEST_ASSERT_TRUE(seg.setEffect("twocolor"));
    seg.setColor(0, CRGB(0x11, 0x22, 0x33));   // -> colorStart, slot 1
    seg.setColor(1, CRGB(0x44, 0x55, 0x66));   // -> colorEnd,   slot 2
    seg.setColor(2, CRGB(0x77, 0x88, 0x99));   // out of range -> no-op

    const ParamValues& pv = seg.getParamValues();
    CRGB start = pv.getColor(1);
    CRGB end   = pv.getColor(2);
    TEST_ASSERT_EQUAL_UINT8(0x11, start.r);    // index 0 did NOT get clobbered
    TEST_ASSERT_EQUAL_UINT8(0x22, start.g);
    TEST_ASSERT_EQUAL_UINT8(0x33, start.b);
    TEST_ASSERT_EQUAL_UINT8(0x44, end.r);
    TEST_ASSERT_EQUAL_UINT8(0x55, end.g);
    TEST_ASSERT_EQUAL_UINT8(0x66, end.b);
}

// After deleting a middle segment, IDs go non-contiguous ({0,2}); enumerating by
// index must still find every survivor, where the old by-id loop dropped id 2 (P0.5).
void test_enumeration_survives_middle_delete() {
    LumeController c;
    c.begin(60);
    c.enqueueCommand(Command::applyEffectSpec(255, makeCreateSpec(0,  10, 100)));  // id 0
    c.enqueueCommand(Command::applyEffectSpec(255, makeCreateSpec(10, 10, 100)));  // id 1
    c.enqueueCommand(Command::applyEffectSpec(255, makeCreateSpec(20, 10, 100)));  // id 2
    c.update();
    TEST_ASSERT_EQUAL_UINT8(3, c.getSegmentCount());

    c.enqueueCommand(Command::removeSegment(1));  // delete the middle one
    c.update();
    TEST_ASSERT_EQUAL_UINT8(2, c.getSegmentCount());

    TEST_ASSERT_NULL(c.getSegment(1));            // by-id lookup of the gone id

    Segment* s0 = c.getSegmentByIndex(0);
    Segment* s1 = c.getSegmentByIndex(1);
    TEST_ASSERT_NOT_NULL(s0);
    TEST_ASSERT_NOT_NULL(s1);
    TEST_ASSERT_EQUAL_UINT8(0, s0->getId());
    TEST_ASSERT_EQUAL_UINT8(2, s1->getId());      // survivor a by-id loop would drop
    TEST_ASSERT_NULL(c.getSegmentByIndex(2));     // out of range
}

// P2 (scratchpad aliasing): removeSegment shifts survivors down with copy-
// assignment. That duplicates each Segment's inline scratchpad bytes correctly,
// but used to leave a shifted survivor's SegmentView.scratchpad pointing at the
// REMOVED neighbor's inline pad — two segments then rendered through one physical
// pad and stateful effects (fire heat, rain drops) bled across channels. After a
// mid-array delete, every survivor's view must point back at its OWN inline pad.
void test_scratchpad_rebinds_to_own_pad_after_middle_delete() {
    LumeController c;
    c.begin(60);
    c.enqueueCommand(Command::applyEffectSpec(255, makeCreateSpec(0,  20, 100)));  // id 0
    c.enqueueCommand(Command::applyEffectSpec(255, makeCreateSpec(20, 20, 100)));  // id 1
    c.enqueueCommand(Command::applyEffectSpec(255, makeCreateSpec(40, 20, 100)));  // id 2
    c.update();
    TEST_ASSERT_EQUAL_UINT8(3, c.getSegmentCount());

    c.enqueueCommand(Command::removeSegment(1));   // delete the MIDDLE segment
    c.update();
    TEST_ASSERT_EQUAL_UINT8(2, c.getSegmentCount());

    Segment* s0 = c.getSegmentByIndex(0);          // id 0, slot untouched by the shift
    Segment* s1 = c.getSegmentByIndex(1);          // id 2, shifted down one slot
    TEST_ASSERT_NOT_NULL(s0);
    TEST_ASSERT_NOT_NULL(s1);
    TEST_ASSERT_EQUAL_UINT8(0, s0->getId());
    TEST_ASSERT_EQUAL_UINT8(2, s1->getId());

    // getScratchpad<>() returns the address of a segment's OWN inline pad. Each
    // survivor's view must point there; pre-fix, s1's view aliased the removed
    // neighbor's pad, so this pointer would differ.
    TEST_ASSERT_EQUAL_PTR(s0->getScratchpad<uint8_t>(), s0->getView().scratchpad);
    TEST_ASSERT_EQUAL_PTR(s1->getScratchpad<uint8_t>(), s1->getView().scratchpad);

    // ...and the two survivors must not share one physical pad.
    TEST_ASSERT_TRUE(s0->getView().scratchpad != s1->getView().scratchpad);

    // Behavioral proof of isolation: state written through one view's pad must not
    // appear in the other's — the exact cross-segment bleed the aliasing caused.
    s0->getView().scratchpad[0] = 0xAA;
    s1->getView().scratchpad[0] = 0x55;
    TEST_ASSERT_EQUAL_UINT8(0xAA, s0->getView().scratchpad[0]);
    TEST_ASSERT_EQUAL_UINT8(0x55, s1->getView().scratchpad[0]);
}

// P0.8: the ReconfigureProtocols command runs the registered hook on the loop.
static int g_reconfigCalls = 0;
static void fakeReconfig() { g_reconfigCalls++; }

void test_reconfigure_protocols_runs_hook_on_loop() {
    LumeController c;
    c.begin(60);
    c.reconfigureProtocolsFn = fakeReconfig;
    g_reconfigCalls = 0;

    c.enqueueCommand(Command::reconfigureProtocols());
    TEST_ASSERT_EQUAL_INT(0, g_reconfigCalls);   // deferred to the loop
    c.update();
    TEST_ASSERT_EQUAL_INT(1, g_reconfigCalls);   // applied once, on the loop task
}

// P0.1 (pixels): staged pixels are applied only on update(), never at stage time.
void test_direct_pixels_are_deferred_then_applied() {
    LumeController c;
    c.begin(60);
    CRGB* leds = c.getLeds();
    for (uint16_t i = 0; i < 60; i++) leds[i] = CRGB(0, 0, 0);   // known baseline

    CRGB frame[60];
    memset(frame, 0, sizeof(frame));
    frame[0] = CRGB(0x11, 0x22, 0x33);
    frame[5] = CRGB(0x44, 0x55, 0x66);
    c.stageDirectPixels(frame, 60);

    // Single-writer: staging (web task) did not touch leds[].
    TEST_ASSERT_EQUAL_UINT8(0, leds[0].r);
    TEST_ASSERT_EQUAL_UINT8(0, leds[5].r);

    c.update();  // render loop drains the staged frame into leds[]
    TEST_ASSERT_EQUAL_UINT8(0x11, leds[0].r);
    TEST_ASSERT_EQUAL_UINT8(0x22, leds[0].g);
    TEST_ASSERT_EQUAL_UINT8(0x44, leds[5].r);
    TEST_ASSERT_EQUAL_UINT8(0x55, leds[5].g);
}

// GET /api/v2/pixels reads copyVizPixels(): the PERCEPTUAL frame (post master
// brightness, pre gamma/correction/dither) — what a screen should display.
// Proves (a) full-brightness white reads back ~white, (b) the readback tracks
// master brightness linearly while the wire bytes (leds[]) sit far below it
// (gamma-encoded), i.e. the viz really reads pre-gamma, (c) maxPixels clamps.
void test_viz_readback_is_perceptual_pre_gamma() {
    LumeController c;
    c.begin(60);
    makeWhiteStrip(c, 60);
    c.enqueueCommand(Command::setGlobalBrightness(255));
    settle(c);

    uint8_t px[60 * 3];
    TEST_ASSERT_EQUAL_UINT16(60, c.copyVizPixels(px, 60));
    // Full white, full brightness: dim-to-warm is neutral at the top, so the
    // perceptual bytes should read back essentially white on all channels.
    TEST_ASSERT_UINT8_WITHIN(2, 255, px[0]);
    TEST_ASSERT_UINT8_WITHIN(2, 255, px[1]);
    TEST_ASSERT_UINT8_WITHIN(2, 255, px[2]);

    // Halve-ish the master brightness: the perceptual readback scales with it
    // linearly (64/255), while the gamma-encoded wire byte drops much further
    // ((64/255)^2.2 ≈ 12/255). The gap is the whole point of the viz reading
    // smooth_ instead of leds[] — a screen would double-gamma the wire bytes.
    c.enqueueCommand(Command::setGlobalBrightness(64));
    settle(c);
    c.copyVizPixels(px, 60);
    TEST_ASSERT_UINT8_WITHIN(4, 64, px[0]);                    // perceptual: ~64
    TEST_ASSERT_LESS_THAN_UINT8(30, c.getLeds()[0].r);         // wire: gamma'd way down
    TEST_ASSERT_LESS_THAN_UINT8(px[0], c.getLeds()[0].r);      // and always below viz

    // maxPixels clamps the copy, and reports what it wrote.
    TEST_ASSERT_EQUAL_UINT16(10, c.copyVizPixels(px, 10));
}

// P1.7: the one canonical projection — colors as #rrggbb hex (not [r,g,b]), a
// string `effect`, and stop/brightness present. Every transport shares this.
void test_serialize_segment_canonical_shape() {
    LumeController c;
    c.begin(60);
    c.enqueueCommand(Command::applyEffectSpec(255, makeCreateSpec(3, 10, 128)));
    c.update();
    Segment* s = c.getSegment(0);
    TEST_ASSERT_NOT_NULL(s);
    s->getParamValues().setColor(1, CRGB(0x11, 0x22, 0x33));  // testfx slot 1 = "color"

    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    serializeSegment(obj, s);

    TEST_ASSERT_EQUAL_INT(0, obj["id"].as<int>());
    TEST_ASSERT_EQUAL_INT(3, obj["start"].as<int>());
    TEST_ASSERT_EQUAL_INT(12, obj["stop"].as<int>());        // 3 + 10 - 1 (inclusive)
    TEST_ASSERT_EQUAL_INT(10, obj["length"].as<int>());
    TEST_ASSERT_EQUAL_INT(255, obj["brightness"].as<int>()); // default
    TEST_ASSERT_EQUAL_STRING("testfx", obj["effect"].as<const char*>());  // string, not object
    TEST_ASSERT_TRUE(obj["params"]["color"].is<const char*>());           // hex, not [r,g,b]
    TEST_ASSERT_FALSE(obj["params"]["color"].is<JsonArrayConst>());
    TEST_ASSERT_EQUAL_STRING("#112233", obj["params"]["color"].as<const char*>());
}

// RFC 0001 §6: the LED output driver is pluggable via ILedOutput. A mock proves
// the controller drives the injected output, not FastLED directly.
struct MockOutput : ILedOutput {
    int begins = 0, shows = 0, clears = 0;
    uint8_t lastBrightness = 0;
    CRGB lastCorrection = CRGB(255, 255, 255);
    CRGB lastTemperature = CRGB(255, 255, 255);
    void begin(CRGB*, uint16_t) override { begins++; }
    void show() override { shows++; }
    void setBrightness(uint8_t b) override { lastBrightness = b; }
    void setCorrection(CRGB c) override { lastCorrection = c; }
    void setTemperature(CRGB t) override { lastTemperature = t; }
    void clear() override { clears++; }
};

// Helper: the max channel across the whole strip (a proxy for "how lit").
static uint16_t maxChannel(LumeController& c) {
    const CRGB* leds = c.getLeds();
    uint16_t m = 0;
    for (uint16_t i = 0; i < c.getLedCount(); i++) {
        m = std::max<uint16_t>(m, std::max(leds[i].r, std::max(leds[i].g, leds[i].b)));
    }
    return m;
}

// RFC 0001 §6: the LED output driver is pluggable via ILedOutput, and under the
// 16-bit pipeline the driver's own scaling is PINNED — master brightness is
// applied per-pixel, so the driver brightness stays 255 regardless of the user's
// level (pushing it to the driver too would double-dim, fighting the dither).
void test_led_output_is_pluggable_and_brightness_is_pinned() {
    LumeController c;
    MockOutput mock;
    c.setLedOutput(&mock);
    c.begin(60);
    TEST_ASSERT_EQUAL_INT(1, mock.begins);        // begin() bound the injected driver
    TEST_ASSERT_TRUE(mock.shows >= 1);            // ...and pushed a frame

    int showsBefore = mock.shows;
    c.update();                                    // every loop pass -> show()
    TEST_ASSERT_TRUE(mock.shows > showsBefore);

    // The driver brightness is pinned at 255 no matter the user's level; and the
    // driver's correction/temperature are never driven per-frame anymore (neutral).
    c.setBrightness(42);  settle(c);
    TEST_ASSERT_EQUAL_UINT8(255, mock.lastBrightness);
    c.setBrightness(255); settle(c);
    TEST_ASSERT_EQUAL_UINT8(255, mock.lastBrightness);
    TEST_ASSERT_EQUAL_UINT8(255, mock.lastCorrection.r);
    TEST_ASSERT_EQUAL_UINT8(255, mock.lastTemperature.r);
}

// Master brightness is applied per-pixel in the 16-bit stage: full-white content
// dims monotonically with the level, and a true zero cuts the strip to black.
void test_master_brightness_dims_content() {
    LumeController c;
    c.begin(60);
    makeWhiteStrip(c, 60);

    c.setBrightness(255); settle(c);
    uint16_t full = maxChannel(c);
    TEST_ASSERT_TRUE(full > 200);                 // full white is bright

    c.setBrightness(96);  settle(c);
    uint16_t mid = maxChannel(c);
    TEST_ASSERT_TRUE(mid < full);                 // dimmer than full
    TEST_ASSERT_TRUE(mid > 0);                     // but still lit

    c.setBrightness(0);   settle(c);
    TEST_ASSERT_EQUAL_UINT16(0, maxChannel(c));    // true off = black
}

// The low end must never go cool. There is no per-channel floor anymore (it lifted
// green/blue in the bottom codes and read as cyan, fighting dim-to-warm); instead
// dim-to-warm + dither own the descent. The invariant that guards the
// "turquoise right before dark" regression: across a sweep of low levels on white
// content, red stays >= blue on every lit pixel — the fade goes warm, never cyan.
void test_low_end_stays_warm_never_cyan() {
    LumeController c;
    c.begin(60);
    makeWhiteStrip(c, 60);
    c.setWarmth(0.6f);

    // Aggregate over frames: at the very bottom the dither flickers channels 0/1
    // independently, so a single frame can transiently show B>R — the meaningful
    // invariant is the time-averaged balance. Total red output must dominate blue
    // at every low level (warm, never cyan) — the "turquoise before dark" guard.
    for (uint8_t bri : {6, 12, 20, 30}) {
        c.setBrightness(bri); settle(c, 200);
        uint32_t sumR = 0, sumB = 0;
        for (int f = 0; f < 120; f++) {
            c.update();
            const CRGB* leds = c.getLeds();
            for (uint16_t i = 0; i < c.getLedCount(); i++) {
                sumR += leds[i].r;
                sumB += leds[i].b;
            }
        }
        TEST_ASSERT_TRUE(sumR >= sumB);
    }
}

// Switching a segment's effect must not snap the output — it crossfades from the
// outgoing mode over ~kModeCrossfadeMs. Change a bright-white segment to a
// black-fill effect: the frame right after the switch is still lit (frozen white
// dominates the eased blend), and only once the window elapses does it reach the
// incoming black mode. A snap would jump to black in one frame.
void test_mode_switch_crossfades_not_snaps() {
    LumeController c;
    c.begin(60);
    makeWhiteStrip(c, 60);   // segment id 0, whitefx
    c.setBrightness(255);
    settle(c);
    TEST_ASSERT_TRUE(maxChannel(c) > 200);   // bright white to start

    // Switch segment 0 to blackfx (the incoming mode renders black).
    EffectSpec spec = {};
    spec.hasEffect = true;
    std::strcpy(spec.effectId, "blackfx");
    c.enqueueCommand(Command::applyEffectSpec(0, spec));
    c.update();  // command applies + crossfade starts; first blended frame ~ white
    TEST_ASSERT_TRUE(maxChannel(c) > 100);   // did NOT snap to black

    settle(c);   // let the crossfade window fully elapse + the IIR settle
    TEST_ASSERT_EQUAL_UINT16(0, maxChannel(c));  // arrived at the incoming black mode
}

// Runtime gamma still flows through the single-writer bus and clamps to
// [LED_GAMMA_MIN, LED_GAMMA_MAX]; and a deeper gamma visibly pulls a mid level
// further down (now observed in the composed per-pixel output, not a global set).
void test_gamma_via_bus_and_clamps() {
    LumeController c;
    c.begin(60);
    makeWhiteStrip(c, 60);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, LED_GAMMA, c.getGamma());   // seeded from the compile default

    c.enqueueCommand(Command::setGamma(2.2f));
    c.update();
    c.setBrightness(110); settle(c);
    uint16_t midGamma22 = maxChannel(c);

    c.enqueueCommand(Command::setGamma(2.8f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.2f, c.getGamma());        // deferred (single writer)
    c.update();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.8f, c.getGamma());        // applied on the loop
    settle(c);
    uint16_t midGamma28 = maxChannel(c);
    TEST_ASSERT_TRUE(midGamma28 < midGamma22);                   // deeper gamma pulls the mid down

    // Above range -> clamped to max; below range -> clamped to min.
    c.enqueueCommand(Command::setGamma(9.0f));
    c.update();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, LED_GAMMA_MAX, c.getGamma());
    c.enqueueCommand(Command::setGamma(0.1f));
    c.update();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, LED_GAMMA_MIN, c.getGamma());
}

// Dim-to-warm strength rides the bus like gamma: deferred to the loop, clamped
// to [LED_WARMTH_MIN, LED_WARMTH_MAX], seeded from the default.
void test_warmth_via_bus_and_clamps() {
    LumeController c;
    c.begin(60);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, LED_WARMTH_DEFAULT, c.getWarmth());  // seeded

    c.enqueueCommand(Command::setWarmth(0.9f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, LED_WARMTH_DEFAULT, c.getWarmth());  // deferred
    c.update();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.9f, c.getWarmth());               // applied on loop

    c.enqueueCommand(Command::setWarmth(5.0f));   // above range
    c.update();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, LED_WARMTH_MAX, c.getWarmth());
    c.enqueueCommand(Command::setWarmth(-1.0f));  // below range
    c.update();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, LED_WARMTH_MIN, c.getWarmth());
}

// Dim-to-warm shows in the composed per-pixel output: the same white content is
// warmer (a lower blue-to-red ratio) at a low level than at full brightness. The
// WS2812B correction is constant now, so the shift is purely the dim-to-warm
// stage. Red always stays the dominant channel.
void test_dim_to_warm_tints_the_low_end() {
    LumeController c;
    c.begin(60);
    makeWhiteStrip(c, 60);
    c.setWarmth(0.6f);

    c.setBrightness(255); settle(c);
    const CRGB* leds = c.getLeds();
    // Full brightness: no warm tint applied (dim == 0). Capture the b/r ratio.
    float ratioFull = (float)leds[0].b / (float)leds[0].r;
    TEST_ASSERT_TRUE(leds[0].r >= leds[0].b);   // red dominant

    c.setBrightness(48); settle(c);
    leds = c.getLeds();
    float ratioLow = (float)leds[0].b / (float)leds[0].r;
    TEST_ASSERT_TRUE(leds[0].r >= leds[0].g);   // red still dominant when dim
    TEST_ASSERT_TRUE(leds[0].r >= leds[0].b);
    TEST_ASSERT_TRUE(ratioLow < ratioFull);     // warmer (less blue) at the low end
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_led_output_is_pluggable_and_brightness_is_pinned);
    RUN_TEST(test_master_brightness_dims_content);
    RUN_TEST(test_low_end_stays_warm_never_cyan);
    RUN_TEST(test_mode_switch_crossfades_not_snaps);
    RUN_TEST(test_gamma_via_bus_and_clamps);
    RUN_TEST(test_warmth_via_bus_and_clamps);
    RUN_TEST(test_dim_to_warm_tints_the_low_end);
    RUN_TEST(test_serialize_segment_canonical_shape);
    RUN_TEST(test_create_is_deferred_then_applied);
    RUN_TEST(test_update_changes_params);
    RUN_TEST(test_update_resizes_segment_geometry);
    RUN_TEST(test_remove_is_deferred_then_applied);
    RUN_TEST(test_power_and_brightness_via_bus);
    RUN_TEST(test_fade_to_zero_rider_powers_off);
    RUN_TEST(test_reported_state_is_target_not_midfade);
    RUN_TEST(test_power_off_fade_defers_then_settles);
    RUN_TEST(test_power_on_fade_is_immediate_logical);
    RUN_TEST(test_fade_to_dim_stays_on);
    RUN_TEST(test_manual_set_clears_power_off_rider);
    RUN_TEST(test_param_transition_eases_then_lands);
    RUN_TEST(test_effect_change_ignores_transition);
    RUN_TEST(test_ai_semantic_params_via_bus);
    RUN_TEST(test_setcolor_maps_index_to_ordered_color_slot);
    RUN_TEST(test_enumeration_survives_middle_delete);
    RUN_TEST(test_scratchpad_rebinds_to_own_pad_after_middle_delete);
    RUN_TEST(test_reconfigure_protocols_runs_hook_on_loop);
    RUN_TEST(test_direct_pixels_are_deferred_then_applied);
    RUN_TEST(test_viz_readback_is_perceptual_pre_gamma);
    return UNITY_END();
}
