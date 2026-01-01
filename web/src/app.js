// Modal management
function openConfigModal() {
    document.getElementById("configModal").classList.add("show");
    loadSegmentsConfig(); // Load segments when modal opens
}

function closeConfigModal() {
    document.getElementById("configModal").classList.remove("show");
}

// Close modal when clicking outside
document.addEventListener("click", function (e) {
    const modal = document.getElementById("configModal");
    if (e.target === modal) {
        closeConfigModal();
    }
});

// Theme management
function toggleTheme() {
    const html = document.documentElement;
    const currentTheme = html.getAttribute("data-theme") || "dark";
    const newTheme = currentTheme === "dark" ? "light" : "dark";

    html.setAttribute("data-theme", newTheme);
    localStorage.setItem("theme", newTheme);

    // Update icon
    const icon = document.getElementById("themeIcon");
    icon.textContent = newTheme === "dark" ? "🌙" : "☀️";
}

// Load theme from localStorage on page load
function loadTheme() {
    const savedTheme = localStorage.getItem("theme") || "dark";
    document.documentElement.setAttribute("data-theme", savedTheme);
    const icon = document.getElementById("themeIcon");
    icon.textContent = savedTheme === "dark" ? "🌙" : "☀️";
}

// Load theme immediately
loadTheme();

// Load palette preset (v2 cannot read preset back reliably)
(function loadPalettePreset() {
    const saved = localStorage.getItem("palettePreset");
    if (saved) {
        const pal = document.getElementById("palette");
        if (pal) pal.value = saved;
        selectTileByValue("paletteTiles", saved);
    }
})();

// State
let sliderBindings = {};
let effectMetadata = {}; // Map of effect ID -> metadata (usesPalette, usesSpeed, etc.)
let activeSegmentId = 0; // Currently selected segment

// Segment name helpers (stored in localStorage)
function getSegmentName(segmentId) {
    const names = JSON.parse(localStorage.getItem("segmentNames") || "{}");
    return names[segmentId] || null;
}

function setSegmentName(segmentId, name) {
    const names = JSON.parse(localStorage.getItem("segmentNames") || "{}");
    if (name && name.trim()) {
        names[segmentId] = name.trim();
    } else {
        delete names[segmentId];
    }
    localStorage.setItem("segmentNames", JSON.stringify(names));
}

// Toast notification
function showToast(message, type = "info") {
    const toast = document.getElementById("toast");
    toast.textContent = message;
    toast.className = "toast show " + type;
    setTimeout(() => toast.classList.remove("show"), 3000);
}

// API helpers
async function api(endpoint, method = "GET", body = null) {
    const options = {
        method,
        headers: { "Content-Type": "application/json" },
    };
    if (body) options.body = JSON.stringify(body);

    const response = await fetch("/api" + endpoint, options);
    if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
    }
    return response.json();
}

// v2 API helpers (segments/controller)
const PALETTE_PRESETS = {
    rainbow: 0,
    lava: 1,
    ocean: 2,
    party: 3,
    forest: 4,
    cloud: 5,
    heat: 6,
};

async function apiV2(path, method = "GET", body = null) {
    const options = {
        method,
        headers: { "Content-Type": "application/json" },
    };
    if (body) options.body = JSON.stringify(body);

    const response = await fetch("/api/v2" + path, options);
    if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
    }
    return response.json();
}

// WebSocket (optional): server should expose /ws and send {type:'state', controller:{...}, segments:[...]}
let ws = null;
function connectWebSocket() {
    try {
        const proto = location.protocol === "https:" ? "wss" : "ws";
        ws = new WebSocket(`${proto}://${location.host}/ws`);
        ws.onopen = () => console.log("WS connected");
        ws.onclose = () => {
            console.log("WS disconnected, retrying...");
            setTimeout(connectWebSocket, 2000);
        };
        ws.onmessage = (evt) => {
            try {
                const msg = JSON.parse(evt.data);
                if (msg.type === "state") {
                    if (msg.controller) applyControllerToUI(msg.controller);
                    if (msg.segments) {
                        // Update the currently active segment, not always segment 0
                        const activeSeg = msg.segments.find(
                            (s) => s.id === activeSegmentId
                        );
                        if (activeSeg) applySegmentToUI(activeSeg);
                    }
                }
            } catch (e) {
                console.warn("WS message parse error", e);
            }
        };
    } catch (e) {
        console.warn("WS init failed", e);
    }
}

