import { create } from 'zustand';
import { AxiosError } from 'axios';
import { sessionsApi } from '@/api/sessions';
import type { Session, SessionConfig } from '@/types';

// Extract error message from axios error or generic error
function getErrorMessage(error: unknown): string {
  if (error instanceof AxiosError && error.response?.data?.error) {
    return error.response.data.error;
  }
  if (error instanceof Error) {
    return error.message;
  }
  return 'Unknown error';
}

interface SessionState {
  sessions: Session[];
  selectedSessionId: string | null;
  isLoading: boolean;
  error: string | null;

  // Actions
  fetchSessions: () => Promise<void>;
  fetchSession: (sessionId: string) => Promise<void>;
  createSession: (config: SessionConfig) => Promise<Session>;
  deleteSession: (sessionId: string) => Promise<void>;
  startSession: (sessionId: string) => Promise<void>;
  pauseSession: (sessionId: string) => Promise<void>;
  resumeSession: (sessionId: string) => Promise<void>;
  stopSession: (sessionId: string) => Promise<void>;
  setSpeed: (sessionId: string, speed: number) => Promise<void>;
  jumpToTime: (sessionId: string, timestamp: string) => Promise<void>;
  selectSession: (sessionId: string | null) => void;
  clearError: () => void;
}

export const useSessionStore = create<SessionState>((set, get) => ({
  sessions: [],
  selectedSessionId: null,
  isLoading: false,
  error: null,

  fetchSessions: async () => {
    set({ isLoading: true, error: null });
    try {
      const sessions = await sessionsApi.listSessions();
      set({ sessions, isLoading: false });
    } catch (error) {
      set({ error: getErrorMessage(error), isLoading: false });
    }
  },

  fetchSession: async (sessionId: string) => {
    try {
      const session = await sessionsApi.getSession(sessionId);
      set(state => ({
        sessions: state.sessions.map(s => s.id === sessionId ? session : s),
      }));
    } catch (error) {
      set({ error: getErrorMessage(error) });
    }
  },

  createSession: async (config: SessionConfig) => {
    set({ isLoading: true, error: null });
    try {
      const session = await sessionsApi.createSession(config);
      set(state => ({
        sessions: [...state.sessions, session],
        isLoading: false,
      }));
      return session;
    } catch (error) {
      set({ error: getErrorMessage(error), isLoading: false });
      throw error;
    }
  },

  deleteSession: async (sessionId: string) => {
    try {
      await sessionsApi.deleteSession(sessionId);
      set(state => ({
        sessions: state.sessions.filter(s => s.id !== sessionId),
        selectedSessionId: state.selectedSessionId === sessionId ? null : state.selectedSessionId,
      }));
    } catch (error) {
      set({ error: getErrorMessage(error) });
    }
  },

  startSession: async (sessionId: string) => {
    try {
      const session = await sessionsApi.startSession(sessionId);
      set(state => ({
        sessions: state.sessions.map(s => s.id === sessionId ? session : s),
      }));
    } catch (error) {
      set({ error: getErrorMessage(error) });
    }
  },

  pauseSession: async (sessionId: string) => {
    try {
      const session = await sessionsApi.pauseSession(sessionId);
      set(state => ({
        sessions: state.sessions.map(s => s.id === sessionId ? session : s),
      }));
    } catch (error) {
      set({ error: getErrorMessage(error) });
    }
  },

  resumeSession: async (sessionId: string) => {
    try {
      const session = await sessionsApi.resumeSession(sessionId);
      set(state => ({
        sessions: state.sessions.map(s => s.id === sessionId ? session : s),
      }));
    } catch (error) {
      set({ error: getErrorMessage(error) });
    }
  },

  stopSession: async (sessionId: string) => {
    try {
      const session = await sessionsApi.stopSession(sessionId);
      set(state => ({
        sessions: state.sessions.map(s => s.id === sessionId ? session : s),
      }));
    } catch (error) {
      set({ error: getErrorMessage(error) });
    }
  },

  setSpeed: async (sessionId: string, speed: number) => {
    try {
      const session = await sessionsApi.setSpeed(sessionId, speed);
      set(state => ({
        sessions: state.sessions.map(s => s.id === sessionId ? session : s),
      }));
    } catch (error) {
      set({ error: getErrorMessage(error) });
    }
  },

  jumpToTime: async (sessionId: string, timestamp: string) => {
    try {
      const session = await sessionsApi.jumpToTime(sessionId, timestamp);
      set(state => ({
        sessions: state.sessions.map(s => s.id === sessionId ? session : s),
      }));
    } catch (error) {
      set({ error: getErrorMessage(error) });
    }
  },

  selectSession: (sessionId: string | null) => {
    set({ selectedSessionId: sessionId });
  },

  clearError: () => {
    set({ error: null });
  },
}));
