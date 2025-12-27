/**
 * Integration tests for Finnhub company endpoints
 * Tests: /stock/profile2, /stock/peers, /stock/metric
 */

import { finnhubClient, finnhubSimulatorClient, config, logTestResult, finnhubApiAvailable } from './setup';
import { extractSchema, compareSchemas, formatComparisonResult } from '../utils/schema-compare';

const describeIfKey = config.finnhubApiKey ? describe : describe.skip;

function skipIfUnavailable(): boolean {
  if (!finnhubApiAvailable) {
    console.warn('Skipping Finnhub tests: FINNHUB_API_KEY invalid.');
    return true;
  }
  return false;
}

describeIfKey('Finnhub Company API', () => {
  describe('GET /stock/profile2', () => {
    it.each(config.testSymbols)('should return matching schema for %s profile', async (symbol) => {
      if (skipIfUnavailable()) {
        return;
      }
      const finnhubResponse = await finnhubClient.getCompanyProfile(symbol);
      const simulatorResponse = await finnhubSimulatorClient.getCompanyProfile(symbol);

      const finnhubSchema = extractSchema(finnhubResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(finnhubSchema, simulatorSchema);

      logTestResult(
        `Finnhub profile ${symbol}`,
        finnhubResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      if (!comparison.match) {
        console.log(formatComparisonResult(comparison));
      }

      expect(finnhubResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });
  });

  describe('GET /stock/peers', () => {
    it.each(config.testSymbols)('should return matching schema for %s peers', async (symbol) => {
      if (skipIfUnavailable()) {
        return;
      }
      const finnhubResponse = await finnhubClient.getCompanyPeers(symbol);
      const simulatorResponse = await finnhubSimulatorClient.getCompanyPeers(symbol);

      const finnhubSchema = extractSchema(finnhubResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(finnhubSchema, simulatorSchema);

      logTestResult(
        `Finnhub peers ${symbol}`,
        finnhubResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      if (!comparison.match) {
        console.log(formatComparisonResult(comparison));
      }

      expect(finnhubResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });
  });

  describe('GET /stock/metric', () => {
    it.each(config.testSymbols)('should return matching schema for %s basic financials', async (symbol) => {
      if (skipIfUnavailable()) {
        return;
      }
      const finnhubResponse = await finnhubClient.getBasicFinancials(symbol, 'all');
      const simulatorResponse = await finnhubSimulatorClient.getBasicFinancials(symbol, 'all');

      const finnhubSchema = extractSchema(finnhubResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(finnhubSchema, simulatorSchema);

      logTestResult(
        `Finnhub basic financials ${symbol}`,
        finnhubResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      if (!comparison.match) {
        console.log(formatComparisonResult(comparison));
      }

      expect(finnhubResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });
  });
});
