import type { ApiEndpoint, ApiService } from '@/types';

// Predefined API endpoints for the API Explorer
export const apiEndpoints: Record<ApiService, ApiEndpoint[]> = {
  control: [
    // Sessions
    { method: 'GET', path: '/sessions', description: 'List all sessions' },
    { method: 'POST', path: '/sessions', description: 'Create new session', body: { symbols: ['AAPL'], start_time: '2024-01-15T09:30:00Z', end_time: '2024-01-15T16:00:00Z', initial_capital: 100000 } },
    { method: 'GET', path: '/sessions/{session_id}', description: 'Get session details', params: [{ name: 'session_id', type: 'path', required: true, description: 'Session ID' }] },
    { method: 'DELETE', path: '/sessions/{session_id}', description: 'Delete session', params: [{ name: 'session_id', type: 'path', required: true, description: 'Session ID' }] },
    { method: 'POST', path: '/sessions/{session_id}/start', description: 'Start session', params: [{ name: 'session_id', type: 'path', required: true, description: 'Session ID' }] },
    { method: 'POST', path: '/sessions/{session_id}/pause', description: 'Pause session', params: [{ name: 'session_id', type: 'path', required: true, description: 'Session ID' }] },
    { method: 'POST', path: '/sessions/{session_id}/resume', description: 'Resume session', params: [{ name: 'session_id', type: 'path', required: true, description: 'Session ID' }] },
    { method: 'POST', path: '/sessions/{session_id}/stop', description: 'Stop session', params: [{ name: 'session_id', type: 'path', required: true, description: 'Session ID' }] },
    { method: 'POST', path: '/sessions/{session_id}/set_speed', description: 'Set replay speed', params: [{ name: 'session_id', type: 'path', required: true, description: 'Session ID' }], body: { speed_factor: 10.0 } },
    { method: 'POST', path: '/sessions/{session_id}/jump_to', description: 'Jump to timestamp', params: [{ name: 'session_id', type: 'path', required: true, description: 'Session ID' }], body: { timestamp: '2024-01-15T10:00:00Z' } },
  ],

  alpaca: [
    // Account
    { method: 'GET', path: '/v2/account', description: 'Get account details' },
    // Positions
    { method: 'GET', path: '/v2/positions', description: 'List all positions' },
    { method: 'GET', path: '/v2/positions/{symbol}', description: 'Get position by symbol', params: [{ name: 'symbol', type: 'path', required: true, description: 'Stock symbol' }] },
    { method: 'DELETE', path: '/v2/positions/{symbol}', description: 'Close position', params: [{ name: 'symbol', type: 'path', required: true, description: 'Stock symbol' }] },
    { method: 'DELETE', path: '/v2/positions', description: 'Close all positions' },
    // Orders
    { method: 'GET', path: '/v2/orders', description: 'List orders', params: [{ name: 'status', type: 'query', required: false, description: 'Order status filter', default: 'all' }] },
    { method: 'POST', path: '/v2/orders', description: 'Create order', body: { symbol: 'AAPL', qty: 100, side: 'buy', type: 'market', time_in_force: 'day' } },
    { method: 'GET', path: '/v2/orders/{order_id}', description: 'Get order by ID', params: [{ name: 'order_id', type: 'path', required: true, description: 'Order ID' }] },
    { method: 'DELETE', path: '/v2/orders/{order_id}', description: 'Cancel order', params: [{ name: 'order_id', type: 'path', required: true, description: 'Order ID' }] },
    { method: 'DELETE', path: '/v2/orders', description: 'Cancel all orders' },
    // Market Data
    { method: 'GET', path: '/v2/stocks/{symbol}/trades', description: 'Get trades', params: [{ name: 'symbol', type: 'path', required: true, description: 'Stock symbol' }] },
    { method: 'GET', path: '/v2/stocks/{symbol}/quotes', description: 'Get quotes', params: [{ name: 'symbol', type: 'path', required: true, description: 'Stock symbol' }] },
    { method: 'GET', path: '/v2/stocks/{symbol}/bars', description: 'Get bars', params: [{ name: 'symbol', type: 'path', required: true, description: 'Stock symbol' }] },
    { method: 'GET', path: '/v2/clock', description: 'Get market clock' },
    { method: 'GET', path: '/v2/calendar', description: 'Get trading calendar' },
  ],

  polygon: [
    // Aggregates
    { method: 'GET', path: '/v2/aggs/ticker/{symbol}/range/{multiplier}/{timespan}/{from}/{to}', description: 'Get aggregates/bars', params: [
      { name: 'symbol', type: 'path', required: true, description: 'Stock symbol' },
      { name: 'multiplier', type: 'path', required: true, description: 'Size of timespan (e.g., 1)', default: '1' },
      { name: 'timespan', type: 'path', required: true, description: 'Timespan (minute, hour, day)', default: 'minute' },
      { name: 'from', type: 'path', required: true, description: 'Start date (YYYY-MM-DD)', default: '2024-01-15' },
      { name: 'to', type: 'path', required: true, description: 'End date (YYYY-MM-DD)', default: '2024-01-15' },
    ]},
    { method: 'GET', path: '/v2/aggs/ticker/{symbol}/prev', description: 'Previous day aggregate', params: [{ name: 'symbol', type: 'path', required: true, description: 'Stock symbol' }] },
    // Trades & Quotes
    { method: 'GET', path: '/v3/trades/{symbol}', description: 'Get trades', params: [{ name: 'symbol', type: 'path', required: true, description: 'Stock symbol' }] },
    { method: 'GET', path: '/v2/last/trade/{symbol}', description: 'Last trade', params: [{ name: 'symbol', type: 'path', required: true, description: 'Stock symbol' }] },
    { method: 'GET', path: '/v3/quotes/{symbol}', description: 'Get quotes', params: [{ name: 'symbol', type: 'path', required: true, description: 'Stock symbol' }] },
    { method: 'GET', path: '/v2/last/nbbo/{symbol}', description: 'Last NBBO quote', params: [{ name: 'symbol', type: 'path', required: true, description: 'Stock symbol' }] },
    // Snapshots
    { method: 'GET', path: '/v2/snapshot/locale/us/markets/stocks/tickers/{symbol}', description: 'Get ticker snapshot', params: [{ name: 'symbol', type: 'path', required: true, description: 'Stock symbol' }] },
    // Ticker Details
    { method: 'GET', path: '/v3/reference/tickers/{symbol}', description: 'Get ticker details', params: [{ name: 'symbol', type: 'path', required: true, description: 'Stock symbol' }] },
    // Market Status
    { method: 'GET', path: '/v1/marketstatus/now', description: 'Current market status' },
  ],

  finnhub: [
    // Real-time
    { method: 'GET', path: '/quote', description: 'Get quote', params: [{ name: 'symbol', type: 'query', required: true, description: 'Stock symbol' }] },
    // Historical
    { method: 'GET', path: '/stock/candle', description: 'Get candles/OHLCV', params: [
      { name: 'symbol', type: 'query', required: true, description: 'Stock symbol' },
      { name: 'resolution', type: 'query', required: true, description: 'Resolution (1, 5, 15, 30, 60, D, W, M)', default: '1' },
      { name: 'from', type: 'query', required: true, description: 'Start timestamp (Unix)', default: '1705312200' },
      { name: 'to', type: 'query', required: true, description: 'End timestamp (Unix)', default: '1705335600' },
    ]},
    // Company Info
    { method: 'GET', path: '/stock/profile2', description: 'Company profile', params: [{ name: 'symbol', type: 'query', required: true, description: 'Stock symbol' }] },
    { method: 'GET', path: '/stock/peers', description: 'Company peers', params: [{ name: 'symbol', type: 'query', required: true, description: 'Stock symbol' }] },
    // News
    { method: 'GET', path: '/company-news', description: 'Company news', params: [
      { name: 'symbol', type: 'query', required: true, description: 'Stock symbol' },
      { name: 'from', type: 'query', required: false, description: 'From date (YYYY-MM-DD)' },
      { name: 'to', type: 'query', required: false, description: 'To date (YYYY-MM-DD)' },
    ]},
    { method: 'GET', path: '/news', description: 'Market news', params: [{ name: 'category', type: 'query', required: false, description: 'News category', default: 'general' }] },
    // Corporate Actions
    { method: 'GET', path: '/stock/dividend', description: 'Dividends', params: [{ name: 'symbol', type: 'query', required: true, description: 'Stock symbol' }] },
    { method: 'GET', path: '/stock/split', description: 'Stock splits', params: [{ name: 'symbol', type: 'query', required: true, description: 'Stock symbol' }] },
    // Analyst
    { method: 'GET', path: '/stock/recommendation', description: 'Analyst recommendations', params: [{ name: 'symbol', type: 'query', required: true, description: 'Stock symbol' }] },
    { method: 'GET', path: '/stock/price-target', description: 'Price targets', params: [{ name: 'symbol', type: 'query', required: true, description: 'Stock symbol' }] },
  ],
};