function selectActiveSegment(segments) {
    // Current UI is single-segment oriented; default to segment 0 if present, else first segment.
    if (!Array.isArray(segments) || segments.length === 0) return null;
    const s0 = segments.find((s) => s.id === 0);
    return s0 || segments[0];
}

function applyControllerToUI(controller) {
    if (!controller) return;
    document.getElementById("powerToggle").checked = controller.power !== false;
    // Brightness input is user-controlled only, never updated from server
}

function applySegmentToUI(seg) {
    if (!seg) return;
    document.getElementById("effect").value = seg.effect || "rainbow";
    // Speed input is user-controlled only, never updated from server

    // Palette cannot be read back reliably in v2 (see docs); keep last selected in localStorage.
    const savedPalette = localStorage.getItem("palettePreset") || "rainbow";
    document.getElementById("palette").value = savedPalette;

    selectTileByValue("effectTiles", seg.effect || "rainbow");
    selectTileByValue("paletteTiles", savedPalette);

    if (seg.primaryColor) {
        const [r, g, b] = seg.primaryColor;
        document.getElementById("primaryColor").value = rgbToHex(r, g, b);
    }
    if (seg.secondaryColor) {
        const [r, g, b] = seg.secondaryColor;
        document.getElementById("secondaryColor").value = rgbToHex(r, g, b);
    }
}

// Load segments into dropdown selector
async function loadSegments() {
    try {
        const data = await apiV2("/segments");
        const selector = document.getElementById("segmentSelector");

        if (data.segments && data.segments.length > 0) {
            selector.innerHTML = data.segments
                .map((seg) => {
                    const customName = getSegmentName(seg.id);
                    const label = customName
                        ? `${customName} (LEDs ${seg.start}-${seg.stop})`
                        : `Segment ${seg.id} (LEDs ${seg.start}-${seg.stop})`;
                    return `<option value="${seg.id}">${label}</option>`;
                })
                .join("");

            // Load the first segment's state
            activeSegmentId = data.segments[0].id;
            selector.value = activeSegmentId;
            await loadSegmentState(activeSegmentId);
        } else {
            selector.innerHTML =
                '<option value="-1">No segments - create one in settings</option>';
        }
    } catch (e) {
        console.error("Failed to load segments:", e);
    }
}

// Switch to a different segment
async function switchSegment(segmentId) {
    activeSegmentId = segmentId;
    await loadSegmentState(segmentId);
}

// Load a specific segment's state into UI controls
async function loadSegmentState(segmentId) {
    try {
        const seg = await apiV2(`/segments/${segmentId}`);

        document.getElementById("effect").value = seg.effect || "rainbow";
        document.getElementById("speed").value = seg.speed || 100;
        document.getElementById("speedValue").textContent = seg.speed || 100;
        document.getElementById("intensity").value = seg.intensity ?? 128;
        document.getElementById("intensityValue").textContent =
            seg.intensity ?? 128;
        selectTileByValue("effectTiles", seg.effect || "rainbow");

        const savedPalette = localStorage.getItem("palettePreset") || "rainbow";
        document.getElementById("palette").value = savedPalette;
        selectTileByValue("paletteTiles", savedPalette);

        if (seg.primaryColor) {
            const [r, g, b] = seg.primaryColor;
            document.getElementById("primaryColor").value = rgbToHex(r, g, b);
        }
        if (seg.secondaryColor) {
            const [r, g, b] = seg.secondaryColor;
            document.getElementById("secondaryColor").value = rgbToHex(r, g, b);
        }

        // Update control visibility based on effect
        updateEffectControls(seg.effect);
    } catch (e) {
        console.error(`Failed to load segment ${segmentId} state:`, e);
    }
}

