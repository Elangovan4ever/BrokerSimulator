/**
 * Broker Simulator Alpaca Trading API Client
 */

import axios, { AxiosInstance, AxiosResponse } from 'axios';

export interface AlpacaSimulatorClientConfig {
  host: string;
  port: number;
  sessionId?: string;
}

export class AlpacaSimulatorClient {
  private client: AxiosInstance;
  private sessionId?: string;

  constructor(config: AlpacaSimulatorClientConfig) {
    this.client = axios.create({
      baseURL: `http://${config.host}:${config.port}`,
      timeout: 30000,
      validateStatus: () => true,
      headers: {
        'Content-Type': 'application/json',
      },
    });

    if (config.sessionId) {
      this.sessionId = config.sessionId;
    }

    this.client.interceptors.request.use((request) => {
      if (this.sessionId) {
        request.params = { ...(request.params || {}), session_id: this.sessionId };
      }
      return request;
    });
  }

  setSessionId(sessionId: string): void {
    this.sessionId = sessionId;
  }

  async getAccount(): Promise<AxiosResponse> {
    return this.client.get('/v2/account');
  }

  async listPositions(): Promise<AxiosResponse> {
    return this.client.get('/v2/positions');
  }

  async getPosition(symbol: string): Promise<AxiosResponse> {
    return this.client.get(`/v2/positions/${symbol}`);
  }

  async closePosition(symbol: string, params?: { qty?: number; percentage?: number }): Promise<AxiosResponse> {
    return this.client.delete(`/v2/positions/${symbol}`, { params });
  }

  async closeAllPositions(params?: { cancel_orders?: boolean }): Promise<AxiosResponse> {
    return this.client.delete('/v2/positions', { params });
  }

  async listOrders(params?: { status?: string; limit?: number }): Promise<AxiosResponse> {
    return this.client.get('/v2/orders', { params });
  }

  async getOrder(orderId: string): Promise<AxiosResponse> {
    return this.client.get(`/v2/orders/${orderId}`);
  }

  async getOrderByClientId(clientOrderId: string): Promise<AxiosResponse> {
    return this.client.get('/v2/orders:by_client_order_id', {
      params: { client_order_id: clientOrderId },
    });
  }

  async submitOrder(payload: Record<string, unknown>): Promise<AxiosResponse> {
    return this.client.post('/v2/orders', payload);
  }

  async replaceOrder(orderId: string, payload: Record<string, unknown>): Promise<AxiosResponse> {
    return this.client.patch(`/v2/orders/${orderId}`, payload);
  }

  async cancelOrder(orderId: string): Promise<AxiosResponse> {
    return this.client.delete(`/v2/orders/${orderId}`);
  }

  async cancelAllOrders(): Promise<AxiosResponse> {
    return this.client.delete('/v2/orders');
  }
}
