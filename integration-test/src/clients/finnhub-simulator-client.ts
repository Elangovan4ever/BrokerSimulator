/**
 * Broker Simulator Finnhub API Client
 * Mirrors the real Finnhub API for comparison testing
 */

import axios, { AxiosInstance, AxiosResponse } from 'axios';

export interface FinnhubSimulatorClientConfig {
  host: string;
  port: number;
}

export class FinnhubSimulatorClient {
  private client: AxiosInstance;

  constructor(config: FinnhubSimulatorClientConfig) {
    this.client = axios.create({
      baseURL: `http://${config.host}:${config.port}`,
      timeout: 30000,
      validateStatus: () => true,
      headers: {
        'Content-Type': 'application/json',
      },
    });
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
