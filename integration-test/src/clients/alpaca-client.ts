/**
 * Alpaca Trading API Client (real paper API).
 */

import axios, { AxiosInstance, AxiosResponse } from 'axios';

export interface AlpacaClientConfig {
  apiKeyId: string;
  apiSecret: string;
  baseUrl: string;
}

export class AlpacaClient {
  private client: AxiosInstance;

  constructor(config: AlpacaClientConfig) {
    this.client = axios.create({
      baseURL: config.baseUrl,
      timeout: 30000,
      validateStatus: () => true,
      headers: {
        'Content-Type': 'application/json',
        'APCA-API-KEY-ID': config.apiKeyId,
        'APCA-API-SECRET-KEY': config.apiSecret,
      },
    });
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
}
