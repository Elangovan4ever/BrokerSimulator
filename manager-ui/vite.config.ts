import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import path from 'path'

export default defineConfig({
  plugins: [react()],
  resolve: {
    alias: {
      '@': path.resolve(__dirname, './src'),
    },
  },
  server: {
    port: 5174,
    host: true,
    proxy: {
      '/api/control': {
        target: 'http://elanlinux:8000',
        changeOrigin: true,
        ws: true,
        rewrite: (path) => path.replace(/^\/api\/control/, ''),
      },
      '/api/alpaca': {
        target: 'http://elanlinux:8100',
        changeOrigin: true,
        ws: true,
        rewrite: (path) => path.replace(/^\/api\/alpaca/, ''),
      },
      '/api/polygon': {
        target: 'http://elanlinux:8200',
        changeOrigin: true,
        ws: true,
        rewrite: (path) => path.replace(/^\/api\/polygon/, ''),
      },
      '/api/finnhub': {
        target: 'http://elanlinux:8300',
        changeOrigin: true,
        ws: true,
        rewrite: (path) => path.replace(/^\/api\/finnhub/, ''),
      },
    },
  },
})
