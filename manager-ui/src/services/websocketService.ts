import { getWsUrl } from '@/api/config';
import type { ApiService, WebSocketConnection, WebSocketMessage } from '@/types';
import { generateId } from '@/lib/utils';

type MessageHandler = (message: WebSocketMessage) => void;
type ConnectionHandler = (connection: WebSocketConnection) => void;

class WebSocketService {
  private connections: Map<string, WebSocket> = new Map();
  private connectionStates: Map<string, WebSocketConnection> = new Map();
  private messageHandlers: Set<MessageHandler> = new Set();
  private connectionHandlers: Set<ConnectionHandler> = new Set();
  private reconnectTimeouts: Map<string, NodeJS.Timeout> = new Map();

  // Register handlers
  onMessage(handler: MessageHandler): () => void {
    this.messageHandlers.add(handler);
    return () => this.messageHandlers.delete(handler);
  }

  onConnectionChange(handler: ConnectionHandler): () => void {
    this.connectionHandlers.add(handler);
    return () => this.connectionHandlers.delete(handler);
  }

  // Connect to a WebSocket endpoint
  connect(service: ApiService, customUrl?: string): string {
    const connectionId = generateId();
    const url = customUrl || getWsUrl(service);

    const connectionState: WebSocketConnection = {
      id: connectionId,
      url,
      status: 'connecting',
      service,
      subscriptions: [],
      messageCount: 0,
    };

    this.connectionStates.set(connectionId, connectionState);
    this.notifyConnectionChange(connectionState);

    try {
      const ws = new WebSocket(url);

      ws.onopen = () => {
        const state = this.connectionStates.get(connectionId);
        if (state) {
          state.status = 'connected';
          state.connectedAt = new Date().toISOString();
          this.notifyConnectionChange(state);
        }
        console.log(`🔌 WebSocket connected: ${service} (${connectionId})`);
      };

      ws.onmessage = (event) => {
        const state = this.connectionStates.get(connectionId);
        if (state) {
          state.messageCount++;
        }

        let data: unknown;
        try {
          data = JSON.parse(event.data);
        } catch {
          data = event.data;
        }

        // Check for session link confirmation
        if (state && typeof data === 'object' && data !== null) {
          const resp = data as Record<string, unknown>;
          if (resp.status === 'session_set' && resp.session_id) {
            state.linkedSessionId = resp.session_id as string;
            this.notifyConnectionChange(state);
          } else if (resp.status === 'session_unlinked') {
            state.linkedSessionId = undefined;
            this.notifyConnectionChange(state);
          }
        }

        const message: WebSocketMessage = {
          id: generateId(),
          connectionId,
          type: 'received',
          data,
          timestamp: new Date().toISOString(),
        };

        this.notifyMessage(message);
      };

      ws.onerror = (error) => {
        console.error(`❌ WebSocket error: ${service} (${connectionId})`, error);
        const state = this.connectionStates.get(connectionId);
        if (state) {
          state.status = 'error';
          this.notifyConnectionChange(state);
        }
      };

      ws.onclose = () => {
        console.log(`🔌 WebSocket disconnected: ${service} (${connectionId})`);
        const state = this.connectionStates.get(connectionId);
        if (state) {
          state.status = 'disconnected';
          this.notifyConnectionChange(state);
        }
      };

      this.connections.set(connectionId, ws);
    } catch (error) {
      console.error(`❌ Failed to create WebSocket: ${service}`, error);
      connectionState.status = 'error';
      this.notifyConnectionChange(connectionState);
    }

    return connectionId;
  }

  // Send a message
  send(connectionId: string, data: unknown): boolean {
    const ws = this.connections.get(connectionId);
    if (!ws || ws.readyState !== WebSocket.OPEN) {
      console.error(`❌ Cannot send: WebSocket not connected (${connectionId})`);
      return false;
    }

    const payload = typeof data === 'string' ? data : JSON.stringify(data);
    ws.send(payload);

    const message: WebSocketMessage = {
      id: generateId(),
      connectionId,
      type: 'sent',
      data,
      timestamp: new Date().toISOString(),
    };

    this.notifyMessage(message);
    return true;
  }

