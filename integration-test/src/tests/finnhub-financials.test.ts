/**
 * Integration tests for Finnhub financials endpoints
 * Tests: /stock/eps-estimate, /stock/revenue-estimate, /stock/earnings,
 *        /stock/financials, /stock/financials-reported
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

describeIfKey('Finnhub Financials API', () => {
  const symbol = config.testSymbols[0] || 'AAPL';
  const from = '2024-01-01';
  const to = config.finnhubTestEndDate;

  describe('GET /stock/eps-estimate', () => {
    it('should return matching schema for EPS estimates', async () => {
      if (skipIfUnavailable()) {
        return;
      }
      const finnhubResponse = await finnhubClient.getEpsEstimate({ symbol, freq: 'quarterly' });
      const simulatorResponse = await finnhubSimulatorClient.getEpsEstimate({ symbol, freq: 'quarterly' });

      const finnhubSchema = extractSchema(finnhubResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(finnhubSchema, simulatorSchema);

      logTestResult(
        'Finnhub EPS estimate',
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

  describe('GET /stock/revenue-estimate', () => {
    it('should return matching schema for revenue estimates', async () => {
      if (skipIfUnavailable()) {
        return;
      }
      const finnhubResponse = await finnhubClient.getRevenueEstimate({ symbol, freq: 'quarterly' });
      const simulatorResponse = await finnhubSimulatorClient.getRevenueEstimate({ symbol, freq: 'quarterly' });

      const finnhubSchema = extractSchema(finnhubResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(finnhubSchema, simulatorSchema);

      logTestResult(
        'Finnhub revenue estimate',
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

  describe('GET /stock/earnings', () => {
    it('should return matching schema for earnings history', async () => {
      if (skipIfUnavailable()) {
        return;
      }
      const finnhubResponse = await finnhubClient.getEarningsHistory({ symbol, from, to });
      const simulatorResponse = await finnhubSimulatorClient.getEarningsHistory({ symbol, from, to });

      const finnhubSchema = extractSchema(finnhubResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(finnhubSchema, simulatorSchema);

      logTestResult(
        'Finnhub earnings history',
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

  describe('GET /stock/financials', () => {
    it('should return matching schema for standardized financials', async () => {
      if (skipIfUnavailable()) {
        return;
      }
      const finnhubResponse = await finnhubClient.getFinancials({ symbol, statement: 'bs', freq: 'annual' });
      const simulatorResponse = await finnhubSimulatorClient.getFinancials({ symbol, statement: 'bs', freq: 'annual' });

      const finnhubSchema = extractSchema(finnhubResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(finnhubSchema, simulatorSchema);

      logTestResult(
        'Finnhub financials',
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

  describe('GET /stock/financials-reported', () => {
    it('should return matching schema for reported financials', async () => {
      if (skipIfUnavailable()) {
        return;
      }
      const finnhubResponse = await finnhubClient.getFinancialsReported({ symbol, freq: 'annual', from, to });
      const simulatorResponse = await finnhubSimulatorClient.getFinancialsReported({ symbol, freq: 'annual', from, to });

      const finnhubSchema = extractSchema(finnhubResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(finnhubSchema, simulatorSchema);

      logTestResult(
        'Finnhub financials reported',
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
