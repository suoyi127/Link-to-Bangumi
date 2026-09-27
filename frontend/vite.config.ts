import { defineConfig } from 'vitest/config'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      '/health': `http://127.0.0.1:${process.env.ANIME_VAULT_DEV_BACKEND_PORT || '8848'}`,
      '/api': `http://127.0.0.1:${process.env.ANIME_VAULT_DEV_BACKEND_PORT || '8848'}`,
    },
  },
  test: {
    environment: 'jsdom',
    setupFiles: ['./src/test/setup.ts'],
  },
})
