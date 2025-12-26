/**
 * Integration tests for Polygon Short Volume API
 * Tests: /stocks/v1/short-volume
 */

import { polygonClient, simulatorClient, logTestResult } from './setup';
import { extractSchema, compareSchemas, formatComparisonResult } from '../utils/schema-compare';

describe('Polygon Short Volume API', () => {
  describe('GET /stocks/v1/short-volume', () => {
    it('should return matching schema for AAPL short volume', async () => {
      const polygonResponse = await polygonClient.getShortVolume({ ticker: 'AAPL', limit: 10 });
      const simulatorResponse = await simulatorClient.getShortVolume({ ticker: 'AAPL', limit: 10 });

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'Short volume AAPL',
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

    it('should handle date.gte parameter', async () => {
      const polygonResponse = await polygonClient.getShortVolume({
        ticker: 'AAPL',
        'date.gte': '2024-01-01',
        limit: 10,
      });
      const simulatorResponse = await simulatorClient.getShortVolume({
        ticker: 'AAPL',
        'date.gte': '2024-01-01',
        limit: 10,
      });

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'Short volume date.gte',
        polygonResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      expect(polygonResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });

    it('should handle order=asc parameter', async () => {
      const polygonResponse = await polygonClient.getShortVolume({
        ticker: 'AAPL',
        order: 'asc',
        limit: 10,
      });
      const simulatorResponse = await simulatorClient.getShortVolume({
        ticker: 'AAPL',
        order: 'asc',
        limit: 10,
      });

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'Short volume order=asc',
        polygonResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      expect(polygonResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });

    it('should match pagination next_url presence', async () => {
      const polygonResponse = await polygonClient.getShortVolume({ ticker: 'AAPL', limit: 1 });
      const simulatorResponse = await simulatorClient.getShortVolume({ ticker: 'AAPL', limit: 1 });

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      const polygonHasNext = Boolean(polygonResponse.data?.next_url);
      const simulatorHasNext = Boolean(simulatorResponse.data?.next_url);

      logTestResult(
        'Short volume pagination next_url',
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
