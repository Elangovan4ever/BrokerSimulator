import type { SimulatorConfig } from '@/types';

// Check if we're in development mode (using Vite proxy)
const isDev = import.meta.env.DEV;

// Default configuration - can be overridden via environment variables
export const defaultConfig: SimulatorConfig = {
  host: import.meta.env.VITE_SIMULATOR_HOST || 'elanlinux',
  controlPort: parseInt(import.meta.env.VITE_CONTROL_PORT || '8000'),
  alpacaPort: parseInt(import.meta.env.VITE_ALPACA_PORT || '8100'),
  polygonPort: parseInt(import.meta.env.VITE_POLYGON_PORT || '8200'),
  finnhubPort: parseInt(import.meta.env.VITE_FINNHUB_PORT || '8300'),
  wsPort: parseInt(import.meta.env.VITE_WS_PORT || '8400'),
};

export function getBaseUrl(service: 'control' | 'alpaca' | 'polygon' | 'finnhub', config = defaultConfig): string {
  // In development, use Vite's proxy to avoid CORS issues
  if (isDev) {
    switch (service) {
      case 'control':
        return '/api/control';
      case 'alpaca':
        return '/api/alpaca';
      case 'polygon':
        return '/api/polygon';
      case 'finnhub':
        return '/api/finnhub';
      default:
        throw new Error(`Unknown service: ${service}`);
    }
  }

  // In production, connect directly
  const { host, controlPort, alpacaPort, polygonPort, finnhubPort } = config;

  switch (service) {
    case 'control':
      return `http://${host}:${controlPort}`;
    case 'alpaca':
      return `http://${host}:${alpacaPort}`;
    case 'polygon':
      return `http://${host}:${polygonPort}`;
    case 'finnhub':
      return `http://${host}:${finnhubPort}`;
    default:
      throw new Error(`Unknown service: ${service}`);
  }
}

export function getWsUrl(service: 'control' | 'alpaca' | 'polygon' | 'finnhub', config = defaultConfig): string {
  const { host, controlPort, alpacaPort, polygonPort, finnhubPort } = config;

  if (isDev) {
    const devHost = window.location.host;
    switch (service) {
      case 'control':
        return `ws://${devHost}/api/control/ws`;
      case 'alpaca':
        return `ws://${devHost}/api/alpaca/stream`;
      case 'polygon':
        return `ws://${devHost}/api/polygon/polygon/ws`;
      case 'finnhub':
        return `ws://${devHost}/api/finnhub/finnhub/ws`;
      default:
        throw new Error(`Unknown service: ${service}`);
    }
  }

  // WebSocket paths must match server's WS_PATH_ADD registrations
  switch (service) {
    case 'control':
      return `ws://${host}:${controlPort}/ws`;
    case 'alpaca':
      return `ws://${host}:${alpacaPort}/stream`;
    case 'polygon':
      return `ws://${host}:${polygonPort}/polygon/ws`;
    case 'finnhub':
      return `ws://${host}:${finnhubPort}/finnhub/ws`;
    default:
      throw new Error(`Unknown service: ${service}`);
  }
}

export function getStatusWsUrl(config = defaultConfig): string {
  const { host, controlPort } = config;
  if (isDev) {
    return `ws://${window.location.host}/api/control/ws/status`;
  }
  return `ws://${host}:${controlPort}/ws/status`;
}
