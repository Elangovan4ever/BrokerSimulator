/**
 * Integration tests for Polygon Financials API
 * Tests: /vX/reference/financials
 */

import { polygonClient, simulatorClient, getSimDate, expectResultsNotEmpty, logTestResult } from './setup';
import { extractSchema, compareSchemas, formatComparisonResult } from '../utils/schema-compare';

describe('Polygon Financials API', () => {
  describe('GET /vX/reference/financials', () => {
    it('should return matching schema for AAPL financials', async () => {
      const cutoffDate = getSimDate();
      const polygonResponse = await polygonClient.getFinancials({
        ticker: 'AAPL',
        'period_of_report_date.lte': cutoffDate,
        'filing_date.lte': cutoffDate,
        limit: 1,
      });
      const simulatorResponse = await simulatorClient.getFinancials({
        ticker: 'AAPL',
        'period_of_report_date.lte': cutoffDate,
        'filing_date.lte': cutoffDate,
        limit: 1,
      });

      expectResultsNotEmpty('Financials AAPL polygon', polygonResponse.data);
      expectResultsNotEmpty('Financials AAPL simulator', simulatorResponse.data);

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'Financials AAPL',
        polygonResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      if (!comparison.match) {
        console.log(formatComparisonResult(comparison));
      }

      expect(polygonResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });

    it('should handle period_of_report_date.lte parameter', async () => {
      const cutoffDate = getSimDate();
      const polygonResponse = await polygonClient.getFinancials({
        ticker: 'AAPL',
        'period_of_report_date.lte': cutoffDate,
        'filing_date.lte': cutoffDate,
        limit: 1,
      });
      const simulatorResponse = await simulatorClient.getFinancials({
        ticker: 'AAPL',
        'period_of_report_date.lte': cutoffDate,
        'filing_date.lte': cutoffDate,
        limit: 1,
      });

      expectResultsNotEmpty('Financials period_of_report_date.lte polygon', polygonResponse.data);
      expectResultsNotEmpty('Financials period_of_report_date.lte simulator', simulatorResponse.data);

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'Financials period_of_report_date.lte',
        polygonResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      expect(polygonResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });

    it('should handle order=asc parameter', async () => {
      const cutoffDate = getSimDate();
      const polygonResponse = await polygonClient.getFinancials({
        ticker: 'AAPL',
        'period_of_report_date.lte': cutoffDate,
        'filing_date.lte': cutoffDate,
        order: 'asc',
        limit: 1,
      });
      const simulatorResponse = await simulatorClient.getFinancials({
        ticker: 'AAPL',
        'period_of_report_date.lte': cutoffDate,
        'filing_date.lte': cutoffDate,
        order: 'asc',
        limit: 1,
      });

      expectResultsNotEmpty('Financials order=asc polygon', polygonResponse.data);
      expectResultsNotEmpty('Financials order=asc simulator', simulatorResponse.data);

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'Financials order=asc',
        polygonResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      expect(polygonResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });

    it('should match pagination next_url presence', async () => {
      const cutoffDate = getSimDate();
      const polygonResponse = await polygonClient.getFinancials({
        ticker: 'AAPL',
        'period_of_report_date.lte': cutoffDate,
        'filing_date.lte': cutoffDate,
        limit: 1,
      });
      const simulatorResponse = await simulatorClient.getFinancials({
        ticker: 'AAPL',
        'period_of_report_date.lte': cutoffDate,
        'filing_date.lte': cutoffDate,
        limit: 1,
      });

      expectResultsNotEmpty('Financials pagination polygon', polygonResponse.data);
      expectResultsNotEmpty('Financials pagination simulator', simulatorResponse.data);

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      const polygonHasNext = Boolean(polygonResponse.data?.next_url);
      const simulatorHasNext = Boolean(simulatorResponse.data?.next_url);

      logTestResult(
        'Financials pagination next_url',
        polygonResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      expect(polygonResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
      expect(polygonHasNext).toBe(simulatorHasNext);
    });
  });
});
