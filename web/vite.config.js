import { defineConfig } from 'vite';
import { resolve } from 'path';

export default defineConfig({
  server: {
    port: 5173,
    open: true,
    // Proxy API requests to the ESP32 device during development
    proxy: {
      '/api': {
        target: 'http://lume.local',
        changeOrigin: true,
      },
      '/ws': {
        target: 'ws://lume.local',
        ws: true,
      },
    },
  },

  // Resolve assets from the assets directory
  resolve: {
    alias: {
      '@': resolve(__dirname),
    },
  },
});
