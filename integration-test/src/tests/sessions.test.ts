/**
 * Integration tests for Control Session APIs.
 */

import axios, { AxiosInstance } from 'axios';
import { config } from './setup';
import { SessionManager } from '../utils/session-manager';

const statusMap: Record<string, number> = {
  CREATED: 0,
  RUNNING: 1,
  PAUSED: 2,
  STOPPED: 3,
  COMPLETED: 4,
  ERROR: 5,
};

function normalizeStatus(status: unknown): number | null {
  if (typeof status === 'number') {
    return status;
  }
  if (typeof status === 'string' && status in statusMap) {
    return statusMap[status];
  }
  return null;
}

describe('Control Sessions API', () => {
  let manager: SessionManager;
  let rawClient: AxiosInstance;
  const createdSessionIds: string[] = [];
  const testSymbol = config.testSymbols[0] || 'AAPL';
  const startTime = `${config.testStartDate}T09:30:00`;
  const endTime = `${config.testStartDate}T16:00:00`;

  const registerSession = (sessionId: string): string => {
    createdSessionIds.push(sessionId);
    return sessionId;
  };

  const createSession = async (speedFactor = 1): Promise<string> => {
    const sessionId = await manager.createSession({
      symbols: [testSymbol],
      start_time: startTime,
      end_time: endTime,
      initial_capital: 100000,
      speed_factor: speedFactor,
    });
    return registerSession(sessionId);
  };

  const createRunningSession = async (speedFactor = 1): Promise<string> => {
    const sessionId = await createSession(speedFactor);
    await manager.startSession(sessionId);
    await manager.waitForReady(sessionId, 30000);
    return sessionId;
  };

  beforeAll(async () => {
    manager = new SessionManager(config.simulatorHost, config.controlPort);
    rawClient = axios.create({
      baseURL: `http://${config.simulatorHost}:${config.controlPort}`,
      timeout: 30000,
      headers: {
        'Content-Type': 'application/json',
      },
    });
  }, 30000);

  afterEach(async () => {
    while (createdSessionIds.length > 0) {
      const id = createdSessionIds.pop();
      if (!id) {
        continue;
      }
      try {
        await manager.stopSession(id);
      } catch (error) {
        console.log(`Cleanup warning (stop ${id}):`, error);
      }
      try {
        await manager.deleteSession(id);
      } catch (error) {
        console.log(`Cleanup warning (delete ${id}):`, error);
      }
    }
  }, 60000);

  it('returns 404 for unknown session id', async () => {
    const missingId = '0000000000000000000000000000000000000000000000000000000000000000';
    let status: number | undefined;
    try {
      await rawClient.get(`/sessions/${missingId}`);
    } catch (error) {
      if (axios.isAxiosError(error)) {
        status = error.response?.status;
      } else {
        throw error;
      }
    }
    expect(status).toBe(404);
  }, 20000);

  it('rejects starting an already started session', async () => {
    const sessionId = await createRunningSession(1);
    let status: number | undefined;
    try {
      await rawClient.post(`/sessions/${sessionId}/start`);
    } catch (error) {
      if (axios.isAxiosError(error)) {
        status = error.response?.status;
      } else {
        throw error;
      }
    }
    expect(status).toBe(400);
  }, 30000);

  it('treats pause/resume as idempotent operations', async () => {
    const sessionId = await createRunningSession(1);
    await manager.pauseSession(sessionId);
    const pausedOnce = await manager.getSession(sessionId);
    expect(normalizeStatus(pausedOnce.status)).toBe(2);

    await manager.pauseSession(sessionId);
    const pausedTwice = await manager.getSession(sessionId);
    expect(normalizeStatus(pausedTwice.status)).toBe(2);

    await manager.resumeSession(sessionId);
    const resumedOnce = await manager.getSession(sessionId);
    expect(normalizeStatus(resumedOnce.status)).toBe(1);

    await manager.resumeSession(sessionId);
    const resumedTwice = await manager.getSession(sessionId);
    expect(normalizeStatus(resumedTwice.status)).toBe(1);
  }, 40000);

  it('updates speed when running and paused', async () => {
    const sessionId = await createRunningSession(1);
    await manager.setSpeed(10, sessionId);
    const session = await manager.getSession(sessionId);
    expect(session.speed_factor).toBeCloseTo(10, 4);

    const runningTimeInfo = await manager.getSessionTime(sessionId);
    expect(runningTimeInfo.speed_factor).toBeCloseTo(10, 4);

    await manager.pauseSession(sessionId);
    await manager.setSpeed(4, sessionId);

    const pausedSession = await manager.getSession(sessionId);
    expect(normalizeStatus(pausedSession.status)).toBe(2);
    expect(pausedSession.speed_factor).toBeCloseTo(4, 4);

    const pausedTimeInfo = await manager.getSessionTime(sessionId);
    expect(pausedTimeInfo.speed_factor).toBeCloseTo(4, 4);
  }, 40000);

  it('keeps session stopped after jump', async () => {
    const sessionId = await createRunningSession(1);
    await manager.stopSession(sessionId);
    const stopped = await manager.getSession(sessionId);
    expect(normalizeStatus(stopped.status)).toBe(3);

    const targetTime = `${config.testStartDate}T10:00:00`;
    await manager.jumpTo(targetTime, sessionId);
    const jumped = await manager.getSession(sessionId);
    expect(normalizeStatus(jumped.status)).toBe(3);
    expect(jumped.current_time).toBe(`${targetTime}Z`);
  }, 40000);

  it('isolates state across multiple sessions', async () => {
    const sessionA = await createRunningSession(1);
    const sessionB = await createRunningSession(1);

    await manager.setSpeed(6, sessionA);
    const sessionAState = await manager.getSession(sessionA);
    const sessionBState = await manager.getSession(sessionB);
    expect(sessionAState.speed_factor).toBeCloseTo(6, 4);
    expect(sessionBState.speed_factor).toBeCloseTo(1, 4);

    await manager.pauseSession(sessionA);
    const pausedA = await manager.getSession(sessionA);
    const stillRunningB = await manager.getSession(sessionB);
    expect(normalizeStatus(pausedA.status)).toBe(2);
    expect(normalizeStatus(stillRunningB.status)).toBe(1);
  }, 60000);
});
