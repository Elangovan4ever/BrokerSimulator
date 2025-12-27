/**
 * Integration tests for Polygon Splits API
 * Tests: /v3/reference/splits
 */

import { polygonClient, simulatorClient, getSimDate, expectResultsNotEmpty, logTestResult } from './setup';
import { extractSchema, compareSchemas, formatComparisonResult } from '../utils/schema-compare';

describe('Polygon Splits API', () => {
  describe('GET /v3/reference/splits', () => {
    it('should return matching schema for AAPL splits', async () => {
      const cutoffDate = getSimDate();
      const polygonResponse = await polygonClient.getSplits({
        ticker: 'AAPL',
        'execution_date.lte': cutoffDate,
        limit: 10,
      });
      const simulatorResponse = await simulatorClient.getSplits({
        ticker: 'AAPL',
        'execution_date.lte': cutoffDate,
        limit: 10,
      });

      expectResultsNotEmpty('Splits AAPL polygon', polygonResponse.data);
      expectResultsNotEmpty('Splits AAPL simulator', simulatorResponse.data);

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'Splits AAPL',
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

    it('should handle execution_date.gte parameter', async () => {
      const cutoffDate = getSimDate();
      const polygonResponse = await polygonClient.getSplits({
        ticker: 'AAPL',
        'execution_date.gte': '2010-01-01',
        'execution_date.lte': cutoffDate,
        limit: 10,
      });
      const simulatorResponse = await simulatorClient.getSplits({
        ticker: 'AAPL',
        'execution_date.gte': '2010-01-01',
        'execution_date.lte': cutoffDate,
        limit: 10,
      });

      expectResultsNotEmpty('Splits execution_date.gte polygon', polygonResponse.data);
      expectResultsNotEmpty('Splits execution_date.gte simulator', simulatorResponse.data);

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'Splits execution_date.gte',
        polygonResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      expect(polygonResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });

    it('should handle order=asc parameter', async () => {
      const cutoffDate = getSimDate();
      const polygonResponse = await polygonClient.getSplits({
        ticker: 'AAPL',
        'execution_date.lte': cutoffDate,
        order: 'asc',
        limit: 10,
      });
      const simulatorResponse = await simulatorClient.getSplits({
        ticker: 'AAPL',
        'execution_date.lte': cutoffDate,
        order: 'asc',
        limit: 10,
      });

      expectResultsNotEmpty('Splits order=asc polygon', polygonResponse.data);
      expectResultsNotEmpty('Splits order=asc simulator', simulatorResponse.data);

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'Splits order=asc',
        polygonResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      expect(polygonResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });

    it('should handle sort=ticker parameter', async () => {
      const cutoffDate = getSimDate();
      const polygonResponse = await polygonClient.getSplits({
        ticker: 'AAPL',
        'execution_date.lte': cutoffDate,
        sort: 'ticker',
        limit: 10,
      });
      const simulatorResponse = await simulatorClient.getSplits({
        ticker: 'AAPL',
        'execution_date.lte': cutoffDate,
        sort: 'ticker',
        limit: 10,
      });

      expectResultsNotEmpty('Splits sort=ticker polygon', polygonResponse.data);
      expectResultsNotEmpty('Splits sort=ticker simulator', simulatorResponse.data);

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'Splits sort=ticker',
        polygonResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      expect(polygonResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });

    it('should match pagination next_url presence', async () => {
      const cutoffDate = getSimDate();
      const polygonResponse = await polygonClient.getSplits({
        ticker: 'AAPL',
        'execution_date.lte': cutoffDate,
        limit: 1,
      });
      const simulatorResponse = await simulatorClient.getSplits({
        ticker: 'AAPL',
        'execution_date.lte': cutoffDate,
        limit: 1,
      });

      expectResultsNotEmpty('Splits pagination polygon', polygonResponse.data);
      expectResultsNotEmpty('Splits pagination simulator', simulatorResponse.data);

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      const polygonHasNext = Boolean(polygonResponse.data?.next_url);
      const simulatorHasNext = Boolean(simulatorResponse.data?.next_url);

      logTestResult(
        'Splits pagination next_url',
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
