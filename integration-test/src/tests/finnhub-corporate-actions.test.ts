/**
 * Integration tests for Finnhub corporate actions endpoints
 * Tests: /stock/dividend, /stock/split
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

describeIfKey('Finnhub Corporate Actions API', () => {
  const from = config.testStartDate;
  const to = config.testEndDate;

  describe('GET /stock/dividend', () => {
    it.each(config.testSymbols)('should return matching schema for %s dividends', async (symbol) => {
      if (skipIfUnavailable()) {
        return;
      }
      const finnhubResponse = await finnhubClient.getDividends(symbol, from, to);
      const simulatorResponse = await finnhubSimulatorClient.getDividends(symbol, from, to);

      const finnhubSchema = extractSchema(finnhubResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(finnhubSchema, simulatorSchema);

      logTestResult(
        `Finnhub dividends ${symbol}`,
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

  describe('GET /stock/split', () => {
    it.each(config.testSymbols)('should return matching schema for %s splits', async (symbol) => {
      if (skipIfUnavailable()) {
        return;
      }
      const finnhubResponse = await finnhubClient.getSplits(symbol, from, to);
      const simulatorResponse = await finnhubSimulatorClient.getSplits(symbol, from, to);

      const finnhubSchema = extractSchema(finnhubResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(finnhubSchema, simulatorSchema);

      logTestResult(
        `Finnhub splits ${symbol}`,
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
