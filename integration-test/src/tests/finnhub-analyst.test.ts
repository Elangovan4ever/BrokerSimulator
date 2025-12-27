/**
 * Integration tests for Finnhub analyst endpoints
 * Tests: /calendar/earnings, /stock/recommendation, /stock/price-target, /stock/upgrade-downgrade
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

describeIfKey('Finnhub Analyst API', () => {
  const from = config.testStartDate;
  const to = config.testEndDate;

  describe('GET /calendar/earnings', () => {
    it.each(config.testSymbols)('should return matching schema for %s earnings calendar', async (symbol) => {
      if (skipIfUnavailable()) {
        return;
      }
      const finnhubResponse = await finnhubClient.getEarningsCalendar({ symbol, from, to });
      const simulatorResponse = await finnhubSimulatorClient.getEarningsCalendar({ symbol, from, to });

      const finnhubSchema = extractSchema(finnhubResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(finnhubSchema, simulatorSchema);

      logTestResult(
        `Finnhub earnings calendar ${symbol}`,
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

  describe('GET /stock/recommendation', () => {
    it.each(config.testSymbols)('should return matching schema for %s recommendations', async (symbol) => {
      if (skipIfUnavailable()) {
        return;
      }
      const finnhubResponse = await finnhubClient.getRecommendation(symbol);
      const simulatorResponse = await finnhubSimulatorClient.getRecommendation(symbol);

      const finnhubSchema = extractSchema(finnhubResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(finnhubSchema, simulatorSchema);

      logTestResult(
        `Finnhub recommendations ${symbol}`,
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

  describe('GET /stock/price-target', () => {
    it.each(config.testSymbols)('should return matching schema for %s price targets', async (symbol) => {
      if (skipIfUnavailable()) {
        return;
      }
      const finnhubResponse = await finnhubClient.getPriceTarget(symbol);
      const simulatorResponse = await finnhubSimulatorClient.getPriceTarget(symbol);

      const finnhubSchema = extractSchema(finnhubResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(finnhubSchema, simulatorSchema);

      logTestResult(
        `Finnhub price targets ${symbol}`,
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

  describe('GET /stock/upgrade-downgrade', () => {
    it.each(config.testSymbols)('should return matching schema for %s upgrades/downgrades', async (symbol) => {
      if (skipIfUnavailable()) {
        return;
      }
      const finnhubResponse = await finnhubClient.getUpgradeDowngrade(symbol);
      const simulatorResponse = await finnhubSimulatorClient.getUpgradeDowngrade(symbol);

      const finnhubSchema = extractSchema(finnhubResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(finnhubSchema, simulatorSchema);

      logTestResult(
        `Finnhub upgrades/downgrades ${symbol}`,
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
