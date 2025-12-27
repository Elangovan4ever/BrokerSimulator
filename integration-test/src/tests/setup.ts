/**
 * Global test setup and teardown
 * Creates a session before all tests and cleans up after
 */

import * as dotenv from 'dotenv';
import * as path from 'path';
import axios from 'axios';
import { expect } from '@jest/globals';
import { SessionManager } from '../utils/session-manager';
import { PolygonClient } from '../clients/polygon-client';
import { SimulatorClient } from '../clients/simulator-client';
import { FinnhubClient } from '../clients/finnhub-client';
import { FinnhubSimulatorClient } from '../clients/finnhub-simulator-client';

// Load environment variables
dotenv.config({ path: path.join(__dirname, '../../.env.test') });

// Global test configuration
export const config = {
  polygonApiKey: process.env.POLYGON_API_KEY || '',
  finnhubApiKey: process.env.FINNHUB_API_KEY || '',
  simulatorHost: process.env.SIMULATOR_HOST || 'elanlinux',
  controlPort: parseInt(process.env.CONTROL_PORT || '8000', 10),
  polygonPort: parseInt(process.env.POLYGON_PORT || '8200', 10),
  finnhubPort: parseInt(process.env.FINNHUB_PORT || '8300', 10),
  polygonBaseUrl: process.env.POLYGON_BASE_URL || 'https://api.polygon.io',
  finnhubBaseUrl: process.env.FINNHUB_BASE_URL || 'https://finnhub.io/api/v1',
  testSymbols: (process.env.TEST_SYMBOLS || 'AAPL,MSFT,AMZN').split(','),
  testStartDate: process.env.TEST_START_DATE || '2025-01-13',
  testEndDate: process.env.TEST_END_DATE || '2025-01-17',
  finnhubTestStartDate: process.env.FINNHUB_TEST_START_DATE || '2025-12-20',
  finnhubTestEndDate: process.env.FINNHUB_TEST_END_DATE || '2025-12-27',
};

// Global clients
export let sessionManager: SessionManager;
export let polygonClient: PolygonClient;
export let simulatorClient: SimulatorClient;
export let finnhubClient: FinnhubClient;
export let finnhubSimulatorClient: FinnhubSimulatorClient;
export let finnhubApiAvailable = false;
export let testSessionId: string;
export let currentSimDate: string | null = null;
export let finnhubSessionId: string;
export let finnhubSimDate: string | null = null;

export function getSimDate(): string {
  return currentSimDate || config.testStartDate;
}

export function getFinnhubSimDate(): string {
  return finnhubSimDate || config.finnhubTestStartDate;
}

export function expectResultsNotEmpty(label: string, data: unknown): void {
  const results = (data as { results?: unknown }).results;
  if (!Array.isArray(results)) {
    throw new Error(`${label}: response missing results array`);
  }
  expect(results.length).toBeGreaterThan(0);
}

