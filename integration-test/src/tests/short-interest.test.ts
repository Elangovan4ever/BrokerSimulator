/**
 * Integration tests for Polygon Short Interest API
 * Tests: /stocks/v1/short-interest
 */

import { polygonClient, simulatorClient, getSimDate, expectResultsNotEmpty, logTestResult } from './setup';
import { extractSchema, compareSchemas, formatComparisonResult } from '../utils/schema-compare';

describe('Polygon Short Interest API', () => {
  describe('GET /stocks/v1/short-interest', () => {
    it('should return matching schema for AAPL short interest', async () => {
      const cutoffDate = getSimDate();
      const polygonResponse = await polygonClient.getShortInterest({
        ticker: 'AAPL',
        'settlement_date.lte': cutoffDate,
        limit: 10,
      });
      const simulatorResponse = await simulatorClient.getShortInterest({
        ticker: 'AAPL',
        'settlement_date.lte': cutoffDate,
        limit: 10,
      });

      expectResultsNotEmpty('Short interest AAPL polygon', polygonResponse.data);
      expectResultsNotEmpty('Short interest AAPL simulator', simulatorResponse.data);

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'Short interest AAPL',
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

    it('should handle settlement_date.gte parameter', async () => {
      const cutoffDate = getSimDate();
      const polygonResponse = await polygonClient.getShortInterest({
        ticker: 'AAPL',
        'settlement_date.gte': '2024-01-01',
        'settlement_date.lte': cutoffDate,
        limit: 10,
      });
      const simulatorResponse = await simulatorClient.getShortInterest({
        ticker: 'AAPL',
        'settlement_date.gte': '2024-01-01',
        'settlement_date.lte': cutoffDate,
        limit: 10,
      });

      expectResultsNotEmpty('Short interest settlement_date.gte polygon', polygonResponse.data);
      expectResultsNotEmpty('Short interest settlement_date.gte simulator', simulatorResponse.data);

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'Short interest settlement_date.gte',
        polygonResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      expect(polygonResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });

    it('should handle order=asc parameter', async () => {
      const cutoffDate = getSimDate();
      const polygonResponse = await polygonClient.getShortInterest({
        ticker: 'AAPL',
        'settlement_date.lte': cutoffDate,
        order: 'asc',
        limit: 10,
      });
      const simulatorResponse = await simulatorClient.getShortInterest({
        ticker: 'AAPL',
        'settlement_date.lte': cutoffDate,
        order: 'asc',
        limit: 10,
      });

      expectResultsNotEmpty('Short interest order=asc polygon', polygonResponse.data);
      expectResultsNotEmpty('Short interest order=asc simulator', simulatorResponse.data);

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'Short interest order=asc',
        polygonResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      expect(polygonResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });

    it('should match pagination next_url presence', async () => {
      const cutoffDate = getSimDate();
      const polygonResponse = await polygonClient.getShortInterest({
        ticker: 'AAPL',
        'settlement_date.lte': cutoffDate,
        limit: 1,
      });
      const simulatorResponse = await simulatorClient.getShortInterest({
        ticker: 'AAPL',
        'settlement_date.lte': cutoffDate,
        limit: 1,
      });

      expectResultsNotEmpty('Short interest pagination polygon', polygonResponse.data);
      expectResultsNotEmpty('Short interest pagination simulator', simulatorResponse.data);

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      const polygonHasNext = Boolean(polygonResponse.data?.next_url);
      const simulatorHasNext = Boolean(simulatorResponse.data?.next_url);

      logTestResult(
        'Short interest pagination next_url',
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
