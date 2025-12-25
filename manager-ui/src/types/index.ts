// Session Types
export type SessionStatus = 'CREATED' | 'RUNNING' | 'PAUSED' | 'STOPPED' | 'COMPLETED' | 'ERROR';

export interface Session {
  id: string;
  status: SessionStatus;
  symbols: string[];
  start_time: string;
  end_time: string;
  current_time: string;
  speed_factor: number;
  initial_capital: number;
  created_at: string;
  events_processed: number;
  account?: AccountState;
  performance?: PerformanceMetrics;
}

export interface SessionConfig {
  symbols: string[];
  start_time: string;
  end_time: string;
  initial_capital: number;
  speed_factor?: number;
}

export interface AccountState {
  cash: number;
  equity: number;
  buying_power: number;
  long_market_value: number;
  short_market_value: number;
  unrealized_pl: number;
  realized_pl: number;
}

export interface Position {
  symbol: string;
  qty: number;
  avg_entry_price: number;
  market_value: number;
  cost_basis: number;
  unrealized_pl: number;
  unrealized_plpc: number;
  current_price: number;
  side: 'long' | 'short';
}

export interface Order {
  id: string;
  client_order_id: string;
  symbol: string;
  qty: number;
  filled_qty: number;
  side: 'buy' | 'sell';
  type: 'market' | 'limit' | 'stop' | 'stop_limit' | 'trailing_stop';
  time_in_force: 'day' | 'gtc' | 'ioc' | 'fok' | 'opg' | 'cls';
  limit_price?: number;
  stop_price?: number;
  trail_price?: number;
  trail_percent?: number;
  status: string;
  filled_avg_price?: number;
  created_at: string;
  updated_at: string;
}

export interface PerformanceMetrics {
  total_return: number;
  max_drawdown: number;
  sharpe_ratio: number;
  win_rate: number;
  total_trades: number;
}

// API Explorer Types
export type ApiService = 'control' | 'alpaca' | 'polygon' | 'finnhub';
export type HttpMethod = 'GET' | 'POST' | 'PUT' | 'PATCH' | 'DELETE';

export interface ApiEndpoint {
  method: HttpMethod;
  path: string;
  description: string;
  params?: ApiParam[];
  body?: Record<string, unknown>;
}

export interface ApiParam {
  name: string;
  type: 'path' | 'query' | 'body';
  required: boolean;
  description: string;
  default?: string;
}

export interface ApiResponse {
  status: number;
  statusText: string;
  data: unknown;
  duration: number;
  timestamp: string;
}

// WebSocket Types
export interface WebSocketConnection {
  id: string;
  url: string;
  status: 'connecting' | 'connected' | 'disconnected' | 'error';
  service: ApiService;
  subscriptions: string[];
  messageCount: number;
  connectedAt?: string;
  linkedSessionId?: string;  // Session ID this connection is linked to
}

export interface WebSocketMessage {
  id: string;
  connectionId: string;
  type: 'sent' | 'received';
  data: unknown;
  timestamp: string;
}

// Simulator Status
export interface SimulatorStatus {
  service: ApiService;
  host: string;
  port: number;
  status: 'online' | 'offline' | 'error';
  latency?: number;
  lastChecked: string;
}

// Config
export interface SimulatorConfig {
  host: string;
  controlPort: number;
  alpacaPort: number;
  polygonPort: number;
  finnhubPort: number;
  wsPort: number;
}