// v2: Load LED state (controller + segments)
async function loadLedState() {
    try {
        const state = await apiV2("/segments");
        // Update controller state
        document.getElementById("powerToggle").checked = state.power !== false;
        document.getElementById("brightness").value = state.brightness ?? 128;
        document.getElementById("brightnessValue").textContent =
            state.brightness ?? 128;

        // Load segments into dropdown
        await loadSegments();
    } catch (e) {
        console.error("Failed to load LED state (v2):", e);
    }
}

// Load effect metadata from API
async function loadEffectMetadata() {
    try {
        const data = await apiV2("/effects");
        if (data.effects) {
            data.effects.forEach((effect) => {
                effectMetadata[effect.id] = {
                    usesPalette: effect.usesPalette,
                    usesPrimaryColor: effect.usesPrimaryColor,
                    usesSecondaryColor: effect.usesSecondaryColor,
                    usesSpeed: effect.usesSpeed,
                    usesIntensity: effect.usesIntensity,
                };
            });
        }
    } catch (e) {
        console.error("Failed to load effect metadata:", e);
    }
}

// Update control visibility based on selected effect
function updateEffectControls(effectId) {
    const metadata = effectMetadata[effectId];
    if (!metadata) return; // Metadata not loaded yet or effect not found

    // Palette controls
    const paletteSection = document.querySelector(".palette-section");
    if (paletteSection) {
        paletteSection.style.display = metadata.usesPalette ? "" : "none";
    }

    // Speed controls
    const speedControl = document.querySelector(".speed-control");
    if (speedControl) {
        speedControl.style.display = metadata.usesSpeed ? "" : "none";
    }

    // Intensity controls
    const intensityControl = document.querySelector(".intensity-control");
    if (intensityControl) {
        intensityControl.style.display = metadata.usesIntensity ? "" : "none";
    }

    // Primary color
    const primaryColorControl = document.querySelector(".primary-color");
    if (primaryColorControl) {
        primaryColorControl.style.display = metadata.usesPrimaryColor ? "" : "none";
    }

    // Secondary color
    const secondaryColorControl = document.querySelector(".secondary-color");
    if (secondaryColorControl) {
        secondaryColorControl.style.display = metadata.usesSecondaryColor
            ? ""
            : "none";
    }

    // Hide entire color section if neither color is used
    // EXCEPT when custom palette is selected (needs color input)
    const colorControls = document.querySelector(".color-controls");
    if (colorControls) {
        const selectedPalette = document.getElementById("palette")?.value;
        const isCustomPalette = selectedPalette === "custom";
        const usesAnyColor =
            metadata.usesPrimaryColor ||
            metadata.usesSecondaryColor ||
            (metadata.usesPalette && isCustomPalette);
        colorControls.style.display = usesAnyColor ? "" : "none";

        // Show both colors for custom palette
        if (isCustomPalette && metadata.usesPalette) {
            if (primaryColorControl) primaryColorControl.style.display = "";
            if (secondaryColorControl) secondaryColorControl.style.display = "";
        }
    }
}

// v2: Apply LED state - updates controller + segment 0
async function applyLedState() {
    const controller = {
        power: document.getElementById("powerToggle").checked,
        brightness: parseInt(document.getElementById("brightness").value),
    };

    const paletteName = document.getElementById("palette").value;
    localStorage.setItem("palettePreset", paletteName);

    const segment = {
        effect: document.getElementById("effect").value,
        speed: parseInt(document.getElementById("speed").value),
        intensity: parseInt(document.getElementById("intensity").value),
        primaryColor: hexToRgb(document.getElementById("primaryColor").value),
        secondaryColor: hexToRgb(document.getElementById("secondaryColor").value),
        palette: PALETTE_PRESETS[paletteName] ?? 0,
    };

    try {
        // Update controller
        await apiV2("/controller", "PUT", controller);

        // Update active segment
        if (activeSegmentId >= 0) {
            await apiV2(`/segments/${activeSegmentId}`, "PUT", segment);
            showToast("Settings applied!", "success");
        } else {
            showToast("No segment selected", "error");
        }
    } catch (e) {
        showToast("Failed to apply settings (v2)", "error");
        console.error(e);
    }
}

