import axios from 'axios';
import { getBaseUrl, defaultConfig } from './config';
import type { SimulatorStatus, ApiService } from '@/types';

const services: ApiService[] = ['control', 'alpaca', 'polygon', 'finnhub'];

// Get port for a service
function getServicePort(service: ApiService): number {
  switch (service) {
    case 'control': return defaultConfig.controlPort;
    case 'alpaca': return defaultConfig.alpacaPort;
    case 'polygon': return defaultConfig.polygonPort;
    case 'finnhub': return defaultConfig.finnhubPort;
    default: return 0;
  }
}

export async function checkServiceStatus(service: ApiService): Promise<SimulatorStatus> {
  const baseUrl = getBaseUrl(service);
  const startTime = Date.now();
  const port = getServicePort(service);

  try {
    const statusPath = service === 'control' ? '/sessions' : '/health';
    await axios.get(`${baseUrl}${statusPath}`, { timeout: 5000 });

    return {
      service,
      host: defaultConfig.host,
      port,
      status: 'online',
      latency: Date.now() - startTime,
      lastChecked: new Date().toISOString(),
    };
  } catch {
    // Try root endpoint as fallback
    try {
      await axios.get(baseUrl, { timeout: 5000 });
      return {
        service,
        host: defaultConfig.host,
        port,
        status: 'online',
        latency: Date.now() - startTime,
        lastChecked: new Date().toISOString(),
      };
    } catch {
      return {
        service,
        host: defaultConfig.host,
        port,
        status: 'offline',
        lastChecked: new Date().toISOString(),
      };
    }
  }
}

export async function checkAllServicesStatus(): Promise<SimulatorStatus[]> {
  const results = await Promise.all(services.map(checkServiceStatus));
  return results;
}
