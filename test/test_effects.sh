#!/bin/bash
#
# test_effects.sh - Cycle through all LED effects for testing
# Usage: ./test_effects.sh [host] [delay]
#
# Examples:
#   ./test_effects.sh                     # Use lume.local, 3s delay
#   ./test_effects.sh 192.168.1.100       # Use IP, 3s delay
#   ./test_effects.sh lume.local 5        # Use lume.local, 5s delay
#

HOST="${1:-lume.local}"
DELAY="${2:-1}"

echo "🎨 LUME Effect Tester"
echo "Target: http://${HOST}"
echo "Delay: ${DELAY}s between effects"
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
    echo "  3. Try using IP address instead: ./test_effects.sh 192.168.x.x"
    echo "  4. If in AP mode: Connect to 'LUME-Setup' WiFi and use:"
    echo "     ./test_effects.sh 192.168.4.1"
    echo ""
    echo "Quick test: curl http://${HOST}/health"
    exit 1
fi
echo ""

# Get list of effects
echo "📋 Fetching effects list..."
EFFECTS=$(curl -s "http://${HOST}/api/v2/effects" | jq -r '.effects[] | .id')

if [ -z "$EFFECTS" ]; then
    echo "❌ Error: Failed to fetch effects list"
    exit 1
fi

EFFECT_COUNT=$(echo "$EFFECTS" | wc -l | tr -d ' ')
echo "Found ${EFFECT_COUNT} effects"
echo ""

# Get current segments
SEGMENTS=$(curl -s "http://${HOST}/api/v2/segments")
SEGMENT_COUNT=$(echo "$SEGMENTS" | jq -r '.segments | length')

# If no segments exist, create a full strip segment
if [ "$SEGMENT_COUNT" -eq 0 ]; then
    echo "📝 No segments found, creating full strip segment..."
    LED_COUNT=$(echo "$SEGMENTS" | jq -r '.ledCount // 160')
    
    CREATE_RESULT=$(curl -s -X POST "http://${HOST}/api/v2/segments" \
        -H "Content-Type: application/json" \
        -d "{\"start\":0,\"length\":${LED_COUNT},\"effect\":\"rainbow\",\"speed\":128,\"intensity\":128}")
    
    if [ $? -eq 0 ]; then
        echo "✅ Segment created"
    else
        echo "❌ Error: Failed to create segment"
        exit 1
    fi
    
    # Re-fetch segments
    SEGMENTS=$(curl -s "http://${HOST}/api/v2/segments")
    sleep 1
fi

# Get first segment ID
SEGMENT_ID=$(echo "$SEGMENTS" | jq -r '.segments[0].id')
echo "🎯 Using segment ID: ${SEGMENT_ID}"
echo ""

# Turn on power if needed
echo "⚡ Ensuring power is ON..."
curl -s -X PUT "http://${HOST}/api/v2/controller" \
    -H "Content-Type: application/json" \
    -d '{"power":true}' > /dev/null

sleep 1
echo ""

# Cycle through all effects
echo "🔄 Starting effect cycle..."
echo "Press Ctrl+C to stop"
echo ""

COUNTER=1
for EFFECT in $EFFECTS; do
    # Get effect name for display
    EFFECT_NAME=$(curl -s "http://${HOST}/api/v2/effects" | jq -r ".effects[] | select(.id == \"${EFFECT}\") | .name")
    
    printf "[%2d/%2d] Testing: %-20s " "$COUNTER" "$EFFECT_COUNT" "$EFFECT_NAME"
    
    # Update segment with the effect
    RESULT=$(curl -s -X PUT "http://${HOST}/api/v2/segments/${SEGMENT_ID}" \
        -H "Content-Type: application/json" \
        -d "{\"effect\":\"${EFFECT}\"}" \
        -w "%{http_code}" \
        -o /dev/null)
    
    if [ "$RESULT" -eq 200 ]; then
        echo "✅"
    else
        echo "❌ (HTTP ${RESULT})"
    fi
    
    sleep "$DELAY"
    ((COUNTER++))
done

echo ""
echo "🎉 Effect cycle complete!"
echo ""
echo "Current state:"
curl -s "http://${HOST}/api/v2/segments" | jq '.segments[0] | {id, effect, speed, intensity}'