// Load status
async function loadStatus() {
    try {
        const status = await api("/status");

        document.getElementById("wifiDot").className = "status-dot";
        document.getElementById("wifiStatus").textContent =
            status.wifi || "Connected";
        document.getElementById("ipAddress").textContent = status.ip || "--";

        const uptime = status.uptime || 0;
        const hours = Math.floor(uptime / 3600);
        const mins = Math.floor((uptime % 3600) / 60);
        document.getElementById("uptime").textContent = `${hours}h ${mins}m`;

        // Update sACN status
        const sacn = status.sacn || {};
        const sacnDot = document.getElementById("sacnDot");
        const sacnText = document.getElementById("sacnStatusText");
        if (!sacn.enabled) {
            sacnDot.className = "status-dot offline";
            sacnText.textContent = "Not enabled";
        } else if (sacn.receiving) {
            sacnDot.className = "status-dot";
            sacnText.textContent = `Receiving (${sacn.packets} pkts, uni ${sacn.universe})`;
        } else {
            sacnDot.className = "status-dot loading";
            sacnText.textContent = `Waiting for data (uni ${sacn.universe})`;
        }

        // Update MQTT status
        const mqtt = status.mqtt || {};
        const mqttDot = document.getElementById("mqttDot");
        const mqttText = document.getElementById("mqttStatusText");
        if (!mqtt.enabled) {
            mqttDot.className = "status-dot offline";
            mqttText.textContent = "Not enabled";
        } else if (mqtt.connected) {
            mqttDot.className = "status-dot";
            mqttText.textContent = `Connected to ${mqtt.broker}`;
        } else {
            mqttDot.className = "status-dot loading";
            mqttText.textContent = "Connecting...";
        }
    } catch (e) {
        document.getElementById("wifiDot").className = "status-dot offline";
        document.getElementById("wifiStatus").textContent = "Offline";
    }
}

// Load LED state (v2) defined above

// Simple slider handlers - update display on input, apply on release
function setupSlider(inputId, valueId) {
    const input = document.getElementById(inputId);
    const display = document.getElementById(valueId);
    if (!input || !display) return;

    let isDragging = false;

    // Update display while dragging
    input.addEventListener("input", () => {
        display.textContent = input.value;
    });

    // Mark as dragging
    ["pointerdown", "mousedown", "touchstart"].forEach((evt) => {
        input.addEventListener(evt, () => {
            isDragging = true;
        });
    });

    // Send update when released
    ["pointerup", "mouseup", "touchend"].forEach((evt) => {
        input.addEventListener(evt, () => {
            if (isDragging) {
                isDragging = false;
                applyLedState();
            }
        });
    });
}

setupSlider("brightness", "brightnessValue");
setupSlider("speed", "speedValue");
setupSlider("intensity", "intensityValue");

// Tile selector functions - auto-apply on selection
function selectEffect(tile) {
    const container = document.getElementById("effectTiles");
    container
        .querySelectorAll(".tile")
        .forEach((t) => t.classList.remove("selected"));
    tile.classList.add("selected");
    const effectId = tile.dataset.value;
    document.getElementById("effect").value = effectId;
    updateEffectControls(effectId);
    applyLedState();
}

function selectPalette(tile) {
    localStorage.setItem("palettePreset", tile.dataset.value);
    const container = document.getElementById("paletteTiles");
    container
        .querySelectorAll(".tile")
        .forEach((t) => t.classList.remove("selected"));
    tile.classList.add("selected");
    document.getElementById("palette").value = tile.dataset.value;

    // Update control visibility (custom palette needs color pickers)
    const currentEffect = document.getElementById("effect").value;
    updateEffectControls(currentEffect);

    applyLedState();
}

