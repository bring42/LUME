#!/bin/bash
#
# debug_connection.sh - Debug connection issues
#

HOST="${1:-lume.local}"

echo "🔍 LUME Connection Debug"
echo "Target: ${HOST}"
echo "=========================="
echo ""

echo "1️⃣ Testing DNS resolution:"
if host "${HOST}" > /dev/null 2>&1; then
    echo "✅ DNS resolves:"
    host "${HOST}"
else
    echo "❌ DNS resolution failed"
    echo "   Trying ping..."
    ping -c 1 "${HOST}" 2>&1 | head -3
fi
echo ""

echo "2️⃣ Testing root endpoint (/):"
echo "   curl -v http://${HOST}/ 2>&1 | head -20"
curl -v "http://${HOST}/" 2>&1 | head -20
echo ""

echo "3️⃣ Testing /health endpoint:"
echo "   curl -v http://${HOST}/health 2>&1 | head -20"
curl -v "http://${HOST}/health" 2>&1 | head -20
echo ""

echo "4️⃣ Testing /api/status endpoint:"
echo "   curl -v http://${HOST}/api/status"
curl -v "http://${HOST}/api/status" 2>&1 | head -30
echo ""

echo "5️⃣ Testing /api/v2/segments endpoint:"
echo "   curl http://${HOST}/api/v2/segments"
curl -s "http://${HOST}/api/v2/segments" | head -10
echo ""

echo "=========================="
echo "Debug complete"
