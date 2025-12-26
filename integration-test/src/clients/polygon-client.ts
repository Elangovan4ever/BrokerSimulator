/**
 * Real Polygon.io API Client
 * Used for fetching responses to compare against simulator
 */

import axios, { AxiosInstance, AxiosResponse } from 'axios';

export interface PolygonClientConfig {
  apiKey: string;
  baseUrl?: string;
}

export class PolygonClient {
  private client: AxiosInstance;

  constructor(config: PolygonClientConfig) {
    this.client = axios.create({
      baseURL: config.baseUrl || 'https://api.polygon.io',
      timeout: 30000,
      headers: {
        'Authorization': `Bearer ${config.apiKey}`,
      },
    });
  }

  // ============ Aggregates (Bars) ============

  /**
   * Get aggregate bars for a stock over a given date range
   * GET /v2/aggs/ticker/{stocksTicker}/range/{multiplier}/{timespan}/{from}/{to}
   */
  async getAggregateBars(
    ticker: string,
    multiplier: number,
    timespan: string,
    from: string,
    to: string,
    options?: {
      adjusted?: boolean;
      sort?: 'asc' | 'desc';
      limit?: number;
    }
  ): Promise<AxiosResponse> {
    return this.client.get(
      `/v2/aggs/ticker/${ticker}/range/${multiplier}/${timespan}/${from}/${to}`,
      { params: options }
    );
  }

  /**
   * Get the previous day's aggregate bar
   * GET /v2/aggs/ticker/{stocksTicker}/prev
   */
  async getPreviousClose(
    ticker: string,
    options?: { adjusted?: boolean }
  ): Promise<AxiosResponse> {
    return this.client.get(`/v2/aggs/ticker/${ticker}/prev`, { params: options });
  }

  /**
   * Get grouped daily bars for a date
   * GET /v2/aggs/grouped/locale/us/market/stocks/{date}
   */
  async getGroupedDaily(
    date: string,
    options?: { adjusted?: boolean; include_otc?: boolean }
  ): Promise<AxiosResponse> {
    return this.client.get(`/v2/aggs/grouped/locale/us/market/stocks/${date}`, {
      params: options,
    });
  }

  // ============ Trades ============

  /**
   * Get trades for a stock
   * GET /v3/trades/{stocksTicker}
   */
  async getTrades(
    ticker: string,
    options?: {
      timestamp?: string;
      'timestamp.gte'?: string;
      'timestamp.lte'?: string;
      order?: 'asc' | 'desc';
      limit?: number;
      sort?: string;
    }
  ): Promise<AxiosResponse> {
    return this.client.get(`/v3/trades/${ticker}`, { params: options });
  }

  /**
   * Get historic trades for a date
   * GET /v2/ticks/stocks/trades/{ticker}/{date}
   */
  async getHistoricTrades(
    ticker: string,
    date: string,
    options?: {
      timestamp?: number;
      timestampLimit?: number;
      reverse?: boolean;
      limit?: number;
    }
  ): Promise<AxiosResponse> {
    return this.client.get(`/v2/ticks/stocks/trades/${ticker}/${date}`, {
      params: options,
    });
  }

  /**
   * Get last trade for a stock
   * GET /v2/last/trade/{stocksTicker}
   */
  async getLastTrade(ticker: string): Promise<AxiosResponse> {
    return this.client.get(`/v2/last/trade/${ticker}`);
  }

  // ============ Quotes ============

  /**
   * Get quotes for a stock
   * GET /v3/quotes/{stocksTicker}
   */
  async getQuotes(
    ticker: string,
    options?: {
      timestamp?: string;
      'timestamp.gte'?: string;
      'timestamp.lte'?: string;
      order?: 'asc' | 'desc';
      limit?: number;
      sort?: string;
    }
  ): Promise<AxiosResponse> {
    return this.client.get(`/v3/quotes/${ticker}`, { params: options });
  }

  /**
   * Get historic NBBO quotes for a date
   * GET /v2/ticks/stocks/nbbo/{ticker}/{date}
   */
  async getHistoricQuotes(
    ticker: string,
    date: string,
    options?: {
      timestamp?: number;
      timestampLimit?: number;
      reverse?: boolean;
      limit?: number;
    }
  ): Promise<AxiosResponse> {
    return this.client.get(`/v2/ticks/stocks/nbbo/${ticker}/${date}`, {
      params: options,
    });
  }

  /**
   * Get last quote (NBBO) for a stock
   * GET /v2/last/nbbo/{stocksTicker}
   */
  async getLastQuote(ticker: string): Promise<AxiosResponse> {
    return this.client.get(`/v2/last/nbbo/${ticker}`);
  }

  // ============ Dividends ============

  /**
   * Get dividends
   * GET /v3/reference/dividends
   */
  async getDividends(options?: {
    ticker?: string;
    'ticker.gte'?: string;
    'ticker.lte'?: string;
    ex_dividend_date?: string;
    'ex_dividend_date.gte'?: string;
    'ex_dividend_date.lte'?: string;
    record_date?: string;
    declaration_date?: string;
    pay_date?: string;
    frequency?: number;
    cash_amount?: number;
    dividend_type?: string;
    order?: 'asc' | 'desc';
    limit?: number;
    sort?: string;
  }): Promise<AxiosResponse> {
    return this.client.get('/v3/reference/dividends', { params: options });
  }