function selectTileByValue(containerId, value) {
    const container = document.getElementById(containerId);
    if (!container) return;
    container.querySelectorAll(".tile").forEach((t) => {
        if (t.dataset.value === value) {
            t.classList.add("selected");
        } else {
            t.classList.remove("selected");
        }
    });
}

// Apply LED state (v2) defined above

// Nightlight functions
let nightlightPollInterval = null;

async function loadNightlightStatus() {
    try {
        const status = await api("/nightlight");
        updateNightlightUI(status);
    } catch (e) {
        console.error("Failed to load nightlight status:", e);
    }
}

// Flag to prevent toggle change handler from firing during programmatic updates
let updatingNightlightUI = false;

function updateNightlightUI(status) {
    const isActive = status.active;
    const controls = document.getElementById("nightlightControls");
    const toggle = document.getElementById("nightlightToggle");

    // Prevent change handler from running when we set checked programmatically
    updatingNightlightUI = true;
    toggle.checked = isActive;
    updatingNightlightUI = false;

    document.getElementById("startNightlightBtn").style.display = isActive
        ? "none"
        : "";
    document.getElementById("stopNightlightBtn").style.display = isActive
        ? ""
        : "none";
    document.getElementById("nightlightProgress").style.display = isActive
        ? ""
        : "none";

    // Expand/collapse controls based on active state
    if (isActive) {
        controls.classList.add("expanded");
    } else {
        controls.classList.remove("expanded");
    }

    if (isActive) {
        const progress = Math.round((status.progress || 0) * 100);
        document.getElementById("nightlightProgressBar").style.width =
            progress + "%";
        document.getElementById("nightlightProgressText").textContent =
            progress + "% complete";

        // Start polling for progress updates
        if (!nightlightPollInterval) {
            nightlightPollInterval = setInterval(loadNightlightStatus, 2000);
        }
    } else {
        // Stop polling
        if (nightlightPollInterval) {
            clearInterval(nightlightPollInterval);
            nightlightPollInterval = null;
        }
    }
}

function toggleNightlightControls(show) {
    const controls = document.getElementById("nightlightControls");
    if (show) {
        controls.classList.add("expanded");
    } else {
        controls.classList.remove("expanded");
    }
}

async function startNightlight() {
    const durationMinutes = parseInt(
        document.getElementById("nightlightDuration").value
    );
    const targetBrightness = parseInt(
        document.getElementById("nightlightTarget").value
    );

    try {
        const result = await api("/nightlight", "POST", {
            duration: durationMinutes * 60, // Convert to seconds
            targetBrightness: targetBrightness,
        });
        showToast("Nightlight started!", "success");
        // Wait a moment for server to process, then start polling
        setTimeout(() => {
            loadNightlightStatus();
        }, 200);
    } catch (e) {
        showToast("Failed to start nightlight", "error");
        // Reset toggle on error
        const toggle = document.getElementById("nightlightToggle");
        updatingNightlightUI = true;
        toggle.checked = false;
        updatingNightlightUI = false;
    }
}

async function stopNightlight() {
    try {
        await api("/nightlight/stop", "POST", {});
        showToast("Nightlight stopped", "success");
        // Stop polling immediately
        if (nightlightPollInterval) {
            clearInterval(nightlightPollInterval);
            nightlightPollInterval = null;
        }
        // Update UI immediately without waiting for server
        const toggle = document.getElementById("nightlightToggle");
        const controls = document.getElementById("nightlightControls");
        updatingNightlightUI = true;
        toggle.checked = false;
        updatingNightlightUI = false;
        controls.classList.remove("expanded");
        document.getElementById("startNightlightBtn").style.display = "";
        document.getElementById("stopNightlightBtn").style.display = "none";
        document.getElementById("nightlightProgress").style.display = "none";
    } catch (e) {
        showToast("Failed to stop nightlight", "error");
    }
}

