/**
 * Global test setup and teardown
 * Creates a session before all tests and cleans up after
 */

import * as dotenv from 'dotenv';
import * as path from 'path';
import { SessionManager } from '../utils/session-manager';
import { PolygonClient } from '../clients/polygon-client';
import { SimulatorClient } from '../clients/simulator-client';

// Load environment variables
dotenv.config({ path: path.join(__dirname, '../../.env.test') });

// Global test configuration
export const config = {
  polygonApiKey: process.env.POLYGON_API_KEY || '',
  simulatorHost: process.env.SIMULATOR_HOST || 'elanlinux',
  controlPort: parseInt(process.env.CONTROL_PORT || '8000', 10),
  polygonPort: parseInt(process.env.POLYGON_PORT || '8200', 10),
  polygonBaseUrl: process.env.POLYGON_BASE_URL || 'https://api.polygon.io',
  testSymbols: (process.env.TEST_SYMBOLS || 'AAPL,MSFT,AMZN').split(','),
  testStartDate: process.env.TEST_START_DATE || '2025-01-13',
  testEndDate: process.env.TEST_END_DATE || '2025-01-17',
};

// Global clients
export let sessionManager: SessionManager;
export let polygonClient: PolygonClient;
export let simulatorClient: SimulatorClient;
export let testSessionId: string;

// Setup before all tests
beforeAll(async () => {
  console.log('\n========================================');
  console.log('INTEGRATION TEST SETUP');
  console.log('========================================');
  console.log(`Simulator Host: ${config.simulatorHost}`);
  console.log(`Control Port: ${config.controlPort}`);
  console.log(`Polygon Port: ${config.polygonPort}`);
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

  // Note: The Polygon API in the simulator queries the database directly,
  // so we don't need an active session for these tests.
  // Skip session creation to avoid triggering memory issues.
  console.log('Skipping session creation - Polygon API queries database directly');

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
      await sessionManager.cleanup();
    }
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
