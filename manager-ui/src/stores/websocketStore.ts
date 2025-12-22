import { create } from 'zustand';
import { wsService } from '@/services/websocketService';
import type { WebSocketConnection, WebSocketMessage, ApiService } from '@/types';

interface WebSocketState {
  connections: WebSocketConnection[];
  messages: WebSocketMessage[];
  maxMessages: number;

  // Actions
  connect: (service: ApiService, customUrl?: string) => string;
  disconnect: (connectionId: string) => void;
  disconnectAll: () => void;
  send: (connectionId: string, data: unknown) => boolean;
  subscribe: (connectionId: string, channels: string[]) => boolean;
  unsubscribe: (connectionId: string, channels: string[]) => boolean;
  authenticate: (connectionId: string, apiKey: string, apiSecret?: string) => boolean;
  clearMessages: (connectionId?: string) => void;
  setMaxMessages: (max: number) => void;
}

export const useWebSocketStore = create<WebSocketState>((set, get) => {
  // Set up listeners
  wsService.onConnectionChange((connection) => {
    set(state => {
      const existing = state.connections.find(c => c.id === connection.id);
      if (existing) {
        return {
          connections: state.connections.map(c =>
            c.id === connection.id ? connection : c
          ),
        };
      } else {
        return {
          connections: [...state.connections, connection],
        };
      }
    });
  });

  wsService.onMessage((message) => {
    set(state => {
      const messages = [message, ...state.messages].slice(0, state.maxMessages);
      return { messages };
    });
  });

  return {
    connections: [],
    messages: [],
    maxMessages: 1000,

    connect: (service: ApiService, customUrl?: string) => {
      return wsService.connect(service, customUrl);
    },

    disconnect: (connectionId: string) => {
      wsService.disconnect(connectionId);
      set(state => ({
        connections: state.connections.filter(c => c.id !== connectionId),
      }));
    },

    disconnectAll: () => {
      wsService.disconnectAll();
      set({ connections: [] });
    },

    send: (connectionId: string, data: unknown) => {
      return wsService.send(connectionId, data);
    },

    subscribe: (connectionId: string, channels: string[]) => {
      return wsService.subscribe(connectionId, channels);
    },

    unsubscribe: (connectionId: string, channels: string[]) => {
      return wsService.unsubscribe(connectionId, channels);
    },

    authenticate: (connectionId: string, apiKey: string, apiSecret?: string) => {
      return wsService.authenticate(connectionId, apiKey, apiSecret);
    },

    clearMessages: (connectionId?: string) => {
      if (connectionId) {
        set(state => ({
          messages: state.messages.filter(m => m.connectionId !== connectionId),
        }));
      } else {
        set({ messages: [] });
      }
    },

    setMaxMessages: (max: number) => {
      set({ maxMessages: max });
    },
  };
});
