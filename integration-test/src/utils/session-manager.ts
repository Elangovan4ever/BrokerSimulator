/**
 * Session Manager for integration tests
 * Handles creating, starting, stopping, and deleting sessions
 */

import axios, { AxiosInstance } from 'axios';

export interface SessionConfig {
  symbols: string[];
  start_time: string;  // ISO format
  end_time: string;    // ISO format
  initial_capital?: number;
  speed_factor?: number;
}

export interface Session {
  session_id: string;
  status: string;
  symbols: string[];
  start_time: string;
  end_time: string;
}

export class SessionManager {
  private client: AxiosInstance;
  private activeSessionId: string | null = null;

  constructor(host: string, port: number) {
    this.client = axios.create({
      baseURL: `http://${host}:${port}`,
      timeout: 30000,
      headers: {
        'Content-Type': 'application/json',
      },
    });
  }

  /**
   * Create a new simulation session
   */
  async createSession(config: SessionConfig): Promise<string> {
    try {
      const response = await this.client.post('/sessions', {
        symbols: config.symbols,
        start_time: config.start_time,
        end_time: config.end_time,
        initial_capital: config.initial_capital || 100000,
        speed_factor: config.speed_factor || 0,
      });

      const sessionId = response.data.session_id;
      this.activeSessionId = sessionId;
      console.log(`Created session: ${sessionId}`);
      return sessionId;
    } catch (error) {
      if (axios.isAxiosError(error)) {
        throw new Error(`Failed to create session: ${error.response?.data?.error || error.message}`);
      }
      throw error;
    }
  }

  /**
   * Start a session
   */
  async startSession(sessionId?: string): Promise<void> {
    const id = sessionId || this.activeSessionId;
    if (!id) {
      throw new Error('No session ID provided and no active session');
    }

    try {
      await this.client.post(`/sessions/${id}/start`);
      console.log(`Started session: ${id}`);
    } catch (error) {
      if (axios.isAxiosError(error)) {
        throw new Error(`Failed to start session: ${error.response?.data?.error || error.message}`);
      }
      throw error;
    }
  }

  /**
   * Stop a session
   */
  async stopSession(sessionId?: string): Promise<void> {
    const id = sessionId || this.activeSessionId;
    if (!id) {
      throw new Error('No session ID provided and no active session');
    }

    try {
      await this.client.post(`/sessions/${id}/stop`);
      console.log(`Stopped session: ${id}`);
    } catch (error) {
      if (axios.isAxiosError(error)) {
        // Session might already be stopped
        console.log(`Note: Stop session returned: ${error.response?.data?.error || error.message}`);
      } else {
        throw error;
      }
    }
  }

  /**
   * Delete a session
   */
  async deleteSession(sessionId?: string): Promise<void> {
    const id = sessionId || this.activeSessionId;
    if (!id) {
      throw new Error('No session ID provided and no active session');
    }

    try {
      await this.client.delete(`/sessions/${id}`);
      console.log(`Deleted session: ${id}`);
      if (id === this.activeSessionId) {
        this.activeSessionId = null;
      }
    } catch (error) {
      if (axios.isAxiosError(error)) {
        throw new Error(`Failed to delete session: ${error.response?.data?.error || error.message}`);
      }
      throw error;
    }
  }

  /**
   * Get session status
   */
  async getSession(sessionId?: string): Promise<Session> {
    const id = sessionId || this.activeSessionId;
    if (!id) {
      throw new Error('No session ID provided and no active session');
    }

    try {
      const response = await this.client.get(`/sessions/${id}`);
      return response.data;
    } catch (error) {
      if (axios.isAxiosError(error)) {
        throw new Error(`Failed to get session: ${error.response?.data?.error || error.message}`);
      }
      throw error;
    }
  }

  /**
   * List all sessions
   */
  async listSessions(): Promise<Session[]> {
    try {
      const response = await this.client.get('/sessions');
      return response.data.sessions || response.data;
    } catch (error) {
      if (axios.isAxiosError(error)) {
        throw new Error(`Failed to list sessions: ${error.response?.data?.error || error.message}`);
      }
      throw error;
    }
  }

  /**
   * Wait for session to be ready (status is RUNNING)
   */
  async waitForReady(sessionId?: string, maxWaitMs = 30000): Promise<void> {
    const id = sessionId || this.activeSessionId;
    if (!id) {
      throw new Error('No session ID provided and no active session');
    }

    const startTime = Date.now();
    while (Date.now() - startTime < maxWaitMs) {
      try {
        const session = await this.getSession(id);
        if (session.status === 'RUNNING' || session.status === 'PAUSED') {
          console.log(`Session ${id} is ready (status: ${session.status})`);
          return;
        }
        if (session.status === 'ERROR') {
          throw new Error(`Session ${id} is in ERROR state`);
        }
      } catch (error) {
        // Ignore errors during polling
      }
      await this.sleep(500);
    }
    throw new Error(`Timeout waiting for session ${id} to be ready`);
  }

  /**
   * Get the active session ID
   */
  getActiveSessionId(): string | null {
    return this.activeSessionId;
  }

  /**
   * Clean up - stop and delete the active session
   */
  async cleanup(): Promise<void> {
    if (this.activeSessionId) {
      try {
        await this.stopSession();
      } catch (e) {
        console.log('Note: Error stopping session during cleanup:', e);
      }
      try {
        await this.deleteSession();
      } catch (e) {
        console.log('Note: Error deleting session during cleanup:', e);
      }
    }
  }

  private sleep(ms: number): Promise<void> {
    return new Promise(resolve => setTimeout(resolve, ms));
  }
}