  // Subscribe to a stream (Alpaca/Polygon style)
  subscribe(connectionId: string, channels: string[]): boolean {
    const state = this.connectionStates.get(connectionId);
    if (!state) return false;

    // Build subscription message based on service
    let subscribeMessage: unknown;

    switch (state.service) {
      case 'alpaca':
        subscribeMessage = {
          action: 'subscribe',
          trades: channels.filter(c => c.startsWith('T.')).map(c => c.slice(2)),
          quotes: channels.filter(c => c.startsWith('Q.')).map(c => c.slice(2)),
          bars: channels.filter(c => c.startsWith('B.')).map(c => c.slice(2)),
        };
        break;
      case 'polygon':
        subscribeMessage = {
          action: 'subscribe',
          params: channels.join(','),
        };
        break;
      case 'finnhub':
        // Finnhub subscribes one at a time
        channels.forEach(symbol => {
          this.send(connectionId, { type: 'subscribe', symbol });
        });
        state.subscriptions.push(...channels);
        this.notifyConnectionChange(state);
        return true;
      default:
        subscribeMessage = { action: 'subscribe', channels };
    }

    const success = this.send(connectionId, subscribeMessage);
    if (success) {
      state.subscriptions.push(...channels);
      this.notifyConnectionChange(state);
    }
    return success;
  }

  // Unsubscribe from a stream
  unsubscribe(connectionId: string, channels: string[]): boolean {
    const state = this.connectionStates.get(connectionId);
    if (!state) return false;

    let unsubscribeMessage: unknown;

    switch (state.service) {
      case 'alpaca':
        unsubscribeMessage = {
          action: 'unsubscribe',
          trades: channels.filter(c => c.startsWith('T.')).map(c => c.slice(2)),
          quotes: channels.filter(c => c.startsWith('Q.')).map(c => c.slice(2)),
        };
        break;
      case 'polygon':
        unsubscribeMessage = {
          action: 'unsubscribe',
          params: channels.join(','),
        };
        break;
      case 'finnhub':
        channels.forEach(symbol => {
          this.send(connectionId, { type: 'unsubscribe', symbol });
        });
        state.subscriptions = state.subscriptions.filter(s => !channels.includes(s));
        this.notifyConnectionChange(state);
        return true;
      default:
        unsubscribeMessage = { action: 'unsubscribe', channels };
    }

    const success = this.send(connectionId, unsubscribeMessage);
    if (success) {
      state.subscriptions = state.subscriptions.filter(s => !channels.includes(s));
      this.notifyConnectionChange(state);
    }
    return success;
  }

  // Authenticate (Alpaca/Polygon style)
  authenticate(connectionId: string, apiKey: string, apiSecret?: string): boolean {
    const state = this.connectionStates.get(connectionId);
    if (!state) return false;

    let authMessage: unknown;

    switch (state.service) {
      case 'alpaca':
        authMessage = {
          action: 'auth',
          key: apiKey,
          secret: apiSecret || '',
        };
        break;
      case 'polygon':
        authMessage = {
          action: 'auth',
          params: apiKey,
        };
        break;
      default:
        authMessage = { action: 'auth', key: apiKey };
    }

    return this.send(connectionId, authMessage);
  }

  // Link connection to a session
  linkSession(connectionId: string, sessionId: string): boolean {
    const state = this.connectionStates.get(connectionId);
    if (!state) return false;

    // Send set_session action to link
    return this.send(connectionId, { action: 'set_session', session_id: sessionId });
  }

  // Unlink connection from session
  unlinkSession(connectionId: string): boolean {
    const state = this.connectionStates.get(connectionId);
    if (!state) return false;

    // Clear local state immediately
    state.linkedSessionId = undefined;
    this.notifyConnectionChange(state);

    // Send unlink message
    return this.send(connectionId, { action: 'unset_session' });
  }

  // Check if connection is linked to a session
  isLinkedToSession(connectionId: string): boolean {
    const state = this.connectionStates.get(connectionId);
    return !!state?.linkedSessionId;
  }

  // Get linked session ID
  getLinkedSessionId(connectionId: string): string | undefined {
    return this.connectionStates.get(connectionId)?.linkedSessionId;
  }

  // Disconnect
  disconnect(connectionId: string): void {
    const ws = this.connections.get(connectionId);
    if (ws) {
      ws.close();
      this.connections.delete(connectionId);
    }

    const timeout = this.reconnectTimeouts.get(connectionId);
    if (timeout) {
      clearTimeout(timeout);
      this.reconnectTimeouts.delete(connectionId);
    }

    this.connectionStates.delete(connectionId);
    console.log(`🔌 WebSocket closed: ${connectionId}`);
  }

  // Disconnect all
  disconnectAll(): void {
    for (const connectionId of this.connections.keys()) {
      this.disconnect(connectionId);
    }
  }

  // Get connection state
  getConnection(connectionId: string): WebSocketConnection | undefined {
    return this.connectionStates.get(connectionId);
  }

  // Get all connections
  getAllConnections(): WebSocketConnection[] {
    return Array.from(this.connectionStates.values());
  }

  // Private helpers
  private notifyMessage(message: WebSocketMessage): void {
    this.messageHandlers.forEach(handler => handler(message));
  }

  private notifyConnectionChange(connection: WebSocketConnection): void {
    this.connectionHandlers.forEach(handler => handler(connection));
  }
}

// Singleton instance
export const wsService = new WebSocketService();