// Load config
async function loadConfig() {
    try {
        const config = await api("/config");

        document.getElementById("wifiSSID").value = config.wifiSSID || "";
        document.getElementById("ledCount").value = config.ledCount || 160;

        // AI settings
        document.getElementById("aiApiKey").value =
            config.aiApiKey && config.aiApiKey !== "****" ? "" : "";
        document.getElementById("aiModel").value =
            config.aiModel || "claude-3-5-sonnet-20241022";

        // sACN settings
        const sacnEnabled = config.sacnEnabled || false;
        document.getElementById("sacnEnabled").checked = sacnEnabled;
        document.getElementById("sacnUniverse").value = config.sacnUniverse || 1;
        document.getElementById("sacnStartChannel").value =
            config.sacnStartChannel || 1;
        toggleSettings("sacnSettings", sacnEnabled);

        // MQTT settings
        const mqttEnabled = config.mqttEnabled || false;
        document.getElementById("mqttEnabled").checked = mqttEnabled;
        document.getElementById("mqttBroker").value = config.mqttBroker || "";
        document.getElementById("mqttPort").value = config.mqttPort || 1883;
        document.getElementById("mqttUsername").value =
            config.mqttUsername && config.mqttUsername !== "****"
                ? config.mqttUsername
                : "";
        document.getElementById("mqttPassword").value =
            config.mqttPassword && config.mqttPassword !== "****" ? "" : "";
        document.getElementById("mqttTopicPrefix").value =
            config.mqttTopicPrefix || "lume";
        toggleSettings("mqttSettings", mqttEnabled);
    } catch (e) {
        console.error("Failed to load config:", e);
    }
}

// Load segments for config modal
async function loadSegmentsConfig() {
    try {
        const data = await apiV2("/segments");
        const container = document.getElementById("segmentsList");

        if (!data.segments || data.segments.length === 0) {
            container.innerHTML =
                '<p style="color: var(--text-muted); font-size: 13px;">No segments configured</p>';
            return;
        }

        container.innerHTML = data.segments
            .map((seg) => {
                const customName = getSegmentName(seg.id);
                const displayName = customName || `Segment ${seg.id}`;
                return `
                    <div style="display: flex; align-items: center; gap: 8px; padding: 8px; background: var(--surface); border-radius: 8px; margin-bottom: 8px;">
                        <div style="flex: 1;">
                            <div style="font-weight: 500;">${displayName}</div>
                            <div style="font-size: 12px; color: var(--text-muted);">
                                LEDs ${seg.start}-${seg.stop} (${seg.length
                    } LEDs)
                                ${seg.effect ? `• ${seg.effect}` : ""}
                            </div>
                        </div>
                        <button class="btn btn-outline" onclick="editSegmentName(${seg.id
                    })" style="padding: 6px 12px;">Rename</button>
                        ${seg.length > 1
                        ? `<button class="btn btn-outline" onclick="splitSegment(${seg.id}, ${seg.start}, ${seg.length})" style="padding: 6px 12px;">Split</button>`
                        : ""
                    }
                        <button class="btn btn-outline" onclick="deleteSegmentConfig(${seg.id
                    })" style="padding: 6px 12px;">Delete</button>
                    </div>
                `;
            })
            .join("");
    } catch (e) {
        console.error("Failed to load segments:", e);
    }
}

async function editSegmentName(id) {
    const currentName = getSegmentName(id) || "";
    const newName = prompt(`Enter name for segment ${id}:`, currentName);

    if (newName !== null) {
        setSegmentName(id, newName);
        await loadSegmentsConfig(); // Refresh config list
        await loadSegments(); // Refresh dropdown
    }
}

async function splitSegment(id, start, length) {
    const splitAt = prompt(
        `Split segment ${id} at LED position? (${start + 1} to ${start + length - 1
        })`
    );
    if (!splitAt) return;

    const splitPos = parseInt(splitAt);
    if (isNaN(splitPos) || splitPos <= start || splitPos >= start + length) {
        showToast("Invalid split position", "error");
        return;
    }

    try {
        // Calculate new segment lengths
        const firstLength = splitPos - start;
        const secondLength = length - firstLength;

        // Delete original segment
        await fetch("/api/v2/segments/" + id, { method: "DELETE" });

        // Create first segment
        await apiV2("/segments", "POST", {
            start: start,
            length: firstLength,
        });

        // Create second segment
        await apiV2("/segments", "POST", {
            start: splitPos,
            length: secondLength,
        });

        showToast("Segment split successfully!", "success");
        loadSegmentsConfig();
    } catch (e) {
        showToast("Failed to split segment", "error");
        console.error(e);
    }
}

