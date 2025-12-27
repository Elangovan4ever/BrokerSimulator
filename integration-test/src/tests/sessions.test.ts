/**
 * Integration tests for Control Session APIs.
 */

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
  let sessionId = '';

  beforeAll(async () => {
    manager = new SessionManager(config.simulatorHost, config.controlPort);
    sessionId = await manager.createSession({
      symbols: [config.testSymbols[0] || 'AAPL'],
      start_time: `${config.testStartDate}T09:30:00`,
      end_time: `${config.testStartDate}T16:00:00`,
      initial_capital: 100000,
      speed_factor: 1,
    });
    await manager.startSession(sessionId);
    await manager.waitForReady(sessionId, 30000);
  }, 60000);

  afterAll(async () => {
    if (sessionId) {
      await manager.cleanup();
    }
  }, 30000);

  it('should manage session lifecycle and time control', async () => {
    const sessions = await manager.listSessions();
    const sessionIds = sessions.map((s) => (s as { id?: string; session_id?: string }).id ?? (s as { session_id?: string }).session_id);
    expect(sessionIds).toContain(sessionId);

    const session = await manager.getSession(sessionId);
    expect(normalizeStatus(session.status)).toBe(1);

    await manager.setSpeed(10, sessionId);
    const updatedSession = await manager.getSession(sessionId);
    expect(updatedSession.speed_factor).toBeCloseTo(10, 4);

    const timeInfo = await manager.getSessionTime(sessionId);
    expect(timeInfo.speed_factor).toBeCloseTo(10, 4);
    expect(typeof timeInfo.current_time).toBe('string');

    await manager.pauseSession(sessionId);
    const pausedSession = await manager.getSession(sessionId);
    expect(normalizeStatus(pausedSession.status)).toBe(2);

    const targetTime = `${config.testStartDate}T10:00:00`;
    await manager.jumpTo(targetTime, sessionId);
    const jumpedSession = await manager.getSession(sessionId);
    expect(normalizeStatus(jumpedSession.status)).toBe(2);
    expect(jumpedSession.current_time).toBe(`${targetTime}Z`);

    await manager.resumeSession(sessionId);
    const resumedSession = await manager.getSession(sessionId);
    expect(normalizeStatus(resumedSession.status)).toBe(1);

    await manager.stopSession(sessionId);
    const stoppedSession = await manager.getSession(sessionId);
    expect(normalizeStatus(stoppedSession.status)).toBe(3);
  }, 60000);
});
