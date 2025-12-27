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
  private sessionId?: string;

  constructor(config: FinnhubSimulatorClientConfig) {
    this.client = axios.create({
      baseURL: `http://${config.host}:${config.port}`,
      timeout: 30000,
      validateStatus: () => true,
      headers: {
        'Content-Type': 'application/json',
      },
    });

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

  // ============ Company ============

  async getCompanyProfile(symbol?: string): Promise<AxiosResponse> {
    return this.client.get('/stock/profile2', { params: { symbol } });
  }

  async getCompanyPeers(symbol?: string): Promise<AxiosResponse> {
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

  async getNewsSentiment(symbol?: string): Promise<AxiosResponse> {
    return this.client.get('/news-sentiment', { params: { symbol } });
  }

  // ============ Corporate Actions ============

  async getDividends(symbol: string, params: { from?: string; to?: string } = {}): Promise<AxiosResponse> {
    return this.client.get('/stock/dividend', { params: { symbol, ...params } });
  }

  // ============ Analyst ============

  async getEarningsCalendar(options: { symbol?: string; from?: string; to?: string } = {}): Promise<AxiosResponse> {
    return this.client.get('/calendar/earnings', { params: options });
  }

  async getIpoCalendar(options: { from?: string; to?: string } = {}): Promise<AxiosResponse> {
    return this.client.get('/calendar/ipo', { params: options });
  }

  async getRecommendation(symbol?: string): Promise<AxiosResponse> {
    return this.client.get('/stock/recommendation', { params: { symbol } });
  }

  async getPriceTarget(symbol?: string): Promise<AxiosResponse> {
    return this.client.get('/stock/price-target', { params: { symbol } });
  }

  async getUpgradeDowngrade(symbol?: string): Promise<AxiosResponse> {
    return this.client.get('/stock/upgrade-downgrade', { params: { symbol } });
  }

  // ============ Additional Finnhub ============

  async getInsiderTransactions(params: { symbol?: string; from?: string; to?: string } = {}): Promise<AxiosResponse> {
    return this.client.get('/stock/insider-transactions', { params });
  }

  async getSecFilings(params: { symbol?: string; from?: string; to?: string } = {}): Promise<AxiosResponse> {
    return this.client.get('/stock/filings', { params });
  }

  async getCongressionalTrading(params: { symbol: string; from?: string; to?: string }): Promise<AxiosResponse> {
    return this.client.get('/stock/congressional-trading', { params });
  }

  async getInsiderSentiment(params: { symbol?: string; from?: string; to?: string } = {}): Promise<AxiosResponse> {
    return this.client.get('/stock/insider-sentiment', { params });
  }

  async getEpsEstimate(params: { symbol: string; freq?: string; from?: string; to?: string }): Promise<AxiosResponse> {
    return this.client.get('/stock/eps-estimate', { params });
  }

  async getRevenueEstimate(params: { symbol: string; freq?: string; from?: string; to?: string }): Promise<AxiosResponse> {
    return this.client.get('/stock/revenue-estimate', { params });
  }

  async getEarningsHistory(params: { symbol?: string; from?: string; to?: string } = {}): Promise<AxiosResponse> {
    return this.client.get('/stock/earnings', { params });
  }

  async getSocialSentiment(params: { symbol?: string; from?: string; to?: string } = {}): Promise<AxiosResponse> {
    return this.client.get('/stock/social-sentiment', { params });
  }

  async getOwnership(params: { symbol?: string; from?: string; to?: string } = {}): Promise<AxiosResponse> {
    return this.client.get('/stock/ownership', { params });
  }

  async getFinancials(params: { symbol?: string; statement?: string; freq?: string } = {}): Promise<AxiosResponse> {
    return this.client.get('/stock/financials', { params });
  }

  async getFinancialsReported(params: { symbol: string; freq?: string; from?: string; to?: string }): Promise<AxiosResponse> {
    return this.client.get('/stock/financials-reported', { params });
  }
}