async function addSegment() {
    const start = parseInt(document.getElementById("newSegmentStart").value);
    const length = parseInt(document.getElementById("newSegmentLength").value);

    if (isNaN(start) || isNaN(length) || start < 0 || length < 1) {
        showToast("Invalid segment range", "error");
        return;
    }

    try {
        await apiV2("/segments", "POST", {
            start: start,
            length: length,
        });
        showToast("Segment created!", "success");
        loadSegmentsConfig();

        // Update start field for next segment
        document.getElementById("newSegmentStart").value = start + length;
    } catch (e) {
        showToast("Failed to create segment", "error");
        console.error(e);
    }
}

async function deleteSegmentConfig(id) {
    if (!confirm(`Delete segment ${id}?`)) {
        return;
    }

    try {
        await fetch("/api/v2/segments/" + id, { method: "DELETE" });
        showToast("Segment deleted", "success");
        loadSegmentsConfig();
    } catch (e) {
        showToast("Failed to delete segment", "error");
        console.error(e);
    }
}

// Save config
async function saveConfig() {
    const config = {
        wifiSSID: document.getElementById("wifiSSID").value,
        wifiPassword: document.getElementById("wifiPassword").value,
        ledCount: parseInt(document.getElementById("ledCount").value),
        aiApiKey: document.getElementById("aiApiKey").value,
        aiModel: document.getElementById("aiModel").value,
        sacnEnabled: document.getElementById("sacnEnabled").checked,
        sacnUniverse: parseInt(document.getElementById("sacnUniverse").value),
        sacnStartChannel: parseInt(
            document.getElementById("sacnStartChannel").value
        ),
        mqttEnabled: document.getElementById("mqttEnabled").checked,
        mqttBroker: document.getElementById("mqttBroker").value,
        mqttPort: parseInt(document.getElementById("mqttPort").value),
        mqttUsername: document.getElementById("mqttUsername").value,
        mqttPassword: document.getElementById("mqttPassword").value,
        mqttTopicPrefix: document.getElementById("mqttTopicPrefix").value,
    };

    // Don't send masked password/key
    if (config.wifiPassword === "") delete config.wifiPassword;
    if (config.mqttPassword === "") delete config.mqttPassword;
    if (config.aiApiKey === "") delete config.aiApiKey;

    try {
        await api("/config", "POST", config);
        showToast("Configuration saved!", "success");
        loadConfig();
    } catch (e) {
        showToast("Failed to save configuration", "error");
    }
}

// Scene management
async function loadScenes() {
    try {
        const scenes = await api("/scenes");
        const container = document.getElementById("scenesList");

        if (!scenes || scenes.length === 0) {
            container.innerHTML =
                '<p style="color: var(--text-muted); font-size: 14px;">No saved scenes yet</p>';
            return;
        }

        container.innerHTML = scenes
            .map(
                (scene) => `
                    <div class="scene-item">
                        <span class="scene-name">${escapeHtml(
                    scene.name
                )}</span>
                        <div class="scene-actions">
                            <button class="btn btn-primary" onclick="applyScene(${scene.id
                    })">Apply</button>
                            <button class="btn btn-outline" onclick="deleteScene(${scene.id
                    })">🗑️</button>
                        </div>
                    </div>
                `
            )
            .join("");
    } catch (e) {
        console.error("Failed to load scenes:", e);
    }
}

function escapeHtml(text) {
    const div = document.createElement("div");
    div.textContent = text;
    return div.innerHTML;
}