  // ============ Snapshots ============

  /**
   * Get all tickers snapshot
   * GET /v2/snapshot/locale/us/markets/stocks/tickers
   */
  async getAllTickersSnapshot(options?: {
    tickers?: string;
    include_otc?: boolean;
  }): Promise<AxiosResponse> {
    return this.client.get('/v2/snapshot/locale/us/markets/stocks/tickers', {
      params: options,
    });
  }

  /**
   * Get single ticker snapshot
   * GET /v2/snapshot/locale/us/markets/stocks/tickers/{stocksTicker}
   */
  async getTickerSnapshot(ticker: string): Promise<AxiosResponse> {
    return this.client.get(
      `/v2/snapshot/locale/us/markets/stocks/tickers/${ticker}`
    );
  }

  /**
   * Get gainers/losers snapshot
   * GET /v2/snapshot/locale/us/markets/stocks/{direction}
   */
  async getGainersLosers(direction: 'gainers' | 'losers'): Promise<AxiosResponse> {
    return this.client.get(
      `/v2/snapshot/locale/us/markets/stocks/${direction}`
    );
  }

  // ============ Ticker Details ============

  /**
   * Get ticker details
   * GET /v3/reference/tickers/{ticker}
   */
  async getTickerDetails(
    ticker: string,
    options?: { date?: string }
  ): Promise<AxiosResponse> {
    return this.client.get(`/v3/reference/tickers/${ticker}`, { params: options });
  }

  /**
   * List tickers
   * GET /v3/reference/tickers
   */
  async listTickers(options?: {
    ticker?: string;
    'ticker.gte'?: string;
    'ticker.lte'?: string;
    type?: string;
    market?: string;
    exchange?: string;
    cusip?: string;
    cik?: string;
    date?: string;
    search?: string;
    active?: boolean;
    order?: 'asc' | 'desc';
    limit?: number;
    sort?: string;
  }): Promise<AxiosResponse> {
    return this.client.get('/v3/reference/tickers', { params: options });
  }

  // ============ Technical Indicators ============

  /**
   * Get SMA (Simple Moving Average)
   * GET /v1/indicators/sma/{stockTicker}
   */
  async getSMA(
    ticker: string,
    options?: {
      timestamp?: string;
      'timestamp.gte'?: string;
      'timestamp.lte'?: string;
      timespan?: string;
      adjusted?: boolean;
      window?: number;
      series_type?: string;
      expand_underlying?: boolean;
      order?: 'asc' | 'desc';
      limit?: number;
    }
  ): Promise<AxiosResponse> {
    return this.client.get(`/v1/indicators/sma/${ticker}`, { params: options });
  }

  /**
   * Get EMA (Exponential Moving Average)
   * GET /v1/indicators/ema/{stockTicker}
   */
  async getEMA(
    ticker: string,
    options?: {
      timestamp?: string;
      'timestamp.gte'?: string;
      'timestamp.lte'?: string;
      timespan?: string;
      adjusted?: boolean;
      window?: number;
      series_type?: string;
      expand_underlying?: boolean;
      order?: 'asc' | 'desc';
      limit?: number;
    }
  ): Promise<AxiosResponse> {
    return this.client.get(`/v1/indicators/ema/${ticker}`, { params: options });
  }

  /**
   * Get RSI (Relative Strength Index)
   * GET /v1/indicators/rsi/{stockTicker}
   */
  async getRSI(
    ticker: string,
    options?: {
      timestamp?: string;
      'timestamp.gte'?: string;
      'timestamp.lte'?: string;
      timespan?: string;
      adjusted?: boolean;
      window?: number;
      series_type?: string;
      expand_underlying?: boolean;
      order?: 'asc' | 'desc';
      limit?: number;
    }
  ): Promise<AxiosResponse> {
    return this.client.get(`/v1/indicators/rsi/${ticker}`, { params: options });
  }

  /**
   * Get MACD (Moving Average Convergence/Divergence)
   * GET /v1/indicators/macd/{stockTicker}
   */
  async getMACD(
    ticker: string,
    options?: {
      timestamp?: string;
      'timestamp.gte'?: string;
      'timestamp.lte'?: string;
      timespan?: string;
      adjusted?: boolean;
      short_window?: number;
      long_window?: number;
      signal_window?: number;
      series_type?: string;
      expand_underlying?: boolean;
      order?: 'asc' | 'desc';
      limit?: number;
    }
  ): Promise<AxiosResponse> {
    return this.client.get(`/v1/indicators/macd/${ticker}`, { params: options });
  }

  // ============ Open/Close ============

  /**
   * Get daily open/close
   * GET /v1/open-close/{stocksTicker}/{date}
   */
  async getDailyOpenClose(
    ticker: string,
    date: string,
    options?: { adjusted?: boolean }
  ): Promise<AxiosResponse> {
    return this.client.get(`/v1/open-close/${ticker}/${date}`, { params: options });
  }

  // ============ Market Status ============

  /**
   * Get current market status
   * GET /v1/marketstatus/now
   */
  async getMarketStatus(): Promise<AxiosResponse> {
    return this.client.get('/v1/marketstatus/now');
  }

  /**
   * Get upcoming market holidays
   * GET /v1/marketstatus/upcoming
   */
  async getMarketHolidays(): Promise<AxiosResponse> {
    return this.client.get('/v1/marketstatus/upcoming');
  }
}
