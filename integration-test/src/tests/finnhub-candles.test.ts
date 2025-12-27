/**
 * Integration tests for Finnhub candles API
 * Tests: /stock/candle
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

function toEpochSeconds(isoUtc: string): number {
  return Math.floor(new Date(isoUtc).getTime() / 1000);
}

describeIfKey('Finnhub Candles API', () => {
  describe('GET /stock/candle', () => {
    it.each(config.testSymbols)('should return matching schema for %s candles', async (symbol) => {
      if (skipIfUnavailable()) {
        return;
      }
      const from = toEpochSeconds(`${config.testStartDate}T00:00:00Z`);
      const to = toEpochSeconds(`${config.testStartDate}T23:59:59Z`);

      const finnhubResponse = await finnhubClient.getCandles(symbol, {
        resolution: '1',
        from,
        to,
      });
      const simulatorResponse = await finnhubSimulatorClient.getCandles(symbol, {
        resolution: '1',
        from,
        to,
      });

      const finnhubSchema = extractSchema(finnhubResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(finnhubSchema, simulatorSchema);

      logTestResult(
        `Finnhub candles ${symbol}`,
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
