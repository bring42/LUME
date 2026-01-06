        // Modal management
        function openConfigModal() {
            document.getElementById('configModal').classList.add('show');
            loadSegmentsConfig(); // Load segments when modal opens
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

        // Load palette preset (v2 cannot read preset back reliably)
        (function loadPalettePreset(){
            const saved = localStorage.getItem('palettePreset');
            if (saved) {
                const pal = document.getElementById('palette');
                if (pal) pal.value = saved;
                selectTileByValue('paletteTiles', saved);
            }
        })();
        
        // State
        let sliderBindings = {};
        let effectMetadata = {}; // Map of effect ID -> metadata (usesPalette, usesSpeed, etc.)
        let activeSegmentId = 0; // Currently selected segment
        
        // Segment name helpers (stored in localStorage)
        function getSegmentName(segmentId) {
            const names = JSON.parse(localStorage.getItem('segmentNames') || '{}');
            return names[segmentId] || null;
        }
        
        function setSegmentName(segmentId, name) {
            const names = JSON.parse(localStorage.getItem('segmentNames') || '{}');
            if (name && name.trim()) {
                names[segmentId] = name.trim();
            } else {
                delete names[segmentId];
            }
            localStorage.setItem('segmentNames', JSON.stringify(names));
        }
        
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
                // Try to extract error message from response body
                try {
                    const errorData = await response.json();
                    const errorMsg = errorData.error || `HTTP ${response.status}`;
                    throw new Error(errorMsg);
                } catch (e) {
                    // If parsing fails, throw generic error
                    if (e.message && !e.message.startsWith('HTTP')) {
                        throw e; // Re-throw if we successfully parsed the error
                    }
                    throw new Error(`HTTP ${response.status}`);
                }
            }
            return response.json();
        }


        // v2 API helpers (segments/controller)
        const PALETTE_PRESETS = {
  "rainbow": 0,
  "lava": 1,
  "ocean": 2,
  "party": 3,
  "forest": 4,
  "cloud": 5,
  "heat": 6
};

        async function apiV2(path, method = 'GET', body = null) {
            const options = {
                method,
                headers: { 'Content-Type': 'application/json' }
            };
            if (body) options.body = JSON.stringify(body);

            const response = await fetch('/api/v2' + path, options);
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }
            return response.json();
        }

        // WebSocket (optional): server should expose /ws and send {type:'state', controller:{...}, segments:[...]}
        let ws = null;
        function connectWebSocket() {
            try {
                const proto = location.protocol === 'https:' ? 'wss' : 'ws';
                ws = new WebSocket(`${proto}://${location.host}/ws`);
                ws.onopen = () => console.log('WS connected');
                ws.onclose = () => {
                    console.log('WS disconnected, retrying...');
                    setTimeout(connectWebSocket, 2000);
                };
                ws.onmessage = (evt) => {
                    try {
                        const msg = JSON.parse(evt.data);
                        if (msg.type === 'state') {
                            if (msg.controller) applyControllerToUI(msg.controller);
                            if (msg.segments) {
                                // Update the currently active segment, not always segment 0
                                const activeSeg = msg.segments.find(s => s.id === activeSegmentId);
                                if (activeSeg) applySegmentToUI(activeSeg);
                            }
                        }
                    } catch (e) {
                        console.warn('WS message parse error', e);
                    }
                };
            } catch (e) {
                console.warn('WS init failed', e);
            }
        }

        function selectActiveSegment(segments) {
            // Current UI is single-segment oriented; default to segment 0 if present, else first segment.
            if (!Array.isArray(segments) || segments.length === 0) return null;
            const s0 = segments.find(s => s.id === 0);
            return s0 || segments[0];
        }

        function applyControllerToUI(controller) {
            if (!controller) return;
            document.getElementById('powerToggle').checked = controller.power !== false;
            // Brightness input is user-controlled only, never updated from server
        }

        function applySegmentToUI(seg) {
            if (!seg) return;
            
            // Update effect selection
            document.getElementById('effect').value = seg.effect || 'rainbow';
            selectTileByValue('effectTiles', seg.effect || 'rainbow');
            
            // DON'T call updateEffectControls() here - it rebuilds controls and interrupts user input
            // Controls are already rendered when effect changes via loadSegmentState()
            
            // Populate schema control values from seg.params
            if (seg.params) {
                Object.keys(seg.params).forEach(paramId => {
                    const input = document.querySelector(`#param_${paramId}`);
                    if (input) {
                        const value = seg.params[paramId];
                        if (input.type === 'checkbox') {
                            input.checked = value;
                        } else if (input.type === 'color') {
                            // Value might be hex string or RGB array
                            if (typeof value === 'string') {
                                input.value = value;
                            } else if (Array.isArray(value)) {
                                const [r, g, b] = value;
                                input.value = rgbToHex(r, g, b);
                            }
                        } else if (input.type === 'hidden' && paramId === 'palette') {
                            // Palette tiles use hidden input - update tiles selection
                            input.value = value;
                            const tilesContainer = document.getElementById('paletteTiles_' + paramId);
                            if (tilesContainer) {
                                tilesContainer.querySelectorAll('.tile').forEach(t => {
                                    t.classList.toggle('selected', t.dataset.value === value);
                                });
                            }
                        } else {
                            input.value = value;
                            // Update value display for range inputs
                            const valueDisplay = document.getElementById(input.id + '_value');
                            if (valueDisplay) {
                                valueDisplay.textContent = value;
                            }
                        }
                    }
                });
            }
        }

        // Load segments into dropdown selector
        async function loadSegments() {
            try {
                const data = await apiV2('/segments');
                const selector = document.getElementById('segmentSelector');
                
                if (data.segments && data.segments.length > 0) {
                    selector.innerHTML = data.segments.map(seg => {
                        const customName = getSegmentName(seg.id);
                        const label = customName 
                            ? `${customName} (LEDs ${seg.start}-${seg.stop})`
                            : `Segment ${seg.id} (LEDs ${seg.start}-${seg.stop})`;
                        return `<option value="${seg.id}">${label}</option>`;
                    }).join('');
                    
                    // Load the first segment's state
                    activeSegmentId = data.segments[0].id;
                    selector.value = activeSegmentId;
                    await loadSegmentState(activeSegmentId);
                } else {
                    selector.innerHTML = '<option value="-1">No segments - create one in settings</option>';
                }
            } catch (e) {
                console.error('Failed to load segments:', e);
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
                
                // Set effect selection
                document.getElementById('effect').value = seg.effect || 'rainbow';
                selectTileByValue('effectTiles', seg.effect || 'rainbow');
                
                // Update control visibility and render schema controls for this effect
                updateEffectControls(seg.effect);
                
                // Populate schema control values from seg.params
                if (seg.params) {
                    Object.keys(seg.params).forEach(paramId => {
                        const input = document.querySelector(`input[data-param-id="${paramId}"]`);
                        if (input) {
                            const value = seg.params[paramId];
                            if (input.type === 'checkbox') {
                                input.checked = value;
                            } else if (input.type === 'color') {
                                // Value might be hex string or RGB array
                                if (typeof value === 'string') {
                                    input.value = value;
                                } else if (Array.isArray(value)) {
                                    const [r, g, b] = value;
                                    input.value = rgbToHex(r, g, b);
                                }
                            } else if (input.type === 'hidden' && paramId === 'palette') {
                                // Palette tiles use hidden input - update tiles selection
                                input.value = value;
                                const tilesContainer = document.getElementById('paletteTiles_' + paramId);
                                if (tilesContainer) {
                                    tilesContainer.querySelectorAll('.tile').forEach(t => {
                                        t.classList.toggle('selected', t.dataset.value === value);
                                    });
                                }
                            } else {
                                input.value = value;
                                // Update value display for range inputs
                                const valueDisplay = document.getElementById(input.id + '_value');
                                if (valueDisplay) {
                                    valueDisplay.textContent = value;
                                }
                            }
                        }
                    });
                }
            } catch (e) {
                console.error(`Failed to load segment ${segmentId} state:`, e);
            }
        }
        
        // v2: Load LED state (controller + segments)
        async function loadLedState() {
            try {
                const state = await apiV2('/segments');
                // Update controller state
                document.getElementById('powerToggle').checked = state.power !== false;
                document.getElementById('brightness').value = state.brightness ?? 128;
                document.getElementById('brightnessValue').textContent = state.brightness ?? 128;
                
                // Load segments into dropdown
                await loadSegments();
            } catch (e) {
                console.error('Failed to load LED state (v2):', e);
            }
        }
        
        // Load effect metadata from API
        async function loadEffectMetadata() {
            try {
                const data = await apiV2('/effects');
                if (data.effects) {
                    data.effects.forEach(effect => {
                        effectMetadata[effect.id] = {
                            usesPalette: effect.usesPalette,
                            usesPrimaryColor: effect.usesPrimaryColor,
                            usesSecondaryColor: effect.usesSecondaryColor,
                            usesSpeed: effect.usesSpeed,
                            usesIntensity: effect.usesIntensity,
                            params: effect.params || [],  // Schema parameters
                            hasSchema: effect.params && effect.params.length > 0
                        };
                    });
                }
            } catch (e) {
                console.error('Failed to load effect metadata:', e);
            }
        }
        
        // Update control visibility based on selected effect
        function updateEffectControls(effectId) {
            const metadata = effectMetadata[effectId];
            if (!metadata) return; // Metadata not loaded yet or effect not found
            
            // Check if effect has schema-based params
            if (metadata.hasSchema && metadata.params.length > 0) {
                // Render dynamic controls from schema
                renderSchemaControls(metadata.params);
                
                // Hide legacy controls
                hideControl('.palette-section');
                hideControl('.speed-control');
                hideControl('.intensity-control');
                hideControl('.color-controls');
            } else {
                // Legacy control visibility (backward compatibility)
                // Clear schema controls
                const schemaContainer = document.getElementById('schemaControls');
                if (schemaContainer) schemaContainer.innerHTML = '';
                
                // Palette controls
                showHideControl('.palette-section', metadata.usesPalette);
                
                // Speed controls
                showHideControl('.speed-control', metadata.usesSpeed);
                
                // Intensity controls
                showHideControl('.intensity-control', metadata.usesIntensity);
                
                // Primary color
                showHideControl('.primary-color', metadata.usesPrimaryColor);
                
                // Secondary color
                showHideControl('.secondary-color', metadata.usesSecondaryColor);
                
                // Hide entire color section if neither color is used
                // EXCEPT when custom palette is selected (needs color input)
                const colorControls = document.querySelector('.color-controls');
                if (colorControls) {
                    const selectedPalette = document.getElementById('palette')?.value;
                    const isCustomPalette = selectedPalette === 'custom';
                    const usesAnyColor = metadata.usesPrimaryColor || metadata.usesSecondaryColor || (metadata.usesPalette && isCustomPalette);
                    colorControls.style.display = usesAnyColor ? '' : 'none';
                    
                    // Show both colors for custom palette
                    const primaryColorControl = document.querySelector('.primary-color');
                    const secondaryColorControl = document.querySelector('.secondary-color');
                    if (isCustomPalette && metadata.usesPalette) {
                        if (primaryColorControl) primaryColorControl.style.display = '';
                        if (secondaryColorControl) secondaryColorControl.style.display = '';
                    }
                }
            }
        }
        
        function hideControl(selector) {
            const el = document.querySelector(selector);
            if (el) el.style.display = 'none';
        }
        
        function showHideControl(selector, show) {
            const el = document.querySelector(selector);
            if (el) el.style.display = show ? '' : 'none';
        }
        
        function renderSchemaControls(params) {
            let schemaContainer = document.getElementById('schemaControls');
            if (!schemaContainer) {
                // Create container if it doesn't exist
                // Insert after the color-controls section
                const colorControls = document.querySelector('.color-controls');
                if (!colorControls) {
                    console.error('Cannot find color-controls element to insert schema controls');
                    return;
                }
                
                schemaContainer = document.createElement('div');
                schemaContainer.id = 'schemaControls';
                schemaContainer.className = 'form-group';
                schemaContainer.style.marginTop = '16px';
                
                // Insert after color-controls
                colorControls.parentNode.insertBefore(schemaContainer, colorControls.nextSibling);
            }
            
            schemaContainer.innerHTML = ''; // Clear existing
            
            params.forEach(param => {
                const control = createSchemaControl(param);
                if (control) {
                    schemaContainer.appendChild(control);
                }
            });
        }
        
        function createSchemaControl(param) {
            let input;
            
            switch (param.type) {
                case 'int':
                    const formGroup = document.createElement('div');
                    formGroup.className = 'form-group';
                    formGroup.dataset.paramId = param.id;
                    
                    const labelRow = document.createElement('div');
                    labelRow.className = 'label-row';
                    
                    const label = document.createElement('label');
                    label.textContent = param.name;
                    label.setAttribute('for', 'param_' + param.id);
                    
                    const valueDisplay = document.createElement('span');
                    valueDisplay.className = 'label-value';
                    valueDisplay.id = 'param_' + param.id + '_value';
                    valueDisplay.textContent = param.default || 128;
                    
                    labelRow.appendChild(label);
                    labelRow.appendChild(valueDisplay);
                    
                    input = document.createElement('input');
                    input.type = 'range';
                    input.id = 'param_' + param.id;
                    input.min = param.min || 0;
                    input.max = param.max || 255;
                    input.value = param.default || 128;
                    input.dataset.paramId = param.id;
                    
                    input.addEventListener('input', () => {
                        valueDisplay.textContent = input.value;
                    });
                    
                    // Apply on release
                    ['pointerup', 'mouseup', 'touchend'].forEach(evt => {
                        input.addEventListener(evt, () => {
                            applySegmentState();
                        });
                    });
                    
                    formGroup.appendChild(labelRow);
                    formGroup.appendChild(input);
                    return formGroup;
                    
                case 'float':
                    const floatGroup = document.createElement('div');
                    floatGroup.className = 'form-group';
                    floatGroup.dataset.paramId = param.id;
                    
                    const floatLabel = document.createElement('label');
                    floatLabel.textContent = param.name;
                    floatLabel.setAttribute('for', 'param_' + param.id);
                    
                    input = document.createElement('input');
                    input.type = 'number';
                    input.id = 'param_' + param.id;
                    input.step = '0.01';
                    input.min = param.min || 0;
                    input.max = param.max || 1;
                    input.value = param.default || 0.5;
                    input.dataset.paramId = param.id;
                    input.style.width = '100%';
                    input.style.padding = '8px';
                    input.style.borderRadius = '8px';
                    input.style.border = '1px solid var(--border)';
                    input.style.background = 'var(--surface)';
                    input.style.color = 'var(--text)';
                    
                    input.addEventListener('change', () => {
                        applySegmentState();
                    });
                    
                    floatGroup.appendChild(floatLabel);
                    floatGroup.appendChild(input);
                    return floatGroup;
                    
                case 'color':
                    const colorGroup = document.createElement('div');
                    colorGroup.className = 'form-group';
                    colorGroup.dataset.paramId = param.id;
                    
                    const colorLabel = document.createElement('label');
                    colorLabel.textContent = param.name;
                    
                    const colorPicker = document.createElement('div');
                    colorPicker.className = 'color-picker';
                    
                    input = document.createElement('input');
                    input.type = 'color';
                    input.id = 'param_' + param.id;
                    input.value = param.default || '#ff0000';
                    input.dataset.paramId = param.id;
                    
                    input.addEventListener('change', () => {
                        applySegmentState();
                    });
                    
                    colorPicker.appendChild(input);
                    colorGroup.appendChild(colorLabel);
                    colorGroup.appendChild(colorPicker);
                    return colorGroup;
                    
                case 'bool':
                    const boolGroup = document.createElement('div');
                    boolGroup.className = 'form-group';
                    boolGroup.dataset.paramId = param.id;
                    boolGroup.style.display = 'flex';
                    boolGroup.style.alignItems = 'center';
                    boolGroup.style.justifyContent = 'space-between';
                    
                    const boolLabel = document.createElement('label');
                    boolLabel.textContent = param.name;
                    
                    const toggleLabel = document.createElement('label');
                    toggleLabel.className = 'toggle';
                    
                    input = document.createElement('input');
                    input.type = 'checkbox';
                    input.id = 'param_' + param.id;
                    input.checked = param.default || false;
                    input.dataset.paramId = param.id;
                    
                    input.addEventListener('change', () => {
                        applySegmentState();
                    });
                    
                    const slider = document.createElement('span');
                    slider.className = 'toggle-slider';
                    
                    toggleLabel.appendChild(input);
                    toggleLabel.appendChild(slider);
                    
                    boolGroup.appendChild(boolLabel);
                    boolGroup.appendChild(toggleLabel);
                    return boolGroup;
                    
                case 'enum':
                    const enumGroup = document.createElement('div');
                    enumGroup.className = 'form-group';
                    enumGroup.dataset.paramId = param.id;
                    
                    const enumLabel = document.createElement('label');
                    enumLabel.textContent = param.name;
                    
                    input = document.createElement('select');
                    input.id = 'param_' + param.id;
                    input.style.width = '100%';
                    input.style.padding = '8px';
                    input.style.borderRadius = '8px';
                    input.style.border = '1px solid var(--border)';
                    input.style.background = 'var(--surface)';
                    input.style.color = 'var(--text)';
                    input.style.fontSize = '14px';
                    
                    const options = param.options.split('|');
                    options.forEach((opt, idx) => {
                        const option = document.createElement('option');
                        option.value = idx;
                        option.textContent = opt;
                        input.appendChild(option);
                    });
                    input.value = param.default || 0;
                    input.dataset.paramId = param.id;
                    
                    input.addEventListener('change', () => {
                        applySegmentState();
                    });
                    
                    enumGroup.appendChild(enumLabel);
                    enumGroup.appendChild(input);
                    return enumGroup;
                    
                case 'palette':
                    const palGroup = document.createElement('div');
                    palGroup.className = 'form-group';
                    palGroup.dataset.paramId = param.id;
                    
                    const palLabel = document.createElement('label');
                    palLabel.textContent = param.name;
                    
                    // Create tiles container with horizontal scroll (like effects)
                    const palTilesContainer = document.createElement('div');
                    palTilesContainer.className = 'tile-row';
                    palTilesContainer.id = 'paletteTiles_' + param.id;
                    
                    // Hidden input to store value
                    input = document.createElement('input');
                    input.type = 'hidden';
                    input.id = 'param_' + param.id;
                    input.dataset.paramId = param.id;
                    input.value = param.default || 'rainbow';
                    
                    // Define palettes with gradient previews
                    const palettes = [
                        { name: 'rainbow', label: 'Rainbow', gradient: 'linear-gradient(90deg, #f00, #ff0, #0f0, #0ff, #00f, #f0f, #f00)' },
                        { name: 'forest', label: 'Forest', gradient: 'linear-gradient(90deg, #0b6623, #228b22, #90ee90, #006400)' },
                        { name: 'ocean', label: 'Ocean', gradient: 'linear-gradient(90deg, #000080, #0077be, #00bfff, #7fffd4)' },
                        { name: 'heat', label: 'Heat', gradient: 'linear-gradient(90deg, #000, #8b0000, #ff0000, #ffa500, #ffff00)' },
                        { name: 'lava', label: 'Lava', gradient: 'linear-gradient(90deg, #000, #8b0000, #ff4500, #ffa500)' },
                        { name: 'cloud', label: 'Cloud', gradient: 'linear-gradient(90deg, #000080, #4169e1, #87ceeb, #f0f8ff)' },
                        { name: 'party', label: 'Party', gradient: 'linear-gradient(90deg, #ff00ff, #ff0080, #ff0000, #ff8000, #ffff00)' }
                    ];
                    
                    palettes.forEach(pal => {
                        const tile = document.createElement('div');
                        tile.className = 'tile';
                        tile.dataset.value = pal.name;
                        
                        const tileLabel = document.createElement('span');
                        tileLabel.className = 'tile-label';
                        tileLabel.textContent = pal.label;
                        
                        const preview = document.createElement('div');
                        preview.className = 'palette-preview';
                        preview.style.background = pal.gradient;
                        
                        tile.appendChild(tileLabel);
                        tile.appendChild(preview);
                        
                        tile.onclick = () => {
                            // Update hidden input
                            input.value = pal.name;
                            // Update selected tile
                            palTilesContainer.querySelectorAll('.tile').forEach(t => t.classList.remove('selected'));
                            tile.classList.add('selected');
                            // Apply changes
                            applySegmentState();
                        };
                        
                        // Select default
                        if (pal.name === input.value) {
                            tile.classList.add('selected');
                        }
                        
                        palTilesContainer.appendChild(tile);
                    });
                    
                    palGroup.appendChild(palLabel);
                    palGroup.appendChild(palTilesContainer);
                    palGroup.appendChild(input);
                    return palGroup;
                    
                default:
                    return null;
            }
        }

        // v2: Apply LED state - updates controller + segment 0
        async function applyLedState() {
            await applyControllerState();
            await applySegmentState();
        }
        
        // v2: Apply only controller state (power + brightness)
        async function applyControllerState() {
            const controller = {
                power: document.getElementById('powerToggle').checked,
                brightness: parseInt(document.getElementById('brightness').value)
            };

            try {
                await apiV2('/controller', 'PUT', controller);
            } catch (e) {
                console.error('Failed to apply controller settings:', e);
            }
        }
        
        // v2: Apply only segment state (effect + params)
        async function applySegmentState() {
            const effectId = document.getElementById('effect').value;
            const segment = {
                effect: effectId
            };
            
            // Collect schema-based params from dynamic controls
            const metadata = effectMetadata[effectId];
            if (metadata && metadata.hasSchema) {
                const params = {};
                let paletteValue = null;
                const schemaContainer = document.getElementById('schemaControls');
                if (schemaContainer) {
                    const formGroups = schemaContainer.querySelectorAll('[data-param-id]');
                    formGroups.forEach(group => {
                        const paramId = group.dataset.paramId;
                        let input = group.querySelector('input');
                        if (!input) input = group.querySelector('select'); // enum/palette uses select
                        if (input) {
                            let value;
                            if (input.type === 'checkbox') {
                                value = input.checked;
                            } else if (input.type === 'number') {
                                value = parseFloat(input.value);
                            } else if (input.type === 'range') {
                                value = parseInt(input.value);
                            } else if (input.type === 'color') {
                                value = input.value; // hex string
                            } else if (input.tagName === 'SELECT') {
                                value = parseInt(input.value); // enum is int
                            } else {
                                value = input.value;
                            }
                            
                            // Special handling for palette - send as top-level field
                            if (paramId === 'palette') {
                                const paletteName = input.value; // string like 'rainbow'
                                paletteValue = PALETTE_PRESETS[paletteName] ?? 0;
                            } else {
                                params[paramId] = value;
                            }
                        }
                    });
                    if (Object.keys(params).length > 0) {
                        segment.params = params;
                    }
                    if (paletteValue !== null) {
                        segment.palette = paletteValue;
                    }
                }
            }

            try {
                if (activeSegmentId >= 0) {
                    await apiV2(`/segments/${activeSegmentId}`, 'PUT', segment);
                    showToast('Settings applied!', 'success');
                } else {
                    showToast('No segment selected', 'error');
                }
            } catch (e) {
                showToast('Failed to apply settings', 'error');
                console.error(e);
            }
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
        
        // Load LED state (v2) defined above
        
        // Simple slider handlers - update display on input, apply on release
        function setupSlider(inputId, valueId) {
            const input = document.getElementById(inputId);
            const display = document.getElementById(valueId);
            if (!input || !display) return;
            
            let isDragging = false;
            
            // Update display while dragging
            input.addEventListener('input', () => {
                display.textContent = input.value;
            });
            
            // Mark as dragging
            ['pointerdown', 'mousedown', 'touchstart'].forEach(evt => {
                input.addEventListener(evt, () => {
                    isDragging = true;
                });
            });
            
            // Send update when released
            ['pointerup', 'mouseup', 'touchend'].forEach(evt => {
                input.addEventListener(evt, () => {
                    if (isDragging) {
                        isDragging = false;
                        // Brightness updates controller, other sliders update segment
                        if (input.id === 'brightness') {
                            applyControllerState();
                        } else {
                            applySegmentState();
                        }
                    }
                });
            });
        }

        setupSlider('brightness', 'brightnessValue');
        setupSlider('speed', 'speedValue');
        setupSlider('intensity', 'intensityValue');
        
        // Tile selector functions - auto-apply on selection
        function selectEffect(tile) {
            const container = document.getElementById('effectTiles');
            container.querySelectorAll('.tile').forEach(t => t.classList.remove('selected'));
            tile.classList.add('selected');
            const effectId = tile.dataset.value;
            document.getElementById('effect').value = effectId;
            updateEffectControls(effectId);
            applySegmentState();
        }
        
        function selectPalette(tile) {
            localStorage.setItem('palettePreset', tile.dataset.value);
            const container = document.getElementById('paletteTiles');
            container.querySelectorAll('.tile').forEach(t => t.classList.remove('selected'));
            tile.classList.add('selected');
            document.getElementById('palette').value = tile.dataset.value;
            
            // Update control visibility (custom palette needs color pickers)
            const currentEffect = document.getElementById('effect').value;
            updateEffectControls(currentEffect);
            
            applySegmentState();
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
        
        // Apply LED state (v2) defined above
        
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
                // Wait a moment for server to process, then start polling
                setTimeout(() => {
                    loadNightlightStatus();
                }, 200);
            } catch (e) {
                showToast('Failed to start nightlight', 'error');
                // Reset toggle on error
                const toggle = document.getElementById('nightlightToggle');
                updatingNightlightUI = true;
                toggle.checked = false;
                updatingNightlightUI = false;
            }
        }
        
        async function stopNightlight() {
            try {
                await api('/nightlight/stop', 'POST', {});
                showToast('Nightlight stopped', 'success');
                // Stop polling immediately
                if (nightlightPollInterval) {
                    clearInterval(nightlightPollInterval);
                    nightlightPollInterval = null;
                }
                // Update UI immediately without waiting for server
                const toggle = document.getElementById('nightlightToggle');
                const controls = document.getElementById('nightlightControls');
                updatingNightlightUI = true;
                toggle.checked = false;
                updatingNightlightUI = false;
                controls.classList.remove('expanded');
                document.getElementById('startNightlightBtn').style.display = '';
                document.getElementById('stopNightlightBtn').style.display = 'none';
                document.getElementById('nightlightProgress').style.display = 'none';
            } catch (e) {
                showToast('Failed to stop nightlight', 'error');
            }
        }
        
        // Load config
        async function loadConfig() {
            try {
                const config = await api('/config');
                
                document.getElementById('wifiSSID').value = config.wifiSSID || '';
                document.getElementById('ledCount').value = config.ledCount || 160;

                // AI settings
                document.getElementById('aiApiKey').value = config.aiApiKey && config.aiApiKey !== '****' ? '' : '';
                document.getElementById('aiModel').value = config.aiModel || 'claude-3-5-haiku-20241022';

                // sACN settings
                const sacnEnabled = config.sacnEnabled || false;
                document.getElementById('sacnEnabled').checked = sacnEnabled;
                document.getElementById('sacnUniverse').value = config.sacnUniverse || 1;
                document.getElementById('sacnStartChannel').value = config.sacnStartChannel || 1;
                toggleSettings('sacnSettings', sacnEnabled);

                // MQTT settings
                const mqttEnabled = config.mqttEnabled || false;
                document.getElementById('mqttEnabled').checked = mqttEnabled;
                document.getElementById('mqttBroker').value = config.mqttBroker || '';
                document.getElementById('mqttPort').value = config.mqttPort || 1883;
                document.getElementById('mqttUsername').value = config.mqttUsername && config.mqttUsername !== '****' ? config.mqttUsername : '';
                document.getElementById('mqttPassword').value = config.mqttPassword && config.mqttPassword !== '****' ? '' : '';
                document.getElementById('mqttTopicPrefix').value = config.mqttTopicPrefix || 'lume';
                toggleSettings('mqttSettings', mqttEnabled);
            } catch (e) {
                console.error('Failed to load config:', e);
            }
        }
        
        // Load segments for config modal
        async function loadSegmentsConfig() {
            try {
                const data = await apiV2('/segments');
                const container = document.getElementById('segmentsList');
                
                if (!data.segments || data.segments.length === 0) {
                    container.innerHTML = '<p style="color: var(--text-muted); font-size: 13px;">No segments configured</p>';
                    return;
                }
                
                container.innerHTML = data.segments.map(seg => {
                    const customName = getSegmentName(seg.id);
                    const displayName = customName || `Segment ${seg.id}`;
                    return `
                    <div style="display: flex; align-items: center; gap: 8px; padding: 8px; background: var(--surface); border-radius: 8px; margin-bottom: 8px;">
                        <div style="flex: 1;">
                            <div style="font-weight: 500;">${displayName}</div>
                            <div style="font-size: 12px; color: var(--text-muted);">
                                LEDs ${seg.start}-${seg.stop} (${seg.length} LEDs)
                                ${seg.effect ? `• ${seg.effect}` : ''}
                            </div>
                        </div>
                        <button class="btn btn-outline" onclick="editSegmentName(${seg.id})" style="padding: 6px 12px;">Rename</button>
                        ${seg.length > 1 ? `<button class="btn btn-outline" onclick="splitSegment(${seg.id}, ${seg.start}, ${seg.length})" style="padding: 6px 12px;">Split</button>` : ''}
                        <button class="btn btn-outline" onclick="deleteSegmentConfig(${seg.id})" style="padding: 6px 12px;">Delete</button>
                    </div>
                `;
                }).join('');
            } catch (e) {
                console.error('Failed to load segments:', e);
            }
        }
        
        async function editSegmentName(id) {
            const currentName = getSegmentName(id) || '';
            const newName = prompt(`Enter name for segment ${id}:`, currentName);
            
            if (newName !== null) {
                setSegmentName(id, newName);
                await loadSegmentsConfig(); // Refresh config list
                await loadSegments(); // Refresh dropdown
            }
        }
        
        async function splitSegment(id, start, length) {
            const splitAt = prompt(`Split segment ${id} at LED position? (${start + 1} to ${start + length - 1})`);
            if (!splitAt) return;
            
            const splitPos = parseInt(splitAt);
            if (isNaN(splitPos) || splitPos <= start || splitPos >= start + length) {
                showToast('Invalid split position', 'error');
                return;
            }
            
            try {
                // Calculate new segment lengths
                const firstLength = splitPos - start;
                const secondLength = length - firstLength;
                
                // Delete original segment
                await fetch('/api/v2/segments/' + id, { method: 'DELETE' });
                
                // Create first segment
                await apiV2('/segments', 'POST', {
                    start: start,
                    length: firstLength
                });
                
                // Create second segment
                await apiV2('/segments', 'POST', {
                    start: splitPos,
                    length: secondLength
                });
                
                showToast('Segment split successfully!', 'success');
                loadSegmentsConfig();
            } catch (e) {
                showToast('Failed to split segment', 'error');
                console.error(e);
            }
        }
        
        async function addSegment() {
            const start = parseInt(document.getElementById('newSegmentStart').value);
            const length = parseInt(document.getElementById('newSegmentLength').value);
            
            if (isNaN(start) || isNaN(length) || start < 0 || length < 1) {
                showToast('Invalid segment range', 'error');
                return;
            }
            
            try {
                await apiV2('/segments', 'POST', {
                    start: start,
                    length: length
                });
                showToast('Segment created!', 'success');
                loadSegmentsConfig();
                
                // Update start field for next segment
                document.getElementById('newSegmentStart').value = start + length;
            } catch (e) {
                showToast('Failed to create segment', 'error');
                console.error(e);
            }
        }
        
        async function deleteSegmentConfig(id) {
            if (!confirm(`Delete segment ${id}?`)) {
                return;
            }
            
            try {
                await fetch('/api/v2/segments/' + id, { method: 'DELETE' });
                showToast('Segment deleted', 'success');
                loadSegmentsConfig();
            } catch (e) {
                showToast('Failed to delete segment', 'error');
                console.error(e);
            }
        }
        
        // Save config
        async function saveConfig() {
            const config = {
                wifiSSID: document.getElementById('wifiSSID').value,
                wifiPassword: document.getElementById('wifiPassword').value,
                ledCount: parseInt(document.getElementById('ledCount').value),
                aiApiKey: document.getElementById('aiApiKey').value,
                aiModel: document.getElementById('aiModel').value,
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
            if (config.mqttPassword === '') delete config.mqttPassword;
            if (config.aiApiKey === '') delete config.aiApiKey;
            
            try {
                await api('/config', 'POST', config);
                showToast('Configuration saved!', 'success');
                loadConfig();
            } catch (e) {
                showToast('Failed to save configuration', 'error');
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
        
        // Toggle settings visibility
        function toggleSettings(settingsId, show) {
            const settings = document.getElementById(settingsId);
            if (!settings) return;
            settings.style.maxHeight = show ? '500px' : '0';
        }
        
        // Event listeners - sliders handled by SliderBinding helpers
        
        // Color pickers auto-apply on change (when picker closes)
        document.getElementById('primaryColor').addEventListener('change', function() {
            applySegmentState();
        });
        
        document.getElementById('secondaryColor').addEventListener('change', function() {
            applySegmentState();
        });
        
        document.getElementById('powerToggle').addEventListener('change', function() {
            applyControllerState();
        });
        
        // Nightlight slider listeners
        document.getElementById('nightlightDuration').addEventListener('input', function() {
            document.getElementById('nightlightDurationValue').textContent = this.value + ' min';
        });
        
        document.getElementById('nightlightTarget').addEventListener('input', function() {
            const val = parseInt(this.value);
            document.getElementById('nightlightTargetValue').textContent = val === 0 ? '0 (off)' : val;
        });
        
        document.getElementById('nightlightToggle').addEventListener('change', async function() {
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
            const prompt = document.getElementById('aiPrompt').value.trim();
            if (!prompt) {
                showToast('Please enter a prompt', 'error');
                return;
            }
            
            const statusDiv = document.getElementById('aiStatus');
            const statusText = document.getElementById('aiStatusText');
            
            statusDiv.style.display = 'block';
            statusText.textContent = 'Processing your request...';
            
            try {
                const result = await api('/prompt', 'POST', { prompt: prompt });
                
                if (result.success) {
                    showToast('✨ Lights updated!', 'success');
                    statusText.textContent = result.message || 'Applied successfully!';
                    setTimeout(() => {
                        statusDiv.style.display = 'none';
                    }, 3000);
                    
                    // Reload LED state to show changes
                    await loadLedState();
                } else {
                    showToast(result.error || 'Failed to process prompt', 'error');
                    statusDiv.style.display = 'none';
                }
            } catch (e) {
                const errorMsg = e.message || 'Network error';
                showToast('Error: ' + errorMsg, 'error');
                statusText.textContent = 'Error: ' + errorMsg;
                setTimeout(() => {
                    statusDiv.style.display = 'none';
                }, 5000); // Show error for 5 seconds
                console.error('AI prompt error:', e);
            }
        }
        
        // sACN and MQTT toggle handlers
        document.getElementById('sacnEnabled').addEventListener('change', function() {
            toggleSettings('sacnSettings', this.checked);
        });
        
        document.getElementById('mqttEnabled').addEventListener('change', function() {
            toggleSettings('mqttSettings', this.checked);
        });
        
        // Initialize
        async function initialize() {
            loadStatus();
            await loadEffectMetadata(); // Load effect metadata FIRST
            await loadLedState();        // Then load LED state (which needs metadata)
            loadConfig();
            loadScenes();
            loadNightlightStatus();
            setInterval(loadStatus, 10000);
        }
        
        initialize();
    

        // Start WebSocket (optional)
        connectWebSocket();
