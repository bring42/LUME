#!/bin/bash
#
# test_effects_simple.sh - Cycle through all LED effects (no jq required)
# Usage: ./test_effects_simple.sh [host] [delay] [segment_id]
#
# Examples:
#   ./test_effects_simple.sh                    # Use lume.local, 3s delay, segment 0
#   ./test_effects_simple.sh 192.168.1.100      # Use IP, 3s delay, segment 0
#   ./test_effects_simple.sh lume.local 5 0     # Use lume.local, 5s delay, segment 0
#

HOST="${1:-lume.local}"
DELAY="${2:-1}"
SEGMENT_ID="${3:-0}"

echo "🎨 LUME Effect Tester (Simple)"
echo "Target: http://${HOST}"
echo "Delay: ${DELAY}s between effects"
echo "Segment: ${SEGMENT_ID}"
echo "💡 Tip: Use 0.5 for fast mode, 0.2 for rapid fire"
echo ""

# Check if host is reachable
echo "🔍 Checking connection..."
if curl -s --max-time 10 "http://${HOST}/health" > /dev/null 2>&1; then
    echo "✅ Connected to LUME at ${HOST}"
elif curl -s --max-time 10 "http://192.168.4.1/health" > /dev/null 2>&1; then
    echo "✅ Connected to LUME at 192.168.4.1 (AP mode)"
    HOST="192.168.4.1"
else
    echo "❌ Error: Cannot reach LUME"
    echo ""
    echo "Troubleshooting:"
    echo "  1. Check if LUME is powered on"
    echo "  2. If using WiFi: Make sure it's connected to your network"
    echo "  3. Try using IP address instead: ./test_effects_simple.sh 192.168.x.x"
    echo "  4. If in AP mode: Connect to 'LUME-Setup' WiFi and use:"
    echo "     ./test_effects_simple.sh 192.168.4.1"
    echo ""
    echo "Quick test: curl http://${HOST}/health"
    exit 1
fi
echo ""

# Turn on power
echo "⚡ Ensuring power is ON..."
curl -s -X PUT "http://${HOST}/api/v2/controller" \
    -H "Content-Type: application/json" \
    -d '{"power":true}' > /dev/null

sleep 1
echo ""

# All 23 effects in order
EFFECTS=(
    "solid"
    "rainbow"
    "confetti"
    "fire"
    "fireup"
    "gradient"
    "pulse"
    "breathe"
    "colorwaves"
    "wave"
    "theater"
    "sparkle"
    "noise"
    "meteor"
    "comet"
    "rain"
    "twinkle"
    "strobe"
    "sinelon"
    "scanner"
    "candle"
    "pride"
    "pacifica"
)

TOTAL=${#EFFECTS[@]}

echo "🔄 Starting effect cycle through ${TOTAL} effects..."
echo "Press Ctrl+C to stop"
echo ""

COUNTER=1
for EFFECT in "${EFFECTS[@]}"; do
    printf "[%2d/%2d] Testing: %-20s " "$COUNTER" "$TOTAL" "$EFFECT"
    
    # Update segment with the effect (fire and forget for speed)
    HTTP_CODE=$(curl -s --max-time 2 -X PUT "http://${HOST}/api/v2/segments/${SEGMENT_ID}" \
        -H "Content-Type: application/json" \
        -d "{\"effect\":\"${EFFECT}\"}" \
        -w "%{http_code}" \
        -o /dev/null)
    
    if [ "$HTTP_CODE" -eq 200 ]; then
        echo "✅"
    else
        echo "❌ (HTTP ${HTTP_CODE})"
    fi
    
    # Support fractional seconds (e.g., 0.5, 0.2)
    sleep "$DELAY"
    ((COUNTER++))
done

echo ""
echo "🎉 Effect cycle complete!"
echo ""
echo "To view current state, run:"
echo "  curl -s http://${HOST}/api/v2/segments | python3 -m json.tool"
