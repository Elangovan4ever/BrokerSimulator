import { controlClient } from './client';
import type { Session, SessionConfig, SessionStatus, Order, Position } from '@/types';

// Map server status (int) to UI status (string)
const statusMap: Record<number, SessionStatus> = {
  0: 'CREATED',
  1: 'RUNNING',
  2: 'PAUSED',
  3: 'STOPPED',
  4: 'COMPLETED',
  5: 'ERROR',
};

// Transform server session response to UI Session type
function transformSession(data: Record<string, unknown>): Session {
  return {
    id: data.id as string,
    status: statusMap[data.status as number] || 'CREATED',
    symbols: (data.symbols as string[]) || [],
    start_time: (data.start_time as string) || '',
    end_time: (data.end_time as string) || '',
    current_time: (data.current_time as string) || '',
    speed_factor: (data.speed_factor as number) || 0,
    initial_capital: (data.initial_capital as number) || (data.cash as number) || 100000,
    created_at: (data.created_at as string) || new Date().toISOString(),
    events_processed: (data.events_processed as number) || (data.queue_size as number) || 0,
    account: data.cash !== undefined ? {
      cash: data.cash as number,
      equity: data.equity as number,
      buying_power: (data.buying_power as number) || (data.cash as number) * 2,
      long_market_value: (data.long_market_value as number) || 0,
      short_market_value: (data.short_market_value as number) || 0,
      unrealized_pl: (data.unrealized_pl as number) || 0,
      realized_pl: (data.realized_pl as number) || 0,
    } : undefined,
  };
}

export const sessionsApi = {
  // Session Management
  async listSessions(): Promise<Session[]> {
    const response = await controlClient.get('/sessions');
    return (response.data as Record<string, unknown>[]).map(transformSession);
  },

  async getSession(sessionId: string): Promise<Session> {
    const response = await controlClient.get(`/sessions/${sessionId}`);
    return transformSession(response.data);
  },

  async createSession(config: SessionConfig): Promise<Session> {
    const response = await controlClient.post('/sessions', config);
    // Server returns {session_id: "...", status: "created"}, fetch full session
    const sessionId = response.data.session_id;
    const sessionResponse = await controlClient.get(`/sessions/${sessionId}`);
    return transformSession(sessionResponse.data);
  },

  async deleteSession(sessionId: string): Promise<void> {
    await controlClient.delete(`/sessions/${sessionId}`);
  },

  // Session Control
  async startSession(sessionId: string): Promise<Session> {
    await controlClient.post(`/sessions/${sessionId}/start`);
    // Fetch updated session after start
    return this.getSession(sessionId);
  },

  async pauseSession(sessionId: string): Promise<Session> {
    await controlClient.post(`/sessions/${sessionId}/pause`);
    return this.getSession(sessionId);
  },

  async resumeSession(sessionId: string): Promise<Session> {
    await controlClient.post(`/sessions/${sessionId}/resume`);
    return this.getSession(sessionId);
  },

  async stopSession(sessionId: string): Promise<Session> {
    await controlClient.post(`/sessions/${sessionId}/stop`);
    return this.getSession(sessionId);
  },

  // Time Control
  async setSpeed(sessionId: string, speedFactor: number): Promise<Session> {
    await controlClient.post(`/sessions/${sessionId}/speed`, {
      speed: speedFactor,
    });
    return this.getSession(sessionId);
  },

  async jumpToTime(sessionId: string, timestamp: string): Promise<Session> {
    await controlClient.post(`/sessions/${sessionId}/jump`, {
      timestamp,
    });
    return this.getSession(sessionId);
  },

  async getSessionTime(sessionId: string): Promise<{ current_time: string; speed_factor: number }> {
    const response = await controlClient.get(`/sessions/${sessionId}/time`);
    return response.data;
  },

  // Checkpoint
  async createCheckpoint(sessionId: string): Promise<void> {
    await controlClient.post(`/sessions/${sessionId}/checkpoint`);
  },

  // Orders (via session)
  async getOrders(sessionId: string): Promise<Order[]> {
    const response = await controlClient.get(`/sessions/${sessionId}/orders`);
    return response.data;
  },

  // Positions (via session)
  async getPositions(sessionId: string): Promise<Position[]> {
    const response = await controlClient.get(`/sessions/${sessionId}/positions`);
    return response.data;
  },
};
