/**
 * Integration tests for Finnhub quote & trades APIs
 * Tests: /quote, /trades
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

describeIfKey('Finnhub Realtime API', () => {
  describe('GET /quote', () => {
    it.each(config.testSymbols)('should return matching schema for %s quote', async (symbol) => {
      if (skipIfUnavailable()) {
        return;
      }
      const finnhubResponse = await finnhubClient.getQuote(symbol);
      const simulatorResponse = await finnhubSimulatorClient.getQuote(symbol);

      const finnhubSchema = extractSchema(finnhubResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(finnhubSchema, simulatorSchema);

      logTestResult(
        `Finnhub quote ${symbol}`,
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

  describe('GET /trades', () => {
    it.each(config.testSymbols)('should return matching schema for %s trades', async (symbol) => {
      if (skipIfUnavailable()) {
        return;
      }
      const finnhubResponse = await finnhubClient.getTrades(symbol);
      const simulatorResponse = await finnhubSimulatorClient.getTrades(symbol);

      const finnhubSchema = extractSchema(finnhubResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(finnhubSchema, simulatorSchema);

      logTestResult(
        `Finnhub trades ${symbol}`,
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
