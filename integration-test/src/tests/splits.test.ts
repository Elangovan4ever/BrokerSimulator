/**
 * Integration tests for Polygon Splits API
 * Tests: /v3/reference/splits
 */

import { polygonClient, simulatorClient, logTestResult } from './setup';
import { extractSchema, compareSchemas, formatComparisonResult } from '../utils/schema-compare';

describe('Polygon Splits API', () => {
  describe('GET /v3/reference/splits', () => {
    it('should return matching schema for AAPL splits', async () => {
      const polygonResponse = await polygonClient.getSplits({ ticker: 'AAPL', limit: 10 });
      const simulatorResponse = await simulatorClient.getSplits({ ticker: 'AAPL', limit: 10 });

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
      const polygonResponse = await polygonClient.getSplits({
        ticker: 'AAPL',
        'execution_date.gte': '2010-01-01',
        limit: 10,
      });
      const simulatorResponse = await simulatorClient.getSplits({
        ticker: 'AAPL',
        'execution_date.gte': '2010-01-01',
        limit: 10,
      });

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
      const polygonResponse = await polygonClient.getSplits({
        ticker: 'AAPL',
        order: 'asc',
        limit: 10,
      });
      const simulatorResponse = await simulatorClient.getSplits({
        ticker: 'AAPL',
        order: 'asc',
        limit: 10,
      });

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
      const polygonResponse = await polygonClient.getSplits({
        ticker: 'AAPL',
        sort: 'ticker',
        limit: 10,
      });
      const simulatorResponse = await simulatorClient.getSplits({
        ticker: 'AAPL',
        sort: 'ticker',
        limit: 10,
      });

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
      const polygonResponse = await polygonClient.getSplits({ ticker: 'AAPL', limit: 1 });
      const simulatorResponse = await simulatorClient.getSplits({ ticker: 'AAPL', limit: 1 });

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
