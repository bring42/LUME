#!/bin/bash
#
# examples.sh - Example API calls for LUME v2
#
# This script demonstrates common API operations
# Uncomment the examples you want to run
#

HOST="${1:-lume.local}"

echo "🎨 LUME API Examples"
echo "Target: http://${HOST}"
echo ""

# 1. Get current status
echo "📊 Example 1: Get current status"
echo "Command: curl http://${HOST}/api/v2/segments"
# curl -s "http://${HOST}/api/v2/segments" | python3 -m json.tool
echo ""

# 2. Turn on power and set brightness
echo "⚡ Example 2: Power on and set brightness to 200"
echo "Command: curl -X PUT http://${HOST}/api/v2/controller -d '{\"power\":true,\"brightness\":200}'"
# curl -s -X PUT "http://${HOST}/api/v2/controller" \
#     -H "Content-Type: application/json" \
#     -d '{"power":true,"brightness":200}'
echo ""

# 3. Create a full strip rainbow effect
echo "🌈 Example 3: Create full strip rainbow effect"
echo "Command: curl -X POST http://${HOST}/api/v2/segments -d '{\"start\":0,\"length\":160,\"effect\":\"rainbow\"}'"
# curl -s -X POST "http://${HOST}/api/v2/segments" \
#     -H "Content-Type: application/json" \
#     -d '{"start":0,"length":160,"effect":"rainbow","speed":128,"intensity":128}'
echo ""

# 4. Change effect to fire with high speed
echo "🔥 Example 4: Change segment 0 to fire effect with high speed"
echo "Command: curl -X PUT http://${HOST}/api/v2/segments/0 -d '{\"effect\":\"fire\",\"speed\":200}'"
# curl -s -X PUT "http://${HOST}/api/v2/segments/0" \
#     -H "Content-Type: application/json" \
#     -d '{"effect":"fire","speed":200,"intensity":180}'
echo ""

# 5. Set custom colors for gradient effect
echo "🎨 Example 5: Gradient with custom colors (blue to purple)"
echo "Command: curl -X PUT http://${HOST}/api/v2/segments/0 -d '{\"effect\":\"gradient\",\"primaryColor\":[0,0,255],\"secondaryColor\":[128,0,255]}'"
# curl -s -X PUT "http://${HOST}/api/v2/segments/0" \
#     -H "Content-Type: application/json" \
#     -d '{"effect":"gradient","primaryColor":[0,0,255],"secondaryColor":[128,0,255],"speed":100}'
echo ""

# 6. Create multi-segment setup (split strip in half)
echo "✂️ Example 6: Split strip - rainbow on first half, fire on second half"
echo "Command 1: curl -X POST http://${HOST}/api/v2/segments -d '{\"start\":0,\"length\":80,\"effect\":\"rainbow\"}'"
echo "Command 2: curl -X POST http://${HOST}/api/v2/segments -d '{\"start\":80,\"length\":80,\"effect\":\"fire\"}'"
# curl -s -X POST "http://${HOST}/api/v2/segments" \
#     -H "Content-Type: application/json" \
#     -d '{"start":0,"length":80,"effect":"rainbow","speed":128}'
# sleep 1
# curl -s -X POST "http://${HOST}/api/v2/segments" \
#     -H "Content-Type: application/json" \
#     -d '{"start":80,"length":80,"effect":"fire","speed":150}'
echo ""

# 7. Delete all segments (start fresh)
echo "🗑️ Example 7: Delete segment 0"
echo "Command: curl -X DELETE http://${HOST}/api/v2/segments/0"
# curl -s -X DELETE "http://${HOST}/api/v2/segments/0"
echo ""

# 8. List all available effects
echo "📋 Example 8: List all available effects"
echo "Command: curl http://${HOST}/api/v2/effects"
# curl -s "http://${HOST}/api/v2/effects" | python3 -m json.tool
echo ""

# 9. Solid color white at full brightness
echo "💡 Example 9: Solid white at full brightness"
echo "Command: curl -X PUT http://${HOST}/api/v2/segments/0 -d '{\"effect\":\"solid\",\"primaryColor\":[255,255,255]}'"
echo "         curl -X PUT http://${HOST}/api/v2/controller -d '{\"brightness\":255}'"
# curl -s -X PUT "http://${HOST}/api/v2/segments/0" \
#     -H "Content-Type: application/json" \
#     -d '{"effect":"solid","primaryColor":[255,255,255]}'
# curl -s -X PUT "http://${HOST}/api/v2/controller" \
#     -H "Content-Type: application/json" \
#     -d '{"brightness":255}'
echo ""

# 10. Slow breathing effect in blue
echo "🌬️ Example 10: Slow breathing effect in blue"
echo "Command: curl -X PUT http://${HOST}/api/v2/segments/0 -d '{\"effect\":\"breathe\",\"primaryColor\":[0,0,255],\"speed\":50}'"
# curl -s -X PUT "http://${HOST}/api/v2/segments/0" \
#     -H "Content-Type: application/json" \
#     -d '{"effect":"breathe","primaryColor":[0,0,255],"speed":50,"intensity":200}'
echo ""

echo "💡 Tip: Uncomment the curl commands you want to run!"
echo "    Edit this file and remove the # at the start of curl lines"
