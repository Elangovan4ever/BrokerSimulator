/**
 * Integration tests for Alpaca Trading API endpoints.
 */

import { config } from './setup';
import { SessionManager } from '../utils/session-manager';
import { AlpacaSimulatorClient } from '../clients/alpaca-simulator-client';

describe('Alpaca Trading API', () => {
  const testSymbol = config.testSymbols[0] || 'AAPL';
  const startTime = `${config.testStartDate}T09:30:00`;
  const endTime = `${config.testStartDate}T16:00:00`;

  let sessionManager: SessionManager;
  let sessionId: string;
  let alpacaClient: AlpacaSimulatorClient;

  beforeAll(async () => {
    sessionManager = new SessionManager(config.simulatorHost, config.controlPort);
    sessionId = await sessionManager.createSession({
      symbols: [testSymbol],
      start_time: startTime,
      end_time: endTime,
      initial_capital: 100000,
      speed_factor: 0,
    });
    await sessionManager.startSession(sessionId);
    await sessionManager.waitForWarmup(sessionId, 1, 30000);

    alpacaClient = new AlpacaSimulatorClient({
      host: config.simulatorHost,
      port: config.alpacaPort,
      sessionId,
    });
  }, 60000);

  afterEach(async () => {
    const response = await alpacaClient.cancelAllOrders();
    if (response.status !== 200) {
      console.log('Cleanup warning: failed to cancel open orders');
    }
  }, 20000);

  afterAll(async () => {
    try {
      await sessionManager.stopSession(sessionId);
    } catch (error) {
      console.log('Cleanup warning: failed to stop session', error);
    }
    try {
      await sessionManager.deleteSession(sessionId);
    } catch (error) {
      console.log('Cleanup warning: failed to delete session', error);
    }
  }, 30000);

  it('returns account data', async () => {
    const response = await alpacaClient.getAccount();
    expect(response.status).toBe(200);
    expect(response.data).toEqual(
      expect.objectContaining({
        id: sessionId,
        account_number: sessionId,
        status: 'ACTIVE',
        currency: 'USD',
      })
    );
    expect(typeof response.data.cash).toBe('string');
    expect(typeof response.data.buying_power).toBe('string');
  }, 20000);

  it('returns empty positions and 404 for missing position', async () => {
    const listResponse = await alpacaClient.listPositions();
    expect(listResponse.status).toBe(200);
    expect(Array.isArray(listResponse.data)).toBe(true);
    expect(listResponse.data.length).toBe(0);

    const getResponse = await alpacaClient.getPosition(testSymbol);
    expect(getResponse.status).toBe(404);

    const closeResponse = await alpacaClient.closePosition(testSymbol);
    expect(closeResponse.status).toBe(404);

    const closeAllResponse = await alpacaClient.closeAllPositions();
    expect(closeAllResponse.status).toBe(200);
    expect(Array.isArray(closeAllResponse.data)).toBe(true);
    expect(closeAllResponse.data.length).toBe(0);
  }, 30000);

  it('creates, fetches, replaces, and cancels orders', async () => {
    const createResponse = await alpacaClient.submitOrder({
      symbol: testSymbol,
      qty: 1,
      side: 'buy',
      type: 'limit',
      limit_price: 0.01,
      time_in_force: 'day',
    });

    expect(createResponse.status).toBe(200);
    const orderId = createResponse.data.id as string;
    const clientOrderId = createResponse.data.client_order_id as string;
    expect(orderId).toBeTruthy();
    expect(clientOrderId).toBeTruthy();

    const listOpen = await alpacaClient.listOrders({ status: 'open' });
    expect(listOpen.status).toBe(200);
    const openIds = (listOpen.data as Array<{ id?: string }>).map((order) => order.id);
    expect(openIds).toContain(orderId);

    const getResponse = await alpacaClient.getOrder(orderId);
    expect(getResponse.status).toBe(200);
    expect(getResponse.data.id).toBe(orderId);

    const getByClient = await alpacaClient.getOrderByClientId(clientOrderId);
    expect(getByClient.status).toBe(200);
    expect(getByClient.data.id).toBe(orderId);

    const replaceResponse = await alpacaClient.replaceOrder(orderId, {
      qty: 2,
      limit_price: 0.02,
      time_in_force: 'day',
    });
    expect(replaceResponse.status).toBe(200);
    const replacedId = replaceResponse.data.id as string;
    expect(replacedId).toBeTruthy();
    expect(replacedId).not.toBe(orderId);

    const cancelResponse = await alpacaClient.cancelOrder(replacedId);
    expect(cancelResponse.status).toBe(204);

    const closedList = await alpacaClient.listOrders({ status: 'closed' });
    expect(closedList.status).toBe(200);
    const closedIds = (closedList.data as Array<{ id?: string }>).map((order) => order.id);
    expect(closedIds).toContain(replacedId);
  }, 40000);

  it('rejects notional orders', async () => {
    const response = await alpacaClient.submitOrder({
      symbol: testSymbol,
      notional: 100,
      side: 'buy',
      type: 'market',
      time_in_force: 'day',
    });
    expect(response.status).toBe(400);
    expect(response.data).toEqual(
      expect.objectContaining({
        code: 400,
      })
    );
  }, 20000);
});
