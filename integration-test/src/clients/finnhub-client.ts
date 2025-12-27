/**
 * Real Finnhub API Client
 * Used for fetching responses to compare against simulator
 */

import axios, { AxiosInstance, AxiosResponse } from 'axios';
import http from 'http';
import https from 'https';

export interface FinnhubClientConfig {
  apiKey: string;
  baseUrl?: string;
}

export class FinnhubClient {
  private client: AxiosInstance;
  private httpAgent: http.Agent;
  private httpsAgent: https.Agent;

  constructor(config: FinnhubClientConfig) {
    this.httpAgent = new http.Agent({ keepAlive: false });
    this.httpsAgent = new https.Agent({ keepAlive: false });
    this.client = axios.create({
      baseURL: config.baseUrl || 'https://finnhub.io/api/v1',
      timeout: 30000,
      httpAgent: this.httpAgent,
      httpsAgent: this.httpsAgent,
      validateStatus: () => true,
      headers: {
        'Content-Type': 'application/json',
      },
    });

    this.client.interceptors.request.use((request) => {
      const params = { ...(request.params || {}) };
      if (!params.token) {
        params.token = config.apiKey;
      }
      request.params = params;
      return request;
    });
  }

  close(): void {
    this.httpAgent.destroy();
    this.httpsAgent.destroy();
  }

  // ============ Quote & Trades ============

  async getQuote(symbol: string): Promise<AxiosResponse> {
    return this.client.get('/quote', { params: { symbol } });
  }

  async getTrades(symbol: string): Promise<AxiosResponse> {
    return this.client.get('/stock/trade', { params: { symbol } });
  }

  // ============ Candles ============

  async getCandles(
    symbol: string,
    options: { resolution: string; from: number; to: number }
  ): Promise<AxiosResponse> {
    return this.client.get('/stock/candle', { params: { symbol, ...options } });
  }

  // ============ Company ============

  async getCompanyProfile(symbol: string): Promise<AxiosResponse> {
    return this.client.get('/stock/profile2', { params: { symbol } });
  }

  async getCompanyPeers(symbol: string): Promise<AxiosResponse> {
    return this.client.get('/stock/peers', { params: { symbol } });
  }

  async getBasicFinancials(symbol: string, metric = 'all'): Promise<AxiosResponse> {
    return this.client.get('/stock/metric', { params: { symbol, metric } });
  }

  // ============ News ============

  async getCompanyNews(symbol: string, from: string, to: string): Promise<AxiosResponse> {
    return this.client.get('/company-news', { params: { symbol, from, to } });
  }

  async getMarketNews(category = 'general'): Promise<AxiosResponse> {
    return this.client.get('/news', { params: { category } });
  }

  async getNewsSentiment(symbol: string): Promise<AxiosResponse> {
    return this.client.get('/news-sentiment', { params: { symbol } });
  }

  // ============ Corporate Actions ============

  async getDividends(symbol: string, from: string, to: string): Promise<AxiosResponse> {
    return this.client.get('/stock/dividend', { params: { symbol, from, to } });
  }

  async getSplits(symbol: string, from: string, to: string): Promise<AxiosResponse> {
    return this.client.get('/stock/split', { params: { symbol, from, to } });
  }

  // ============ Analyst ============

  async getEarningsCalendar(options: { symbol?: string; from: string; to: string }): Promise<AxiosResponse> {
    return this.client.get('/calendar/earnings', { params: options });
  }

  async getRecommendation(symbol: string): Promise<AxiosResponse> {
    return this.client.get('/stock/recommendation', { params: { symbol } });
  }

  async getPriceTarget(symbol: string): Promise<AxiosResponse> {
    return this.client.get('/stock/price-target', { params: { symbol } });
  }

  async getUpgradeDowngrade(symbol: string): Promise<AxiosResponse> {
    return this.client.get('/stock/upgrade-downgrade', { params: { symbol } });
  }
}
