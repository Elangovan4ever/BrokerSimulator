import { controlClient } from './client';
import type { Session, SessionConfig, Order, Position } from '@/types';

export const sessionsApi = {
  // Session Management
  async listSessions(): Promise<Session[]> {
    const response = await controlClient.get('/sessions');
    return response.data;
  },

  async getSession(sessionId: string): Promise<Session> {
    const response = await controlClient.get(`/sessions/${sessionId}`);
    return response.data;
  },

  async createSession(config: SessionConfig): Promise<Session> {
    const response = await controlClient.post('/sessions', config);
    return response.data;
  },

  async deleteSession(sessionId: string): Promise<void> {
    await controlClient.delete(`/sessions/${sessionId}`);
  },

  // Session Control
  async startSession(sessionId: string): Promise<Session> {
    const response = await controlClient.post(`/sessions/${sessionId}/start`);
    return response.data;
  },

  async pauseSession(sessionId: string): Promise<Session> {
    const response = await controlClient.post(`/sessions/${sessionId}/pause`);
    return response.data;
  },

  async resumeSession(sessionId: string): Promise<Session> {
    const response = await controlClient.post(`/sessions/${sessionId}/resume`);
    return response.data;
  },

  async stopSession(sessionId: string): Promise<Session> {
    const response = await controlClient.post(`/sessions/${sessionId}/stop`);
    return response.data;
  },

  // Time Control
  async setSpeed(sessionId: string, speedFactor: number): Promise<Session> {
    const response = await controlClient.post(`/sessions/${sessionId}/set_speed`, {
      speed_factor: speedFactor,
    });
    return response.data;
  },

  async jumpToTime(sessionId: string, timestamp: string): Promise<Session> {
    const response = await controlClient.post(`/sessions/${sessionId}/jump_to`, {
      timestamp,
    });
    return response.data;
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