// Setup before all tests
beforeAll(async () => {
  console.log('\n========================================');
  console.log('INTEGRATION TEST SETUP');
  console.log('========================================');
  console.log(`Simulator Host: ${config.simulatorHost}`);
  console.log(`Control Port: ${config.controlPort}`);
  console.log(`Polygon Port: ${config.polygonPort}`);
  console.log(`Finnhub Port: ${config.finnhubPort}`);
  console.log(`Test Symbols: ${config.testSymbols.join(', ')}`);
  console.log(`Date Range: ${config.testStartDate} to ${config.testEndDate}`);
  console.log('========================================\n');

  // Initialize session manager
  sessionManager = new SessionManager(config.simulatorHost, config.controlPort);

  // Initialize Polygon client (real API)
  polygonClient = new PolygonClient({
    apiKey: config.polygonApiKey,
    baseUrl: config.polygonBaseUrl,
  });

  // Initialize Simulator client
  simulatorClient = new SimulatorClient({
    host: config.simulatorHost,
    port: config.polygonPort,
  });

  // Initialize Finnhub client (real API)
  finnhubClient = new FinnhubClient({
    apiKey: config.finnhubApiKey,
    baseUrl: config.finnhubBaseUrl,
  });

  // Initialize Finnhub Simulator client
  finnhubSimulatorClient = new FinnhubSimulatorClient({
    host: config.simulatorHost,
    port: config.finnhubPort,
  });

  if (config.finnhubApiKey) {
    try {
      await finnhubClient.getCompanyProfile(config.testSymbols[0] || 'AAPL');
      finnhubApiAvailable = true;
    } catch (error) {
      if (axios.isAxiosError(error) && error.response?.status === 401) {
        console.warn('Finnhub API key invalid. Finnhub comparison tests will be skipped.');
      } else {
        console.warn('Finnhub API check failed. Finnhub comparison tests will be skipped.');
      }
      finnhubApiAvailable = false;
    }
  } else {
    console.warn('FINNHUB_API_KEY not set. Finnhub comparison tests will be skipped.');
  }

  // Create a session for tests that require cached data (lastTrade, lastQuote, snapshots)
  try {
    console.log('Creating test session...');
    testSessionId = await sessionManager.createSession({
      symbols: config.testSymbols,
      start_time: `${config.testStartDate}T09:30:00`,
      end_time: `${config.testStartDate}T16:00:00`,
      initial_capital: 100000,
      speed_factor: 0,  // Run as fast as possible
    });

    console.log('Starting session...');
    await sessionManager.startSession(testSessionId);

    // Wait for session to process events so we have cached data
    console.log('Waiting for session to process events...');
    await sessionManager.waitForWarmup(testSessionId, 1, 30000);

    // Set session ID on simulator client
    simulatorClient.setSessionId(testSessionId);
    finnhubSimulatorClient.setSessionId(testSessionId);
    const session = await sessionManager.getSession(testSessionId);
    if (session.current_time) {
      currentSimDate = session.current_time.split('T')[0];
      console.log(`Session current date: ${currentSimDate}`);
    }
    console.log(`Session ${testSessionId} is ready`);
  } catch (error) {
    console.error('Warning: Failed to create session:', error);
    console.log('Some tests requiring session data may fail');
  }

  // Create a separate session aligned to Finnhub data ranges
  try {
    console.log('Creating Finnhub test session...');
    finnhubSessionId = await sessionManager.createSession({
      symbols: config.testSymbols,
      start_time: `${config.finnhubTestStartDate}T09:30:00`,
      end_time: `${config.finnhubTestEndDate}T16:00:00`,
      initial_capital: 100000,
      speed_factor: 0,
    });

    console.log('Starting Finnhub session...');
    await sessionManager.startSession(finnhubSessionId);

    console.log('Waiting for Finnhub session to process events...');
    await sessionManager.waitForWarmup(finnhubSessionId, 1, 30000);

    finnhubSimulatorClient.setSessionId(finnhubSessionId);
    const session = await sessionManager.getSession(finnhubSessionId);
    if (session.current_time) {
      finnhubSimDate = session.current_time.split('T')[0];
      console.log(`Finnhub session current date: ${finnhubSimDate}`);
    }
    console.log(`Finnhub session ${finnhubSessionId} is ready`);
  } catch (error) {
    console.error('Warning: Failed to create Finnhub session:', error);
    console.log('Finnhub API tests may fall back to system time');
  }

  console.log('\nSetup complete - ready to run tests\n');
}, 120000); // 2 minute timeout for setup

// Teardown after all tests
afterAll(async () => {
  console.log('\n========================================');
  console.log('INTEGRATION TEST CLEANUP');
  console.log('========================================');

  try {
    if (testSessionId) {
      console.log(`Cleaning up session: ${testSessionId}`);
      await sessionManager.stopSession(testSessionId);
      await sessionManager.deleteSession(testSessionId);
    }
    if (finnhubSessionId && finnhubSessionId !== testSessionId) {
      console.log(`Cleaning up Finnhub session: ${finnhubSessionId}`);
      await sessionManager.stopSession(finnhubSessionId);
      await sessionManager.deleteSession(finnhubSessionId);
    }
    polygonClient?.close();
    finnhubClient?.close();
    console.log('Cleanup complete');
  } catch (error) {
    console.error('Error during cleanup:', error);
  }

  console.log('========================================\n');
}, 30000);

// Helper function to log test results
export function logTestResult(
  testName: string,
  polygonStatus: number,
  simulatorStatus: number,
  schemaMatch: boolean
): void {
  const statusMatch = polygonStatus === simulatorStatus;
  const result = statusMatch && schemaMatch ? 'PASS' : 'FAIL';
  console.log(
    `  ${result}: ${testName} | Polygon: ${polygonStatus} | Simulator: ${simulatorStatus} | Schema: ${schemaMatch ? 'Match' : 'Mismatch'}`
  );
}