async function applyScene(id) {
    try {
        const response = await fetch("/api/scenes/" + id + "/apply", {
            method: "POST",
        });

        if (!response.ok) {
            const result = await response.json();
            showToast("Failed: " + (result.error || response.status), "error");
            return;
        }

        showToast("Scene applied!", "success");
        loadLedState();
    } catch (e) {
        showToast("Failed to apply scene: " + e.message, "error");
    }
}

async function deleteScene(id) {
    if (!confirm("Delete this scene?")) {
        return;
    }

    try {
        await fetch("/api/scenes/" + id, { method: "DELETE" });
        showToast("Scene deleted", "success");
        loadScenes();
    } catch (e) {
        showToast("Failed to delete scene", "error");
    }
}

// Color helpers
function hexToRgb(hex) {
    const result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
    return result
        ? [
            parseInt(result[1], 16),
            parseInt(result[2], 16),
            parseInt(result[3], 16),
        ]
        : [0, 0, 255];
}

function rgbToHex(r, g, b) {
    return "#" + [r, g, b].map((x) => x.toString(16).padStart(2, "0")).join("");
}

// Toggle settings visibility
function toggleSettings(settingsId, show) {
    const settings = document.getElementById(settingsId);
    if (!settings) return;
    settings.style.maxHeight = show ? "500px" : "0";
}

// Event listeners - sliders handled by SliderBinding helpers

// Color pickers auto-apply on change (when picker closes)
document.getElementById("primaryColor").addEventListener("change", function () {
    applyLedState();
});

document
    .getElementById("secondaryColor")
    .addEventListener("change", function () {
        applyLedState();
    });

document.getElementById("powerToggle").addEventListener("change", function () {
    applyLedState();
});

// Nightlight slider listeners
document
    .getElementById("nightlightDuration")
    .addEventListener("input", function () {
        document.getElementById("nightlightDurationValue").textContent =
            this.value + " min";
    });

document
    .getElementById("nightlightTarget")
    .addEventListener("input", function () {
        const val = parseInt(this.value);
        document.getElementById("nightlightTargetValue").textContent =
            val === 0 ? "0 (off)" : val;
    });

document
    .getElementById("nightlightToggle")
    .addEventListener("change", async function () {
        // Ignore programmatic changes
        if (updatingNightlightUI) return;

        if (this.checked) {
            // Toggle ON - expand controls to show settings
            toggleNightlightControls(true);
        } else {
            // Toggle OFF - stop if running, otherwise just hide
            await stopNightlight();
        }
    });

// AI Prompt functions
async function sendAIPrompt() {
    const prompt = document.getElementById("aiPrompt").value.trim();
    if (!prompt) {
        showToast("Please enter a prompt", "error");
        return;
    }

    const statusDiv = document.getElementById("aiStatus");
    const statusText = document.getElementById("aiStatusText");

    statusDiv.style.display = "block";
    statusText.textContent = "Processing your request...";

    try {
        const result = await api("/prompt", "POST", { prompt: prompt });

        if (result.success) {
            showToast("✨ Lights updated!", "success");
            statusText.textContent = result.message || "Applied successfully!";
            setTimeout(() => {
                statusDiv.style.display = "none";
            }, 3000);

            // Reload LED state to show changes
            await loadLedState();
        } else {
            showToast(result.error || "Failed to process prompt", "error");
            statusDiv.style.display = "none";
        }
    } catch (e) {
        showToast("Error: " + (e.message || "Network error"), "error");
        statusDiv.style.display = "none";
        console.error("AI prompt error:", e);
    }
}

// sACN and MQTT toggle handlers
document.getElementById("sacnEnabled").addEventListener("change", function () {
    toggleSettings("sacnSettings", this.checked);
});

document.getElementById("mqttEnabled").addEventListener("change", function () {
    toggleSettings("mqttSettings", this.checked);
});

// Initialize
async function initialize() {
    loadStatus();
    await loadEffectMetadata(); // Load effect metadata FIRST
    await loadLedState(); // Then load LED state (which needs metadata)
    loadConfig();
    loadScenes();
    loadNightlightStatus();
    setInterval(loadStatus, 10000);
}

initialize();

// Start WebSocket (optional)
connectWebSocket();
