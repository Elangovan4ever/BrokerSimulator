/**
 * Integration tests for Alpaca Trading API endpoints.
 */

import { config, logTestResult } from './setup';
import { SessionManager } from '../utils/session-manager';
import { AlpacaSimulatorClient } from '../clients/alpaca-simulator-client';
import { AlpacaClient } from '../clients/alpaca-client';
import { extractSchema, compareSchemas, formatComparisonResult } from '../utils/schema-compare';

describe('Alpaca Trading API', () => {
  const testSymbol = config.testSymbols[0] || 'AAPL';
  const startTime = `${config.testStartDate}T09:30:00`;
  const endTime = `${config.testStartDate}T16:00:00`;

  let sessionManager: SessionManager;
  let sessionId: string;
  let alpacaClient: AlpacaSimulatorClient;
  let realAlpacaClient: AlpacaClient;

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

    if (!config.alpacaApiKeyId || !config.alpacaApiSecret) {
      throw new Error('Missing Alpaca API credentials. Set ALPACA_ACCESS_KEY_ID and ALPACA_ACCESS_KEY.');
    }
    realAlpacaClient = new AlpacaClient({
      apiKeyId: config.alpacaApiKeyId,
      apiSecret: config.alpacaApiSecret,
      baseUrl: config.alpacaBaseUrl,
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

  it('matches account schema', async () => {
    const realResponse = await realAlpacaClient.getAccount();
    const simResponse = await alpacaClient.getAccount();

    const realSchema = extractSchema(realResponse.data);
    const simSchema = extractSchema(simResponse.data);
    const comparison = compareSchemas(realSchema, simSchema);

    logTestResult('Alpaca account', realResponse.status, simResponse.status, comparison.match);
    if (!comparison.match) {
      console.log(formatComparisonResult(comparison));
    }

    expect(realResponse.status).toBe(simResponse.status);
    expect(comparison.match).toBe(true);
  }, 20000);

  it('matches positions list schema and error responses', async () => {
    const realList = await realAlpacaClient.listPositions();
    const simList = await alpacaClient.listPositions();

    const realSchema = extractSchema(realList.data);
    const simSchema = extractSchema(simList.data);
    const comparison = compareSchemas(realSchema, simSchema);

    logTestResult('Alpaca positions list', realList.status, simList.status, comparison.match);
    if (!comparison.match) {
      console.log(formatComparisonResult(comparison));
    }

    expect(realList.status).toBe(simList.status);
    expect(comparison.match).toBe(true);

    const realMissing = await realAlpacaClient.getPosition('ZZZ_NOT_REAL');
    const simMissing = await alpacaClient.getPosition('ZZZ_NOT_REAL');
    expect(realMissing.status).toBe(simMissing.status);

    const realCloseMissing = await realAlpacaClient.closePosition('ZZZ_NOT_REAL');
    const simCloseMissing = await alpacaClient.closePosition('ZZZ_NOT_REAL');
    expect(realCloseMissing.status).toBe(simCloseMissing.status);
  }, 30000);

  it('matches order lifecycle schemas', async () => {
    const clientOrderId = `sim-test-${Date.now()}`;
    const orderPayload = {
      symbol: testSymbol,
      qty: 1,
      side: 'buy',
      type: 'limit',
      limit_price: 1,
      time_in_force: 'day',
      client_order_id: clientOrderId,
    };

    const realCreate = await realAlpacaClient.submitOrder(orderPayload);
    const simCreate = await alpacaClient.submitOrder(orderPayload);

    const createComparison = compareSchemas(
      extractSchema(realCreate.data),
      extractSchema(simCreate.data)
    );

    logTestResult('Alpaca order create', realCreate.status, simCreate.status, createComparison.match);
    if (!createComparison.match) {
      console.log(formatComparisonResult(createComparison));
    }

    expect(realCreate.status).toBe(simCreate.status);
    expect(createComparison.match).toBe(true);

    const realOrderId = realCreate.data.id as string;
    const simOrderId = simCreate.data.id as string;
    expect(realOrderId).toBeTruthy();
    expect(simOrderId).toBeTruthy();

    const realList = await realAlpacaClient.listOrders({ status: 'open' });
    const simList = await alpacaClient.listOrders({ status: 'open' });
    const listComparison = compareSchemas(
      extractSchema(realList.data),
      extractSchema(simList.data)
    );
    logTestResult('Alpaca orders list', realList.status, simList.status, listComparison.match);
    if (!listComparison.match) {
      console.log(formatComparisonResult(listComparison));
    }
    expect(realList.status).toBe(simList.status);
    expect(listComparison.match).toBe(true);

    const realGet = await realAlpacaClient.getOrder(realOrderId);
    const simGet = await alpacaClient.getOrder(simOrderId);
    const getComparison = compareSchemas(
      extractSchema(realGet.data),
      extractSchema(simGet.data)
    );
    logTestResult('Alpaca order get', realGet.status, simGet.status, getComparison.match);
    if (!getComparison.match) {
      console.log(formatComparisonResult(getComparison));
    }
    expect(realGet.status).toBe(simGet.status);
    expect(getComparison.match).toBe(true);

    const realByClient = await realAlpacaClient.getOrderByClientId(clientOrderId);
    const simByClient = await alpacaClient.getOrderByClientId(clientOrderId);
    const byClientComparison = compareSchemas(
      extractSchema(realByClient.data),
      extractSchema(simByClient.data)
    );
    logTestResult(
      'Alpaca order by client id',
      realByClient.status,
      simByClient.status,
      byClientComparison.match
    );
    if (!byClientComparison.match) {
      console.log(formatComparisonResult(byClientComparison));
    }
    expect(realByClient.status).toBe(simByClient.status);
    expect(byClientComparison.match).toBe(true);

    const replacePayload = {
      qty: 2,
      limit_price: 1.1,
      time_in_force: 'day',
    };

    const realReplace = await realAlpacaClient.replaceOrder(realOrderId, replacePayload);
    const simReplace = await alpacaClient.replaceOrder(simOrderId, replacePayload);
    const replaceComparison = compareSchemas(
      extractSchema(realReplace.data),
      extractSchema(simReplace.data)
    );
    logTestResult(
      'Alpaca order replace',
      realReplace.status,
      simReplace.status,
      replaceComparison.match
    );
    if (!replaceComparison.match) {
      console.log(formatComparisonResult(replaceComparison));
    }
    expect(realReplace.status).toBe(simReplace.status);
    expect(replaceComparison.match).toBe(true);

    const realCancelId = (realReplace.data.id as string) || realOrderId;
    const simCancelId = (simReplace.data.id as string) || simOrderId;
    const realCancel = await realAlpacaClient.cancelOrder(realCancelId);
    const simCancel = await alpacaClient.cancelOrder(simCancelId);
    expect(realCancel.status).toBe(simCancel.status);
    expect([204, 200]).toContain(simCancel.status);
    expect([204, 200]).toContain(realCancel.status);

    const realClosed = await realAlpacaClient.listOrders({ status: 'closed' });
    const simClosed = await alpacaClient.listOrders({ status: 'closed' });
    const closedComparison = compareSchemas(
      extractSchema(realClosed.data),
      extractSchema(simClosed.data)
    );
    logTestResult(
      'Alpaca orders closed list',
      realClosed.status,
      simClosed.status,
      closedComparison.match
    );
    if (!closedComparison.match) {
      console.log(formatComparisonResult(closedComparison));
    }
    expect(realClosed.status).toBe(simClosed.status);
    expect(closedComparison.match).toBe(true);
  }, 60000);
});
