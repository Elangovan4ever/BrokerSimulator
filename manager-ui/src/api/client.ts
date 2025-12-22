import axios, { AxiosInstance, AxiosError, InternalAxiosRequestConfig } from 'axios';
import { getBaseUrl } from './config';
import type { ApiService, ApiResponse } from '@/types';

// Create axios instances for each service
const createClient = (service: ApiService): AxiosInstance => {
  const client = axios.create({
    baseURL: getBaseUrl(service),
    timeout: 30000,
    headers: {
      'Content-Type': 'application/json',
    },
  });

  // Request interceptor for timing
  client.interceptors.request.use((config: InternalAxiosRequestConfig) => {
    (config as InternalAxiosRequestConfig & { metadata: { startTime: number } }).metadata = { startTime: Date.now() };
    return config;
  });

  // Response interceptor for logging and timing
  client.interceptors.response.use(
    (response) => {
      const config = response.config as InternalAxiosRequestConfig & { metadata?: { startTime: number } };
      const duration = config.metadata ? Date.now() - config.metadata.startTime : 0;
      console.log(`✅ ${response.config.method?.toUpperCase()} ${response.config.url} (${duration}ms)`);
      return response;
    },
    (error: AxiosError) => {
      const config = error.config as InternalAxiosRequestConfig & { metadata?: { startTime: number } };
      const duration = config?.metadata ? Date.now() - config.metadata.startTime : 0;
      console.error(`❌ ${error.config?.method?.toUpperCase()} ${error.config?.url} (${duration}ms)`, error.message);
      return Promise.reject(error);
    }
  );

  return client;
};

// Service clients
export const controlClient = createClient('control');
export const alpacaClient = createClient('alpaca');
export const polygonClient = createClient('polygon');
export const finnhubClient = createClient('finnhub');

// Get client by service name
export function getClient(service: ApiService): AxiosInstance {
  switch (service) {
    case 'control':
      return controlClient;
    case 'alpaca':
      return alpacaClient;
    case 'polygon':
      return polygonClient;
    case 'finnhub':
      return finnhubClient;
    default:
      throw new Error(`Unknown service: ${service}`);
  }
}

// Generic request function for API Explorer
export async function makeRequest(
  service: ApiService,
  method: string,
  path: string,
  data?: unknown,
  params?: Record<string, string>
): Promise<ApiResponse> {
  const client = getClient(service);
  const startTime = Date.now();

  try {
    const response = await client.request({
      method,
      url: path,
      data,
      params,
    });

    return {
      status: response.status,
      statusText: response.statusText,
      data: response.data,
      duration: Date.now() - startTime,
      timestamp: new Date().toISOString(),
    };
  } catch (error) {
    const axiosError = error as AxiosError;
    return {
      status: axiosError.response?.status || 0,
      statusText: axiosError.response?.statusText || axiosError.message,
      data: axiosError.response?.data || { error: axiosError.message },
      duration: Date.now() - startTime,
      timestamp: new Date().toISOString(),
    };
  }
}
