#ifndef WEB_UI_H
#define WEB_UI_H

#include <Arduino.h>

// Embedded HTML/CSS/JS for the web interface
// Modern, shadcn-inspired design with card-based layout

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>LUME</title>
    <style>
        :root {
            --bg: #0a0a0a;
            --card: #18181b;
            --card-hover: #27272a;
            --border: #27272a;
            --text: #fafafa;
            --text-muted: #a1a1aa;
            --primary: #3b82f6;
            --primary-hover: #2563eb;
            --success: #22c55e;
            --error: #ef4444;
            --warning: #f59e0b;
            --radius: 8px;
        }
        
        [data-theme="light"] {
            --bg: #ffffff;
            --card: #f9fafb;
            --card-hover: #f3f4f6;
            --border: #e5e7eb;
            --text: #0a0a0a;
            --text-muted: #6b7280;
            --primary: #3b82f6;
            --primary-hover: #2563eb;
            --success: #22c55e;
            --error: #ef4444;
            --warning: #f59e0b;
            --radius: 8px;
        }
        
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: var(--bg);
            color: var(--text);
            line-height: 1.6;
            min-height: 100vh;
            padding: 16px;
        }
        
        .container {
            max-width: 800px;
            margin: 0 auto;
        }
        
        header {
            text-align: center;
            margin-bottom: 24px;
            padding: 16px 0;
            position: relative;
        }
        
        header h1 {
            font-size: 24px;
            font-weight: 600;
            margin-bottom: 4px;
        }
        
        header p {
            color: var(--text-muted);
            font-size: 14px;
        }
        
        .theme-toggle, .config-toggle {
            position: absolute;
            top: 16px;
            background: var(--card);
            border: 1px solid var(--border);
            border-radius: var(--radius);
            width: 40px;
            height: 40px;
            display: flex;
            align-items: center;
            justify-content: center;
            cursor: pointer;
            transition: all 0.2s;
            font-size: 20px;
        }
        
        .theme-toggle {
            right: 0;
        }
        
        .config-toggle {
            left: 0;
        }
        
        .theme-toggle:hover, .config-toggle:hover {
            background: var(--card-hover);
            transform: scale(1.05);
        }
        
        .theme-toggle:active, .config-toggle:active {
            transform: scale(0.95);
        }
        
        .modal {
            display: none;
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background: rgba(0, 0, 0, 0.8);
            z-index: 1000;
            align-items: center;
            justify-content: center;
            padding: 16px;
        }
        
        .modal.show {
            display: flex;
        }
        
        .modal-content {
            background: var(--bg);
            border: 1px solid var(--border);
            border-radius: var(--radius);
            max-width: 600px;
            width: 100%;
            max-height: 90vh;
            overflow-y: auto;
            padding: 24px;
            position: relative;
        }
        
        .modal-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 20px;
            padding-bottom: 16px;
            border-bottom: 1px solid var(--border);
        }
        
        .modal-title {
            font-size: 20px;
            font-weight: 600;
        }
        
        .modal-close {
            background: transparent;
            border: none;
            font-size: 24px;
            cursor: pointer;
            color: var(--text-muted);
            padding: 0;
            width: 32px;
            height: 32px;
            display: flex;
            align-items: center;
            justify-content: center;
            border-radius: 4px;
            transition: all 0.2s;
        }
        
        .modal-close:hover {
            background: var(--card-hover);
            color: var(--text);
        }
        
        .status-bar {
            display: flex;
            gap: 16px;
            flex-wrap: wrap;
            justify-content: center;
            margin-bottom: 24px;
        }
        
        .status-item {
            display: flex;
            align-items: center;
            gap: 6px;
            font-size: 12px;
            color: var(--text-muted);
        }
        
        .status-dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
            background: var(--success);
        }
        
        .status-dot.offline { background: var(--error); }
        .status-dot.loading { background: var(--warning); animation: pulse 1s infinite; }
        
        @keyframes pulse {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.5; }
        }
        
        .card {
            background: var(--card);
            border: 1px solid var(--border);
            border-radius: var(--radius);
            padding: 20px;
            margin-bottom: 16px;
        }
        
        .card-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 16px;
        }
        
        .card-title {
            font-size: 16px;
            font-weight: 600;
        }
        
        .form-group {
            margin-bottom: 16px;
        }
        
        .form-group:last-child {
            margin-bottom: 0;
        }
        
        label {
            display: block;
            font-size: 14px;
            font-weight: 500;
            margin-bottom: 6px;
            color: var(--text);
        }
        
        .label-row {
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        
        .label-value {
            font-size: 12px;
            color: var(--text-muted);
            font-family: monospace;
        }
        
        input[type="text"],
        input[type="password"],
        input[type="number"],
        select,
        textarea {
            width: 100%;
            padding: 10px 12px;
            background: var(--bg);
            border: 1px solid var(--border);
            border-radius: var(--radius);
            color: var(--text);
            font-size: 14px;
            outline: none;
            transition: border-color 0.2s;
        }
        
        input:focus,
        select:focus,
        textarea:focus {
            border-color: var(--primary);
        }
        
        textarea {
            resize: vertical;
            min-height: 80px;
        }
        
        input[type="range"] {
            width: 100%;
            height: 6px;
            border-radius: 3px;
            background: var(--border);
            outline: none;
            -webkit-appearance: none;
        }
        
        input[type="range"]::-webkit-slider-thumb {
            -webkit-appearance: none;
            width: 18px;
            height: 18px;
            border-radius: 50%;
            background: var(--primary);
            cursor: pointer;
        }
        
        input[type="color"] {
            width: 100%;
            height: 40px;
            border: none;
            border-radius: var(--radius);
            cursor: pointer;
            padding: 0;
        }
        
        .color-row {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 12px;
        }
        
        .btn {
            display: inline-flex;
            align-items: center;
            justify-content: center;
            gap: 8px;
            padding: 10px 16px;
            font-size: 14px;
            font-weight: 500;
            border-radius: var(--radius);
            border: none;
            cursor: pointer;
            transition: all 0.2s;
        }
        
        .btn-primary {
            background: var(--primary);
            color: white;
        }
        
        .btn-primary:hover {
            background: var(--primary-hover);
        }
        
        .btn-primary:disabled {
            opacity: 0.5;
            cursor: not-allowed;
        }
        
        .btn-outline {
            background: transparent;
            border: 1px solid var(--border);
            color: var(--text);
        }
        
        .btn-outline:hover {
            background: var(--card-hover);
        }
        
        .btn-group {
            display: flex;
            gap: 8px;
            flex-wrap: wrap;
        }
        
        .toggle {
            position: relative;
            width: 48px;
            height: 26px;
        }
        
        .toggle input {
            opacity: 0;
            width: 0;
            height: 0;
        }
        
        .toggle-slider {
            position: absolute;
            cursor: pointer;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background: var(--border);
            border-radius: 26px;
            transition: 0.2s;
        }
        
        .toggle-slider:before {
            content: "";
            position: absolute;
            height: 20px;
            width: 20px;
            left: 3px;
            bottom: 3px;
            background: white;
            border-radius: 50%;
            transition: 0.2s;
        }
        
        .toggle input:checked + .toggle-slider {
            background: var(--primary);
        }
        
        .toggle input:checked + .toggle-slider:before {
            transform: translateX(22px);
        }
        
        .toast {
            position: fixed;
            bottom: 20px;
            right: 20px;
            padding: 12px 20px;
            background: var(--card);
            border: 1px solid var(--border);
            border-radius: var(--radius);
            font-size: 14px;
            transform: translateY(100px);
            opacity: 0;
            transition: all 0.3s;
            z-index: 1000;
            max-width: 300px;
        }
        
        .toast.show {
            transform: translateY(0);
            opacity: 1;
        }
        
        .toast.success { border-color: var(--success); }
        .toast.error { border-color: var(--error); }
        
        .prompt-output {
            background: var(--bg);
            border: 1px solid var(--border);
            border-radius: var(--radius);
            padding: 12px;
            font-family: monospace;
            font-size: 12px;
            max-height: 200px;
            overflow-y: auto;
            white-space: pre-wrap;
            word-break: break-all;
        }
        
        .prompt-status {
            display: flex;
            align-items: center;
            gap: 8px;
            padding: 8px 12px;
            background: var(--bg);
            border-radius: var(--radius);
            font-size: 13px;
            margin-bottom: 12px;
        }
        
        .prompt-status.running { color: var(--warning); }
        .prompt-status.done { color: var(--success); }
        .prompt-status.error { color: var(--error); }
        
        .scene-item {
            display: flex;
            align-items: center;
            justify-content: space-between;
            padding: 10px 12px;
            background: var(--bg);
            border: 1px solid var(--border);
            border-radius: var(--radius);
            margin-bottom: 8px;
        }
        
        .scene-item:hover {
            border-color: var(--primary);
        }
        
        .scene-name {
            font-weight: 500;
        }
        
        .scene-actions {
            display: flex;
            gap: 8px;
        }
        
        .scene-actions button {
            padding: 4px 10px;
            font-size: 12px;
        }
        
        .grid-2 {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 12px;
        }
        
        @media (max-width: 600px) {
            .grid-2, .color-row {
                grid-template-columns: 1fr;
            }
            
            body {
                padding: 12px;
            }
        }
        
        .collapsible-header {
            cursor: pointer;
            user-select: none;
        }
        
        .collapsible-header::after {
            content: "▼";
            font-size: 10px;
            margin-left: 8px;
            transition: transform 0.2s;
        }
        
        .collapsible.collapsed .collapsible-header::after {
            transform: rotate(-90deg);
        }
        
        .collapsible.collapsed .collapsible-content {
            display: none;
        }
        
        /* Tile selector for effects and palettes */
        .tile-row {
            display: flex;
            gap: 8px;
            overflow-x: auto;
            padding: 4px 0 12px 0;
            scroll-behavior: smooth;
            -webkit-overflow-scrolling: touch;
            scrollbar-width: thin;
        }
        
        .tile-row::-webkit-scrollbar {
            height: 4px;
        }
        
        .tile-row::-webkit-scrollbar-thumb {
            background: var(--border);
            border-radius: 2px;
        }
        
        .tile {
            flex: 0 0 auto;
            min-width: 70px;
            padding: 10px 14px;
            background: var(--card);
            border: 2px solid var(--border);
            border-radius: var(--radius);
            cursor: pointer;
            text-align: center;
            font-size: 12px;
            transition: all 0.15s;
            white-space: nowrap;
        }
        
        .tile:hover {
            background: var(--card-hover);
            border-color: var(--text-muted);
        }
        
        .tile.selected {
            border-color: var(--primary);
            background: rgba(59, 130, 246, 0.1);
        }
        
        .tile-icon {
            font-size: 18px;
            display: block;
            margin-bottom: 4px;
        }
        
        .tile-label {
            font-size: 11px;
            color: var(--text-muted);
        }
        
        .tile.selected .tile-label {
            color: var(--primary);
        }
        
        /* Palette preview strip */
        .palette-preview {
            height: 4px;
            border-radius: 2px;
            margin-top: 6px;
        }
        
        /* Nightlight expand/collapse */
        #nightlightControls {
            max-height: 0;
            overflow: hidden;
            transition: max-height 0.3s ease-out, opacity 0.2s;
            opacity: 0;
        }
        
        #nightlightControls.expanded {
            max-height: 300px;
            opacity: 1;
        }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <button class="config-toggle" onclick="openConfigModal()" title="Configuration">
                ⚙️
            </button>
            <button class="theme-toggle" onclick="toggleTheme()" title="Toggle theme">
                <span id="themeIcon">🌙</span>
            </button>
            <h1 style="font-weight: 200; letter-spacing: 0.15em;">LUME</h1>
            <p>AI-Powered LED Control</p>
        </header>
        
        <div class="status-bar">
            <div class="status-item">
                <div class="status-dot" id="wifiDot"></div>
                <span id="wifiStatus">Connecting...</span>
            </div>
            <div class="status-item">
                <span id="ipAddress">--</span>
            </div>
            <div class="status-item">
                <span id="uptime">--</span>
            </div>
        </div>
        
        <!-- Power & Brightness -->
        <div class="card">
            <div class="card-header">
                <span class="card-title">Power & Brightness</span>
                <label class="toggle">
                    <input type="checkbox" id="powerToggle" checked>
                    <span class="toggle-slider"></span>
                </label>
            </div>
            
            <div class="form-group">
                <div class="label-row">
                    <label for="brightness">Brightness</label>
                    <span class="label-value" id="brightnessValue">128</span>
                </div>
                <input type="range" id="brightness" min="0" max="255" value="128">
            </div>
        </div>
        
        <!-- Effects -->
        <div class="card">
            <div class="card-header">
                <span class="card-title">Effect</span>
            </div>
            
            <div class="form-group">
                <label>Effect Type</label>
                <div class="tile-row" id="effectTiles">
                    <div class="tile" data-value="solid" onclick="selectEffect(this)">
                        <span class="tile-icon">⬤</span>
                        <span class="tile-label">Solid</span>
                    </div>
                    <div class="tile selected" data-value="rainbow" onclick="selectEffect(this)">
                        <span class="tile-icon">🌈</span>
                        <span class="tile-label">Rainbow</span>
                    </div>
                    <div class="tile" data-value="confetti" onclick="selectEffect(this)">
                        <span class="tile-icon">🎊</span>
                        <span class="tile-label">Confetti</span>
                    </div>
                    <div class="tile" data-value="fire" onclick="selectEffect(this)">
                        <span class="tile-icon">🔥</span>
                        <span class="tile-label">Fire</span>
                    </div>
                    <div class="tile" data-value="fireup" onclick="selectEffect(this)">
                        <span class="tile-icon">🔥</span>
                        <span class="tile-label">Fire Up</span>
                    </div>
                    <div class="tile" data-value="colorwaves" onclick="selectEffect(this)">
                        <span class="tile-icon">🌊</span>
                        <span class="tile-label">Waves</span>
                    </div>
                    <div class="tile" data-value="wave" onclick="selectEffect(this)">
                        <span class="tile-icon">〰️</span>
                        <span class="tile-label">Wave</span>
                    </div>
                    <div class="tile" data-value="theater" onclick="selectEffect(this)">
                        <span class="tile-icon">🎭</span>
                        <span class="tile-label">Theater</span>
                    </div>
                    <div class="tile" data-value="gradient" onclick="selectEffect(this)">
                        <span class="tile-icon">◐</span>
                        <span class="tile-label">Gradient</span>
                    </div>
                    <div class="tile" data-value="sparkle" onclick="selectEffect(this)">
                        <span class="tile-icon">✨</span>
                        <span class="tile-label">Sparkle</span>
                    </div>
                    <div class="tile" data-value="pulse" onclick="selectEffect(this)">
                        <span class="tile-icon">💓</span>
                        <span class="tile-label">Pulse</span>
                    </div>
                    <div class="tile" data-value="breathe" onclick="selectEffect(this)">
                        <span class="tile-icon">🌬️</span>
                        <span class="tile-label">Breathe</span>
                    </div>
                    <div class="tile" data-value="noise" onclick="selectEffect(this)">
                        <span class="tile-icon">📺</span>
                        <span class="tile-label">Noise</span>
                    </div>
                    <div class="tile" data-value="meteor" onclick="selectEffect(this)">
                        <span class="tile-icon">☄️</span>
                        <span class="tile-label">Meteor</span>
                    </div>
                    <div class="tile" data-value="comet" onclick="selectEffect(this)">
                        <span class="tile-icon">💫</span>
                        <span class="tile-label">Comet</span>
                    </div>
                    <div class="tile" data-value="rain" onclick="selectEffect(this)">
                        <span class="tile-icon">🌧️</span>
                        <span class="tile-label">Rain</span>
                    </div>
                    <div class="tile" data-value="twinkle" onclick="selectEffect(this)">
                        <span class="tile-icon">⭐</span>
                        <span class="tile-label">Twinkle</span>
                    </div>
                    <div class="tile" data-value="strobe" onclick="selectEffect(this)">
                        <span class="tile-icon">⚡</span>
                        <span class="tile-label">Strobe</span>
                    </div>
                    <div class="tile" data-value="sinelon" onclick="selectEffect(this)">
                        <span class="tile-icon">🎯</span>
                        <span class="tile-label">Sinelon</span>
                    </div>
                    <div class="tile" data-value="scanner" onclick="selectEffect(this)">
                        <span class="tile-icon">👁️</span>
                        <span class="tile-label">Scanner</span>
                    </div>
                    <div class="tile" data-value="candle" onclick="selectEffect(this)">
                        <span class="tile-icon">🕯️</span>
                        <span class="tile-label">Candle</span>
                    </div>
                    <div class="tile" data-value="pride" onclick="selectEffect(this)">
                        <span class="tile-icon">🏳️‍🌈</span>
                        <span class="tile-label">Pride</span>
                    </div>
                    <div class="tile" data-value="pacifica" onclick="selectEffect(this)">
                        <span class="tile-icon">🐚</span>
                        <span class="tile-label">Pacifica</span>
                    </div>
                </div>
                <input type="hidden" id="effect" value="rainbow">
            </div>
            
            <div class="form-group">
                <label>Color Palette</label>
                <div class="tile-row" id="paletteTiles">
                    <div class="tile selected" data-value="rainbow" onclick="selectPalette(this)">
                        <span class="tile-label">Rainbow</span>
                        <div class="palette-preview" style="background: linear-gradient(90deg, red, orange, yellow, green, blue, violet);"></div>
                    </div>
                    <div class="tile" data-value="lava" onclick="selectPalette(this)">
                        <span class="tile-label">Lava</span>
                        <div class="palette-preview" style="background: linear-gradient(90deg, #000, #f00, #ff0, #fff);"></div>
                    </div>
                    <div class="tile" data-value="ocean" onclick="selectPalette(this)">
                        <span class="tile-label">Ocean</span>
                        <div class="palette-preview" style="background: linear-gradient(90deg, #000040, #0080ff, #00ffff, #80ffff);"></div>
                    </div>
                    <div class="tile" data-value="party" onclick="selectPalette(this)">
                        <span class="tile-label">Party</span>
                        <div class="palette-preview" style="background: linear-gradient(90deg, #5500ab, #84007c, #b5004b, #e5001b, #e81700, #ab7700);"></div>
                    </div>
                    <div class="tile" data-value="forest" onclick="selectPalette(this)">
                        <span class="tile-label">Forest</span>
                        <div class="palette-preview" style="background: linear-gradient(90deg, #004400, #228b22, #90ee90, #006400);"></div>
                    </div>
                    <div class="tile" data-value="cloud" onclick="selectPalette(this)">
                        <span class="tile-label">Cloud</span>
                        <div class="palette-preview" style="background: linear-gradient(90deg, #00bfff, #87ceeb, #fff, #87ceeb);"></div>
                    </div>
                    <div class="tile" data-value="heat" onclick="selectPalette(this)">
                        <span class="tile-label">Heat</span>
                        <div class="palette-preview" style="background: linear-gradient(90deg, #000, #800, #f00, #ff0, #fff);"></div>
                    </div>
                    <div class="tile" data-value="sunset" onclick="selectPalette(this)">
                        <span class="tile-label">Sunset</span>
                        <div class="palette-preview" style="background: linear-gradient(90deg, #ff8c00, #ff1493, #9400d3, #191970);"></div>
                    </div>
                    <div class="tile" data-value="autumn" onclick="selectPalette(this)">
                        <span class="tile-label">Autumn</span>
                        <div class="palette-preview" style="background: linear-gradient(90deg, #8b4513, #d2691e, #ffa500, #b22222);"></div>
                    </div>
                    <div class="tile" data-value="retro" onclick="selectPalette(this)">
                        <span class="tile-label">Retro</span>
                        <div class="palette-preview" style="background: linear-gradient(90deg, #00ffff, #8a2be2, #ff00ff, #ff1493);"></div>
                    </div>
                    <div class="tile" data-value="ice" onclick="selectPalette(this)">
                        <span class="tile-label">Ice</span>
                        <div class="palette-preview" style="background: linear-gradient(90deg, #fff, #87ceeb, #4682b4, #1e3a5f);"></div>
                    </div>
                    <div class="tile" data-value="pink" onclick="selectPalette(this)">
                        <span class="tile-label">Pink</span>
                        <div class="palette-preview" style="background: linear-gradient(90deg, #ffb6c1, #ff69b4, #ff00ff, #c71585);"></div>
                    </div>
                    <div class="tile" data-value="custom" onclick="selectPalette(this)">
                        <span class="tile-label">Custom</span>
                        <div class="palette-preview" style="background: linear-gradient(90deg, var(--primary), var(--text-muted));"></div>
                    </div>
                </div>
                <input type="hidden" id="palette" value="rainbow">
            </div>
            
            <div class="form-group">
                <div class="label-row">
                    <label for="speed">Speed</label>
                    <span class="label-value" id="speedValue">100</span>
                </div>
                <input type="range" id="speed" min="1" max="200" value="100">
            </div>
            
            <div class="form-group">
                <label>Colors</label>
                <div class="color-row">
                    <div>
                        <label style="font-size: 12px; color: var(--text-muted);">Primary</label>
                        <input type="color" id="primaryColor" value="#0000ff">
                    </div>
                    <div>
                        <label style="font-size: 12px; color: var(--text-muted);">Secondary</label>
                        <input type="color" id="secondaryColor" value="#800080">
                    </div>
                </div>
            </div>
        </div>
        
        <!-- Nightlight -->
        <div class="card">
            <div class="card-header">
                <span class="card-title">🌙 Nightlight</span>
                <label class="toggle">
                    <input type="checkbox" id="nightlightToggle">
                    <span class="toggle-slider"></span>
                </label>
            </div>
            
            <div id="nightlightControls" class="">
                <div class="form-group">
                    <div class="label-row">
                        <label for="nightlightDuration">Duration</label>
                        <span class="label-value" id="nightlightDurationValue">15 min</span>
                    </div>
                    <input type="range" id="nightlightDuration" min="1" max="60" value="15">
                </div>
                
                <div class="form-group">
                    <div class="label-row">
                        <label for="nightlightTarget">Target Brightness</label>
                        <span class="label-value" id="nightlightTargetValue">0 (off)</span>
                    </div>
                    <input type="range" id="nightlightTarget" min="0" max="255" value="0">
                </div>
                
                <div id="nightlightProgress" style="display: none;">
                    <div class="form-group">
                        <label>Progress</label>
                        <div style="background: var(--border); border-radius: 4px; height: 8px; overflow: hidden;">
                            <div id="nightlightProgressBar" style="background: var(--primary); height: 100%; width: 0%; transition: width 1s;"></div>
                        </div>
                        <span style="font-size: 12px; color: var(--text-muted);" id="nightlightProgressText">0%</span>
                    </div>
                </div>
                
                <div class="btn-group">
                    <button class="btn btn-primary" id="startNightlightBtn" onclick="startNightlight()">Start Nightlight</button>
                    <button class="btn btn-outline" id="stopNightlightBtn" onclick="stopNightlight()" style="display: none;">Stop</button>
                </div>
            </div>
        </div>
        
        <!-- AI Prompt -->
        <div class="card">
            <div class="card-header">
                <span class="card-title">✨ AI Effect Generator</span>
            </div>
            
            <div class="form-group">
                <label for="prompt">Describe your desired effect</label>
                <textarea id="prompt" placeholder="E.g., 'Warm sunset colors slowly fading between orange and purple' or 'Fast energetic party lights with confetti'"></textarea>
            </div>
            
            <div id="promptStatus" class="prompt-status" style="display: none;">
                <div class="status-dot loading"></div>
                <span id="promptStatusText">Processing...</span>
            </div>
            
            <div class="btn-group" style="margin-bottom: 16px;">
                <button class="btn btn-primary" id="generateBtn" onclick="generateEffect()">Generate Effect</button>
                <button class="btn btn-outline" id="applyGeneratedBtn" onclick="applyGeneratedEffect()" disabled>Apply Generated</button>
                <button class="btn btn-outline" id="saveSceneBtn" onclick="saveAsScene()" disabled>💾 Save as Scene</button>
            </div>
            
            <div class="form-group">
                <label>Generated Specification</label>
                <div class="prompt-output" id="generatedSpec">No effect generated yet</div>
            </div>
        </div>
        
        <!-- Saved Scenes -->
        <div class="card">
            <div class="card-header">
                <span class="card-title">🎬 Saved Scenes</span>
            </div>
            
            <div id="scenesList" style="margin-bottom: 16px;">
                <p style="color: var(--text-muted); font-size: 14px;">No saved scenes yet</p>
            </div>
            
            <div class="btn-group">
                <button class="btn btn-outline" onclick="loadScenes()">Refresh Scenes</button>
            </div>
        </div>
        
        <!-- Configuration Modal -->
        <div id="configModal" class="modal">
            <div class="modal-content">
                <div class="modal-header">
                    <span class="modal-title">⚙️ Configuration</span>
                    <button class="modal-close" onclick="closeConfigModal()">×</button>
                </div>
                <div>
                <div class="grid-2">
                    <div class="form-group">
                        <label for="wifiSSID">WiFi SSID</label>
                        <input type="text" id="wifiSSID" placeholder="Your WiFi network">
                    </div>
                    <div class="form-group">
                        <label for="wifiPassword">WiFi Password</label>
                        <input type="password" id="wifiPassword" placeholder="Password">
                    </div>
                </div>
                
                <div class="form-group">
                    <label for="apiKey">OpenRouter API Key</label>
                    <input type="password" id="apiKey" placeholder="sk-or-...">
                </div>
                
                <div class="form-group">
                    <label for="authToken">API Auth Token <span style="color: var(--text-muted); font-weight: normal;">(optional)</span></label>
                    <input type="password" id="authToken" placeholder="Leave empty to disable auth">
                    <small style="color: var(--text-secondary); font-size: 11px;\">Protects API &amp; OTA. Use as Bearer token or X-API-Key header.</small>
                </div>
                
                <div class="form-group">
                    <label for="model">AI Model</label>
                    <select id="model">
                        <option value="claude-sonnet-4-5-20250929">Claude Sonnet 4.5 (20250929)</option>
                        <option value="claude-opus-4-5-20251101">Claude Opus 4.5 (20251101)</option>
                        <option value="claude-haiku-4-5-20251001">Claude Haiku 4.5 (20251001)</option>
                    </select>
                </div>
                
                <div class="grid-2">
                    <div class="form-group">
                        <label>LED Data Pin</label>
                        <input type="text" value="GPIO 21 (compile-time)" disabled style="opacity: 0.7; cursor: not-allowed;">
                        <small style="color: var(--text-secondary); font-size: 11px;">Set LED_DATA_PIN in <a href="https://github.com/bring42/LUME/blob/main/src/constants.h" target="_blank">constants.h</a></small>
                    </div>
                    <div class="form-group">
                        <label for="ledCount">LED Count</label>
                        <input type="number" id="ledCount" value="160" min="1" max="300">
                    </div>
                </div>
                
                <!-- sACN (E1.31) Settings -->
                <div style="margin-top: 16px; padding-top: 16px; border-top: 1px solid var(--border);">
                    <div class="form-group">
                        <label style="display: flex; align-items: center; gap: 8px; cursor: pointer;">
                            <input type="checkbox" id="sacnEnabled" style="width: auto;">
                            <span>Enable sACN (E1.31) Input</span>
                        </label>
                        <p style="font-size: 12px; color: var(--text-muted); margin-top: 4px;">
                            Receive DMX data over network. Multicast: 239.255.0.X
                        </p>
                    </div>
                    
                    <div class="grid-2">
                        <div class="form-group">
                            <label for="sacnUniverse">Universe</label>
                            <input type="number" id="sacnUniverse" value="1" min="1" max="63999">
                        </div>
                        <div class="form-group">
                            <label for="sacnStartChannel">Start Channel</label>
                            <input type="number" id="sacnStartChannel" value="1" min="1" max="512">
                        </div>
                    </div>
                    
                    <div id="sacnStatus" class="status-item" style="margin-top: 8px;">
                        <div class="status-dot offline" id="sacnDot"></div>
                        <span id="sacnStatusText">Not enabled</span>
                    </div>
                </div>
                
                <!-- MQTT Settings -->
                <div style="margin-top: 16px; padding-top: 16px; border-top: 1px solid var(--border);">
                    <div class="form-group">
                        <label style="display: flex; align-items: center; gap: 8px; cursor: pointer;">
                            <input type="checkbox" id="mqttEnabled" style="width: auto;">
                            <span>Enable MQTT</span>
                        </label>
                        <p style="font-size: 12px; color: var(--text-muted); margin-top: 4px;">
                            Connect to MQTT broker for Home Assistant / automation
                        </p>
                    </div>
                    
                    <div class="grid-2">
                        <div class="form-group">
                            <label for="mqttBroker">Broker (hostname/IP)</label>
                            <input type="text" id="mqttBroker" placeholder="192.168.1.100">
                        </div>
                        <div class="form-group">
                            <label for="mqttPort">Port</label>
                            <input type="number" id="mqttPort" value="1883" min="1" max="65535">
                        </div>
                    </div>
                    
                    <div class="grid-2">
                        <div class="form-group">
                            <label for="mqttUsername">Username (optional)</label>
                            <input type="text" id="mqttUsername" placeholder="">
                        </div>
                        <div class="form-group">
                            <label for="mqttPassword">Password (optional)</label>
                            <input type="password" id="mqttPassword" placeholder="">
                        </div>
                    </div>
                    
                    <div class="form-group">
                        <label for="mqttTopicPrefix">Topic Prefix</label>
                        <input type="text" id="mqttTopicPrefix" value="lume" placeholder="lume">
                        <p style="font-size: 11px; color: var(--text-muted); margin-top: 4px;">
                            Topics: {prefix}/state, {prefix}/set, {prefix}/status
                        </p>
                    </div>
                    
                    <div id="mqttStatus" class="status-item" style="margin-top: 8px;">
                        <div class="status-dot offline" id="mqttDot"></div>
                        <span id="mqttStatusText">Not enabled</span>
                    </div>
                </div>
                
                <!-- Debug Console (collapsible) -->
                <div style="margin-top: 16px; padding-top: 16px; border-top: 1px solid var(--border);" class="collapsible collapsed">
                    <div class="collapsible-header" onclick="this.parentElement.classList.toggle('collapsed')" style="display: flex; align-items: center; gap: 8px; cursor: pointer; margin-bottom: 12px;">
                        <span style="font-weight: 500;">🐛 Debug Console</span>
                    </div>
                    
                    <div class="collapsible-content">
                        <div class="form-group">
                            <label>Request Prompt</label>
                            <pre id="debugPrompt" style="background: var(--bg); border: 1px solid var(--border); padding: 12px; border-radius: 4px; font-size: 11px; overflow-x: auto; max-height: 150px; overflow-y: auto;">No request yet</pre>
                        </div>
                        
                        <div class="form-group">
                            <label>Raw AI Response</label>
                            <pre id="debugResponse" style="background: var(--bg); border: 1px solid var(--border); padding: 12px; border-radius: 4px; font-size: 11px; overflow-x: auto; max-height: 200px; overflow-y: auto;">No response yet</pre>
                        </div>
                        
                        <div class="form-group">
                            <label>Parsed Effect JSON</label>
                            <pre id="debugParsed" style="background: var(--bg); border: 1px solid var(--border); padding: 12px; border-radius: 4px; font-size: 11px; overflow-x: auto; max-height: 150px; overflow-y: auto;">No parsed data yet</pre>
                        </div>
                        
                        <div class="form-group">
                            <label>Status / Errors</label>
                            <pre id="debugErrors" style="background: var(--bg); border: 1px solid var(--border); padding: 12px; border-radius: 4px; font-size: 11px; overflow-x: auto; color: var(--error);">No errors</pre>
                        </div>
                    </div>
                </div>
                
                <div class="btn-group" style="margin-top: 16px;">
                    <button class="btn btn-primary" onclick="saveConfig()">Save Configuration</button>
                    <button class="btn btn-outline" onclick="loadConfig()">Reload</button>
                </div>
                </div>
            </div>
        </div>
    </div>
    
    <div class="toast" id="toast"></div>
    
    <script>
        // Modal management
        function openConfigModal() {
            document.getElementById('configModal').classList.add('show');
        }
        
        function closeConfigModal() {
            document.getElementById('configModal').classList.remove('show');
        }
        
        // Close modal when clicking outside
        document.addEventListener('click', function(e) {
            const modal = document.getElementById('configModal');
            if (e.target === modal) {
                closeConfigModal();
            }
        });
        
        // Theme management
        function toggleTheme() {
            const html = document.documentElement;
            const currentTheme = html.getAttribute('data-theme') || 'dark';
            const newTheme = currentTheme === 'dark' ? 'light' : 'dark';
            
            html.setAttribute('data-theme', newTheme);
            localStorage.setItem('theme', newTheme);
            
            // Update icon
            const icon = document.getElementById('themeIcon');
            icon.textContent = newTheme === 'dark' ? '🌙' : '☀️';
        }
        
        // Load theme from localStorage on page load
        function loadTheme() {
            const savedTheme = localStorage.getItem('theme') || 'dark';
            document.documentElement.setAttribute('data-theme', savedTheme);
            const icon = document.getElementById('themeIcon');
            icon.textContent = savedTheme === 'dark' ? '🌙' : '☀️';
        }
        
        // Load theme immediately
        loadTheme();
        
        // State
        let lastSpec = null;
        let pollInterval = null;
        
        // Toast notification
        function showToast(message, type = 'info') {
            const toast = document.getElementById('toast');
            toast.textContent = message;
            toast.className = 'toast show ' + type;
            setTimeout(() => toast.classList.remove('show'), 3000);
        }
        
        // API helpers
        async function api(endpoint, method = 'GET', body = null) {
            const options = {
                method,
                headers: { 'Content-Type': 'application/json' }
            };
            if (body) options.body = JSON.stringify(body);
            
            const response = await fetch('/api' + endpoint, options);
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }
            return response.json();
        }
        
        // Load status
        async function loadStatus() {
            try {
                const status = await api('/status');
                
                document.getElementById('wifiDot').className = 'status-dot';
                document.getElementById('wifiStatus').textContent = status.wifi || 'Connected';
                document.getElementById('ipAddress').textContent = status.ip || '--';
                
                const uptime = status.uptime || 0;
                const hours = Math.floor(uptime / 3600);
                const mins = Math.floor((uptime % 3600) / 60);
                document.getElementById('uptime').textContent = `${hours}h ${mins}m`;
                
                // Update sACN status
                const sacn = status.sacn || {};
                const sacnDot = document.getElementById('sacnDot');
                const sacnText = document.getElementById('sacnStatusText');
                if (!sacn.enabled) {
                    sacnDot.className = 'status-dot offline';
                    sacnText.textContent = 'Not enabled';
                } else if (sacn.receiving) {
                    sacnDot.className = 'status-dot';
                    sacnText.textContent = `Receiving (${sacn.packets} pkts, uni ${sacn.universe})`;
                } else {
                    sacnDot.className = 'status-dot loading';
                    sacnText.textContent = `Waiting for data (uni ${sacn.universe})`;
                }
                
                // Update MQTT status
                const mqtt = status.mqtt || {};
                const mqttDot = document.getElementById('mqttDot');
                const mqttText = document.getElementById('mqttStatusText');
                if (!mqtt.enabled) {
                    mqttDot.className = 'status-dot offline';
                    mqttText.textContent = 'Not enabled';
                } else if (mqtt.connected) {
                    mqttDot.className = 'status-dot';
                    mqttText.textContent = `Connected to ${mqtt.broker}`;
                } else {
                    mqttDot.className = 'status-dot loading';
                    mqttText.textContent = 'Connecting...';
                }
            } catch (e) {
                document.getElementById('wifiDot').className = 'status-dot offline';
                document.getElementById('wifiStatus').textContent = 'Offline';
            }
        }
        
        // Load LED state
        async function loadLedState() {
            try {
                const state = await api('/led');
                
                document.getElementById('powerToggle').checked = state.power !== false;
                document.getElementById('brightness').value = state.brightness || 128;
                document.getElementById('brightnessValue').textContent = state.brightness || 128;
                document.getElementById('effect').value = state.effect || 'rainbow';
                document.getElementById('palette').value = state.palette || 'rainbow';
                document.getElementById('speed').value = state.speed || 100;
                document.getElementById('speedValue').textContent = state.speed || 100;
                
                // Update tile selections
                selectTileByValue('effectTiles', state.effect || 'rainbow');
                selectTileByValue('paletteTiles', state.palette || 'rainbow');
                
                if (state.primaryColor) {
                    const [r, g, b] = state.primaryColor;
                    document.getElementById('primaryColor').value = rgbToHex(r, g, b);
                }
                if (state.secondaryColor) {
                    const [r, g, b] = state.secondaryColor;
                    document.getElementById('secondaryColor').value = rgbToHex(r, g, b);
                }
            } catch (e) {
                console.error('Failed to load LED state:', e);
            }
        }
        
        // Debounce utility for sliders
        function debounce(func, wait) {
            let timeout;
            return function(...args) {
                clearTimeout(timeout);
                timeout = setTimeout(() => func.apply(this, args), wait);
            };
        }
        
        // Debounced apply for sliders (300ms delay)
        const debouncedApply = debounce(() => applyLedState(), 300);
        
        // Tile selector functions - auto-apply on selection
        function selectEffect(tile) {
            const container = document.getElementById('effectTiles');
            container.querySelectorAll('.tile').forEach(t => t.classList.remove('selected'));
            tile.classList.add('selected');
            document.getElementById('effect').value = tile.dataset.value;
            applyLedState();
        }
        
        function selectPalette(tile) {
            const container = document.getElementById('paletteTiles');
            container.querySelectorAll('.tile').forEach(t => t.classList.remove('selected'));
            tile.classList.add('selected');
            document.getElementById('palette').value = tile.dataset.value;
            applyLedState();
        }
        
        function selectTileByValue(containerId, value) {
            const container = document.getElementById(containerId);
            if (!container) return;
            container.querySelectorAll('.tile').forEach(t => {
                if (t.dataset.value === value) {
                    t.classList.add('selected');
                } else {
                    t.classList.remove('selected');
                }
            });
        }
        
        // Apply LED state
        async function applyLedState() {
            const state = {
                power: document.getElementById('powerToggle').checked,
                brightness: parseInt(document.getElementById('brightness').value),
                effect: document.getElementById('effect').value,
                palette: document.getElementById('palette').value,
                speed: parseInt(document.getElementById('speed').value),
                primaryColor: hexToRgb(document.getElementById('primaryColor').value),
                secondaryColor: hexToRgb(document.getElementById('secondaryColor').value)
            };
            
            try {
                await api('/led', 'POST', state);
                showToast('Settings applied!', 'success');
            } catch (e) {
                showToast('Failed to apply settings', 'error');
            }
        }
        
        // Nightlight functions
        let nightlightPollInterval = null;
        
        async function loadNightlightStatus() {
            try {
                const status = await api('/nightlight');
                updateNightlightUI(status);
            } catch (e) {
                console.error('Failed to load nightlight status:', e);
            }
        }
        
        // Flag to prevent toggle change handler from firing during programmatic updates
        let updatingNightlightUI = false;
        
        function updateNightlightUI(status) {
            const isActive = status.active;
            const controls = document.getElementById('nightlightControls');
            const toggle = document.getElementById('nightlightToggle');
            
            // Prevent change handler from running when we set checked programmatically
            updatingNightlightUI = true;
            toggle.checked = isActive;
            updatingNightlightUI = false;
            
            document.getElementById('startNightlightBtn').style.display = isActive ? 'none' : '';
            document.getElementById('stopNightlightBtn').style.display = isActive ? '' : 'none';
            document.getElementById('nightlightProgress').style.display = isActive ? '' : 'none';
            
            // Expand/collapse controls based on active state
            if (isActive) {
                controls.classList.add('expanded');
            } else {
                controls.classList.remove('expanded');
            }
            
            if (isActive) {
                const progress = Math.round((status.progress || 0) * 100);
                document.getElementById('nightlightProgressBar').style.width = progress + '%';
                document.getElementById('nightlightProgressText').textContent = progress + '% complete';
                
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
            const controls = document.getElementById('nightlightControls');
            if (show) {
                controls.classList.add('expanded');
            } else {
                controls.classList.remove('expanded');
            }
        }
        
        async function startNightlight() {
            const durationMinutes = parseInt(document.getElementById('nightlightDuration').value);
            const targetBrightness = parseInt(document.getElementById('nightlightTarget').value);
            
            try {
                const result = await api('/nightlight', 'POST', {
                    duration: durationMinutes * 60,  // Convert to seconds
                    targetBrightness: targetBrightness
                });
                showToast('Nightlight started!', 'success');
                loadNightlightStatus();
            } catch (e) {
                showToast('Failed to start nightlight', 'error');
            }
        }
        
        async function stopNightlight() {
            try {
                await api('/nightlight/stop', 'POST', {});
                showToast('Nightlight stopped', 'success');
                loadNightlightStatus();
            } catch (e) {
                showToast('Failed to stop nightlight', 'error');
            }
        }
        
        // Load config
        async function loadConfig() {
            try {
                const config = await api('/config');
                
                document.getElementById('wifiSSID').value = config.wifiSSID || '';
                document.getElementById('apiKey').value = config.apiKeySet ? '****' : '';
                document.getElementById('model').value = config.openRouterModel || 'openai/gpt-4o-mini';
                document.getElementById('authToken').value = config.authEnabled ? '****' : '';
                document.getElementById('ledCount').value = config.ledCount || 160;
                
                // sACN settings
                document.getElementById('sacnEnabled').checked = config.sacnEnabled || false;
                document.getElementById('sacnUniverse').value = config.sacnUniverse || 1;
                document.getElementById('sacnStartChannel').value = config.sacnStartChannel || 1;
                
                // MQTT settings
                document.getElementById('mqttEnabled').checked = config.mqttEnabled || false;
                document.getElementById('mqttBroker').value = config.mqttBroker || '';
                document.getElementById('mqttPort').value = config.mqttPort || 1883;
                document.getElementById('mqttUsername').value = config.mqttUsername && config.mqttUsername !== '****' ? config.mqttUsername : '';
                document.getElementById('mqttPassword').value = config.mqttPassword && config.mqttPassword !== '****' ? '' : '';
                document.getElementById('mqttTopicPrefix').value = config.mqttTopicPrefix || 'lume';
            } catch (e) {
                console.error('Failed to load config:', e);
            }
        }
        
        // Save config
        async function saveConfig() {
            const config = {
                wifiSSID: document.getElementById('wifiSSID').value,
                wifiPassword: document.getElementById('wifiPassword').value,
                apiKey: document.getElementById('apiKey').value,
                authToken: document.getElementById('authToken').value,
                openRouterModel: document.getElementById('model').value,
                ledCount: parseInt(document.getElementById('ledCount').value),
                sacnEnabled: document.getElementById('sacnEnabled').checked,
                sacnUniverse: parseInt(document.getElementById('sacnUniverse').value),
                sacnStartChannel: parseInt(document.getElementById('sacnStartChannel').value),
                mqttEnabled: document.getElementById('mqttEnabled').checked,
                mqttBroker: document.getElementById('mqttBroker').value,
                mqttPort: parseInt(document.getElementById('mqttPort').value),
                mqttUsername: document.getElementById('mqttUsername').value,
                mqttPassword: document.getElementById('mqttPassword').value,
                mqttTopicPrefix: document.getElementById('mqttTopicPrefix').value
            };
            
            // Don't send masked password/key
            if (config.wifiPassword === '') delete config.wifiPassword;
            if (config.apiKey.startsWith('****')) delete config.apiKey;
            if (config.authToken.startsWith('****')) delete config.authToken;
            if (config.mqttPassword === '') delete config.mqttPassword;
            
            try {
                await api('/config', 'POST', config);
                showToast('Configuration saved!', 'success');
                loadConfig();
            } catch (e) {
                showToast('Failed to save configuration', 'error');
            }
        }
        
        // Generate effect
        async function generateEffect() {
            const prompt = document.getElementById('prompt').value.trim();
            if (!prompt) {
                showToast('Please enter a prompt', 'error');
                return;
            }
            
            const btn = document.getElementById('generateBtn');
            btn.disabled = true;
            btn.textContent = 'Generating...';
            
            const statusDiv = document.getElementById('promptStatus');
            statusDiv.style.display = 'flex';
            statusDiv.className = 'prompt-status running';
            document.getElementById('promptStatusText').textContent = 'Processing...';
            
            try {
                await api('/prompt', 'POST', { prompt });
                
                // Poll for result
                pollInterval = setInterval(pollPromptStatus, 1000);
            } catch (e) {
                btn.disabled = false;
                btn.textContent = 'Generate Effect';
                statusDiv.className = 'prompt-status error';
                document.getElementById('promptStatusText').textContent = 'Failed to start: ' + e.message;
            }
        }
        
        // Poll prompt status
        async function pollPromptStatus() {
            try {
                const result = await api('/prompt/status');
                const statusDiv = document.getElementById('promptStatus');
                
                // Update debug console
                if (result.prompt) {
                    document.getElementById('debugPrompt').textContent = result.prompt;
                }
                if (result.rawResponse) {
                    document.getElementById('debugResponse').textContent = result.rawResponse;
                }
                if (result.lastSpec) {
                    try {
                        document.getElementById('debugParsed').textContent = JSON.stringify(JSON.parse(result.lastSpec), null, 2);
                    } catch {
                        document.getElementById('debugParsed').textContent = result.lastSpec;
                    }
                }
                
                if (result.state === 'done') {
                    clearInterval(pollInterval);
                    statusDiv.className = 'prompt-status done';
                    document.getElementById('promptStatusText').textContent = 'Complete!';
                    document.getElementById('generateBtn').disabled = false;
                    document.getElementById('generateBtn').textContent = 'Generate Effect';
                    
                    lastSpec = result.lastSpec;
                    document.getElementById('generatedSpec').textContent = JSON.stringify(JSON.parse(result.lastSpec), null, 2);
                    document.getElementById('applyGeneratedBtn').disabled = false;
                    document.getElementById('saveSceneBtn').disabled = false;
                    document.getElementById('debugErrors').textContent = 'Success - no errors';
                    document.getElementById('debugErrors').style.color = 'var(--success)';
                    showToast('Effect generated!', 'success');
                } else if (result.state === 'error') {
                    clearInterval(pollInterval);
                    statusDiv.className = 'prompt-status error';
                    document.getElementById('promptStatusText').textContent = result.message || 'Error';
                    document.getElementById('generateBtn').disabled = false;
                    document.getElementById('generateBtn').textContent = 'Generate Effect';
                    document.getElementById('debugErrors').textContent = result.message || 'Unknown error';
                    document.getElementById('debugErrors').style.color = 'var(--error)';
                    showToast('Generation failed: ' + result.message, 'error');
                } else if (result.state === 'running' || result.state === 'queued') {
                    document.getElementById('promptStatusText').textContent = result.message || 'Processing...';
                    document.getElementById('debugErrors').textContent = 'Processing...';
                    document.getElementById('debugErrors').style.color = 'var(--warning)';
                }
            } catch (e) {
                clearInterval(pollInterval);
                document.getElementById('generateBtn').disabled = false;
                document.getElementById('generateBtn').textContent = 'Generate Effect';
                document.getElementById('debugErrors').textContent = 'API Error: ' + e.message;
                document.getElementById('debugErrors').style.color = 'var(--error)';
            }
        }
        
        // Apply generated effect
        async function applyGeneratedEffect() {
            if (!lastSpec) {
                showToast('No effect to apply', 'error');
                return;
            }
            
            try {
                const response = await fetch('/api/prompt/apply', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ spec: lastSpec })
                });
                
                const result = await response.json();
                
                if (!response.ok) {
                    showToast('Failed: ' + (result.error || response.status), 'error');
                    return;
                }
                
                showToast('Effect applied!', 'success');
                loadLedState();
            } catch (e) {
                console.error('Apply error:', e);
                showToast('Failed to apply effect: ' + e.message, 'error');
            }
        }
        
        // Scene management
        async function loadScenes() {
            try {
                const scenes = await api('/scenes');
                const container = document.getElementById('scenesList');
                
                if (!scenes || scenes.length === 0) {
                    container.innerHTML = '<p style="color: var(--text-muted); font-size: 14px;">No saved scenes yet</p>';
                    return;
                }
                
                container.innerHTML = scenes.map(scene => `
                    <div class="scene-item">
                        <span class="scene-name">${escapeHtml(scene.name)}</span>
                        <div class="scene-actions">
                            <button class="btn btn-primary" onclick="applyScene(${scene.id})">Apply</button>
                            <button class="btn btn-outline" onclick="deleteScene(${scene.id})">🗑️</button>
                        </div>
                    </div>
                `).join('');
            } catch (e) {
                console.error('Failed to load scenes:', e);
            }
        }
        
        function escapeHtml(text) {
            const div = document.createElement('div');
            div.textContent = text;
            return div.innerHTML;
        }
        
        async function saveAsScene() {
            if (!lastSpec) {
                showToast('No effect to save', 'error');
                return;
            }
            
            const name = prompt('Enter a name for this scene:');
            if (!name || name.trim().length === 0) {
                return;
            }
            
            try {
                await api('/scenes', 'POST', { name: name.trim(), spec: lastSpec });
                showToast('Scene saved!', 'success');
                loadScenes();
            } catch (e) {
                showToast('Failed to save scene: ' + e.message, 'error');
            }
        }
        
        async function applyScene(id) {
            try {
                const response = await fetch('/api/scenes/' + id + '/apply', {
                    method: 'POST'
                });
                
                if (!response.ok) {
                    const result = await response.json();
                    showToast('Failed: ' + (result.error || response.status), 'error');
                    return;
                }
                
                showToast('Scene applied!', 'success');
                loadLedState();
            } catch (e) {
                showToast('Failed to apply scene: ' + e.message, 'error');
            }
        }
        
        async function deleteScene(id) {
            if (!confirm('Delete this scene?')) {
                return;
            }
            
            try {
                await fetch('/api/scenes/' + id, { method: 'DELETE' });
                showToast('Scene deleted', 'success');
                loadScenes();
            } catch (e) {
                showToast('Failed to delete scene', 'error');
            }
        }
        
        // Color helpers
        function hexToRgb(hex) {
            const result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
            return result ? [
                parseInt(result[1], 16),
                parseInt(result[2], 16),
                parseInt(result[3], 16)
            ] : [0, 0, 255];
        }
        
        function rgbToHex(r, g, b) {
            return '#' + [r, g, b].map(x => x.toString(16).padStart(2, '0')).join('');
        }
        
        // Event listeners - sliders use debounced auto-apply
        document.getElementById('brightness').addEventListener('input', function() {
            document.getElementById('brightnessValue').textContent = this.value;
            debouncedApply();
        });
        
        document.getElementById('speed').addEventListener('input', function() {
            document.getElementById('speedValue').textContent = this.value;
            debouncedApply();
        });
        
        // Color pickers auto-apply on change (when picker closes)
        document.getElementById('primaryColor').addEventListener('change', function() {
            applyLedState();
        });
        
        document.getElementById('secondaryColor').addEventListener('change', function() {
            applyLedState();
        });
        
        document.getElementById('powerToggle').addEventListener('change', function() {
            applyLedState();
        });
        
        // Nightlight slider listeners
        document.getElementById('nightlightDuration').addEventListener('input', function() {
            document.getElementById('nightlightDurationValue').textContent = this.value + ' min';
        });
        
        document.getElementById('nightlightTarget').addEventListener('input', function() {
            const val = parseInt(this.value);
            document.getElementById('nightlightTargetValue').textContent = val === 0 ? '0 (off)' : val;
        });
        
        document.getElementById('nightlightToggle').addEventListener('change', function() {
            // Ignore programmatic changes
            if (updatingNightlightUI) return;
            
            if (this.checked) {
                // Expand to show controls so user can configure and start
                toggleNightlightControls(true);
            } else {
                // User is stopping - call stop and collapse
                stopNightlight();
                toggleNightlightControls(false);
            }
        });
        
        // Initialize
        loadStatus();
        loadLedState();
        loadConfig();
        loadScenes();
        loadNightlightStatus();
        setInterval(loadStatus, 10000);
    </script>
</body>
</html>
)rawliteral";

#endif // WEB_UI_H
