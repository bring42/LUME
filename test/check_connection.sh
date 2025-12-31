#!/bin/bash
#
# check_connection.sh - Diagnostic tool for LUME connectivity
# Helps troubleshoot connection issues
#

echo "🔍 LUME Connection Diagnostic Tool"
echo "=================================="
echo ""

# Function to test a connection
test_connection() {
    local host=$1
    local name=$2
    
    printf "Testing %-30s " "$name ($host)..."
    
    if curl -s --max-time 10 "http://${host}/health" > /dev/null 2>&1; then
        echo "✅ Success"
        return 0
    else
        echo "❌ Failed"
        return 1
    fi
}

# Test common hostnames and IPs
FOUND=0

echo "📡 Testing common LUME addresses:"
echo ""

# Test mDNS hostname
if test_connection "lume.local" "mDNS hostname"; then
    FOUND=1
    FOUND_HOST="lume.local"
fi

# Test AP mode IP
if test_connection "192.168.4.1" "AP Mode"; then
    FOUND=1
    FOUND_HOST="192.168.4.1"
fi

# Test common local IPs
echo ""
echo "🌐 Scanning common local network ranges..."
echo "   (This may take a moment)"
echo ""

for i in {100..110}; do
    host="192.168.1.${i}"
    if timeout 10 curl -s --max-time 10 "http://${host}/health" > /dev/null 2>&1; then
        printf "Testing %-30s ✅ Success\n" "192.168.1.${i}"
        FOUND=1
        FOUND_HOST="192.168.1.${i}"
    fi
done

echo ""
echo "=================================="
echo ""

if [ $FOUND -eq 1 ]; then
    echo "✅ LUME found at: ${FOUND_HOST}"
    echo ""
    echo "📊 Device Status:"
    curl -s "http://${FOUND_HOST}/api/status" | python3 -m json.tool 2>/dev/null || \
        curl -s "http://${FOUND_HOST}/api/status"
    echo ""
    echo "🎯 To run tests:"
    echo "   ./test_effects_simple.sh ${FOUND_HOST}"
    echo "   ./test_effects.sh ${FOUND_HOST}"
    echo ""
    echo "🌐 Web UI:"
    echo "   http://${FOUND_HOST}"
else
    echo "❌ LUME not found"
    echo ""
    echo "Troubleshooting steps:"
    echo ""
    echo "1. Check power:"
    echo "   - Is the ESP32 powered on?"
    echo "   - Do you see LED activity?"
    echo ""
    echo "2. Check WiFi connection:"
    echo "   - Connect to 'LUME-Setup' WiFi network"
    echo "   - Password: ledcontrol"
    echo "   - Then run: ./check_connection.sh"
    echo ""
    echo "3. Check your network:"
    echo "   - Open web UI on device to configure WiFi"
    echo "   - Make sure LUME and your computer are on same network"
    echo ""
    echo "4. Try manual IP:"
    echo "   - Check your router for LUME's IP address"
    echo "   - Run: ./test_effects_simple.sh 192.168.x.x"
    echo ""
    echo "5. Check via serial monitor:"
    echo "   - Connect USB cable"
    echo "   - Run: pio device monitor"
    echo "   - Look for IP address in boot logs"
fi
